**English** | [한국어](JsonSchema.ko.md)

# Output and JSON schema

The format of the JSON that the commandlet and `age_get_asset` produce. For usage, see the [Guide](Guide.md).

## Output

One JSON file per asset, placed by its package path.
Example: `/Game/Folder/BP_Thing` → `<Out>/Game/Folder/BP_Thing.json`

The full list and any failures are recorded in `<Out>/index.json`.

### Incremental export (`-Incremental`)

Each asset in `index.json` gets a `key`. If any of the following changes, the key changes, and `-Incremental` skips only assets whose key is the same.

- The asset file's saved hash (a value the engine writes into the package when saving, Asset Registry `GetPackageSavedHash`). If there is none, the file size and modification time.
- The same value for every package this asset hard-depends on (followed all the way). The JSON also changes when a parent BP or a Niagara module script changes.
- C++ code: the number of DLLs and the latest modification time in the project's `Binaries/`, the project's `Plugins/`, and this plugin's folder. Rebuilding exports every asset again.
- Plugin version, `schemaVersion`, `-Full`, language, engine version.

A full export and an incremental export produce the same result folder (`index.json` included).

- Key order is fixed. After exporting again, a diff shows only the parts that changed.
- Every GUID is in `8-4-4-4-12` form. Node links, bindings and transition targets can be found from each other by text search.

### Default comparison rules (diff mode, the default)

| Target | Compared with |
|---|---|
| Objects such as assets and nodes | The archetype (the class CDO, or the template) |
| A BP's `classDefaults` | The parent class CDO. So only the values this BP changed in the Class Defaults panel appear. Variables this BP added always appear. |
| Struct values (array elements, `FInstancedStruct` contents) | A newly made struct of the same type |
| Parameter bags (`FInstancedPropertyBag`) | Not compared; always written in full (it is user data). |
| Vectors, rotators, colors and other structs of at most four plain numbers | Compared as a whole: when any field differs, every field is written (`{"X":0,"Y":-1,"Z":0}`, not `{"Y":-1}`). |

`-Full` turns this comparison off and writes everything.

## JSON schema (schemaVersion 2)

Version 2: `blueprint.variables[].default` changed from a string to a JSON value (the CDO value).

### Common (every asset)

```jsonc
{
  "schemaVersion": 2,
  "exporter": { "name": "AssetGraphExporter", "version": "0.3.1", "diffOnly": true },
  "engineVersion": "5.8.4",
  "asset": { "path": "/Game/Folder/BP_Thing.BP_Thing", "class": "/Script/Engine.Blueprint" },
  "handler": "Blueprint",          // name of the dedicated handler (omitted if none)
  "properties": { },               // UPROPERTYs of the asset itself (generic reflection)
  "blueprint": { },                // section added by a handler (Blueprint / AnimBlueprint)
  "stateTree": { },                // 〃 (StateTree)
  "niagara": { },                  // 〃 (Niagara System)
  "warnings": [ "..." ]            // problems that didn't stop the export
}
```

### Values (generic reflection)

| Type | JSON |
|---|---|
| bool, numbers | As is. NaN and Infinity as strings |
| enum | Name string. Bit flags as `"A \| B"` |
| FName, FString, FText | String |
| References to other assets or classes | Path string |
| Soft references | Path string. The target is not loaded. |
| Objects the asset owns (Instanced within the same package, or whose outer is the owner) | Written out as `{"$class", "$name", ...properties}`. The second time the same object appears, only its path is written. |
| Ordinary structs | Object |
| Structs of at most four plain numbers (FVector, FRotator, FLinearColor and so on) | Object with every field |
| Structs with a text form (FGuid, FSoftObjectPath, FGameplayTag and so on) | String |
| `FInstancedStruct` | `{"$type": struct path, ...fields}`. null if empty |
| `FInstancedPropertyBag` | `{parameter name: value}` |
| Arrays, sets | Array |
| Maps | Object. Keys in their text form |
| Delegates and other types | The engine's text form |

