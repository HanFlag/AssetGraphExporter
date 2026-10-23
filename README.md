# AssetGraphExporter

에디터 창을 열지 않고 Unreal 에셋의 구조를 JSON으로 내보내는 에디터 플러그인입니다.
Blueprint, Animation Blueprint, StateTree를 비롯해 어떤 UObject 에셋이든 처리합니다.
에디터를 끈 상태에서도 AI 코딩 도구(Claude Code 등)가 BP 그래프나 StateTree 구조를 읽을 수 있게 하는 것이 목적입니다.

- `.uasset` 바이너리를 직접 파싱하지 않습니다. 엔진이 에셋을 로드하는 커맨드렛(`UCommandlet`)으로 동작합니다.
- 프로젝트 이름, 에셋 경로, 게임 클래스는 코드 어디에도 들어 있지 않습니다. 플러그인 폴더만 복사하면 다른 프로젝트에서도 그대로 동작합니다.
- 에셋은 읽기만 하고 저장하지 않습니다.
- 대상 엔진: UE 5.8 (5.8.4에서 빌드 확인, Win64)

## 폴더 구조

```
AssetGraphExporter/            ← 저장소
  AssetGraphExporter/          ← 플러그인 본체 (설치할 때 이 폴더를 복사)
    AssetGraphExporter.uplugin
    Source/AssetGraphExporter/            범용 처리 + Blueprint/AnimBP + 커맨드렛 (Editor 모듈)
    Source/AssetGraphExporterStateTree/   StateTree 처리기 (Editor 모듈)
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
Scripts\Build.bat         플러그인 빌드 → Packaged\
Scripts\Export.bat -Assets=/Game/Folder/BP_Thing,/Game/Folder/ST_Tree
Scripts\Export.bat -Path=/Game/Folder -Class=Blueprint -Out=C:\Temp\Export
```

`Scripts\LocalConfig.bat`(git 무시)에 엔진 경로 `UE_ROOT`와 대상 프로젝트 `AGE_PROJECT`를 지정합니다.

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

## 확장: 새 에셋 타입 처리기

1. `IAGEAssetHandler`를 구현합니다.
2. 모듈의 `StartupModule`에서 `FAGEHandlerRegistry::Get().Register(...)`로 등록합니다.

규칙:
- 범용 `properties`는 항상 먼저 써집니다. 처리기는 리플렉션만으로 읽기 어려운 부분만 추가하면 됩니다.
- 여러 처리기가 같은 에셋을 맡겠다고 하면 우선순위가 높은 쪽이 이깁니다(AnimBlueprint 20 > Blueprint 10).
- 값을 쓸 때는 `FAGEPropertyWriter`, 그래프를 쓸 때는 `FAGEGraphWriter`를 재사용할 수 있습니다.
- 다른 프런트엔드(예: MCP 서버)에서는 `FAGEAssetExporter::ExportAsset`을 그대로 호출하면 됩니다.

## 알려진 한계 (0.1.0)

- Blueprint의 상속 컴포넌트 오버라이드(`InheritableComponentHandler`)는 범용 `properties`에만 나옵니다.
- Actor BP의 `components[].properties`는 컴포넌트 클래스 기본값과 비교한 차이입니다.
- `-Class` 필터로 BP를 부모 클래스 기준으로 찾을 때, 부모가 BP이면 그 부모 BP를 로드합니다.
- 레벨(맵) 에셋은 범용 `properties`만 나옵니다.
