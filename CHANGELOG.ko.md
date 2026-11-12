[English](CHANGELOG.md) | **한국어**

# 변경 기록

## 아직 릴리즈 안 함

- Unreal Engine 5.9에서 빌드됩니다(5.8에서는 더 이상 빌드되지 않습니다. 5.8.4에서 마지막으로 돌아가던 상태는 태그 `ue5.8`입니다).
- `age_niagara_overview`가 기본으로 짧은 개요(모듈 이름만)를 돌려줍니다. `emitter`는 이미터 하나를, `full`은 전체를 예전처럼 자세히 보여 줍니다.
- 새 도구 `age_level_actors`: 에디터에 열린 레벨에 무엇이 어디에 있는지 호출 한 번에. 상자나 반경 안(또는 레벨 전체)의 액터를 한 줄에 하나씩, 라벨, 오브젝트 이름, 클래스, 위치, 회전, 크기, 세계 축 경계 상자, 메시 에셋과 함께 보여 줍니다. 경계 상자는 기본으로 카메라 시야 표시처럼 에디터에서만 쓰는 부품을 뺍니다. 월드 파티션에서 로드 안 된 곳의 액터는 로드하지 않고 저장된 경계 상자로 보여 줍니다.
- `AGEActorBounds.h`: 다른 플러그인이 서버에 더한 도구도 같은 경계 상자를 쓰도록.
- 스켈레톤과 스켈레탈 메시에 `bones` 칸이 생깁니다: 뼈마다 이름과 부모. 스켈레탈 메시는 그 스켈레톤에는 있지만 메시에는 없는 뼈도 보여 줍니다(`skeletonBonesNotInMesh`).
- `age_status`가 고치기 플러그인이 설치됐는지 알려 주고, 설치돼 있으면 `age_guide`가 "읽기만 한다" 대신 그 도구를 안내합니다.
- 벡터, 회전, 색처럼 숫자 4개 이하로 된 구조체는 한 칸이라도 기본값과 다르면 통째로 씁니다. 바라보기 축 (0, -1, 0)이 `{"Y":-1}`로 나와 "Y만 설정됨"처럼 읽혔는데, 이제 `{"X":0,"Y":-1,"Z":0}`입니다. 이 때문에 대부분 에셋의 내보내기 결과가 한 번 바뀝니다(컴포넌트 위치·회전·색).
- 애니메이션(과 다른 시퀀서 데이터)을 디스크에서 다시 불러와도 같게 내보내집니다. 엔진이 바뀜 표시 때마다 새로 주는 `Signature` GUID를 더 이상 쓰지 않습니다.
- `age_get_asset`에 `details: true`: 값마다 Details 패널의 이름과 섹션이 붙고(`$details`, 예: `InitialUpdateFunction`은 "Functions" 아래 "On Initial Update"), Blueprint 변수에는 표시 이름이 붙습니다. 기본 출력은 그대로입니다.
- `age_animbp_overview`가 AnimGraph 노드마다 바꾼 설정을 뒤에 붙입니다. 예: `Look At Bone: Head_M(Look at Location=@LookAtTarget) [LookAt_Axis.Axis=(0,-1,0), LookAtClamp=30, InterpolationType=Sinusoidal]`.

## 0.3.1 (베타) — 2026-11-03

- **라이선스**: MIT에서 [PolyForm Strict License 1.0.0](LICENSE)으로 바꿨습니다. 비상업적 용도로는 쓸 수 있고, 고치거나, 이를 바탕으로 새로 만들거나, 나눠 주거나 파는 것은 안 됩니다. 상업적 용도는 HanFlag의 서면 허락이 있어야 하며, 보통 유료 라이선스로 허락합니다. 자세한 내용은 README에 있습니다.

## 0.3.0 (베타) — 2026-11-03

라이선스가 MIT로 바뀌고, 문서가 영어·한국어 두 가지로 나오고, 활동 창이 따로 도는 작은 프로그램이 됐습니다.

