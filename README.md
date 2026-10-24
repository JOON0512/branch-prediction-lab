# Branch prediction lab — experimental archive

2025 CE Challenge 환경에서 작업하던 gem5 분기 예측기 실험 코드를 보관합니다. TAGE 형태의 태그 테이블, 통계 보정, IMLI 형태의 상태를 조합하려던 구현 초안입니다. **현재 기반 gem5와 API가 맞지 않아 빌드 가능한 완성본이 아닙니다.** 성능 개선이나 대회 점수를 재현했다고 주장하지 않습니다.

## 포함한 파일

- `src/TageScImli128.cc`, `.hh`: 로컬에 남아 있던 예측기 구현
- `patches/integration.patch`: gem5 등록 및 CE Challenge CPU 설정 변경
- `BASE_COMMIT`: 작업한 [cechallenge/2025](https://github.com/cechallenge/2025) 기반 커밋
- `licenses/`: 기반 프로젝트의 라이선스와 수정 대상 파일의 원래 헤더

전체 gem5 소스, 대회 데이터셋과 개인 저장소 이력은 이 보관본에 포함하지 않습니다. 파일 이름의 `128`은 검증된 128 KiB 저장 예산을 뜻하지 않습니다.

## 확인된 미완료 항목

1. 생성자가 포인터 `Params*`를 받지만 기반 gem5는 참조 `Params&`를 사용합니다.
2. `update`, `squash` 및 히스토리 처리 인터페이스가 기반 `BPredUnit`과 다릅니다. `updateHistories` 구현도 필요합니다.
3. 최대 19비트 태그를 16비트 필드에 저장하는 부분이 있어 상위 비트가 잘립니다.
4. speculative history 복원, 다중 스레드, 실제 저장 용량과 정확도 평가가 필요합니다.

## 원래 작업 배치 복원

아래 절차는 개발 배치만 복원하며 빌드 성공을 의미하지 않습니다. 호환성 문제를 수정한 뒤 별도 검증해야 합니다.

```sh
git clone https://github.com/cechallenge/2025.git ce-challenge
git -C ce-challenge checkout "$(cat BASE_COMMIT)"
git -C ce-challenge apply ../patches/integration.patch
cp src/TageScImli128.cc src/TageScImli128.hh ce-challenge/gem5/src/cpu/pred/
```

2026-10-24에 로컬 변경을 복원하고 기반 API와 대조했습니다. 시뮬레이터 빌드·워크로드 실행·성능 측정은 수행하지 않았습니다. 기존 파일의 라이선스 조건은 그대로 유지되며 이 저장소 전체에 새 단일 라이선스를 적용하지 않습니다.