`Transient` and `Deprecated` properties are not written. Neither is `Signature` of Sequencer objects (`UMovieSceneSignedObject`, e.g. an animation's data model): the engine gives it a new GUID whenever the object is marked as changed, so the same asset would export differently after a reload. For the same reason the `Guid` of the events of Anim Blueprint states and transitions (`StateEntered`, `StateLeft`, `StateFullyBlended`, `TransitionStart`, `TransitionEnd`, `TransitionInterrupt`) is not written: the engine fills an empty one with a new random value whenever it reads the event back, an undo included. The `Guid` of an animation's notifies is written. In the default (diff) mode a struct that differs from its default only in values that are not written is left out, not written as `{}`.

With `age_get_asset` `details: true`, every written object also gets `"$details": {"Key": {"displayName": ..., "category": ...}}` for its properties shown in the Details panel: the row name and the section title the panel shows (e.g. `"InitialUpdateFunction": {"displayName": "On Initial Update", "category": "Functions"}`). Blueprint variables get `displayName`. Not in commandlet exports.

### `blueprint` (Blueprint, Animation Blueprint)

```jsonc
{
  "type": "BPTYPE_Normal",
  "parentClass": "/Script/GameplayAbilities.GameplayAbility",
  "nativeParentClass": "/Script/...",          // the first C++ class found going up the parents
  "generatedClass": "/Game/.../BP_Thing.BP_Thing_C",
  "interfaces": [ "/Script/..." ],
  "variables": [ {
    "name": "Speed", "guid": "...", "type": { /* pin type */ },
    "category": "Default", "default": 600.0,     // CDO value. The same JSON value as classDefaults (number, bool, string, object)
    "flags": [ "Edit", "BlueprintVisible" ],    // Edit, DisableEditOnInstance, BlueprintVisible, BlueprintReadOnly,
                                                // ExposeOnSpawn, Replicated, RepNotify, Transient, SaveGame, Config, Interp, AdvancedDisplay
    "repNotify": "OnRep_Speed", "metaData": { "tooltip": "..." }
  } ],
  "components": [ {                            // SimpleConstructionScript (Actor BP)
    "name": "Mesh", "class": "/Script/Engine.StaticMeshComponent",
    "parent": "DefaultSceneRoot", "attachSocket": "hand_r", "properties": { }
  } ],
  "classDefaults": { },                        // CDO values that differ from the parent
  "graphs": [ /* graphs, see below */ ]
}
```

### Graphs, nodes, pins

```jsonc
{
  "name": "EventGraph",
  "kind": "Ubergraph",            // Ubergraph | Function | Macro | DelegateSignature | SubGraph
  "class": "EdGraph", "schema": "EdGraphSchema_K2",
  "ownerNode": "<guid>",          // sub-graphs only: the node that owns this graph
                                  // (state machine, state, transition rule, collapsed graph)
  "nodes": [ {
    "guid": "...", "class": "K2Node_CallFunction",
    "title": "Play Montage and Wait",    // the title shown in the editor (can be several lines)
    "pos": [ 400, 120 ], "comment": "...", "enabledState": "Disabled",
    "ghost": true,                       // a disabled default event the engine placed; its engine-added hint comment is omitted
    "properties": { "FunctionReference": { "MemberParent": "...", "MemberName": "..." } },
    "animBindings": [ ],                 // AnimGraph nodes only (see below)
    "pins": [ {
      "id": "...",                       // only when linked or the parent of a split pin (only needed when referred to)
      "name": "Duration", "displayName": "...", "dir": "in",
      "type": {
        "category": "real", "subCategory": "double", "object": "/Script/...", "member": "...",
        "container": "array",            // array | set | map (omitted if none)
        "valueType": { },                // a map's value type
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

- A node's `properties` contain only the node object's UPROPERTYs that differ from the class defaults. The placement, size and comment fields of `UEdGraphNode` itself are left out.
- For AnimGraph nodes of an AnimBP, the `FAnimNode_*` values go in `properties.Node`.

### `animBindings` (AnimGraph nodes)

Pins bound to a variable path or a function.
UE 5.8 stores this in the node's `Binding` object (`UAnimGraphNodeBinding_Base::PropertyBindings`). Assets that still have the old `PropertyBindings_DEPRECATED` form are read too.

```jsonc
{ "property": "Alpha", "type": "Property", "bound": true, "path": "Speed", "pathText": "Speed",
  "pinType": { }, "promotedFrom": { }, "arrayIndex": 0, "context": "...", "onlyUpdateWhenActive": true }
```

`type` is `Property` or `Function`.

### `bones` (Skeleton, Skeletal Mesh)

The bone hierarchy, which is not a reflected property (the reflected `BoneTree` of a Skeleton has one entry per bone but no names). In the engine's order, a parent before its children; the root has no `parent`. Virtual bones are not in it (a Skeleton lists them in `properties.VirtualBones`).

```jsonc
"bones": [ { "name": "root" }, { "name": "pelvis", "parent": "root" } ],
"skeletonBonesNotInMesh": [ "weapon_r" ]   // Skeletal Mesh only: bones of its Skeleton that the mesh does not have
```

### `stateTree`

Written from the editor data (`UStateTreeEditorData`), not the compiled runtime data.
States and nodes are also shown by paths such as `"Root/Combat/Attack"`, so bindings and transition targets can be found by path instead of GUID.
If names collide within the same list, an index is added to the path, for example `.../Conditions/Float Compare[0]`, `.../Conditions/Float Compare[1]`.
If names don't collide, no index is added. The same goes for states with the same name under the same parent.

```jsonc
{
  "schema": { "$class": "/Script/GameplayStateTreeModule.StateTreeComponentSchema", ... },
  "parameters": { "id": "...", "values": { "Target": null } },
  "evaluators": [ /* nodes */ ],
  "globalTasks": [ /* nodes */ ],
  "globalTasksCompletion": "Any",
  "states": [ {
    "id": "...", "name": "Combat", "path": "Root/Combat", "enabled": true,
    "type": "State",                     // State | Group | Linked | LinkedAsset | Subtree
    "selectionBehavior": "TrySelectChildrenInOrder",
    "tasksCompletion": "Any",
    "linkedSubtree": { "type": "GotoState", "state": "Root/Sub", "id": "..." },   // type == Linked
    "linkedAsset": "/Game/...",                                                    // type == LinkedAsset
    "parameters": { "id": "...", "values": { } },
    "enterConditions": [ /* nodes, conditions */ ],
    "considerations": [ /* nodes */ ],
    "tasks": [ /* nodes */ ],
    "singleTask": { },                   // schemas that allow only one task per state
    "transitions": [ {
      "id": "...", "enabled": true,
      "trigger": "OnStateCompleted",
      "target": { "type": "GotoState", "state": "Root/Idle", "id": "..." },
      "priority": "Normal",
      "delay": { "duration": 0.5, "randomVariance": 0.0 },
      "conditions": [ /* nodes, conditions */ ],
      "properties": { "RequiredEvent": { }, "ReactivateTargetState": "..." }    // the rest not listed above, where it differs from the default
    } ],
    "properties": { "Tag": "...", "Weight": 2.0 },   // the rest of the state settings not listed above, where it differs from the default
    "children": [ /* states */ ]
  } ],
  "bindings": [ {
    "source": { "id": "...", "owner": "Root/Combat/Tasks/FindTarget", "path": "Result" },
    "target": { "id": "...", "owner": "Root/Combat/Tasks/MoveTo", "path": "Destination" },
    "function": { "$type": "...", ... }  // for property function bindings
  } ]
}
```

Node format (shared by tasks, conditions, evaluators and considerations):

```jsonc
{
  "id": "...", "name": "MoveTo",
  "type": "/Script/StateTreeModule.StateTreeDelayTask",
  "description": "Delay 2.0s",       // the one-line summary shown in the StateTree editor
  "operand": "And", "indent": 0,     // conditions only
  "node": { },                       // fields of the node struct
  "instance": { },                   // the instance data struct
  "instanceObject": { "$class": "/Game/.../STT_MyTask.STT_MyTask_C", ... }   // nodes made as Blueprints
}
```

- A binding's `owner` is the path of a state, node or parameter in this tree.
- Context data provided by the schema is written with a `Context:` prefix, such as `Context:Actor` or `Context:AIController`.
- Anything else outside the tree (property functions and so on) uses the display name the engine gives it.
- An empty `path` means the whole struct or object is bound, for example when the Actor itself is connected to a task input.

### `niagara`

The stacks are read by following the script graphs directly, without building the engine's stack view model (`FNiagaraSystemViewModel`).
Each stack has one output node (by usage: emitter and particle spawn/update, event handlers, one per Simulation Stage), and following the parameter map links backward from it gives the module order. This is the same rule the editor stack uses.

```jsonc
{
  "userParameters": [ { "name": "User.WorldSpaceSize", "type": "Vector3f", "value": "(200,200,350)" } ],
  "system": { "stacks": [ /* SystemSpawn, SystemUpdate */ ] },
  "emitters": [ {
    "name": "Grid3D_Gas_Master_Emitter", "id": "...", "simTarget": "GPU",   // CPU | GPU
    "parent": "/NiagaraFluids/...", "mode": "Stateless",                     // mode only for lightweight emitters
    "settings": { },                   // emitter settings that differ from the default
    "stacks": [ {
      "usage": "SimulationStage",      // EmitterSpawn | EmitterUpdate | ParticleSpawn | ParticleUpdate | Event | SimulationStage
      "name": "Advect Density",        // event handlers and Simulation Stages only
      "iterationSource": "DataInterface", "dataInterface": "Emitter.ScalarGrid", "iterations": "1",
      "modules": [ {
        "name": "Grid3D_SetResolution", "script": "/NiagaraFluids/...", "node": "<guid>",
        "kind": "scratchPad",            // scratchPad | setParameters | customHlsl (omitted for ordinary modules)
        "enabled": false,                // disabled modules only
        "switches": [ { "name": "Lifetime Mode", "value": "NewEnumerator1", "displayName": "Random" } ],
        "inputs": [
          { "name": "Color", "type": "LinearColor", "value": "(8,4,1,1)" },          // a value typed in directly
          { "name": "Position", "linked": "User.BeamEnd" },                            // linked to another parameter
          { "name": "Beam Width", "dynamic": { "name": "FloatFromCurve002", ... } },   // dynamic input (nested in the same format)
          { "name": "Grid", "dataInterface": { "$class": "...", ... } },               // data interface
          { "name": "Lifetime", "value": "2", "inactive": true }                       // set, but hidden right now by a switch
        ],
        "defaults": [ "Mass", "Drag" ]    // inputs shown on screen that nobody changed (names only)
      } ],
      "properties": { }                // event handler and Simulation Stage settings that differ from the default
    } ],
    "renderers": [ { "$class": "/Script/Niagara.NiagaraSpriteRendererProperties", ... } ]
  } ],
  "graphs": [ /* raw system, emitter and scratch pad graphs (the "Graphs, nodes, pins" format above) */ ]
}
```

- Input values are read from two places. Links, dynamic inputs and data interfaces come from the pins of the override node in front of the module; constants typed in directly come from the script's `RapidIterationParameters` (naming rule from the engine's `FNiagaraUtilities::CreateRapidIterationConstantName`).
- `defaults` and `inactive` are built with the input-list functions of the engine's stack code (`FNiagaraStackGraphUtilities::GetStackFunctionInputs`, `GetStackFunctionStaticSwitchPins`). Inputs hidden by the current switch values are not put in `defaults`.
- Enum values are written with their internal name (`NewEnumerator1`), plus the name shown in the editor as `displayName` when it differs.
- The `CachedChangeId` of function call nodes changes every time the asset is loaded, so it is left out of the graph. That way the same asset always gives the same JSON.
