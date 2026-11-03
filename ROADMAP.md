**English** | [한국어](ROADMAP.ko.md)

# Roadmap

## Stage 1 (0.1.0): commandlet export

Status: done (first commit `4fae8f6`).

- Generic reflection export, Blueprint/AnimBP (graphs and bindings), StateTree (states, transitions, bindings)
- Checked on 3 test assets; the values matched what unreal-mcp showed.
- Works with the editor open, too. Even then it reads the version saved on disk.
- The same asset gives byte-for-byte the same JSON on every run.
- Measurements (test project, 3 assets, 2026-10-23)

| Step | Time |
|---|---|
| Engine start | 8.6–12.2 s |
| Asset list scan | 0.26 s |
| Load + export | about 0.5 s |
| Total | 9.9–13.5 s |

  - Almost all of the cost is the engine start.
  - So we decided not to optimize by narrowing the scan.

## Relation to the built-in engine MCP (surveyed 2026-10-23)

UE 5.8 ships the `ModelContextProtocol` ("Unreal MCP") plugin, `ToolsetRegistry` and a set of toolsets (`Experimental/Toolsets/*`). The tool list from a running server had 52 toolsets and 833 tools, and all of them need the editor to be running. Only read tools were called during the survey, and no assets were changed.

Results of reading the same assets both ways:

| Item | Built-in MCP | This plugin |
|---|---|---|
| StateTree | 120 calls, 49K chars | 1 run |
| StateTree bindings | Descriptive sentences only | A list organized by path |
| Blueprint (GameplayAbility) | 13 calls, 20K chars | 2.3K chars |
| BP graphs | `read_graph_dsl` shows a short script (easier to read) | Node and pin JSON |
| AnimBP, state machines | No dedicated tool | Yes |
| GA/GE, montages, notifies | No dedicated tool | Partly, through the generic export |
| Editor closed | Not possible | Possible |
| Niagara Simulation Stages | Its structure can't hold them | Added in stage 2 |

- For BP graphs, the built-in summary format is easier to read. We use it as a reference when building the graph summary in the stage 2 MCP tools (the format only; the implementation is our own).
- **Niagara evidence**: `FNiagaraExt_EmitterTopology` (`NiagaraEditor/Public/NiagaraExternalSystemEditorUtilities.h`), which the built-in tools use to hold the emitter structure, has fields for only 4 stacks (EmitterSpawn, EmitterUpdate, ParticleSpawn, ParticleUpdate) and none for Simulation Stages. Niagara Fluids do most of their work in Simulation Stages, so the built-in tools can't show the core logic. On a tested fluid system, even reading the 4 basic stacks failed (cause not found).

Division of roles (through stage 2):

- **This plugin**: reading and analysis. Works with the editor closed and gives large structures in one organized answer.
- **Built-in MCP**: changes, PIE, tests, logs, Config, plugin management, material, Niagara and PCG editing.

From stage 3 on, this plugin also changes and checks things. Even work the built-in MCP can do is in scope if there is a better way (below).

## Stage 2: don't start the engine for every question (structure B)

### Connection layout (decided 2026-10-23)

```
Claude Code ──→ 127.0.0.1:8000/mcp  (one setting)
   ├ editor open:   the Unreal MCP server inside the editor + our tools (sees unsaved changes too)
   └ editor closed: Serve.bat → a background commandlet runs the same server + our tools (from disk)
```

- **Tool registration**: registered directly with the built-in MCP server through `IModelContextProtocolModule::AddTool`, not through `ToolsetRegistry`.
  - Directly registered tools appear in `tools/list` right away (`ProcessListToolsJsonRpcCall` in `ModelContextProtocolServer.cpp`). Toolset tools have to go through `list_toolsets` → `describe_toolset` → `call_tool`.
  - We decide the result format. Through ToolsetRegistry, string results get wrapped once more inside `{"returnValue":"..."}` and every quote becomes `\"` (checked with the built-in `list_properties` result).
- **Background mode**: a commandlet starts the built-in server and keeps waiting instead of exiting.
  - The engine allows this. There is a comment saying commandlets should start it directly with `-ModelContextProtocolStartServer` (`ModelContextProtocolSettings.cpp`). Editor commandlets also create `UEditorEngine` and go through PostEngineInit (`LaunchEngineLoop.cpp`).
  - The HTTP and MCP servers run on `FTSTicker`, so ticking it in the commandlet loop is enough (`HttpServerModule.h`, `ModelContextProtocolServer.cpp`).
  - It has to be stopped before building C++ because of DLL locks. Handled with `age_shutdown`, an idle timeout, and stopping automatically when the editor starts.
- **Folders**: the MCP part is a separate plugin, `AssetGraphExporterMcp`. Unreal MCP is Experimental, so this narrows the impact, and it keeps Unreal MCP from being forced on in every project that uses the main plugin.
- **Summaries**: the summary functions (exported JSON → short text) live in the main plugin, so they work without MCP.

### Order of work (fixed)

