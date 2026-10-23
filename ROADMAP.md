# 로드맵

## 1단계 (0.1.0): 커맨드렛 내보내기

상태: 완료 (첫 커밋 `4fae8f6`).

- 범용 리플렉션 내보내기, Blueprint/AnimBP(그래프·바인딩), StateTree(상태·전환·바인딩)
- 테스트 에셋 3개로 검증했고, unreal-mcp로 확인한 값과 일치했습니다.
- 에디터를 켠 상태에서도 동작합니다. 이때도 디스크에 저장된 버전을 읽습니다.
- 같은 에셋은 실행할 때마다 바이트 단위로 같은 JSON이 나옵니다.
- 측정 결과 (MomentTrigger, 3개 에셋, 2026-10-23)

| 단계 | 시간 |
|---|---|
| 엔진 시작 | 8.6~12.2초 |
| 에셋 목록 스캔 | 0.26초 |
| 로드 + 내보내기 | 약 0.5초 |
| 전체 | 9.9~13.5초 |

  - 비용은 거의 전부 엔진 시작에서 나옵니다.
  - 그래서 스캔 범위를 줄이는 최적화는 하지 않기로 했습니다.

## 엔진 내장 MCP와의 관계 (2026-10-23 조사)

UE 5.8에는 `ModelContextProtocol`("Unreal MCP") 플러그인, `ToolsetRegistry`, 툴셋 모음(`Experimental/Toolsets/*`)이 들어 있습니다. 실행 중인 서버에서 도구 목록을 받아 보니 52개 툴셋, 833개 도구였고, 전부 에디터가 켜져 있어야 동작합니다. 조사할 때는 읽기 도구만 호출했고, 에셋은 바뀌지 않았습니다.

같은 에셋을 양쪽으로 읽어 비교한 결과:

| 항목 | 내장 MCP | 이 플러그인 |
|---|---|---|
| StateTree (`ST_Boss`) | 120번 호출, 49K자 | 1번 실행 |
| StateTree 바인딩 | 설명 문장만 | 경로로 정리된 목록 |
| Blueprint (`BP_GA_EnemyDodge`) | 13번 호출, 20K자 | 2.3K자 |
| BP 그래프 | `read_graph_dsl`이 짧은 스크립트로 보여 줌 (더 읽기 쉬움) | 노드·핀 JSON |
| AnimBP, 스테이트 머신 | 전용 도구 없음 | 있음 |
| GA/GE, 몽타주, 노티파이 | 전용 도구 없음 | 범용 내보내기로 일부 |
| 에디터를 끈 상태 | 불가 | 가능 |
| 나이아가라 Simulation Stage | 구조상 담을 수 없음 | 2단계에서 추가 |

- BP 그래프는 내장 쪽 요약 형식이 더 읽기 쉽습니다. 2단계 MCP 도구에서 그래프 요약을 만들 때 참고합니다(형식만 참고하고, 구현은 우리 것으로 따로 만듭니다).
- **나이아가라 근거**: 내장 도구가 이미터 구조를 담는 `FNiagaraExt_EmitterTopology`(`NiagaraEditor/Public/NiagaraExternalSystemEditorUtilities.h`)에는 스택 4개(EmitterSpawn, EmitterUpdate, ParticleSpawn, ParticleUpdate) 필드만 있고, Simulation Stage 필드가 없습니다. 나이아가라 플루이드는 계산 대부분을 Simulation Stage에서 하므로, 내장 도구로는 핵심 로직이 보이지 않습니다. 실제로 `NS_HeatLaser_Fluid`에서는 기본 스택 4개 조회도 실패했습니다(원인 미확인).

역할 나누기:

- **이 플러그인**: 읽기와 분석. 에디터를 끈 상태에서도 되고, 큰 구조를 한 번에 정리해서 줍니다.
- **내장 MCP**: 수정, PIE, 테스트, 로그, Config, 플러그인 관리, 머티리얼·나이아가라·PCG 편집.

## 2단계: 질문할 때마다 엔진을 띄우지 않기 (B 구조)

### 연결 구조 (2026-10-23 결정)

```
Claude Code ──→ 127.0.0.1:8000/mcp  (설정은 하나)
   ├ 에디터 켜짐: 에디터 안의 Unreal MCP 서버 + 우리 도구 (저장 안 한 변경까지 보임)
   └ 에디터 꺼짐: Serve.bat → 상주 커맨드렛이 같은 서버를 띄움 + 우리 도구 (디스크 기준)
```

- **도구 등록**: `IModelContextProtocolModule::AddTool`로 내장 MCP 서버에 직접 등록합니다. `ToolsetRegistry`를 거치지 않습니다.
  - 직접 등록한 도구는 `tools/list`에 바로 나옵니다(`ModelContextProtocolServer.cpp`의 `ProcessListToolsJsonRpcCall`). 툴셋 도구는 `list_toolsets` → `describe_toolset` → `call_tool`을 거쳐야 합니다.
  - 결과 형식을 우리가 정합니다. ToolsetRegistry를 거치면 문자열 결과가 `{"returnValue":"..."}` 안에 한 번 더 감싸져 따옴표가 전부 `\"`가 됩니다(내장 `list_properties` 결과로 확인).
