**English** | [한국어](Guide.ko.md)

# Guide

Installation, how to use the commandlet and the MCP tools, how to extend the plugin, and known limitations. For the output format, see the [JSON schema](JsonSchema.md).

## Folder layout

```
AssetGraphExporter/            ← repository
  AssetGraphExporter/          ← the plugin itself (copy this folder to install)
    AssetGraphExporter.uplugin
    Source/AssetGraphExporter/            generic export + Blueprint/AnimBP + commandlet + summaries (Editor module)
    Source/AssetGraphExporterStateTree/   StateTree handler (Editor module)
    Source/AssetGraphExporterNiagara/     Niagara System handler (Editor module)
  AssetGraphExporterMcp/       ← MCP plugin (optional, needs the Unreal MCP engine plugin)
    AssetGraphExporterMcp.uplugin
    Source/AssetGraphExporterMcp/         age_* tools + background commandlet AssetGraphServe + activity page (Editor module)
    Resources/Activity.html               activity page
  Scripts/                     batch files for building and running
  Packaged/                    Build.bat output (ignored by git)
  Logs/                        build and run logs (ignored by git)
```

## Installation

### Method A: copy into the project (the usual way)

1. Copy the `AssetGraphExporter/` folder to `<Project>/Plugins/AssetGraphExporter/`.
2. Build the project. For a C++ project, build the Editor target in your IDE.

You don't need to add it to the `.uproject`. The engine enables plugins in the project's `Plugins/` folder automatically unless `EnabledByDefault` says otherwise (`FPlugin::IsEnabledByDefault`).

### Method B: use it without touching the project

1. Build only the plugins with `Scripts\Build.bat`. It uses the engine's `BuildPlugin` command and puts the result in `Packaged\`.
2. Load them at run time with the `-PLUGIN=<path to .uplugin>` option. `Scripts\Export.bat` adds this option for you.

Note: with method B, the StateTree handler loads only if the target project already has the StateTree plugin enabled and built. For other projects, use method A.

### StateTree dependency

The StateTree handler module depends on the `StateTree` plugin, and that dependency is marked `"Optional": true` in the `.uplugin`.
Behavior checked in the engine source (UBT `UEBuildTarget.cs`, `PluginManager.cpp`):

- Optional means "ignore it if the plugin files are missing".
- StateTree ships with the engine. So adding this plugin also enables StateTree in the editor, even in projects that don't use StateTree.
- Because of `TargetAllowList: Editor`, game builds are not affected.

The Niagara handler module (`AssetGraphExporterNiagara`) depends on the `Niagara` plugin in the same Optional way. Niagara is enabled by default in the engine (`EnabledByDefault: true`), so this dependency enables nothing new.

## Usage

```
UnrealEditor-Cmd.exe <Project>.uproject -run=AssetGraphExport [options]
```

| Option | Description |
|---|---|
| `-Assets=<paths>` | Comma-separated package paths. Both `/Game/Folder/Asset` and `/Game/Folder/Asset.Asset` work. |
| `-Path=<folders>` | Comma-separated folders, including all subfolders. |
| `-Class=<classes>` | Class filter (subclasses included). Native classes by short name (`Blueprint`, `StateTree`, `GameplayAbility`), BP classes by full path (`/Game/BP_Base.BP_Base_C`). Blueprints also match by parent class, so `-Class=GameplayAbility` finds GA Blueprints. |
| `-Out=<folder>` | Output folder. The default is `<Project>/Saved/AssetExport`; relative paths are relative to the project folder. |
| `-Full` | Write every value, including ones equal to the default. By default only values that differ from the default are written (see below). |
| `-Incremental` | If the same `-Out` folder has an earlier export, assets that haven't changed are not loaded and their files are left as they are. Files of assets that were in the last export but not in this one are deleted (only files listed in the last `index.json`). See [Incremental export](JsonSchema.md#incremental-export--incremental) below. |

