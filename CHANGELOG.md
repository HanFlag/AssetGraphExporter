# 변경 기록

## 0.2.0 (베타) — 2026-10-27

첫 공개 릴리즈입니다. 에셋 구조 내보내기(0.1.0)에 MCP 도구, 나이아가라 처리기, 프로젝트 전체 검색과 버전 비교를 더했습니다.

### 요구 사항
- Unreal Engine 5.8, Win64 (5.8.4에서 빌드와 동작 확인).
- MCP 도구(`AssetGraphExporterMcp`): 엔진에 들어 있는 Unreal MCP 플러그인(`ModelContextProtocol`, Experimental).
- 선택 사항: StateTree, Niagara 플러그인. 없으면 그 처리기만 빠지고 나머지는 동작합니다.
- `age_diff`: git이 PATH에 있어야 합니다.

### 들어 있는 것
- **내보내기 (`AssetGraphExporter`)**
  - 커맨드렛 `AssetGraphExport`: 모든 UObject 에셋을 JSON으로. Blueprint·AnimBP(그래프, 노드, 핀, 바인딩, 스테이트 머신), StateTree(상태, 전환, 바인딩), Niagara System(이미터 스택, Simulation Stage, 모듈 입력값).
  - `-Incremental`: 바뀐 에셋(자신이나 의존 에셋, C++ 코드)만 다시 내보내기.
  - 같은 에셋은 매번 바이트 단위로 같은 JSON. JSON 스키마 버전 2.
- **MCP 도구 (`AssetGraphExporterMcp`)**: 에디터 안이나, 에디터 없이 띄우는 상주 서버(`Serve.bat`)에서 같은 주소로 동작.
  - `age_find_assets`, `age_get_asset`: 에셋 찾기, 구조 JSON 일부 꺼내기.
  - `age_blueprint_overview`, `age_animbp_overview`, `age_statetree_overview`, `age_statetree_bindings`, `age_niagara_overview`: 짧은 요약. BP는 실행 흐름을 한 줄씩.
  - `age_references`: 에셋이 쓰는 것과 그 에셋을 쓰는 것.
  - `age_find_usages`: C++ 클래스·함수·변수를 쓰는 BP 노드.
  - `age_diff`: git 예전 버전과 지금의 차이.
  - `age_status`, `age_shutdown`, 활동 창(`/age/activity`).

### 확인한 것과 아직 확인하지 않은 것
- 확인: 한 프로젝트(UE 5.8.4, Win64)에서 빌드 경고 0, 모든 도구를 상주 서버 모드로 호출. 처음 7개 도구는 에디터 모드에서도 호출.
- 아직: 다른 프로젝트, 다른 플랫폼. `age_references`·`age_find_usages`·`age_diff`·`age_blueprint_overview`의 에디터 모드. 에셋을 다시 저장한 뒤 상주 서버와 증분 내보내기가 새 내용을 읽는지(시험하려면 에셋을 바꿔야 해서).
- 그 밖의 한계는 [README의 알려진 한계](README.md#알려진-한계-020)를 보세요.

### 라이선스
[MIT](LICENSE).

## 0.1.0 — 2026-10-23 (공개 안 함)
- 커맨드렛 내보내기: 범용 리플렉션, Blueprint/AnimBP, StateTree.