- **상주 모드**: 커맨드렛이 내장 서버를 띄우고 끄지 않고 기다립니다.
  - 엔진이 이 방식을 허용합니다. 커맨드렛에서는 `-ModelContextProtocolStartServer`로 직접 켜라는 주석이 있습니다(`ModelContextProtocolSettings.cpp`). 에디터 커맨드렛도 `UEditorEngine`을 만들고 PostEngineInit까지 거칩니다(`LaunchEngineLoop.cpp`).
  - HTTP 서버와 MCP 서버는 `FTSTicker`로 돌아가므로, 커맨드렛 루프에서 ticker를 돌리면 됩니다(`HttpServerModule.h`, `ModelContextProtocolServer.cpp`).
  - C++ 빌드 전에는 DLL 잠금 때문에 꺼야 합니다. `age_shutdown`, 유휴 시간 자동 종료, 에디터가 켜지면 자동 종료로 대응합니다.
- **폴더**: MCP 부분은 별도 플러그인 `AssetGraphExporterMcp`로 둡니다. Unreal MCP가 Experimental이라 영향 범위를 좁히고, 기본 플러그인을 쓰는 모든 프로젝트에서 Unreal MCP가 강제로 켜지지 않게 하기 위해서입니다.
- **요약**: 요약 함수(내보낸 JSON → 짧은 텍스트)는 기본 플러그인에 둡니다. MCP 없이도 쓸 수 있습니다.

### 진행 순서 (확정)

1. **2-1. MCP 연결 틀 + AnimBP·StateTree 도구**
   - 도구 직접 등록, 상주 커맨드렛(`AssetGraphServe`), `Serve.bat`.
   - 도구: `age_find_assets`, `age_get_asset`, `age_statetree_overview`, `age_statetree_bindings`, `age_animbp_overview`, `age_status`, `age_shutdown`.
   - 상태: 완료 (2026-10-24, MomentTrigger). 빌드 경고 0. 상주 서버 모드와 에디터 모드 모두 7개 도구가 최상위 목록에 나오고 동작함. 내장 `call_tool`도 에디터 없이 동작함.
   - 요약 크기: ST_Boss 3.1K자(전체 JSON 39.7K, 내장 MCP 120번 호출 49K), ABP_Boss_SteampunkCat 1.8K자(전체 JSON 127K, 공백 없는 기준).
   - 시간: 상주 서버 준비 62초(재부팅 뒤 첫 실행, Zen 서버 시작과 셰이더 준비 포함), 이후 호출 4–460 ms. 에디터 모드는 호출마다 약 0.33초(에디터 프레임 간격으로 보임).
   - 남은 확인: 상주 중 다시 저장된 에셋 다시 읽기(`Content/`를 바꿔야 해서 미시험), 재부팅 없이 다시 켰을 때의 준비 시간.
2. **2-2. 나이아가라 처리기**
   - 시스템 → 이미터 → 스택(Simulation Stage 포함) → 모듈 → 입력값, 렌더러, Grid 데이터 인터페이스.
   - 엔진이 스택을 구성하는 코드를 먼저 읽고 설계합니다.
3. **2-3. 검색·diff + 캐시**
   - C++ ↔ BP 교차 검색, 에셋 참조 관계, git 리비전 diff.
   - 캐시 + 증분 내보내기(바뀐 `.uasset`만 다시 내보냄)는 여기서 만듭니다. 상주 서버가 매번 최신 파일을 읽으므로 2-1에는 필요 없고, 전체 검색과 리비전 비교에 필요하기 때문입니다.
4. **BP 그래프 요약(실행 흐름)**: 2-1 범위 밖. 전환 규칙을 식으로 바꾸는 코드를 다시 쓸 수 있으므로 2-1 뒤에 다시 정합니다.

### 2단계 후보 기능 (설명)

- **C++ ↔ BP 교차 검색**: 함수 호출 노드와 변수 노드에 대상 클래스·멤버가 정확히 기록돼 있으므로, 추정이 아니라 정확한 값으로 답할 수 있습니다.
- **리플렉션 심볼 덤프**: UCLASS/UFUNCTION/UPROPERTY와 헤더 위치(메타데이터). 한계: UFUNCTION이 아닌 일반 C++ 함수는 나오지 않습니다.
- **에셋 참조 관계**: Asset Registry로 "누가 이 에셋을 쓰나"와 "이 에셋이 무엇에 의존하나"를 뽑습니다.
- **에셋 diff**: git의 이전 리비전과 현재 버전을 내보내 JSON끼리 비교합니다.
- **연결 고리 추적**: GA → 몽타주 → 애님 노티파이(시간순) → GE. AnimMontage 처리기가 필요합니다.
- **설정 최종값**: `GConfig`에서 모든 레이어를 합친 최종값과, 그 값을 정한 파일을 보여 줍니다.
- **BP 컴파일 진단**과 **엔진 업그레이드 점검**: 고아 핀, 없어진 함수를 가리키는 노드 찾기.

## 3단계 후보: 에디터를 끈 상태에서의 수정

에디터를 켠 상태의 수정은 엔진 내장 MCP가 이미 하고 있습니다. 그래서 이 단계의 차별점은 "에디터를 끈 상태에서 안전하게 수정"입니다.

- 반드시 이 순서로 동작합니다: 변경안을 먼저 보여 줌(실제 저장 안 함) → 승인 → 저장 → 다시 내보내 diff로 검증
- git에 커밋된 상태에서만 실행합니다.
- 에디터가 켜져 있으면 실행하지 않습니다.

## 보류

- `.uasset` 직접 파싱(엔진 없이 읽기): 2단계 구조로도 속도가 부족할 때 다시 검토합니다.
