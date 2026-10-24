// gem5/src/cpu/pred/TageScImli128.cc
#include "cpu/pred/TageScImli128.hh"

#include <cassert>
#include <cstring>
#include <algorithm>  // std::min
#include <cstdlib>    // std::abs for int

#include "base/bitfield.hh"
#include "base/intmath.hh"
#include "base/logging.hh"


namespace gem5 {
namespace branch_prediction {

// ---- ctor ----
TageScImli128::TageScImli128(const Params *p)
  : BPredUnit(p),
    tage(),
    bimodal(kBimodalEntries, 0), // 3-bit centered counter: [-4..+3], start 0
    sc1(kTblEntries, 0),
    sc2(kTblEntries, 0),
    imli(kIMLISize, 0),
    theta(kThetaInit),
    ghrs(p->numThreads),
    rng(1u)
{
    // build TAGE tables
    tage.reserve(kNumTables);
    for (unsigned t = 0; t < kNumTables; ++t)
        tage.emplace_back(kTblEntries, kTagBits[t]);
}

// ---- hash helpers ----
// simple fold-xor of GHR bits down to kIdxBits/tagBits
unsigned
TageScImli128::idxHash(Addr pc, const GHR &ghr, unsigned histLen) const
{
    uint32_t h = static_cast<uint32_t>(pc) ^ static_cast<uint32_t>(pc >> 2);
    // fold top histLen bits
    uint32_t acc = 0;
    const unsigned n = std::min<unsigned>(histLen, ghr.bits.size());
    for (unsigned i = 0; i < n; ++i) {
        acc ^= (ghr.bits[i] ? 0x9e3779b9u : 0u) >> (i % 17);
        acc = (acc << 1) | (acc >> 31);
    }
    h ^= acc;
    return h & ((1u << kIdxBits) - 1u);
}

uint16_t
TageScImli128::tagHash(Addr pc, const GHR &ghr, unsigned histLen,
                       unsigned tagBits) const
{
    uint32_t h = static_cast<uint32_t>((pc >> 1) ^ (pc >> 5));
    uint32_t acc = 0;
    const unsigned n = std::min<unsigned>(histLen, ghr.bits.size());
    for (unsigned i = 0; i < n; ++i) {
        acc ^= (ghr.bits[i] ? 0x7f4a7c15u : 0u) >> (i % 13);
        acc = (acc << 3) | (acc >> 29);
    }
    h ^= acc;
    const uint16_t mask = static_cast<uint16_t>((1u << tagBits) - 1u);
    return static_cast<uint16_t>(h) & mask;
}

// ---- GHR helpers ----
void
TageScImli128::pushSpec(ThreadID tid, bool taken)
{
    auto &q = ghrs[tid].bits;
    if (q.size() >= kMaxHist)
        q.pop_back();
    q.push_front(static_cast<uint8_t>(taken));
}

void
TageScImli128::restoreGHR(ThreadID tid, const std::vector<uint8_t> &snap)
{
    auto &q = ghrs[tid].bits;
    q.clear();
    q.insert(q.begin(), snap.begin(), snap.end());
}

// ---- TAGE lookup ----
bool
TageScImli128::tageLookup(ThreadID tid, Addr pc, int &provT, int &provI,
                          int &altT, int &altI, bool &altDir, bool baseDir)
{
    provT = provI = altT = altI = -1;
    altDir = baseDir;

    const auto &ghr = ghrs[tid];

    // find longest matching provider
    for (int t = kNumTables - 1; t >= 0; --t) {
        const unsigned idx = idxHash(pc, ghr, kHistLen[t]);
        const uint16_t tag  = tagHash(pc, ghr, kHistLen[t], kTagBits[t]);

        const auto &e = tage[t].rows[idx];
        const uint16_t storedTag = (tage[t].tagBits >= 16)
            ? e.tag : (e.tag & ((1u << tage[t].tagBits) - 1u));
        if (storedTag == tag) {
            provT = t; provI = idx;
            break;
        }
    }

    // find alternate (next shorter)
    if (provT >= 0) {
        for (int t = provT - 1; t >= 0; --t) {
            const unsigned idx = idxHash(pc, ghr, kHistLen[t]);
            const uint16_t tag  = tagHash(pc, ghr, kHistLen[t], kTagBits[t]);
            const auto &e = tage[t].rows[idx];
            const uint16_t storedTag = (tage[t].tagBits >= 16)
                ? e.tag : (e.tag & ((1u << tage[t].tagBits) - 1u));
            if (storedTag == tag) {
                altT = t; altI = idx;
                altDir = (e.ctr >= 0);
                break;
            }
        }
    }

    // compose TAGE decision
    bool provDir = false;
    bool useAlt  = false;
    if (provT >= 0) {
        const auto &p = tage[provT].rows[provI];
        provDir = (p.ctr >= 0);
        // simple alt policy: if provider weak, consult alternate/base
        if ((p.ctr == -1) || (p.ctr == 0)) useAlt = true;
    }

    if (provT < 0) {
        // no match: use base
        return baseDir;
    } else if (useAlt) {
        if (altT >= 0)
            return altDir;
        return baseDir;
    } else {
        return provDir;
    }
}

// ---- BPredUnit interface impl ----
bool
TageScImli128::lookup(ThreadID tid, Addr pc, void *&bp_history)
{
    // save snapshot for squash
    auto *h = new BPHistory();
    h->ghr_snapshot.assign(ghrs[tid].bits.begin(), ghrs[tid].bits.end());

    // base prediction (bimodal)
    const int baseIdx = (static_cast<uint32_t>(pc) ^ (pc >> 2)) & (kBimodalEntries - 1);
    // 3-bit signed style: predict taken if ctr >= 0
    const bool baseDir = (bimodal[baseIdx] >= 0);
    h->baseIdx = baseIdx;

    // TAGE decision
    int provT, provI, altT, altI; bool altDir;
    const bool tageDir = tageLookup(tid, pc, provT, provI, altT, altI, altDir, baseDir);
    h->provTable = provT; h->provIdx = provI;
    h->altTable  = altT; h->altIdx  = altI;

    // SC combine (two tiny 4K tables)
    const unsigned scIdx1 = ((pc >> 2) ^ provI) & (kTblEntries - 1);
    const unsigned scIdx2 = ((pc >> 1) ^ idxHash(pc, ghrs[tid], 32)) & (kTblEntries - 1);
    const int scSum = static_cast<int>(sc1[scIdx1]) + static_cast<int>(sc2[scIdx2]);

    bool scFlip = false;
    if (std::abs(scSum) >= theta) {
        const bool scSuggestTaken = (scSum >= 0);
        scFlip = (scSuggestTaken != tageDir);
    }
    bool dir = tageDir;
    if (scFlip) { dir = !dir; h->usedSC = true; }

    // IMLI-lite: simple backward-taken streak on 64B region
    const unsigned region = static_cast<unsigned>(pc >> kRegionShift) & (kIMLISize - 1);
    const int8_t im = imli[region];
    if (im >= 4) { // confident
        const bool imliSuggest = true; // streak says keep taking
        if (h->provTable < 0 || (h->provTable >= 0 && std::abs(scSum) < theta))
        {
            dir = imliSuggest;
            h->usedIMLI = true;
        }
    }

    h->predTaken = dir;
    bp_history = h;

    // speculative update of GHR
    pushSpec(tid, dir);
    return dir;
}

void
TageScImli128::uncondBranch(ThreadID tid, Addr pc, void *&bp_history)
{
    auto *h = new BPHistory();
    h->ghr_snapshot.assign(ghrs[tid].bits.begin(), ghrs[tid].bits.end());
    h->predTaken = true;
    bp_history = h;
    pushSpec(tid, true);
}

void
TageScImli128::btbUpdate(ThreadID tid, Addr pc, void *&bp_history)
{
    // if BTB miss, treat as not-taken and update history
    auto *h = new BPHistory();
    h->ghr_snapshot.assign(ghrs[tid].bits.begin(), ghrs[tid].bits.end());
    h->predTaken = false;
    bp_history = h;
    pushSpec(tid, false);
}

void
TageScImli128::update(ThreadID tid, Addr pc, bool taken,
                      void *bp_history, bool squashed)
{
    auto *h = static_cast<BPHistory*>(bp_history);

    // if this is called during squash path, just clean up
    if (squashed) {
        delete h;
        return;
    }

    // fixup GHR last speculative bit if needed
    if (!ghrs[tid].bits.empty()) {
        if (ghrs[tid].bits.front() != static_cast<uint8_t>(taken)) {
            // replace head with actual
            ghrs[tid].bits.pop_front();
            ghrs[tid].bits.push_front(static_cast<uint8_t>(taken));
        }
    }

    // train components
    trainIMLI(pc, taken);
    trainSC(pc, tid, taken, h);
    trainTAGE(tid, pc, taken, h);

    // train base
    if (taken) {
        if (bimodal[h->baseIdx] < kCtrMax) {
            ++bimodal[h->baseIdx];
        }
    } else {
        if (bimodal[h->baseIdx] > kCtrMin) {
            --bimodal[h->baseIdx];
        }
    }
    delete h;
}

void
TageScImli128::squash(ThreadID tid, void *bp_history)
{
    auto *h = static_cast<BPHistory*>(bp_history);
    // precise rollback
    restoreGHR(tid, h->ghr_snapshot);
    delete h;
}

// ---- training ----
void
TageScImli128::trainIMLI(Addr pc, bool taken)
{
    const unsigned region = static_cast<unsigned>(pc >> kRegionShift) & (kIMLISize - 1);
    int8_t &v = imli[region];
    // very small model: increase on backward-taken streak, decay otherwise
    if (taken)
        v = std::min<int8_t>(v + 1, 127);
    else
        v = std::max<int8_t>(v - 2, 0); // quick decay on exit
}

void
TageScImli128::trainSC(Addr pc, ThreadID tid, bool taken, const BPHistory *h)
{
    const unsigned scIdx1 = ((pc >> 2) ^ static_cast<unsigned>(h->provIdx < 0 ? 0 : h->provIdx))
                            & (kTblEntries - 1);
    const unsigned scIdx2 = ((pc >> 1) ^ idxHash(pc, ghrs[tid], 32)) & (kTblEntries - 1);

    // teacher signal: actual taken/not-taken
    if (taken) {
        sc1[scIdx1] = satInc8(sc1[scIdx1]);
        sc2[scIdx2] = satInc8(sc2[scIdx2]);
    } else {
        sc1[scIdx1] = satDec8(sc1[scIdx1]);
        sc2[scIdx2] = satDec8(sc2[scIdx2]);
    }

    // very light adaptive theta (prevent over-flipping)
    // If SC often disagrees with outcome, expand |theta|
    static int adapt = 0;
    const bool scTaken = ( (int(sc1[scIdx1]) + int(sc2[scIdx2])) >= 0 );
    if (scTaken != taken) adapt++; else adapt--;
    if (adapt > 64 && theta < 32) { theta++; adapt = 0; }
    if (adapt < -64 && theta > 4) { theta--; adapt = 0; }
}

void
TageScImli128::trainTAGE(ThreadID tid, Addr pc, bool taken, const BPHistory *h)
{
    // provider update
    if (h->provTable >= 0) {
        auto &pe = tage[h->provTable].rows[h->provIdx];
        pe.ctr = taken ? satInc3(pe.ctr) : satDec3(pe.ctr);
        if ((pe.u < 3) && ((pe.ctr >= -1) && (pe.ctr <= 1))) {
            // weak entries age slower
        } else {
            // protect useful entries
            if ( (taken && pe.ctr > 0) || (!taken && pe.ctr < 0) ) {
                if (pe.u < 3) pe.u++;
            }
        }
    }

    // allocate on miss or weak provider mismatch
    const bool provider_exists = (h->provTable >= 0);
    bool need_alloc = false;
    if (!provider_exists) {
        need_alloc = true;
    } else {
        const auto &pe = tage[h->provTable].rows[h->provIdx];
        const bool provDir = (pe.ctr >= 0);
        if (provDir != taken) need_alloc = true;
        if ((pe.ctr >= -1) && (pe.ctr <= 1)) need_alloc = true;
    }

    if (need_alloc) {
        // allocate up to two longer tables
        std::uniform_int_distribution<int> coin(0, 15); // ~1/16, 1/4, 1/2 heuristic 근사
        int allocs = 0;
        for (int t = kNumTables - 1; t >= 0 && allocs < 2; --t) {
            if (provider_exists && t <= h->provTable) break; // longer only
            const unsigned idx = idxHash(pc, ghrs[tid], kHistLen[t]);
            auto &e = tage[t].rows[idx];
            if (e.u == 0) {
                e.tag = tagHash(pc, ghrs[tid], kHistLen[t], tage[t].tagBits);
                e.ctr = taken ? 0 : -1; // weak toward outcome
                e.u   = 0;
                allocs++;
            } else {
                // probabilistic replacement of less-useful entries
                if (coin(rng) == 0) {
                    e.tag = tagHash(pc, ghrs[tid], kHistLen[t], tage[t].tagBits);
                    e.ctr = taken ? 0 : -1;
                    e.u   = 0;
                    allocs++;
                }
            }
        }
    }

    // light aging of usefulness (periodic could be better)
    static uint64_t tick = 0;
    if ((++tick & 0x3FFFu) == 0) { // every ~16K updates
        for (unsigned t = 0; t < kNumTables; ++t)
            for (auto &e : tage[t].rows)
                if (e.u > 0) e.u--;
    }
}

} // namespace branch_prediction
} // namespace gem5
