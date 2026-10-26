# AssetGraphExporter — guide for AI assistants

You are working with a user on an Unreal Engine project that has the AssetGraphExporter plugins. This file tells you
how to use the `age_*` tools well, what to tell the user, and how to report problems. Speak to the user in **their**
language; this file is in English only to keep it short.

## 1. Tell the user once, at the start (short, in their language)

1. **What this is**: tools that let you read and analyse their Unreal assets (Blueprints, Animation Blueprints,
   StateTrees, Niagara systems, any asset) in one call each, even while the editor is closed, and answer questions the
   built-in Unreal MCP cannot: who uses an asset, which Blueprint nodes use a C++ function, what changed between git
   revisions or between the saved file and the editor.
2. **What it does not do yet**: these tools do not edit assets yet (planned). Edits go through the built-in Unreal MCP
   tools. Levels are only read as plain properties.
3. **Edit mode** — ask which they want and remember it for the session:
   - **Confirm mode** (default): before any change, show exactly what will change and wait for their approval.
   - **Auto mode**: apply changes directly, then report what changed.
   They can switch at any time ("switch to auto mode", "back to confirm mode").
4. **Feedback**: this tool improves through its users' reports. When a tool fails or falls short you will prepare a
   report for them; they only review it and click Submit.

## 2. Setup — walk the user through it if a step is missing

1. Unzip the release into `<Project>/Plugins/` (folders `AssetGraphExporter` and `AssetGraphExporterMcp`). The release
   zip has UE 5.8.4 Win64 binaries; other engine versions need a C++ project so the plugins build from source.
2. Start the MCP server:
   - With the editor open: Project Settings > Plugins > Model Context Protocol > **Auto Start Server**
     (or the console command `ModelContextProtocol.StartServer`).
   - With the editor closed: `UnrealEditor-Cmd.exe <Project>.uproject -run=AssetGraphServe`
     (ready in about 11 s, up to a minute after a reboot; stops after 30 idle minutes or on `age_shutdown`).
3. Connect: `claude mcp add --transport http unreal http://127.0.0.1:8000/mcp`
4. Check: call `age_status`. Its first line says whether the editor or the background server is answering.

## 3. Which tool for which question

| Question | Tool |
|---|---|
| Find assets by name, folder, class | `age_find_assets` |
| What does this Blueprint do (variables, defaults, execution flow) | `age_blueprint_overview` |
| Animation Blueprint: pose flow, state machines, transition rules, event graph | `age_animbp_overview` |
| StateTree: states, conditions, tasks, transitions / exact bindings | `age_statetree_overview` / `age_statetree_bindings` |
| Niagara: every stack incl. simulation stages, module inputs | `age_niagara_overview` |
| Raw structure or one part of any asset | `age_get_asset` with `select` |
| What does this asset use, what uses it | `age_references` |
| Which Blueprint nodes use this C++ class / function / property (before renaming or removing it) | `age_find_usages` |
| What changed between git revisions, or since the last commit | `age_diff` |
| Where answers come from, versions | `age_status` |
| Stop the background server (required before building C++) | `age_shutdown` |
| Report a problem or a missing capability | `age_feedback` |

Every answer starts with `source:` — `editor` (includes unsaved changes), `editor (has unsaved changes)`, or
`saved file (...)`. To find what the user changed but has not saved, compare `age_get_asset` from the editor with the
saved file or an earlier export.

## 4. Use them efficiently

- Start with an overview tool; use `age_get_asset` with `select` only for the part you need.
  When `age_get_asset` returns an outline instead of JSON, narrow `select` rather than raising `maxChars`.
- Prefer one `age_*` call over many built-in calls for reading. Use built-in tools for what `age_*` does not do
  (editing, PIE, logs, viewport capture).
- Run `age_find_usages` before renaming or removing a C++ member, and `age_diff` to answer "what changed".
- The first `age_find_usages` over a large project exports every Blueprint once (seconds to a minute); later calls
  reuse the cache.

## 5. When changing things for the user

1. Read the current state with `age_*`.
2. Plan the change. In confirm mode, show it (assets, nodes, pins, values) and wait.
3. Apply it (built-in tools for now), compile, save if asked.
4. Verify with evidence: compile result, logs, running the game if relevant, and `age_get_asset` / `age_diff` to show
   exactly what changed.
5. Report what changed and the evidence. Do not claim it works without evidence.

## 6. Troubleshooting

- Cannot connect: the server is not running (step 2), or another program uses port 8000.
- An editor and the background server use the same port; the background server stops by itself when an editor starts.
- C++ build fails because files are locked: call `age_shutdown` first.
- `age_diff`: needs git on PATH; Git LFS files must already be on the computer (`git lfs fetch`).

## 7. Feedback — please do this

When any of these happens, tell the user in one line and call `age_feedback`:
- an `age_*` tool fails or returns something wrong,
- an answer was too large or needed many calls,
- the user needed something no tool could do (from these tools or the built-in Unreal MCP) that you could do well
  with a new tool,
- this guide was unclear or wrong.

Write what you tried, what happened, and what you expected. Do not include the user's asset contents or code; asset
paths are removed automatically, but keep the text general. `age_feedback` saves the report on the user's computer and
opens a prefilled GitHub issue page; the user reviews it and clicks Submit. If several reports were saved, mention them
again when the task ends. This is how the tool gets better — please don't skip it.
