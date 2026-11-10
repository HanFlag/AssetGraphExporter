[English](Guide.md) | **한국어**

# 사용 안내

설치, 커맨드렛과 MCP 도구 사용법, 확장 방법, 알려진 한계입니다. 출력 형식은 [JSON 스키마](JsonSchema.ko.md)를 보세요.

## 폴더 구조

```
AssetGraphExporter/            ← 저장소
  AssetGraphExporter/          ← 플러그인 본체 (설치할 때 이 폴더를 복사)
    AssetGraphExporter.uplugin
    Source/AssetGraphExporter/            범용 처리 + Blueprint/AnimBP + 커맨드렛 + 요약 (Editor 모듈)
    Source/AssetGraphExporterStateTree/   StateTree 처리기 (Editor 모듈)
    Source/AssetGraphExporterNiagara/     Niagara System 처리기 (Editor 모듈)
  AssetGraphExporterMcp/       ← MCP 연결 플러그인 (선택 설치, Unreal MCP 엔진 플러그인 필요)
    AssetGraphExporterMcp.uplugin
    Source/AssetGraphExporterMcp/         age_* 도구 + 상주 커맨드렛 AssetGraphServe + 활동 창 (Editor 모듈)
    Source/ActivityWindow/                활동 창 (작은 C# 프로그램, Build.bat이 빌드)
    Resources/Activity.html               활동 페이지
  Scripts/                     빌드·실행용 배치 파일
  Packaged/                    Build.bat 결과물 (git 무시)
  Logs/                        빌드·실행 로그 (git 무시)
```

## 설치

### 방법 A: 프로젝트에 복사 (일반적인 설치)

1. `AssetGraphExporter/` 폴더를 `<프로젝트>/Plugins/AssetGraphExporter/`로 복사합니다.
2. 프로젝트를 빌드합니다. C++ 프로젝트라면 IDE에서 Editor 타깃을 빌드하면 됩니다.

`.uproject`에 따로 등록하지 않아도 됩니다. 엔진은 프로젝트 `Plugins/` 폴더에 있는 플러그인을 `EnabledByDefault`가 지정되지 않은 한 자동으로 켭니다(`FPlugin::IsEnabledByDefault`).

### 방법 B: 프로젝트를 건드리지 않고 사용