1. **2-1. MCP connection framework + AnimBP and StateTree tools**
   - Direct tool registration, the background commandlet (`AssetGraphServe`), `Serve.bat`.
   - Tools: `age_find_assets`, `age_get_asset`, `age_statetree_overview`, `age_statetree_bindings`, `age_animbp_overview`, `age_status`, `age_shutdown`.
   - Status: done (2026-10-24). Zero build warnings. In both background-server mode and editor mode, all 7 tools appear in the top-level list and work. The built-in `call_tool` also works without the editor.
   - Summary sizes: StateTree 3.1K chars (full JSON 39.7K, built-in MCP 120 calls 49K), AnimBP 1.8K chars (full JSON 127K, without whitespace).
   - Time: background server ready in 62 s (first run after a reboot, including Zen server start and shader preparation), later calls 4–460 ms. In editor mode about 0.33 s per call (looks like the editor's frame interval).
   - Ready time when restarted without a reboot: 11.4–11.7 s (2026-10-26).
   - Not tested yet: re-reading assets saved again while the server runs (it would mean changing `Content/`).
2. **2-2. Niagara handler**
   - Status: done (2026-10-26).
   - System → emitters → stacks (Simulation Stages included) → modules → input values, renderers, Grid data interfaces.
   - Approach (decided 2026-10-24): follow the script graphs directly. Reasons checked in the engine code:
     - The built-in tools (`UNiagaraExternalEditUtilities`) build the whole stack view model every time they read a value, and look for only four stacks. On a tested fluid system even those four failed with "Script not found".
     - The light public API (`SystemAdapter`) can't read input values yet in 5.8 (`TryGetLocalValue` and others always return failure).
     - The engine functions that find module order and override nodes aren't exported, so we implemented the same rules ourselves. The input-list function (`GetStackFunctionInputs`) is exported, so we use it as is.
   - First results (2026-10-26): zero build warnings. Both exports exited with code 0 (18.2 s / 15.6 s), zero warnings.
     - Fluid system: read 25 Simulation Stages, 97 modules and 2 renderers (the built-in MCP can't read the emitter stacks).
     - Another Niagara system: 7 emitters. Compared 4 modules with the built-in MCP's `GetModuleInputValues` — the 5 values that differ are all ones the built-in side couldn't read (`Unsupported`); other differences are only formatting; no input exists only on this plugin's side and not on the built-in side.
     - Exporting the same asset twice differed only in `CachedChangeId` lines → excluded from graphs (second pass).
   - Second pass (done 2026-10-26; two exports were byte-for-byte the same): exclude `CachedChangeId`, enum display names (`displayName`), names of inputs left at their default (`defaults`), mark settings hidden by switches (`inactive`).
   - **Activity page**: a page at `/age/activity` that shows what Claude did through MCP. Shared by editor and background modes, kept in memory only. Since 0.3.0 a small Windows program, the activity window, shows this page and stays open across server restarts.
3. **2-3. Search, diff + cache**
   - Status: done (2026-10-27).
   - C++ ↔ BP cross search, asset references, git revision diff.
   - The cache + incremental export (re-export only changed `.uasset` files) is built here. 2-1 doesn't need it because the background server reads the latest files every time; project-wide search and revision comparison do.
4. **BP graph summary (execution flow)**: outside the scope of 2-1. The code that turns transition rules into expressions could be reused, so this is decided again after 2-1.
   - Status: done (2026-10-26, `age_blueprint_overview` and the execution flow in `age_animbp_overview`).

### Stage 2 candidate features (descriptions)

- **C++ ↔ BP cross search**: function call nodes and variable nodes record their target class and member exactly, so the answer is an exact value, not a guess.
- **Reflection symbol dump**: UCLASS/UFUNCTION/UPROPERTY and header locations (metadata). Limit: plain C++ functions that aren't UFUNCTIONs don't appear.
- **Asset references**: use the Asset Registry for "who uses this asset" and "what does this asset depend on".
- **Asset diff**: export an earlier git revision and the current version and compare the JSON.
- **Chain tracing**: GA → montage → anim notifies (in time order) → GE. Needs an AnimMontage handler.
- **Final config values**: the final value merged from every `GConfig` layer, and the file that set it.
- **BP compile diagnostics** and **engine upgrade checks**: find orphaned pins and nodes pointing at removed functions.

## Stage 3 (next): change things and run them to check

The goal is for Claude not to stop at "this is how you'd do it", but to try it, run it to check, and then ask "this works — shall I apply it?"

- **Two edit modes**: confirm mode (the default: shows the proposed change and applies it after approval) and auto mode (applies right away). You can switch at any time.
- In either mode, what was changed is recorded and can be undone (editor Undo, git).
- Features aren't built broadly in advance. They are added when, while working together, something comes up that "MCP can't do or does awkwardly, but Claude would do well".
- First feature candidate: AnimBP look-at (add the needed logic and a Look At node, run it to check the bone faces the target, then propose it).
- The earlier plan ("safe edits only with the editor closed and only on a committed git state") was replaced by this approach on 2026-10-27.

## On hold

- Parsing `.uasset` directly (reading without the engine): to be reconsidered if structure B is still too slow.
