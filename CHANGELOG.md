**English** | [한국어](CHANGELOG.ko.md)

# Changelog

## Unreleased

- **License**: changed from the tester license to [MIT](LICENSE).
- **Documentation**: English is now the default language, and every document has a Korean version (`*.ko.md`). Switch with `English | 한국어` at the top of each document.
- **Notices**: the README's "how it was made and notices" section is now "Notices" (not affiliated with Epic Games and trademarks, the AI data note under the EULA, no warranty).
- The activity page (`/age/activity`) is now in English.
- Examples in the documentation and tool descriptions now use names not tied to any project.

## 0.2.2 (beta) — 2026-10-27

Usage is down to three steps: install, connect, start.

- **Server starts on its own**: when the editor opens, the plugin starts the Unreal MCP server itself. The engine's "Auto Start Server" is a per-user setting each person has to turn on, so that step is gone. To prevent it, use `-AGENoServerAutoStart`. If the background server is just shutting down, it retries for a short while.
- **The AI starts the editor-less mode**: `AI_GUIDE.md` explains how to find the engine (the registry's `InstalledDirectory`) and start the background server directly. The user doesn't need to type commands.
- README: three-step usage; the harder command lines moved to the Guide.
- `Serve.bat`: fixed the check for a running editor being skipped when run from Git Bash (Windows `find` and `tasklist` are now called by full path). Checked in Git Bash: refuses within 1 second when the editor is open, starts normally when it is closed.
- Checked `age_references` and `age_guide` in editor mode (same results as background mode).

## 0.2.1 (beta) — 2026-10-27

The AI now learns the tool by itself and reports problems through the user.

- **AI guide `AI_GUIDE.md`** (English, `AssetGraphExporterMcp/Resources/`): what to tell the user first (what it is, what it can and can't do yet, confirm mode and auto mode), step-by-step connection, which tool for which question, efficiency rules, troubleshooting, a request for feedback.
- **`age_guide`**: its tool description says "call this first", so a connected AI gets the guide.
- **`age_feedback`**: saves a problem or a needed feature on the user's PC (`Saved/AssetGraphExporter/Feedback/feedback.md`, change with `-AGEFeedbackDir=`) and opens a pre-filled GitHub issue page in the browser. The user checks it and presses Submit. Project asset paths and user folder paths are removed automatically. It goes to the plugin's `SupportURL`.
- When an `age_*` tool fails, one line is added at the end of the result: "if you're stuck, tell the user and leave it with age_feedback".
- **License**: changed from MIT to a tester license (invited people, for testing and feedback only).
- Checked `age_find_usages`, `age_diff`, `age_blueprint_overview`, `age_statetree_overview` and `age_niagara_overview` in editor mode.

## 0.2.0 (beta) — 2026-10-27

The first release. Adds MCP tools, a Niagara handler, project-wide search and revision diffs to the asset structure export (0.1.0).

### Requirements
- Unreal Engine 5.8, Win64 (built and checked on 5.8.4).
- MCP tools (`AssetGraphExporterMcp`): the Unreal MCP plugin that ships with the engine (`ModelContextProtocol`, Experimental).
- Optional: the StateTree and Niagara plugins. Without them, only that handler is missing and the rest works.
- `age_diff`: git must be on the PATH.

### What's in it
- **Export (`AssetGraphExporter`)**
  - Commandlet `AssetGraphExport`: every UObject asset to JSON. Blueprint and AnimBP (graphs, nodes, pins, bindings, state machines), StateTree (states, transitions, bindings), Niagara System (emitter stacks, Simulation Stages, module input values).
  - `-Incremental`: export again only assets that changed (the asset itself, its dependencies, or C++ code).
  - The same asset always gives byte-for-byte the same JSON. JSON schema version 2.
- **MCP tools (`AssetGraphExporterMcp`)**: work at the same address inside the editor or in a background server started without the editor (`Serve.bat`).
  - `age_find_assets`, `age_get_asset`: find assets, pull out part of the structure JSON.
  - `age_blueprint_overview`, `age_animbp_overview`, `age_statetree_overview`, `age_statetree_bindings`, `age_niagara_overview`: short summaries. BP execution flow one line at a time.
  - `age_references`: what an asset uses and what uses it.
  - `age_find_usages`: BP nodes that use a C++ class, function or variable.
  - `age_diff`: differences between an older git revision and now.
  - `age_status`, `age_shutdown`, the activity page (`/age/activity`).

### Checked and not checked yet
- Checked: in one project (UE 5.8.4, Win64), zero build warnings, every tool called in background-server mode. The first 7 tools also called in editor mode.
- Not yet: other projects, other platforms. Editor mode of `age_references`, `age_find_usages`, `age_diff` and `age_blueprint_overview`. Whether the background server and incremental export read new content after an asset is saved again (testing it would mean changing assets).
- For other limits, see [Known limitations](Documentation/Guide.md#known-limitations).

### License
MIT.

## 0.1.0 — 2026-10-23 (not released)
- Commandlet export: generic reflection, Blueprint/AnimBP, StateTree.