1. `Scripts\Build.bat`로 플러그인만 따로 빌드합니다. 엔진의 `BuildPlugin` 명령을 쓰며, 결과는 `Packaged\`에 생깁니다.
2. 실행할 때 `-PLUGIN=<.uplugin 경로>` 옵션으로 불러옵니다. `Scripts\Export.bat`이 이 옵션을 자동으로 붙입니다.

주의: 방법 B에서 StateTree 처리기가 로드되려면, 대상 프로젝트가 이미 StateTree 플러그인을 켜고 빌드되어 있어야 합니다. 그렇지 않은 프로젝트에서는 방법 A를 쓰세요.

### StateTree 의존성

StateTree 처리기 모듈은 `StateTree` 플러그인에 의존하며, 이 의존성은 `.uplugin`에 `"Optional": true`로 걸려 있습니다.
엔진 소스(UBT `UEBuildTarget.cs`, `PluginManager.cpp`)로 확인한 동작은 다음과 같습니다.

- Optional은 "플러그인 파일이 없으면 무시"라는 뜻입니다.
- StateTree는 엔진에 기본으로 들어 있습니다. 따라서 이 플러그인을 넣으면 StateTree를 안 쓰는 프로젝트에서도 에디터에서는 StateTree가 함께 켜집니다.
- `TargetAllowList: Editor`를 걸어 두었기 때문에 게임 빌드에는 영향이 없습니다.

나이아가라 처리기 모듈(`AssetGraphExporterNiagara`)도 같은 방식으로 `Niagara` 플러그인에 Optional로 의존합니다. 나이아가라는 엔진 기본으로 켜져 있는 플러그인(`EnabledByDefault: true`)이라, 이 의존성 때문에 새로 켜지는 것은 없습니다.

## 사용법

```
UnrealEditor-Cmd.exe <Project>.uproject -run=AssetGraphExport [옵션]
```

| 옵션 | 설명 |
|---|---|
| `-Assets=<경로들>` | 쉼표로 구분한 패키지 경로입니다. `/Game/Folder/Asset`과 `/Game/Folder/Asset.Asset` 둘 다 받습니다. |
| `-Path=<폴더들>` | 쉼표로 구분한 폴더입니다. 하위 폴더까지 모두 포함합니다. |
| `-Class=<클래스들>` | 클래스 필터입니다(하위 클래스 포함). 네이티브 클래스는 짧은 이름(`Blueprint`, `StateTree`, `GameplayAbility`)으로, BP 클래스는 전체 경로(`/Game/BP_Base.BP_Base_C`)로 씁니다. Blueprint는 부모 클래스로도 맞춥니다. 예를 들어 `-Class=GameplayAbility`로 GA 블루프린트를 찾을 수 있습니다. |
| `-Out=<폴더>` | 출력 폴더입니다. 기본값은 `<프로젝트>/Saved/AssetExport`이고, 상대 경로는 프로젝트 폴더를 기준으로 합니다. |
| `-Full` | 기본값과 같은 값까지 전부 씁니다. 기본 동작은 기본값과 다른 값만 쓰는 것입니다(아래 참고). |
| `-Incremental` | 같은 `-Out` 폴더에 지난번 내보낸 결과가 있으면, 바뀌지 않은 에셋은 로드하지 않고 그 파일을 그대로 둡니다. 지난번에는 있었는데 이번에는 없는 에셋의 파일은 지웁니다(지난 `index.json`에 적힌 파일만). 아래 [증분 내보내기](JsonSchema.ko.md#증분-내보내기--incremental) 참고. |

`-Assets`나 `-Path` 중 하나 이상은 반드시 지정해야 합니다.
종료 코드는 모든 에셋이 성공하면 0, 아니면 1입니다.
대상 프로젝트의 에디터를 켜 둔 채로 실행해도 됩니다(UE 5.8.4에서 확인).
커맨드렛은 에디터와 별개의 프로세스이고, **디스크에 저장된 에셋**을 읽습니다.
에디터에서 고치고 아직 저장하지 않은 변경은 나오지 않으므로, 최신 내용을 보려면 먼저 저장하세요.
(에디터 메모리를 직접 읽는 unreal-mcp 같은 도구와 다른 점입니다.)

노드 제목, 핀 표시 이름, StateTree 노드 설명은 내보낼 때의 언어로 만들어집니다.
그래서 커맨드렛은 언어를 영어(`en`)로 고정해, 에디터 언어가 다른 PC에서 내보내도 결과가 같게 합니다.
엔진 옵션 `-culture=ko`처럼 직접 지정하면 그 언어를 씁니다. 사용한 언어는 `index.json`의 `query.culture`에 남습니다.

### 배치 파일 (방법 B)

```
Scripts\Setup.bat         최초 1회: git init, Scripts\LocalConfig.bat 생성
Scripts\Build.bat         두 플러그인 빌드 → Packaged\  (Build.bat core / Build.bat mcp 로 하나만)
Scripts\Export.bat -Assets=/Game/Folder/BP_Thing,/Game/Folder/ST_Tree
Scripts\Export.bat -Path=/Game/Folder -Class=Blueprint -Out=C:\Temp\Export
Scripts\Serve.bat         에디터 없이 MCP 서버 켜기 (아래 MCP 도구 참고)
Scripts\Editor.bat        두 플러그인을 -PLUGIN=으로 불러온 채 에디터 열기 (프로젝트 수정 없음)
```

`Scripts\LocalConfig.bat`(git 무시)에 엔진 경로 `UE_ROOT`와 대상 프로젝트 `AGE_PROJECT`를 지정합니다.

## MCP 도구 (AssetGraphExporterMcp)

엔진 내장 Unreal MCP 서버(`ModelContextProtocol` 플러그인, Experimental)에 `age_*` 도구를 추가하는 플러그인입니다.
Claude Code는 서버 하나(기본 `http://127.0.0.1:8000/mcp`)에만 연결하면 되고, 상황에 따라 대답하는 쪽이 바뀝니다.

```
Claude Code ──→ 127.0.0.1:8000/mcp
   ├ 에디터 켜짐: 에디터 안의 Unreal MCP 서버 + age_* 도구   (저장 안 한 변경까지 보임)
   └ 에디터 꺼짐: Serve.bat → 상주 커맨드렛이 같은 서버를 띄움  (디스크에 저장된 파일 기준)
```

