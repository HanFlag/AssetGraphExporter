# 출력과 JSON 스키마

커맨드렛과 `age_get_asset`이 내보내는 JSON의 형식입니다. 사용법은 [사용 안내](Guide.md)를 보세요.

## 출력

에셋마다 JSON 파일을 하나씩 만들고, 위치는 패키지 경로를 그대로 따릅니다.
예: `/Game/Folder/BP_Thing` → `<Out>/Game/Folder/BP_Thing.json`

전체 목록과 실패 내역은 `<Out>/index.json`에 기록됩니다.

### 증분 내보내기 (`-Incremental`)

`index.json`의 에셋마다 `key`가 적힙니다. 아래 중 하나라도 바뀌면 키가 바뀌고, `-Incremental`은 키가 같은 에셋만 건너뜁니다.

- 에셋 파일의 저장 해시(엔진이 저장할 때 패키지에 기록하는 값, Asset Registry `GetPackageSavedHash`). 없으면 파일 크기와 수정 시각.
- 이 에셋이 hard 의존하는 패키지 전부(끝까지 따라감)의 같은 값. 부모 BP나 나이아가라 모듈 스크립트가 바뀌어도 JSON이 바뀌기 때문입니다.
- C++ 코드: 프로젝트 `Binaries/`, 프로젝트 `Plugins/`, 이 플러그인 폴더의 DLL 개수와 가장 최근 수정 시각. 다시 빌드하면 모든 에셋을 다시 내보냅니다.
- 플러그인 버전, `schemaVersion`, `-Full`, 언어, 엔진 버전.

전체 내보내기와 증분 내보내기의 결과 폴더는 같습니다(`index.json` 포함).

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

## JSON 스키마 (schemaVersion 2)

버전 2: `blueprint.variables[].default`가 문자열에서 JSON 값(CDO 값)으로 바뀌었습니다.

### 공통 (모든 에셋)

```jsonc
{
  "schemaVersion": 2,
  "exporter": { "name": "AssetGraphExporter", "version": "0.2.2", "diffOnly": true },
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
    "category": "Default", "default": 600.0,     // CDO 값. classDefaults와 같은 JSON 값(숫자, 불, 문자열, 객체)
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
