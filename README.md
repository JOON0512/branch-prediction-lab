# branch-prediction-lab

gem5에서 TAGE 태그 테이블, 통계 보정, IMLI 형태의 히스토리를 조합해 본 분기 예측기 초안. [2025 CE Challenge](https://github.com/cechallenge/2025) 환경에서 작업했습니다.

현재 기반 gem5 API와 맞지 않아 빌드되지 않습니다.

## Files

- `src/TageScImli128.cc`, `.hh` — 예측기 구현
- `patches/integration.patch` — gem5 등록과 CPU 설정
- `BASE_COMMIT` — 기반 저장소 커밋
- `licenses/` — 기반 코드의 라이선스

## Open issues

- 생성자의 `Params*`를 기반 API의 `Params&`로 변경
- `update`, `squash`, `updateHistories`와 speculative history 복원 구현
- 최대 19비트 태그가 16비트 필드에 잘리는 문제 수정
- 다중 스레드, 저장 용량, 예측 정확도 평가

파일명의 `128`은 목표 이름이며 측정한 저장 용량은 아닙니다.

## Setup

이 저장소 루트에서 기반 소스에 변경분을 적용합니다. 위 API 문제를 해결해야 빌드를 진행할 수 있습니다.

```sh
git clone https://github.com/cechallenge/2025.git ce-challenge
git -C ce-challenge checkout "$(cat BASE_COMMIT)"
git -C ce-challenge apply ../patches/integration.patch
cp src/TageScImli128.cc src/TageScImli128.hh ce-challenge/gem5/src/cpu/pred/
```