- 도구는 `IModelContextProtocolModule::AddTool`로 서버에 직접 등록합니다. 그래서 `tools/list`에 바로 나오고, 결과도 이스케이프 없는 텍스트로 돌려줍니다.
  (ToolsetRegistry 툴셋은 `list_toolsets` → `describe_toolset` → `call_tool`을 거쳐야 하고, 문자열 결과가 한 번 더 감싸집니다.)
- 별도 플러그인으로 둔 이유: Unreal MCP는 Experimental이라 엔진 버전마다 바뀔 수 있습니다. 또 기본 플러그인에 넣으면, 그것을 쓰는 모든 프로젝트에서 Unreal MCP가 강제로 켜집니다.
- 각 대답의 첫 줄은 출처입니다. 예: `source: editor`, `source: editor (has unsaved changes)`, `source: saved file (2026-10-23 12:00:00 UTC)`.

### 도구

| 도구 | 하는 일 |
|---|---|
| `age_find_assets` | 이름(일부)·폴더·클래스로 에셋 찾기. 로드하지 않습니다. |
| `age_references` | 참조 관계: 이 에셋이 쓰는 것(`uses`)과 이 에셋을 쓰는 것(`usedBy`). 한 줄에 경로, 클래스, `hard`(함께 로드)/`soft`(필요할 때 로드), 게임에 필요 없으면 `editor-only`. C++ 모듈(`/Script/...`)은 `native:` 한 줄로 모읍니다. `asset`에 `/Script/게임모듈`을 주면 그 모듈을 쓰는 에셋. `depth`(1~3)로 몇 단계까지 따라갈지. 에셋을 로드하지 않습니다(Asset Registry). |
| `age_find_usages` | C++ 클래스·UFUNCTION·UPROPERTY의 사용처. 멤버는 그것을 부르는(읽는·쓰는·덮어쓰는·바인딩하는) 그래프 노드를 한 줄씩(그래프, 노드 종류, 제목), 클래스는 JSON에서 그 클래스 경로가 나오는 모든 곳(부모 클래스, 캐스트, 핀 타입, 클래스 기본값). `symbol` 예: `GameplayAbility::GetAvatarActorFromActorInfo`, `ACharacter.JumpMaxCount`, `UGameplayAbility`. `path` 아래 BP는 전부, 그 밖의 에셋은 그 C++ 모듈에 의존하는 것만 찾고, 맵과 월드 파티션 레벨의 액터 패키지는 건너뜁니다. 아래 [사용처 찾기 캐시](#사용처-찾기-캐시) 참고. |
| `age_diff` | 버전 비교: git의 예전 버전(`from`, 기본 `HEAD`)과 지금(또는 다른 버전 `to`)의 차이. BP·AnimBP·StateTree·나이아가라는 요약 도구의 글을 줄 단위로 비교(`-` 예전, `+` 지금)하고, 모든 에셋은 내보낸 JSON을 비교해 바뀐 개수를 알려 줍니다(`detail=json`이면 경로별 목록, 그래프 노드는 GUID로 짝을 맞춤). 예전 파일은 git에서 꺼내 임시 패키지로 로드합니다(에디터의 Diff 기능과 같은 방식). |
| `age_get_asset` | 위 JSON 스키마 그대로(공백 없는 JSON). `select`로 일부만 받습니다. 예: `stateTree.bindings`, `blueprint.graphs[EventGraph]`, `blueprint.graphs[0].nodes[3]`. 결과가 `maxChars`(기본 40000자)보다 길면 대신 목차(경로, 항목 수, 크기)를 돌려줍니다. |
| `age_statetree_overview` | 상태 트리 요약: 상태 계층, 진입 조건, 태스크, 전환(대상, 트리거, 조건)을 한 줄씩 |
| `age_statetree_bindings` | 바인딩을 `원본 -> 대상` 한 줄씩. `filter`로 거르기 |
| `age_blueprint_overview` | BP 요약(Actor, GameplayAbility, Widget 등): 부모 클래스, 인터페이스, 변수와 기본값, 컴포넌트, 이 BP가 바꾼 클래스 기본값, EventGraph·함수·매크로·접힌 그래프의 **실행 흐름** |
| `age_animbp_overview` | AnimBP 요약: 변수와 기본값, AnimGraph 포즈 흐름, 스테이트 머신의 상태별 포즈와 전환 규칙(식으로), EventGraph·함수의 실행 흐름 |
| `age_niagara_overview` | 나이아가라 요약: 사용자 파라미터, 시스템 스택, 이미터마다 실행 순서대로 모든 스택(이벤트 핸들러, **Simulation Stage** 포함)의 모듈과 설정한 입력값, 렌더러. 옵션 없이 부르면 개요(모듈 이름만)이고, `emitter`는 이미터 하나를, `full`은 전체를 자세히 보여 줍니다 |
| `age_level_actors` | 에디터에 열린 레벨(저장 안 한 변경 포함)에 무엇이 어디에 있는지 호출 한 번에. 상자(`min`, `max`), 공(`center` 또는 액터 이름 `around`, `radius`), 또는 아무것도 안 주면 레벨 전체. `class`(부모 클래스도 맞음)와 `name`으로 거르기. 가까운 순으로 한 줄에 액터 하나: 라벨과 오브젝트 이름, 클래스, 위치, 회전·크기(0과 1이 아닐 때), 세계 축 경계 상자(`box` 최소-최대), 메시 에셋. `bounds`: `visible`(기본)은 스프라이트나 카메라 시야 표시처럼 에디터에서만 쓰는 부품을 빼고 잽니다(이것 때문에 캐릭터 상자가 몇 미터씩 커질 수 있음). `collision`은 충돌 있는 부품만, `all`은 엔진 `GetActorBounds`와 같습니다. 월드 파티션 맵에서 로드 안 된 곳의 액터는 로드하지 않고 `unloaded`와 저장된 경계 상자로 보여 줍니다. 상주 서버에서는 쓸 수 없습니다. |
| `age_status` | 지금 대답하는 쪽(에디터 / 상주 서버), 프로젝트, 엔진·플러그인 버전 |
| `age_shutdown` | 상주 서버 끄기 (에디터 안에서는 아무것도 하지 않음) |
| `age_open_window` | 활동 창 열기, 또는 앞으로 가져오기 |

요약 도구의 출력 예 (`age_animbp_overview` 일부):

```
StateMachine Main
  entry -> Locomotion
  state Locomotion: BS_Locomotion Blendspace Player(X=Direction, Y=Speed2D)
  Strafe -> TurnInPlace if (bIsStrafing AND (Speed2D < 10) AND (Abs(AimTurnRate) > 20))
EventGraph (Ubergraph):
  Event BlueprintUpdateAnimation(DeltaTimeX):
    Is Valid(TryGetPawnOwner())
      [Is Valid]
        AimTurnRate = (NormalizeAxis((TryGetPawnOwner().K2_GetActorRotation().Yaw - LastYaw)) / DeltaTimeX)
        LastYaw = TryGetPawnOwner().K2_GetActorRotation().Yaw
```

실행 흐름 읽는 법 (`age_blueprint_overview`, `age_animbp_overview` 공통):
- 이벤트·함수 시작 노드마다 한 덩어리. 실행 핀을 따라 노드 하나에 한 줄입니다.
- Set 노드는 `변수 = 식`, Branch는 `if 조건`, 함수 반환은 `return 이름=식`. 식은 연결된 순수 노드를 펼쳐 쓴 것입니다.
- 실행 출력이 여러 개인 노드(Branch, Sequence, Is Valid, 반복문, Switch)는 연결된 출력마다 `[출력 이름]` 아래로 들여 씁니다.
- 이미 쓴 노드로 다시 합쳐지면 `-> continues at "..." above`로 가리키기만 합니다(실행 루프도 여기서 끝남).
- 실행 노드의 결과 값은 `함수이름.핀이름`으로 가리킵니다(호출은 그 노드의 줄에 있음). 나뉜 구조체 핀은 `.Yaw`, `(X=..., Y=..., Z=...)`처럼 씁니다.
- 어떤 시작 노드에서도 닿지 않는 실행 노드는 그래프 끝에 `not reached from an entry`로 셉니다.

`age_references` 출력 예 (`BP_GA_Dodge`):

```
source: asset registry (saved files; unsaved editor changes are not included)
/Game/Abilities/BP_GA_Dodge  [Blueprint]
uses (4 + 3 native modules):
  /Game/Abilities/BP_GE_Cooldown  [Blueprint]  hard
  /Game/Animations/MT_Dodge_Back  [AnimMontage]  hard
  /Game/Animations/MT_Dodge_L  [AnimMontage]  hard
  /Game/Animations/MT_Dodge_R  [AnimMontage]  hard
  native: /Script/GameplayAbilities, /Script/GameplayTags, /Script/MyGame
used by (2):
  /Game/AI/ST_Enemy  [StateTree]  hard
  /Game/Characters/BP_Enemy  [Blueprint]  hard
```

### 사용처 찾기 캐시

`age_find_usages`는 후보 에셋을 한 번 내보내 캐시 폴더에 두고, 다음부터는 바뀐 에셋만 다시 내보냅니다(커맨드렛 `-Incremental`과 같은 키, 같은 폴더 형식).

- 위치: `<프로젝트>/Saved/AssetGraphExporter/Cache`. 실행 인자 `-AGECacheDir=<폴더>`로 바꿉니다(예: `Serve.bat -AGECacheDir=D:\AgeCache`).
- 처음 검색은 후보를 모두 로드하므로 오래 걸릴 수 있고, 그동안 서버(에디터 모드에서는 에디터)가 멈춥니다. 커맨드렛으로 미리 채울 수 있습니다: `Export.bat -Path=/Game -Class=Blueprint -Out=<캐시 폴더> -Incremental`. 단, 커맨드렛 `-Incremental`은 그 질의에 없는 에셋의 파일을 지우므로 캐시에는 넓은 질의로만 쓰세요. 언어가 다르면(커맨드렛은 `en`) 키가 달라 다시 내보냅니다.
- 에디터 모드에서 저장하지 않은 변경이 있는 에셋은 캐시를 쓰지 않고 메모리에서 바로 읽습니다(캐시에 쓰지 않음).

### 버전 비교 (`age_diff`)

- 예전 파일은 `git cat-file`로 꺼냅니다. Git LFS 파일은 이 PC의 LFS 저장소(`.git/lfs/objects`)에 있을 때만 읽고, 내려받지 않습니다. 없으면 `git lfs fetch`로 받은 뒤 다시 하라고 알려 줍니다.
- 꺼낸 파일은 `<프로젝트>/Saved/AssetGraphExporter/Diff`(실행 인자 `-AGEDiffDir=<폴더>`로 변경)에 두고 `/AGEDiff/<커밋>/...` 패키지로 로드합니다. 에셋 안에서 원래 패키지를 가리키는 참조는 그 임시 패키지로 이어지고, BP는 컴파일하지 않습니다. 비교 전에 임시 패키지 이름은 원래 이름으로 되돌립니다.
- `/AGEDiff/`는 서버(또는 에디터)가 끝날 때까지 연결된 채로 둡니다. 로드한 예전 버전이 다음 정리 때까지 메모리에 남기 때문입니다.

### 활동 창 (Claude가 MCP로 하는 일 보기)

작은 Windows 프로그램 `AGEActivityWindow.exe`가 AI가 하는 일을 보여 줍니다. AI 연결, 작업 하나하나, 작업 끝남이 나옵니다.

- AI가 처음 연결하거나 도구를 처음 부를 때 스스로 뜹니다. 작업 중인 프로그램의 키보드 입력은 뺏지 않습니다.
- 별도 프로그램이라 **서버가 꺼져도, 에디터를 닫아도, 빌드해도 창은 꺼지지 않습니다.** 서버가 돌아오면 같은 창이 앞선 기록을 그대로 둔 채 이어 갑니다. 포트마다 창은 하나입니다.
- "끝났습니다"는 1분 동안 새 요청이 없을 때 나옵니다. MCP에 "AI가 일을 끝냈다"는 신호가 없기 때문입니다. "서버가 꺼졌습니다"는 연결 거부로 판단하므로 정확합니다.
- 창을 닫으면 다음에 서버를 켤 때 다시 뜹니다. 바꾸려면 **옵션 > 창을 닫았을 때 > 오늘은 다시 열지 않기**를 고릅니다(다음 날 저절로 풀립니다).
- 언제든 직접 열기: AI에게 말하기(`age_open_window`), 콘솔 명령 `AGE.OpenActivity`, exe 실행. 이미 열려 있으면 앞으로 나옵니다.
- **옵션 > 테마**: Windows 설정 따르기(기본), 밝게, 어둡게.
- 스스로 뜨지 않게 하려면: 콘솔 변수 `AGE.ActivityWindow 0` 또는 실행 인자 `-AGENoActivityWindow`.
- 창은 `%LOCALAPPDATA%\AssetGraphExporter`의 복사본으로 실행됩니다. 그래서 플러그인 폴더의 파일을 잠그지 않습니다. 새 버전은 창을 다음에 켤 때 적용됩니다. 옵션 선택도 같은 폴더에 저장됩니다.
- `Scripts\Build.bat mcp`가 Windows에 들어 있는 C# 컴파일러로 빌드하므로 따로 설치할 것은 없습니다. 코드 서명은 없습니다. 보안 프로그램이 막거나 빌드에 없으면 대신 아래 페이지가 브라우저로 열립니다.

### 활동 페이지

창은 MCP 서버와 같은 주소에 붙는 작은 페이지를 읽습니다: `http://127.0.0.1:8000/age/activity` (포트는 MCP 설정을 따름). 이 주소를 브라우저로 열어 볼 수도 있습니다.
에디터 모드와 상주 서버 모드 모두 같은 페이지입니다. 1초마다 새 항목이 위에 추가됩니다.

- `age_*` 호출: 무엇을 했는지(예: "StateTree summary ST_Enemy"), 저장된 파일·에디터 중 어디서 읽었는지, 결과 길이, 걸린 시간, 오류.
- 엔진 내장 도구: 이름과 시각만. Unreal MCP 서버가 호출마다 남기는 로그 한 줄(`Running tool: '...'`, `ModelContextProtocolServer.cpp`)을 받아 씁니다. `call_tool` 안에서 실제로 부른 도구 이름은 이 로그에 없어서 보이지 않습니다.
- 다른 플러그인이 같은 MCP 서버에 추가한 도구: 그 플러그인이 `NoteExternalToolStart`와 `RecordExternalToolCall`(`AGEMcpToolLog.h`, 모듈 `AssetGraphExporterMcp`)을 부르면 `age_*` 호출처럼 인자와 결과가 나옵니다. 이름이 `age_`로 시작하지 않는 도구는 위의 이름만 나오는 줄도 함께 남습니다.
- AI 클라이언트가 새로 연결하면(`Session initialized`) 한 줄 남깁니다.
- 페이지 맨 위에 서버가 응답하는지와 마지막 활동이 몇 분 전인지 글자로 나옵니다. 열어 둔 탭은 서버를 다시 켜도 계속 동작합니다. 목록은 새로 시작하고, 서버가 돌아오면 상태도 돌아옵니다. 브라우저가 숨은 탭을 늦추거나 얼리므로 숨겨 둔 동안에는 페이지가 뒤처질 수 있습니다. 창은 그렇지 않습니다.
- 서버는 기록을 메모리에만 둡니다(최근 200개). 서버가 꺼지면 사라지고, 서버가 이 일로 쓰는 파일은 없습니다.
- 페이지는 MCP 서버와 같은 HTTP 서버(같은 포트)에서 나가므로, 접근 범위도 MCP 서버와 같습니다.

### 상주 서버 (`Serve.bat`, 커맨드렛 `AssetGraphServe`)

```
UnrealEditor-Cmd.exe <Project>.uproject -run=AssetGraphServe [-IdleMinutes=30] [-IgnoreEditor]
```

- 엔진 시작은 켤 때 한 번입니다. 그 뒤 질문은 엔진을 다시 띄우지 않습니다.
- 측정(시험 프로젝트, 2026-10-24):
  - 재부팅 뒤 첫 실행은 준비까지 62초였습니다. 이 중 Zen 저장소 서버 시작이 8.8초, 셰이더 타입 준비가 17초입니다. 바로 다음 실행(에디터 거절 시험)에서는 둘 다 1초 안팎이었습니다.
  - 켜진 뒤 도구 호출은 4–460 ms입니다. 첫 `age_find_assets`만 2.7초 걸렸습니다(에셋 목록 캐시 126 MB를 디스크에 쓰는 시간).
  - `age_shutdown` 후 프로세스가 끝나기까지 3.4초(응답을 보내려고 기다리는 1초 + 엔진 종료 2.4초).
- 엔진이 허용하는 방식입니다. Unreal MCP 설정 코드(`ModelContextProtocolSettings.cpp`)에 커맨드렛은 서버를 직접 켜라는 주석이 있고, 에디터 커맨드렛도 `UEditorEngine`을 만들고 PostEngineInit까지 거칩니다(`LaunchEngineLoop.cpp`). 커맨드렛 루프에서 `FTSTicker`를 돌려 HTTP·MCP 서버를 움직입니다.
- 다음 경우에 스스로 꺼집니다: `age_shutdown`, Ctrl+C, `age_*` 요청 없이 `-IdleMinutes`분(기본 30, 0이면 끄지 않음), Unreal Editor가 켜졌을 때(같은 포트를 에디터에 넘김). 에디터가 켜져 있으면 시작하지 않습니다. `Serve.bat`이 엔진을 띄우기 전에 먼저 확인하므로 바로 거절됩니다(`-IgnoreEditor`로 무시).
- **C++ 빌드 전에는 꺼야 합니다.** 켜져 있는 동안 프로젝트와 플러그인 DLL이 로드되어 있어 빌드가 덮어쓰지 못합니다.
- 디스크 기준입니다. 서버가 켜진 뒤 다시 저장된 에셋은 다음 요청 때 다시 읽습니다(`UPackageTools::ReloadPackages`).
- 내장 Unreal MCP 도구(`call_tool`)도 같이 열립니다. 다만 에디터 창이나 뷰포트가 필요한 도구는 이 모드에서 실패할 수 있습니다.
- 텍스트 언어는 Export 커맨드렛과 같이 영어로 고정합니다. 에디터 모드는 에디터 언어를 그대로 씁니다(사용자 화면을 바꾸지 않기 위해).
- 포트와 URL은 Unreal MCP 설정을 따릅니다. `-ModelContextProtocolPort=N`으로 포트를 바꿀 수 있습니다.

### 설치와 연결

1. 두 플러그인을 설치합니다. 방법 A는 `AssetGraphExporter/`와 `AssetGraphExporterMcp/`를 함께 `<프로젝트>/Plugins/`에 복사, 방법 B는 `Scripts\Build.bat` 후 `Serve.bat` / `Editor.bat`(둘 다 `-PLUGIN=`으로 불러옴).
2. 에디터 모드: 플러그인이 에디터가 열릴 때 Unreal MCP 서버를 스스로 켭니다(엔진의 사용자별 설정 "Auto Start Server"를 켤 필요 없음). 끄려면 에디터를 `-AGENoServerAutoStart`로 엽니다. 상주 서버가 막 꺼지는 중이라 포트가 잠깐 잡혀 있으면 2초 간격으로 다시 시도합니다(최대 15번). 에디터 모드에서는 호출마다 약 0.33초가 걸렸습니다. `tools/list`도 같은 시간이 걸리므로, 도구 처리 시간이 아니라 에디터가 요청을 처리하는 프레임 간격으로 보입니다(에디터 창이 뒤에 있으면 프레임을 줄입니다). 자동 켜기를 끈 경우에는 Project Settings > Plugins > Model Context Protocol의 Auto Start Server를 켜거나, 콘솔 명령 `ModelContextProtocol.StartServer`를 씁니다.
3. Claude Code에 서버를 등록합니다: `claude mcp add --transport http unreal http://127.0.0.1:8000/mcp`
   (엔진 콘솔 명령 `ModelContextProtocol.GenerateClientConfig ClaudeCode`로 설정 파일을 만들 수도 있습니다.)

## 확장: 새 에셋 타입 처리기

1. `IAGEAssetHandler`를 구현합니다.
2. 모듈의 `StartupModule`에서 `FAGEHandlerRegistry::Get().Register(...)`로 등록합니다.

규칙:
- 범용 `properties`는 항상 먼저 써집니다. 처리기는 리플렉션만으로 읽기 어려운 부분만 추가하면 됩니다.
- 여러 처리기가 같은 에셋을 맡겠다고 하면 우선순위가 높은 쪽이 이깁니다(AnimBlueprint 20 > Blueprint 10).
- 값을 쓸 때는 `FAGEPropertyWriter`, 그래프를 쓸 때는 `FAGEGraphWriter`를 재사용할 수 있습니다.
- 다른 프런트엔드(예: MCP 서버)에서는 `FAGEAssetExporter::ExportAsset`을 그대로 호출하면 됩니다.
- 내보낸 JSON을 짧은 텍스트로 바꾸는 함수는 `AGE::Summary`(`AGESummary.h`), JSON 일부 선택·목차는 `AGE::JsonQuery`(`AGEJsonQuery.h`)에 있습니다. 둘 다 내보낸 JSON만 읽으므로 MCP 없이도 쓸 수 있습니다.

## 알려진 한계

- Blueprint의 상속 컴포넌트 오버라이드(`InheritableComponentHandler`)는 범용 `properties`에만 나옵니다.
- Actor BP의 `components[].properties`는 컴포넌트 클래스 기본값과 비교한 차이입니다.
- `-Class` 필터로 BP를 부모 클래스 기준으로 찾을 때, 부모가 BP이면 그 부모 BP를 로드합니다.
- 레벨(맵) 에셋은 범용 `properties`만 나옵니다.
- 상주 서버의 "다시 저장된 에셋 다시 읽기"는 아직 실제로 시험하지 않았습니다(시험하려면 `Content/` 파일을 바꿔야 해서).
- 상주 서버는 요청받은 에셋의 패키지만 다시 읽습니다. 그 에셋이 참조하는 다른 에셋(예: 부모 BP)이 바뀐 경우, 그것이 메모리에서 정리(2분마다)되거나 직접 요청될 때까지 예전 값이 섞일 수 있습니다.
- `age_references`는 디스크에 저장된 내용 기준입니다(Asset Registry). 에디터에서 저장하지 않은 변경은 반영되지 않습니다.
- `age_find_usages`가 찾지 않는 곳: 구조체 Break/Make 노드의 멤버, AnimBP 프로퍼티 액세스 바인딩 문자열 안의 멤버 경로, 맵(레벨 블루프린트). `MemberParent`가 로드되지 않은 BP 클래스인 노드는 판정하지 못해 빠집니다.
- Asset Registry의 C++ 모듈 의존은 빠진 것이 있습니다(5.8로 저장한 시험 에셋에서 `/Script/Engine`, `/Script/CoreUObject` 의존이 기록되지 않음). 그래서 `age_references`의 `native:` 줄과 `/Script/...` 사용처는 전부가 아닐 수 있고, `age_find_usages`는 BP를 모듈 의존과 상관없이 모두 찾습니다.
- `age_diff`: 예전 버전도 지금의 C++ 클래스로 읽으므로, 그 사이 없어진 C++ 멤버는 빠진 값이나 끊긴 핀처럼 보일 수 있습니다(에디터의 Diff 기능도 같음). 맵은 지원하지 않습니다. git이 PATH에 있어야 합니다.
- 실행 흐름 요약에서 순수 노드는 쓰일 때마다 식으로 다시 펼쳐집니다(같은 `TryGetPawnOwner()`가 여러 줄에 나올 수 있음).
- `variables[].default`는 CDO 값입니다. CDO에 그 변수가 없으면(예: 변수를 추가한 뒤 컴파일하지 않음) `FBPVariableDescription::DefaultValue` 문자열을 대신 씁니다. 시험한 에셋에서는 이 문자열이 모두 비어 있었습니다.
- 나이아가라: 경량(Stateless) 이미터는 범용 리플렉션으로만 씁니다. 스크래치 패드 모듈은 이름만 스택에 나오고, 내용은 `graphs`에 있습니다. `defaults`는 이름만 있고 값은 없습니다(모듈 에셋 쪽 기본값).
- 나이아가라 그래프는 큽니다(시험한 유체 시스템의 JSON이 116만 자이고, 대부분이 노드 604개짜리 이미터 그래프입니다). `age_niagara_overview`부터 보고, 필요한 부분만 `select`로 꺼내세요.
