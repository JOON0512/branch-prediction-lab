// gem5/src/cpu/pred/TageScImli128.hh
#ifndef __CPU_PRED_TAGE_SC_IMLI128_HH__
#define __CPU_PRED_TAGE_SC_IMLI128_HH__

#include <array>
#include <cstdint>
#include <deque>
#include <random>
#include <vector>

#include "base/types.hh"
#include "cpu/pred/bpred_unit.hh"
#include "base/sat_counter.hh"
#include "params/TageScImli128.hh"

namespace gem5 {
namespace branch_prediction {

class TageScImli128 : public BPredUnit
{
  public:
    using Params = TageScImli128Params;
    TageScImli128(const Params *p);

    bool lookup(ThreadID tid, Addr branch_addr, void *&bp_history) override;
    void uncondBranch(ThreadID tid, Addr pc, void *&bp_history);
    void btbUpdate(ThreadID tid, Addr branch_addr, void *&bp_history);
    void update(ThreadID tid, Addr branch_addr, bool taken,
                void *bp_history, bool squashed);
    void squash(ThreadID tid, void *bp_history);

  private:
    // ---- design constants (can be tuned) ----
    static constexpr unsigned kNumTables      = 12;     // TAGE tables
    static constexpr unsigned kTblEntries     = 4096;   // 4K per table
    static constexpr unsigned kIdxBits        = 12;     // log2(4096)
    static constexpr unsigned kBimodalEntries = 16384;  // 16K base
    static constexpr unsigned kIMLISize       = 512;    // regions
    static constexpr unsigned kMaxHist        = 704;    // longest history
    static constexpr int      kCtrMin         = -4;     // 3-bit signed
    static constexpr int      kCtrMax         = +3;
    static constexpr uint32_t kRegionShift    = 6;      // 64B region
    static constexpr int8_t   kThetaInit      = 8;      // SC threshold

    // tag bits per table (8..19)
    static constexpr std::array<unsigned, kNumTables> kTagBits = {
      8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19
    };

    // geometric history lengths (approx α≈1.6; Lmin=4)
    static constexpr std::array<unsigned, kNumTables> kHistLen = {
      4, 6, 10, 16, 26, 42, 67, 107, 172, 275, 440, 704
    };

    // ---- internal entry types ----
    struct TaggedEntry {
        int8_t   ctr;   // 3-bit signed in [-4..+3]
        uint16_t tag;   // variable-length stored in 16b, masked
        uint8_t  u;     // 2-bit usefulness
        TaggedEntry() : ctr(0), tag(0), u(0) {}
    };
    struct Table {
        std::vector<TaggedEntry> rows;
        unsigned tagBits = 0;
        Table() = default;
        Table(unsigned nEntries, unsigned tBits)
        : rows(nEntries), tagBits(tBits) {}
    };

    // per-thread global history buffer (bit deque, MSB at front)
    struct GHR {
        std::deque<uint8_t> bits; // 0/1
        GHR() : bits(kMaxHist, 0) {}
    };

    // predictor-private history for squash/update
    struct BPHistory {
        // snapshot for precise rollback
        std::vector<uint8_t> ghr_snapshot;
        // provider/alternate indices
        int  provTable = -1;
        int  provIdx   = -1;
        int  altTable  = -1;
        int  altIdx    = -1;
        bool predTaken = false; // final predicted dir
        bool usedSC    = false;
        bool usedIMLI  = false;
        // bimodal index for training
        int  baseIdx   = -1;
    };

    // ---- components ----
    std::vector<Table> tage;                 // TAGE tables
    std::vector<int8_t> bimodal;             // base 3-bit counter [-4..+3]
    std::vector<int8_t> sc1, sc2;            // 4K×8-bit each
    std::vector<int8_t> imli;                // 512×7-bit (we clamp)
    int8_t theta;                            // SC decision threshold
    std::vector<GHR> ghrs;                   // per-thread GHR

    // RNG for probabilistic allocation (lightweight, deterministic)
    std::mt19937 rng;

    // ---- helpers ----
    inline unsigned idxHash(Addr pc, const GHR &ghr, unsigned histLen) const;
    inline uint16_t tagHash(Addr pc, const GHR &ghr, unsigned histLen,
                            unsigned tagBits) const;
    inline int8_t   satInc3(int8_t v) const { return (v < kCtrMax) ? (v+1) : v; }
    inline int8_t   satDec3(int8_t v) const { return (v > kCtrMin) ? (v-1) : v; }
    inline int8_t   satInc8(int8_t v) const { return (v <  127) ? (v+1) : v; }
    inline int8_t   satDec8(int8_t v) const { return (v > -128) ? (v-1) : v; }

    void pushSpec(ThreadID tid, bool taken);
    void restoreGHR(ThreadID tid, const std::vector<uint8_t> &snap);

    // TAGE lookup: returns (provTable, provIdx, altTable, altIdx, dir)
    bool tageLookup(ThreadID tid, Addr pc, int &provT, int &provI,
                    int &altT, int &altI, bool &altDir, bool baseDir);

    // train components
    void trainTAGE(ThreadID tid, Addr pc, bool taken,
                   const BPHistory *h);
    void trainSC(Addr pc, ThreadID tid, bool taken,
                 const BPHistory *h);
    void trainIMLI(Addr pc, bool taken);
};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_TAGE_SC_IMLI128_HH__