- **라이선스**: 테스트용 라이선스에서 [MIT](LICENSE)로 바꿨습니다.
- **문서**: 기본 언어를 영어로 바꾸고, 문서마다 한국어판(`*.ko.md`)을 두었습니다. 각 문서 맨 위의 `English | 한국어`로 오갑니다.
- **고지**: README의 "만든 과정과 고지"를 "고지"로 바꿨습니다(Epic Games와 무관·상표, EULA에 따른 AI 데이터 안내, 보증 없음).
- 활동 페이지(`/age/activity`)가 브라우저 언어를 따릅니다. 한국어 브라우저에서는 한글, 그 밖에는 영어로 나옵니다.
- **활동 창**: 서버를 켤 때마다 열리던 브라우저 탭 대신 작은 Windows 프로그램을 씁니다. 서버가 꺼져도, 에디터를 닫아도, 빌드해도 꺼지지 않고, 서버가 돌아오면 같은 창이 이어 갑니다. AI 연결, 작업 하나하나, 작업 끝남(1분 동안 새 요청 없음)을 보여 줍니다. 닫으면 다음에 서버를 켤 때 다시 뜨고, **옵션**에서 그날은 안 뜨게 하거나 밝은·어두운 테마를 고를 수 있습니다. 새 도구 `age_open_window`와 `AGE.OpenActivity`로 열 수 있습니다. 자세한 내용은 사용 안내에 있습니다.
- 활동 창에 전용 아이콘이 생겼습니다.
- 다른 플러그인이 같은 MCP 서버에 추가한 도구도 `AGEMcpToolLog.h`의 함수 2개를 부르면 `age_*` 도구처럼 활동 페이지와 창에 나옵니다.
- 활동 페이지에 서버가 응답하는지와 마지막 활동 시각이 글자로 나오고, 에디터에서는 AI가 연결하기 전에도 페이지가 응답합니다.
- 고침: 에디터가 한동안 바빴다가 돌아오면 활동 페이지에 같은 항목이 두 번 나올 수 있던 문제.
- 문서와 도구 설명의 예시를 특정 프로젝트와 무관한 이름으로 바꿨습니다.

## 0.2.2 (베타) — 2026-10-27

사용법을 설치·연결·시작 3단계로 줄였습니다.

- **서버 자동 켜기**: 에디터가 열리면 플러그인이 Unreal MCP 서버를 스스로 켭니다. 엔진의 "Auto Start Server"는 사용자마다 따로 켜야 하는 설정이라, 그 단계를 없앴습니다. 끄려면 `-AGENoServerAutoStart`. 상주 서버가 막 꺼지는 중이면 잠시 다시 시도합니다.
- **에디터 없이 쓰기는 AI가 켭니다**: `AI_GUIDE.md`에 엔진 위치를 찾아(레지스트리의 `InstalledDirectory`) 상주 서버를 직접 켜는 방법을 적었습니다. 사용자는 명령을 칠 필요가 없습니다.
- README: 사용법 3단계, 어려운 명령 줄은 사용 안내로 옮김.
- `Serve.bat`: 에디터가 켜져 있는지 보는 확인이 Git Bash에서 실행하면 건너뛰어지던 문제를 고쳤습니다(윈도우 `find`·`tasklist`를 전체 경로로 부름). Git Bash에서 에디터가 켜져 있을 때 1초 만에 거절하고, 꺼져 있을 때는 정상으로 켜지는 것을 확인했습니다.
- 에디터 모드에서 `age_references`와 `age_guide` 동작을 확인했습니다(상주 모드와 결과 같음).

## 0.2.1 (베타) — 2026-10-27

AI가 이 도구를 스스로 익히고, 문제를 사용자를 통해 알려 오게 했습니다.

- **AI용 안내 `AI_GUIDE.md`**(영어, `AssetGraphExporterMcp/Resources/`): 처음에 사용자에게 알릴 것(무엇인지, 할 수 있는 것과 아직 못 하는 것, 확인 모드·자동 모드), 단계별 연결, 질문별 도구, 효율 규칙, 문제 해결, 피드백 부탁.
- **`age_guide`**: 도구 설명에 "처음 쓸 때 먼저 부를 것"이 있어, 연결한 AI가 안내를 받습니다.
- **`age_feedback`**: 문제나 필요한 기능을 사용자 PC(`Saved/AssetGraphExporter/Feedback/feedback.md`, `-AGEFeedbackDir=`로 변경)에 남기고, 내용이 채워진 GitHub 이슈 화면을 브라우저로 엽니다. 사용자가 확인하고 Submit을 누릅니다. 프로젝트 에셋 경로와 사용자 폴더 경로는 자동으로 지웁니다. 받는 곳은 플러그인의 `SupportURL`입니다.
- `age_*` 도구가 실패하면 결과 끝에 "막혔다면 사용자에게 알리고 age_feedback으로 남겨 달라"는 한 줄이 붙습니다.
- **라이선스**: MIT에서 테스트용 라이선스(초대받은 사람이 시험·피드백 용도로만)로 바꿨습니다.
- 에디터 모드에서 `age_find_usages`, `age_diff`, `age_blueprint_overview`, `age_statetree_overview`, `age_niagara_overview` 동작을 확인했습니다.

## 0.2.0 (베타) — 2026-10-27

첫 릴리즈입니다. 에셋 구조 내보내기(0.1.0)에 MCP 도구, 나이아가라 처리기, 프로젝트 전체 검색과 버전 비교를 더했습니다.

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
- 그 밖의 한계는 [알려진 한계](Documentation/Guide.ko.md#알려진-한계)를 보세요.

### 라이선스
MIT.

## 0.1.0 — 2026-10-23 (릴리즈 안 함)
- 커맨드렛 내보내기: 범용 리플렉션, Blueprint/AnimBP, StateTree.
