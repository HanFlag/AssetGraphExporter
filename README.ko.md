[English](README.md) | **한국어**

# AssetGraphExporter

Claude와 함께 Unreal을 쓸 때, Claude가 엔진 내장 Unreal MCP보다 나은 방식으로 에셋을 읽고, 고치고, 결과를 확인하게 하는 UE 5.8 에디터 플러그인입니다. 어느 프로젝트에나 넣어 씁니다.

지금 버전(0.2.2)에는 에셋을 **에디터를 끈 상태에서도** 읽고 분석하는 기능이 들어 있습니다. 다음은 직접 고치고 실행해서 확인하는 기능입니다(확인 모드가 기본, 자동 모드 선택 가능).

[릴리즈 받기](https://github.com/HanFlag/AssetGraphExporter/releases) · [AI용 안내](AssetGraphExporterMcp/Resources/AI_GUIDE.md) · [사용 안내](Documentation/Guide.ko.md) · [JSON 스키마](Documentation/JsonSchema.ko.md) · [변경 기록](CHANGELOG.ko.md) · [로드맵](ROADMAP.ko.md)

## 사용법

이 도구는 Claude가 씁니다. 사람은 설치하고 연결한 뒤, Claude에게 말로 물으면 됩니다.

1. **설치**: 릴리즈 zip을 풀어 `AssetGraphExporter`, `AssetGraphExporterMcp` 두 폴더를 `<프로젝트>/Plugins/`에 넣고 에디터를 엽니다. 서버는 플러그인이 알아서 켭니다.
2. **연결**: 터미널에서 한 번 실행합니다.
   ```
   claude mcp add --transport http unreal http://127.0.0.1:8000/mcp
   ```
3. **시작**: Claude에게 "age_guide를 읽고 시작해 줘"라고 말합니다. 그다음부터는 그냥 물어보면 됩니다.
   - "BP_Enemy가 하는 일 요약해 줘"
   - "이 C++ 함수 이름 바꾸면 어디가 깨져?"
   - "지난 커밋 이후 StateTree에서 뭐가 바뀌었어?"

에디터를 끈 채로 쓰고 싶으면 Claude에게 "에디터 없이 쓸래"라고 하면 Claude가 켜 줍니다. 막히면 Claude에게 "`Plugins/AssetGraphExporterMcp/Resources/AI_GUIDE.md`를 읽고 도와줘"라고 하세요. 설치의 다른 방법과 모든 옵션은 [사용 안내](Documentation/Guide.ko.md)에 있습니다.

## 왜 쓰나

엔진 내장 Unreal MCP와 같은 에셋으로 비교한 결과입니다(자세한 측정은 [로드맵](ROADMAP.ko.md)).

| | 내장 Unreal MCP | 이 플러그인 |
|---|---|---|
| 에디터를 끈 상태 | 불가 | 상주 서버로 가능 |
| StateTree 하나 읽기 | 120번 호출, 49K자 | 1번, 3.1K자 |
| Blueprint 하나 읽기 | 13번 호출, 20K자 | 2.3K자 |
| 나이아가라 Simulation Stage | 읽지 못함 | 읽음 (유체 시스템에서 25개) |
| AnimBP 전환 규칙 | 전용 도구 없음 | 식으로 (`if (bIsStrafing AND (Speed2D < 10))`) |

- **프로젝트 전체 질문**: 누가 이 에셋을 쓰나, 이 C++ 함수를 어느 BP가 부르나, 지난 커밋 뒤 이 BP에서 무엇이 바뀌었나.
- **같은 에셋은 매번 같은 JSON**: 그래서 비교와 캐시가 됩니다(증분 내보내기 13.0초 → 0.57초).
- **범용**: 코드에 프로젝트 이름이 없어서 어느 프로젝트에나 그대로 넣습니다.

## 도구

| 도구 | 하는 일 |
|---|---|
| `age_blueprint_overview` · `age_animbp_overview` | BP 요약: 변수와 기본값, 실행 흐름을 한 줄씩. AnimBP는 포즈 흐름과 스테이트 머신까지 |
| `age_statetree_overview` · `age_statetree_bindings` | StateTree 상태, 조건, 태스크, 전환, 바인딩 |
| `age_niagara_overview` | 이미터마다 모든 스택(Simulation Stage 포함)의 모듈과 입력값 |
| `age_references` | 이 에셋이 쓰는 것, 이 에셋을 쓰는 것 |
| `age_find_usages` | C++ 클래스·함수·변수를 쓰는 BP 노드 |
| `age_diff` | git 예전 버전과 지금의 차이 |
| `age_find_assets` · `age_get_asset` | 에셋 찾기, 구조 JSON 일부 꺼내기 |
| `age_status` · `age_shutdown` | 상태 확인, 상주 서버 끄기 |
| `age_guide` · `age_feedback` | AI가 처음 읽는 사용 안내, 문제 보고(내용이 채워진 GitHub 이슈 화면을 열어 사용자가 Submit) |

## 요구 사항

- Unreal Engine 5.8, Win64 (5.8.4에서 확인). 릴리즈 zip에는 5.8.4 Win64용 빌드가 들어 있습니다. 다른 엔진 버전에서는 C++ 프로젝트에 넣어 소스로 빌드해야 합니다.
- MCP 도구: 엔진에 들어 있는 Unreal MCP 플러그인(`ModelContextProtocol`, Experimental).
- 선택: StateTree, Niagara 플러그인(없으면 그 처리기만 빠짐). `age_diff`는 git이 필요합니다.
- 아직 하지 않는 것: 에셋 수정(다음 버전), 레벨 내용 분석. 그 밖의 한계는 [알려진 한계](Documentation/Guide.ko.md#알려진-한계).

## 고지

- 개인이 따로 만든 프로젝트입니다. Epic Games, Inc.와 관계가 없고, Epic Games가 후원하거나 보증하지 않습니다. Unreal, Unreal Engine, Unreal MCP는 미국과 그 밖의 나라에서 Epic Games, Inc.의 상표 또는 등록 상표입니다.
- 이 도구는 Unreal MCP를 통해 프로젝트 데이터를 연결된 AI에 보냅니다. Unreal Engine EULA에 따라, 쓰는 AI 서비스가 그 데이터를 학습에 쓰지 않도록 하는 것은 사용자 책임입니다.
- 이 소프트웨어는 어떤 보증도 없이 "있는 그대로" 제공됩니다. [LICENSE](LICENSE)를 보세요.

## 라이선스

[MIT](LICENSE) © 2026 HanFlag. 누구나 쓰고, 고치고, 나눠 주고, 팔 수 있습니다. 단 저작권 표시와 라이선스 문구를 함께 남겨야 합니다. 법적으로는 영어 원문 [LICENSE](LICENSE)가 기준입니다.