At least one of `-Assets` or `-Path` is required.
The exit code is 0 if every asset succeeded, otherwise 1.
You can run it while the target project's editor is open (checked on UE 5.8.4).
The commandlet is a separate process from the editor and reads **assets as saved on disk**.
Changes made in the editor but not saved yet don't show up, so save first to see the latest content.
(This is the difference from tools such as unreal-mcp that read the editor's memory directly.)

Node titles, pin display names and StateTree node descriptions are produced in the language active at export time.
So the commandlet fixes the language to English (`en`), and exports are the same on PCs whose editor language differs.
If you pass an engine option such as `-culture=ko`, that language is used. The language used is recorded in `query.culture` in `index.json`.

### Batch files (method B)

```
Scripts\Setup.bat         first time only: git init, creates Scripts\LocalConfig.bat
Scripts\Build.bat         builds both plugins → Packaged\  (Build.bat core / Build.bat mcp for just one)
Scripts\Export.bat -Assets=/Game/Folder/BP_Thing,/Game/Folder/ST_Tree
Scripts\Export.bat -Path=/Game/Folder -Class=Blueprint -Out=C:\Temp\Export
Scripts\Serve.bat         starts the MCP server without the editor (see MCP tools below)
Scripts\Editor.bat        opens the editor with both plugins loaded through -PLUGIN= (the project is not changed)
```

Set the engine path `UE_ROOT` and the target project `AGE_PROJECT` in `Scripts\LocalConfig.bat` (ignored by git).

## MCP tools (AssetGraphExporterMcp)

This plugin adds `age_*` tools to the engine's built-in Unreal MCP server (the `ModelContextProtocol` plugin, Experimental).
Claude Code connects to one server only (by default `http://127.0.0.1:8000/mcp`), and the side that answers changes with the situation.

```
Claude Code ──→ 127.0.0.1:8000/mcp
   ├ editor open:   the Unreal MCP server inside the editor + age_* tools    (sees unsaved changes too)
   └ editor closed: Serve.bat → a background commandlet runs the same server (files saved on disk)
```

- The tools are registered with the server directly through `IModelContextProtocolModule::AddTool`. So they appear in `tools/list` right away, and their results are returned as unescaped text.
  (ToolsetRegistry toolsets have to go through `list_toolsets` → `describe_toolset` → `call_tool`, and string results get wrapped once more.)
- Why a separate plugin: Unreal MCP is Experimental and may change between engine versions. Also, putting it in the main plugin would force Unreal MCP on in every project that uses it.
- The first line of every answer is its source, for example `source: editor`, `source: editor (has unsaved changes)`, `source: saved file (2026-10-23 12:00:00 UTC)`.

### Tools

| Tool | What it does |
|---|---|
| `age_find_assets` | Find assets by (part of a) name, folder and class. Doesn't load them. |
| `age_references` | References: what this asset uses (`uses`) and what uses this asset (`usedBy`). One line each: path, class, `hard` (loaded together) / `soft` (loaded when needed), and `editor-only` if the game doesn't need it. C++ modules (`/Script/...`) are gathered on one `native:` line. Give `/Script/GameModule` as `asset` to get the assets that use that module. `depth` (1–3) sets how many steps to follow. Doesn't load assets (Asset Registry). |
| `age_find_usages` | Where a C++ class, UFUNCTION or UPROPERTY is used. For a member, one line per graph node that calls, reads, writes, overrides or binds it (graph, node kind, title); for a class, every place its class path appears in the JSON (parent class, casts, pin types, class defaults). `symbol` examples: `GameplayAbility::GetAvatarActorFromActorInfo`, `ACharacter.JumpMaxCount`, `UGameplayAbility`. Searches every BP under `path`, and other assets only if they depend on that C++ module; skips maps and World Partition actor packages. See [Usage search cache](#usage-search-cache) below. |
| `age_diff` | Revision diff: the difference between an older git revision (`from`, default `HEAD`) and now (or another revision `to`). For BP, AnimBP, StateTree and Niagara it compares the summary tools' text line by line (`-` old, `+` now); for every asset it compares the exported JSON and reports how many values changed (`detail=json` lists them by path; graph nodes are matched by GUID). The old file is taken from git and loaded as a temporary package (the same way as the editor's Diff feature). |
| `age_get_asset` | The JSON schema above as is (compact JSON). Use `select` to get just a part, for example `stateTree.bindings`, `blueprint.graphs[EventGraph]`, `blueprint.graphs[0].nodes[3]`. If the result is longer than `maxChars` (default 40000 characters), a table of contents (paths, item counts, sizes) is returned instead. |
| `age_statetree_overview` | StateTree summary: state hierarchy, enter conditions, tasks, transitions (target, trigger, conditions), one line each |
| `age_statetree_bindings` | Bindings as `source -> target`, one per line. Filter with `filter` |
| `age_blueprint_overview` | BP summary (Actor, GameplayAbility, Widget and so on): parent class, interfaces, variables and defaults, components, class defaults this BP changed, and the **execution flow** of the EventGraph, functions, macros and collapsed graphs |
| `age_animbp_overview` | AnimBP summary: variables and defaults, AnimGraph pose flow, each state machine's per-state pose and transition rules (as expressions), execution flow of the EventGraph and functions |
| `age_niagara_overview` | Niagara summary: user parameters, system stacks, and for each emitter every stack in execution order (event handlers and **Simulation Stages** included) with its modules and set input values, plus renderers |
| `age_status` | Which side is answering (editor / background server), the project, engine and plugin versions |
| `age_shutdown` | Stops the background server (does nothing inside the editor) |

Example output of a summary tool (part of `age_animbp_overview`):

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

How to read the execution flow (same for `age_blueprint_overview` and `age_animbp_overview`):
- One block per event or function entry node. Following the exec pins, one line per node.
- A Set node is `variable = expression`, a Branch is `if condition`, a function return is `return name=expression`. Expressions are the connected pure nodes written out.
- Nodes with several exec outputs (Branch, Sequence, Is Valid, loops, Switch) indent each connected output under `[output name]`.
- When the flow joins a node already written, it only points to it with `-> continues at "..." above` (exec loops also end here).
- The result of an exec node is referred to as `FunctionName.PinName` (the call is on that node's line). Split struct pins are written like `.Yaw` or `(X=..., Y=..., Z=...)`.
- Exec nodes that no entry node reaches are counted at the end of the graph as `not reached from an entry`.

Example output of `age_references` (`BP_GA_Dodge`):

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

### Usage search cache

`age_find_usages` exports the candidate assets once into a cache folder, and after that re-exports only assets that changed (the same keys and folder format as the commandlet's `-Incremental`).

- Location: `<Project>/Saved/AssetGraphExporter/Cache`. Change it with the `-AGECacheDir=<folder>` argument (for example `Serve.bat -AGECacheDir=D:\AgeCache`).
- The first search loads every candidate, so it can take a while, and the server (the editor, in editor mode) is blocked meanwhile. You can fill the cache in advance with the commandlet: `Export.bat -Path=/Game -Class=Blueprint -Out=<cache folder> -Incremental`. But the commandlet's `-Incremental` deletes files of assets not in that query, so use only a broad query for the cache. If the language differs (the commandlet uses `en`), the keys differ and assets are exported again.
- In editor mode, assets with unsaved changes skip the cache and are read from memory directly (and not written to the cache).

### Revision diff (`age_diff`)

- The old file is taken out with `git cat-file`. Git LFS files are read only if they are in this PC's LFS store (`.git/lfs/objects`); nothing is downloaded. If a file is missing, it tells you to fetch it with `git lfs fetch` and try again.
- Extracted files go to `<Project>/Saved/AssetGraphExporter/Diff` (change with the `-AGEDiffDir=<folder>` argument) and are loaded as `/AGEDiff/<commit>/...` packages. References inside the asset that point to its original packages lead to those temporary packages, and BPs are not compiled. Before comparing, temporary package names are turned back into the original names.
- `/AGEDiff/` stays mounted until the server (or editor) exits, because loaded old versions stay in memory until the next cleanup.

### Activity page (see what Claude did through MCP)

A small page is served at the same address as the MCP server: `http://127.0.0.1:8000/age/activity` (the port follows the MCP settings).
It is the same page in editor mode and background-server mode. New entries are added at the top every second.

- `age_*` calls: what was done (for example "StateTree summary ST_Enemy"), whether it was read from saved files or the editor, result length, time taken, errors.
- Built-in engine tools: only the name and time. It uses the log line the Unreal MCP server writes for each call (`Running tool: '...'`, `ModelContextProtocolServer.cpp`). The tool actually called inside `call_tool` is not in that log, so it isn't shown.
- When an AI client connects (`Session initialized`), one line is added.
- It opens once in the default browser on the first connection or the first tool call. To turn that off, use the console variable `AGE.ActivityWindow 0` or the argument `-AGENoActivityWindow`. To open it yourself: the console command `AGE.OpenActivity`.
- Entries live only in memory (the latest 200). Nothing is written to files, and they are gone when the server stops.
- The page is served by the same HTTP server (same port) as the MCP server, so its access scope is the same as the MCP server's.

### Background server (`Serve.bat`, commandlet `AssetGraphServe`)

```
UnrealEditor-Cmd.exe <Project>.uproject -run=AssetGraphServe [-IdleMinutes=30] [-IgnoreEditor]
```

- The engine starts once, when the server starts. Later questions don't start the engine again.
- Measurements (test project, 2026-10-24):
  - The first run after a reboot took 62 seconds to get ready. Of that, starting the Zen storage server took 8.8 seconds and preparing shader types took 17 seconds. On the very next run (the editor-refusal test), both took about 1 second.
  - Once up, tool calls take 4–460 ms. Only the first `age_find_assets` took 2.7 seconds (writing the 126 MB asset list cache to disk).
  - 3.4 seconds from `age_shutdown` to process exit (1 second waiting to send the reply + 2.4 seconds of engine shutdown).
- The engine allows this. The Unreal MCP settings code (`ModelContextProtocolSettings.cpp`) has a comment saying commandlets should start the server themselves, and editor commandlets also create `UEditorEngine` and go through PostEngineInit (`LaunchEngineLoop.cpp`). The commandlet loop runs `FTSTicker` to drive the HTTP and MCP servers.
- It stops by itself on: `age_shutdown`, Ctrl+C, `-IdleMinutes` minutes without an `age_*` request (default 30, 0 means never), or when Unreal Editor starts (it hands the port to the editor). It doesn't start if the editor is running; `Serve.bat` checks this before starting the engine, so it refuses right away (ignore with `-IgnoreEditor`).
- **Stop it before building C++.** While it runs, the project and plugin DLLs are loaded and the build can't overwrite them.
- It works from disk. Assets saved again after the server started are read again on the next request (`UPackageTools::ReloadPackages`).
- The built-in Unreal MCP tools (`call_tool`) are available too. But tools that need an editor window or viewport may fail in this mode.
- The text language is fixed to English, as in the Export commandlet. Editor mode uses the editor's language as is (so the user's UI isn't changed).
- The port and URL follow the Unreal MCP settings. Change the port with `-ModelContextProtocolPort=N`.

### Install and connect

1. Install both plugins. Method A: copy `AssetGraphExporter/` and `AssetGraphExporterMcp/` together into `<Project>/Plugins/`. Method B: `Scripts\Build.bat`, then `Serve.bat` / `Editor.bat` (both load the plugins through `-PLUGIN=`).
2. Editor mode: the plugin starts the Unreal MCP server by itself when the editor opens (no need to turn on the engine's per-user setting "Auto Start Server"). To prevent this, open the editor with `-AGENoServerAutoStart`. If the port is still held briefly because the background server is shutting down, it retries every 2 seconds (up to 15 times). In editor mode each call took about 0.33 seconds. `tools/list` takes the same time, so this looks like the frame interval at which the editor handles requests, not the tools' processing time (the editor lowers its frame rate when its window is in the background). If you turned auto start off, enable Auto Start Server in Project Settings > Plugins > Model Context Protocol, or use the console command `ModelContextProtocol.StartServer`.
3. Register the server in Claude Code: `claude mcp add --transport http unreal http://127.0.0.1:8000/mcp`
   (You can also create a config file with the engine console command `ModelContextProtocol.GenerateClientConfig ClaudeCode`.)

## Extending: a handler for a new asset type

1. Implement `IAGEAssetHandler`.
2. Register it in the module's `StartupModule` with `FAGEHandlerRegistry::Get().Register(...)`.

Rules:
- The generic `properties` are always written first. A handler only needs to add what reflection alone can't read well.
- If several handlers claim the same asset, the one with the higher priority wins (AnimBlueprint 20 > Blueprint 10).
- You can reuse `FAGEPropertyWriter` for values and `FAGEGraphWriter` for graphs.
- Other front ends (such as the MCP server) can call `FAGEAssetExporter::ExportAsset` as is.
- Functions that turn exported JSON into short text are in `AGE::Summary` (`AGESummary.h`); selecting part of the JSON and building a table of contents are in `AGE::JsonQuery` (`AGEJsonQuery.h`). Both read only the exported JSON, so they work without MCP.

## Known limitations

- Inherited component overrides of a Blueprint (`InheritableComponentHandler`) appear only in the generic `properties`.
- An Actor BP's `components[].properties` are differences from the component class defaults.
- When the `-Class` filter finds BPs by parent class and the parent is a BP, that parent BP is loaded.
- Level (map) assets get only the generic `properties`.
- The background server's "read assets saved again" has not been tested for real yet (testing it would mean changing files in `Content/`).
- The background server reloads only the packages of the requested assets. If another asset it references (for example a parent BP) changed, old values may be mixed in until that asset is cleaned out of memory (every 2 minutes) or requested directly.
- `age_references` is based on what is saved on disk (Asset Registry). Unsaved editor changes are not reflected.
- What `age_find_usages` doesn't search: members in struct Break/Make nodes, member paths inside AnimBP property access binding strings, maps (level Blueprints). Nodes whose `MemberParent` is a BP class that isn't loaded can't be judged and are left out.
- Some C++ module dependencies are missing from the Asset Registry (in test assets saved with 5.8, dependencies on `/Script/Engine` and `/Script/CoreUObject` were not recorded). So the `native:` line of `age_references` and usages of `/Script/...` may be incomplete, and `age_find_usages` searches every BP regardless of module dependencies.
- `age_diff`: old revisions are also read with the current C++ classes, so C++ members removed in between may look like missing values or broken pins (the editor's Diff feature does the same). Maps are not supported. git must be on the PATH.
- In the execution-flow summary, pure nodes are written out again as expressions each time they are used (the same `TryGetPawnOwner()` can appear on several lines).
- `variables[].default` is the CDO value. If the CDO doesn't have the variable (for example, a variable added without compiling), the `FBPVariableDescription::DefaultValue` string is used instead. In the tested assets this string was always empty.
- Niagara: lightweight (Stateless) emitters are written with generic reflection only. Scratch pad modules appear in the stack by name only; their contents are in `graphs`. `defaults` has only names, no values (the defaults live in the module asset).
- Niagara graphs are big (a tested fluid system's JSON is 1.16 million characters, mostly one emitter graph with 604 nodes). Start with `age_niagara_overview`, then take out only what you need with `select`.
