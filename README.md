# AssetGraphExporter

에디터 창을 열지 않고 Unreal 에셋의 구조를 JSON으로 내보내는 에디터 플러그인입니다.
Blueprint, Animation Blueprint, StateTree, Niagara System을 비롯해 어떤 UObject 에셋이든 처리합니다.
에디터를 끈 상태에서도 AI 코딩 도구(Claude Code 등)가 BP 그래프나 StateTree 구조를 읽을 수 있게 하는 것이 목적입니다.

- `.uasset` 바이너리를 직접 파싱하지 않습니다. 엔진이 에셋을 로드하는 커맨드렛(`UCommandlet`)으로 동작합니다.
- 프로젝트 이름, 에셋 경로, 게임 클래스는 코드 어디에도 들어 있지 않습니다. 플러그인 폴더만 복사하면 다른 프로젝트에서도 그대로 동작합니다.
- 에셋은 읽기만 하고 저장하지 않습니다.
- 대상 엔진: UE 5.8 (5.8.4에서 빌드 확인, Win64). MCP 플러그인은 5.8.4에서 빌드와 동작(상주 서버 모드, 에디터 모드)까지 확인했습니다.
- 별도 플러그인 `AssetGraphExporterMcp`를 함께 설치하면, 같은 내용을 엔진 내장 Unreal MCP 서버의 도구(`age_*`)로 바로 물어볼 수 있습니다. 에디터를 끈 상태에서도 됩니다. 아래 [MCP 도구](#mcp-도구-assetgraphexportermcp)를 보세요.

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
    Resources/Activity.html               활동 창 페이지
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
| `age_get_asset` | 위 JSON 스키마 그대로(공백 없는 JSON). `select`로 일부만 받습니다. 예: `stateTree.bindings`, `blueprint.graphs[EventGraph]`, `blueprint.graphs[0].nodes[3]`. 결과가 `maxChars`(기본 40000자)보다 길면 대신 목차(경로, 항목 수, 크기)를 돌려줍니다. |
| `age_statetree_overview` | 상태 트리 요약: 상태 계층, 진입 조건, 태스크, 전환(대상, 트리거, 조건)을 한 줄씩 |
| `age_statetree_bindings` | 바인딩을 `원본 -> 대상` 한 줄씩. `filter`로 거르기 |
| `age_animbp_overview` | AnimBP 요약: 변수, AnimGraph 포즈 흐름, 스테이트 머신의 상태별 포즈와 전환 규칙(식으로) |
| `age_niagara_overview` | 나이아가라 요약: 사용자 파라미터, 시스템 스택, 이미터마다 실행 순서대로 모든 스택(이벤트 핸들러, **Simulation Stage** 포함)의 모듈과 설정한 입력값, 렌더러 |
| `age_status` | 지금 대답하는 쪽(에디터 / 상주 서버), 프로젝트, 엔진·플러그인 버전 |
| `age_shutdown` | 상주 서버 끄기 (에디터 안에서는 아무것도 하지 않음) |

요약 도구의 출력 예 (`age_animbp_overview` 일부):

```
StateMachine Main
  entry -> Locomotion
  state Locomotion: BS2D_BossMoveLocomotion Blendspace Player(X=Direction, Y=Speed2D)
  CombatMove -> TurnInPlace if (bIsCombatMove AND (Speed2D < 10) AND (Abs(AimTurnRate) > 20))
```

### 활동 창 (Claude가 MCP로 한 일 보기)

MCP 서버와 같은 주소에 작은 페이지가 붙습니다: `http://127.0.0.1:8000/age/activity` (포트는 MCP 설정을 따름).
에디터 모드와 상주 서버 모드 모두 같은 페이지입니다. 1초마다 새 항목이 위에 추가됩니다.

- `age_*` 호출: 무엇을 했는지(예: "StateTree 요약 ST_Boss"), 저장된 파일·에디터 중 어디서 읽었는지, 결과 길이, 걸린 시간, 오류.
- 엔진 내장 도구: 이름과 시각만. Unreal MCP 서버가 호출마다 남기는 로그 한 줄(`Running tool: '...'`, `ModelContextProtocolServer.cpp`)을 받아 씁니다. `call_tool` 안에서 실제로 부른 도구 이름은 이 로그에 없어서 보이지 않습니다.
- AI 클라이언트가 새로 연결하면(`Session initialized`) 한 줄 남깁니다.
- 처음 연결되거나 도구를 처음 부를 때 기본 브라우저로 한 번 열립니다. 끄려면 콘솔 변수 `AGE.ActivityWindow 0` 또는 실행 인자 `-AGENoActivityWindow`. 직접 열기: 콘솔 명령 `AGE.OpenActivity`.
- 기록은 메모리에만 있습니다(최근 200개). 파일을 쓰지 않고, 서버가 꺼지면 사라집니다.
- 페이지는 MCP 서버와 같은 HTTP 서버(같은 포트)에서 나가므로, 접근 범위도 MCP 서버와 같습니다.

### 상주 서버 (`Serve.bat`, 커맨드렛 `AssetGraphServe`)

```
UnrealEditor-Cmd.exe <Project>.uproject -run=AssetGraphServe [-IdleMinutes=30] [-IgnoreEditor]
```

- 엔진 시작은 켤 때 한 번입니다. 그 뒤 질문은 엔진을 다시 띄우지 않습니다.
- 측정(MomentTrigger, 2026-10-24):
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
2. 에디터 모드: Unreal MCP 서버가 켜져 있어야 합니다. 에디터 모드에서는 호출마다 약 0.33초가 걸렸습니다. `tools/list`도 같은 시간이 걸리므로, 도구 처리 시간이 아니라 에디터가 요청을 처리하는 프레임 간격으로 보입니다(에디터 창이 뒤에 있으면 프레임을 줄입니다). Project Settings > Plugins > Model Context Protocol의 Auto Start Server를 켜거나(엔진 로그가 안내하는 위치), 콘솔 명령 `ModelContextProtocol.StartServer`를 씁니다.
3. Claude Code에 서버를 등록합니다: `claude mcp add --transport http unreal http://127.0.0.1:8000/mcp`
   (엔진 콘솔 명령 `ModelContextProtocol.GenerateClientConfig ClaudeCode`로 설정 파일을 만들 수도 있습니다.)

## 출력

에셋마다 JSON 파일을 하나씩 만들고, 위치는 패키지 경로를 그대로 따릅니다.
예: `/Game/Folder/BP_Thing` → `<Out>/Game/Folder/BP_Thing.json`

전체 목록과 실패 내역은 `<Out>/index.json`에 기록됩니다.

- 키 순서는 고정되어 있습니다. 다시 내보낸 뒤 diff를 보면 바뀐 부분만 나타납니다.
- GUID는 모두 `8-4-4-4-12` 형식입니다. 노드 링크, 바인딩, 전환 대상을 문자열 검색으로 서로 찾아갈 수 있습니다.

### 기본값 비교 규칙 (diff 모드, 기본)

| 대상 | 비교 기준 |
|---|---|
| 에셋·노드 같은 객체 | 아키타입(클래스의 CDO, 또는 템플릿) |
| BP의 `classDefaults` | 부모 클래스의 CDO. 즉 Class Defaults 패널에서 이 BP가 바꾼 값만 나옵니다. 이 BP가 새로 추가한 변수는 항상 나옵니다. |
| 구조체 값(배열 요소, `FInstancedStruct` 내용) | 새로 만든 같은 타입의 구조체 |
| 파라미터 백(`FInstancedPropertyBag`) | 비교하지 않고 항상 전부 씁니다(사용자 데이터이므로). |

`-Full`을 주면 이 비교를 끄고 전부 씁니다.

## JSON 스키마 (schemaVersion 1)

### 공통 (모든 에셋)

```jsonc
{
  "schemaVersion": 1,
  "exporter": { "name": "AssetGraphExporter", "version": "0.1.0", "diffOnly": true },
  "engineVersion": "5.8.4",
  "asset": { "path": "/Game/Folder/BP_Thing.BP_Thing", "class": "/Script/Engine.Blueprint" },
  "handler": "Blueprint",          // 전용 처리기 이름 (없으면 생략)
  "properties": { },               // 에셋 자체의 UPROPERTY (범용 리플렉션)
  "blueprint": { },                // 처리기가 추가하는 섹션 (Blueprint / AnimBlueprint)
  "stateTree": { },                // 〃 (StateTree)
  "niagara": { },                  // 〃 (Niagara System)
  "warnings": [ "..." ]            // 중단되지는 않았지만 문제가 있었던 것
}
```

### 값 표기 (범용 리플렉션)

| 타입 | JSON 표기 |
|---|---|
| bool, 숫자 | 그대로. NaN과 Infinity는 문자열 |
| enum | 이름 문자열. 비트 플래그는 `"A \| B"` |
| FName, FString, FText | 문자열 |
| 다른 에셋·클래스 참조 | 경로 문자열 |
| soft 참조 | 경로 문자열. 대상을 로드하지 않습니다. |
| 에셋이 소유한 객체(같은 패키지 안의 Instanced 또는 outer가 소유자인 객체) | `{"$class", "$name", ...속성}`로 펼쳐서 씁니다. 같은 객체가 두 번째로 나오면 경로만 씁니다. |
| 일반 구조체 | 객체 |
| 텍스트 표기가 있는 구조체(FGuid, FSoftObjectPath, FGameplayTag 등) | 문자열 |
| `FInstancedStruct` | `{"$type": 구조체 경로, ...필드}`. 비어 있으면 null |
| `FInstancedPropertyBag` | `{파라미터 이름: 값}` |
| 배열, 셋 | 배열 |
| 맵 | 객체. 키는 텍스트 표기 |
| 델리게이트 등 그 밖의 타입 | 엔진의 텍스트 표기 |

`Transient`와 `Deprecated` 속성은 쓰지 않습니다.

### `blueprint` (Blueprint, Animation Blueprint)

```jsonc
{
  "type": "BPTYPE_Normal",
  "parentClass": "/Script/GameplayAbilities.GameplayAbility",
  "nativeParentClass": "/Script/...",          // 부모를 따라 올라가서 처음 만나는 C++ 클래스
  "generatedClass": "/Game/.../BP_Thing.BP_Thing_C",
  "interfaces": [ "/Script/..." ],
  "variables": [ {
    "name": "Speed", "guid": "...", "type": { /* 핀 타입 */ },
    "category": "Default", "default": "600.0",
    "flags": [ "Edit", "BlueprintVisible" ],    // Edit, DisableEditOnInstance, BlueprintVisible, BlueprintReadOnly,
                                                // ExposeOnSpawn, Replicated, RepNotify, Transient, SaveGame, Config, Interp, AdvancedDisplay
    "repNotify": "OnRep_Speed", "metaData": { "tooltip": "..." }
  } ],
  "components": [ {                            // SimpleConstructionScript (Actor BP)
    "name": "Mesh", "class": "/Script/Engine.StaticMeshComponent",
    "parent": "DefaultSceneRoot", "attachSocket": "hand_r", "properties": { }
  } ],
  "classDefaults": { },                        // CDO 값 중 부모와 다른 것
  "graphs": [ /* 그래프, 아래 참고 */ ]
}
```

### 그래프, 노드, 핀

```jsonc
{
  "name": "EventGraph",
  "kind": "Ubergraph",            // Ubergraph | Function | Macro | DelegateSignature | SubGraph
  "class": "EdGraph", "schema": "EdGraphSchema_K2",
  "ownerNode": "<guid>",          // 하위 그래프만: 이 그래프를 가진 노드
                                  // (스테이트머신, 상태, 전환 규칙, 접힌 그래프)
  "nodes": [ {
    "guid": "...", "class": "K2Node_CallFunction",
    "title": "Play Montage and Wait",    // 에디터에 보이는 제목 (여러 줄일 수 있음)
    "pos": [ 400, 120 ], "comment": "...", "enabledState": "Disabled",
    "ghost": true,                       // 엔진이 자동 배치한 비활성 기본 이벤트. 이때 엔진이 넣은 안내 코멘트는 생략
    "properties": { "FunctionReference": { "MemberParent": "...", "MemberName": "..." } },
    "animBindings": [ ],                 // AnimGraph 노드만 (아래 참고)
    "pins": [ {
      "id": "...",                       // 연결이 있거나 분할 핀의 부모일 때만 (다른 곳에서 가리킬 때만 필요)
      "name": "Duration", "displayName": "...", "dir": "in",
      "type": {
        "category": "real", "subCategory": "double", "object": "/Script/...", "member": "...",
        "container": "array",            // array | set | map (없으면 생략)
        "valueType": { },                // map의 값 타입
        "byRef": true, "const": true
      },
      "default": "1.0",
      "hidden": true, "orphaned": true, "parentPin": "<pin id>",
      "links": [ { "node": "<guid>", "pin": "<pin id>", "pinName": "Then" } ]
    } ]
  } ],
  "subGraphs": [ ]
}
```

- 노드의 `properties`에는 노드 객체의 UPROPERTY 중 클래스 기본값과 다른 것만 들어갑니다. `UEdGraphNode` 자체의 배치·크기·코멘트 필드는 빠집니다.
- AnimBP의 AnimGraph 노드는 `FAnimNode_*` 값이 `properties.Node`에 들어갑니다.

### `animBindings` (AnimGraph 노드)

핀을 변수 경로나 함수에 바인딩한 정보입니다.
UE 5.8은 이 정보를 노드의 `Binding` 객체(`UAnimGraphNodeBinding_Base::PropertyBindings`)에 저장합니다. 예전 형식인 `PropertyBindings_DEPRECATED`가 남아 있는 에셋도 함께 읽습니다.

```jsonc
{ "property": "Alpha", "type": "Property", "bound": true, "path": "Speed", "pathText": "Speed",
  "pinType": { }, "promotedFrom": { }, "arrayIndex": 0, "context": "...", "onlyUpdateWhenActive": true }
```

`type` 값은 `Property` 또는 `Function`입니다.

### `stateTree`

컴파일된 런타임 데이터가 아니라 에디터 데이터(`UStateTreeEditorData`)를 씁니다.
상태와 노드는 `"Root/Combat/Attack"` 같은 경로로도 표시되어, 바인딩과 전환 대상에서 GUID 대신 경로로 찾을 수 있습니다.
같은 목록 안에서 이름이 겹치면 경로에 순번이 붙습니다. 예: `.../Conditions/Float Compare[0]`, `.../Conditions/Float Compare[1]`.
이름이 겹치지 않으면 순번은 붙지 않습니다. 같은 부모 아래 이름이 같은 상태도 마찬가지입니다.

```jsonc
{
  "schema": { "$class": "/Script/GameplayStateTreeModule.StateTreeComponentSchema", ... },
  "parameters": { "id": "...", "values": { "Target": null } },
  "evaluators": [ /* 노드 */ ],
  "globalTasks": [ /* 노드 */ ],
  "globalTasksCompletion": "Any",
  "states": [ {
    "id": "...", "name": "Combat", "path": "Root/Combat", "enabled": true,
    "type": "State",                     // State | Group | Linked | LinkedAsset | Subtree
    "selectionBehavior": "TrySelectChildrenInOrder",
    "tasksCompletion": "Any",
    "linkedSubtree": { "type": "GotoState", "state": "Root/Sub", "id": "..." },   // type == Linked
    "linkedAsset": "/Game/...",                                                    // type == LinkedAsset
    "parameters": { "id": "...", "values": { } },
    "enterConditions": [ /* 노드, 조건 */ ],
    "considerations": [ /* 노드 */ ],
    "tasks": [ /* 노드 */ ],
    "singleTask": { },                   // 상태당 태스크를 하나만 허용하는 스키마
    "transitions": [ {
      "id": "...", "enabled": true,
      "trigger": "OnStateCompleted",
      "target": { "type": "GotoState", "state": "Root/Idle", "id": "..." },
      "priority": "Normal",
      "delay": { "duration": 0.5, "randomVariance": 0.0 },
      "conditions": [ /* 노드, 조건 */ ],
      "properties": { "RequiredEvent": { }, "ReactivateTargetState": "..." }    // 위에 없는 나머지 중 기본값과 다른 것
    } ],
    "properties": { "Tag": "...", "Weight": 2.0 },   // 위에 없는 나머지 상태 설정 중 기본값과 다른 것
    "children": [ /* 상태 */ ]
  } ],
  "bindings": [ {
    "source": { "id": "...", "owner": "Root/Combat/Tasks/FindTarget", "path": "Result" },
    "target": { "id": "...", "owner": "Root/Combat/Tasks/MoveTo", "path": "Destination" },
    "function": { "$type": "...", ... }  // 프로퍼티 함수 바인딩일 때
  } ]
}
```

노드 형식(태스크, 조건, 평가기, 고려 요소 공통):

```jsonc
{
  "id": "...", "name": "MoveTo",
  "type": "/Script/StateTreeModule.StateTreeDelayTask",
  "description": "Delay 2.0s",       // StateTree 에디터에 보이는 한 줄 요약
  "operand": "And", "indent": 0,     // 조건만
  "node": { },                       // 노드 구조체 필드
  "instance": { },                   // 인스턴스 데이터 구조체
  "instanceObject": { "$class": "/Game/.../STT_MyTask.STT_MyTask_C", ... }   // 블루프린트로 만든 노드
}
```

- 바인딩의 `owner`는 이 트리 안의 상태·노드·파라미터 경로입니다.
- 스키마가 제공하는 컨텍스트 데이터는 `Context:Actor`, `Context:AIController`처럼 `Context:`를 붙여 씁니다.
- 그 밖에 트리 밖에 있는 것(프로퍼티 함수 등)은 엔진이 붙인 표시 이름입니다.
- `path`가 빈 문자열이면 그 구조체나 객체 전체를 바인딩했다는 뜻입니다. 예: Actor 자체를 태스크 입력에 연결한 경우.

### `niagara`

스택은 엔진의 스택 화면 모델(`FNiagaraSystemViewModel`)을 만들지 않고, 스크립트 그래프를 직접 따라가서 읽습니다.
스택마다 출력 노드가 하나 있고(사용처: 이미터·파티클 스폰/업데이트, 이벤트 핸들러, Simulation Stage마다 하나), 거기서 파라미터 맵 연결을 거꾸로 따라가면 모듈 순서가 나옵니다. 에디터 스택과 같은 규칙입니다.

```jsonc
{
  "userParameters": [ { "name": "User.WorldSpaceSize", "type": "Vector3f", "value": "(200,200,350)" } ],
  "system": { "stacks": [ /* SystemSpawn, SystemUpdate */ ] },
  "emitters": [ {
    "name": "Grid3D_Gas_Master_Emitter", "id": "...", "simTarget": "GPU",   // CPU | GPU
    "parent": "/NiagaraFluids/...", "mode": "Stateless",                     // mode는 경량 이미터만
    "settings": { },                   // 이미터 설정 중 기본값과 다른 것
    "stacks": [ {
      "usage": "SimulationStage",      // EmitterSpawn | EmitterUpdate | ParticleSpawn | ParticleUpdate | Event | SimulationStage
      "name": "Advect Density",        // 이벤트 핸들러·Simulation Stage만
      "iterationSource": "DataInterface", "dataInterface": "Emitter.ScalarGrid", "iterations": "1",
      "modules": [ {
        "name": "Grid3D_SetResolution", "script": "/NiagaraFluids/...", "node": "<guid>",
        "kind": "scratchPad",            // scratchPad | setParameters | customHlsl (일반 모듈은 생략)
        "enabled": false,                // 꺼진 모듈만
        "switches": [ { "name": "Lifetime Mode", "value": "NewEnumerator1", "displayName": "Random" } ],
        "inputs": [
          { "name": "Color", "type": "LinearColor", "value": "(8,4,1,1)" },          // 직접 넣은 값
          { "name": "Position", "linked": "User.BeamEnd" },                            // 다른 파라미터에 연결
          { "name": "Beam Width", "dynamic": { "name": "FloatFromCurve002", ... } },   // 동적 입력 (같은 형식으로 중첩)
          { "name": "Grid", "dataInterface": { "$class": "...", ... } },               // 데이터 인터페이스
          { "name": "Lifetime", "value": "2", "inactive": true }                       // 설정했지만 스위치 때문에 지금은 숨겨진 입력
        ],
        "defaults": [ "Mass", "Drag" ]    // 화면에 보이지만 아무도 바꾸지 않은 입력 (이름만)
      } ],
      "properties": { }                // 이벤트 핸들러·Simulation Stage 설정 중 기본값과 다른 것
    } ],
    "renderers": [ { "$class": "/Script/Niagara.NiagaraSpriteRendererProperties", ... } ]
  } ],
  "graphs": [ /* 시스템·이미터·스크래치 패드 그래프 원본 (위 "그래프, 노드, 핀" 형식) */ ]
}
```

- 입력값은 두 곳에서 읽습니다. 연결·동적 입력·데이터 인터페이스는 모듈 앞 오버라이드 노드의 핀에서, 직접 넣은 상수는 스크립트의 `RapidIterationParameters`에서 읽습니다(이름 규칙은 엔진의 `FNiagaraUtilities::CreateRapidIterationConstantName`).
- `defaults`와 `inactive`는 엔진 스택 코드의 입력 목록 함수(`FNiagaraStackGraphUtilities::GetStackFunctionInputs`, `GetStackFunctionStaticSwitchPins`)로 만듭니다. 지금 스위치 값 때문에 숨겨진 입력은 `defaults`에 넣지 않습니다.
- enum 값은 내부 이름(`NewEnumerator1`)과 함께, 다를 때 에디터에 보이는 이름을 `displayName`으로 씁니다.
- 함수 호출 노드의 `CachedChangeId`는 에셋을 불러올 때마다 바뀌는 값이라 그래프에서 뺍니다. 그래서 같은 에셋은 매번 같은 JSON이 나옵니다.

## 확장: 새 에셋 타입 처리기

1. `IAGEAssetHandler`를 구현합니다.
2. 모듈의 `StartupModule`에서 `FAGEHandlerRegistry::Get().Register(...)`로 등록합니다.

규칙:
- 범용 `properties`는 항상 먼저 써집니다. 처리기는 리플렉션만으로 읽기 어려운 부분만 추가하면 됩니다.
- 여러 처리기가 같은 에셋을 맡겠다고 하면 우선순위가 높은 쪽이 이깁니다(AnimBlueprint 20 > Blueprint 10).
- 값을 쓸 때는 `FAGEPropertyWriter`, 그래프를 쓸 때는 `FAGEGraphWriter`를 재사용할 수 있습니다.
- 다른 프런트엔드(예: MCP 서버)에서는 `FAGEAssetExporter::ExportAsset`을 그대로 호출하면 됩니다.
- 내보낸 JSON을 짧은 텍스트로 바꾸는 함수는 `AGE::Summary`(`AGESummary.h`), JSON 일부 선택·목차는 `AGE::JsonQuery`(`AGEJsonQuery.h`)에 있습니다. 둘 다 내보낸 JSON만 읽으므로 MCP 없이도 쓸 수 있습니다.

## 알려진 한계 (0.1.0)

- Blueprint의 상속 컴포넌트 오버라이드(`InheritableComponentHandler`)는 범용 `properties`에만 나옵니다.
- Actor BP의 `components[].properties`는 컴포넌트 클래스 기본값과 비교한 차이입니다.
- `-Class` 필터로 BP를 부모 클래스 기준으로 찾을 때, 부모가 BP이면 그 부모 BP를 로드합니다.
- 레벨(맵) 에셋은 범용 `properties`만 나옵니다.
- 상주 서버의 "다시 저장된 에셋 다시 읽기"는 아직 실제로 시험하지 않았습니다(시험하려면 `Content/` 파일을 바꿔야 해서).
- 상주 서버는 요청받은 에셋의 패키지만 다시 읽습니다. 그 에셋이 참조하는 다른 에셋(예: 부모 BP)이 바뀐 경우, 그것이 메모리에서 정리(2분마다)되거나 직접 요청될 때까지 예전 값이 섞일 수 있습니다.
- `age_animbp_overview`는 EventGraph와 함수 그래프를 요약하지 않습니다(이름과 노드 수만). BP 그래프 요약은 다음 단계 후보입니다.
- 나이아가라: 경량(Stateless) 이미터는 범용 리플렉션으로만 씁니다. 스크래치 패드 모듈은 이름만 스택에 나오고, 내용은 `graphs`에 있습니다. `defaults`는 이름만 있고 값은 없습니다(모듈 에셋 쪽 기본값).
- 나이아가라 그래프는 큽니다(유체 시스템 `NS_HeatLaser_Fluid`의 JSON이 116만 자이고, 대부분이 노드 604개짜리 이미터 그래프입니다). `age_niagara_overview`부터 보고, 필요한 부분만 `select`로 꺼내세요.
