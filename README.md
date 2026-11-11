**English** | [한국어](README.ko.md)

# AssetGraphExporter

A UE 5.8 editor plugin that lets Claude read and analyze Unreal assets better than the engine's built-in Unreal MCP does. It works in any project.

This version (0.3.1) reads and analyzes assets, **even with the editor closed**.

[Releases](https://github.com/HanFlag/AssetGraphExporter/releases) · [AI guide](AssetGraphExporterMcp/Resources/AI_GUIDE.md) · [Guide](Documentation/Guide.md) · [JSON schema](Documentation/JsonSchema.md) · [Changelog](CHANGELOG.md) · [Roadmap](ROADMAP.md)

## Usage

Claude uses the tools. You install and connect the plugin, then ask Claude in plain words.

1. **Install**: unzip the release and put the `AssetGraphExporter` and `AssetGraphExporterMcp` folders in `<Project>/Plugins/`, then open the editor. The plugin starts the server on its own.
2. **Connect**: run this once in a terminal.
   ```
   claude mcp add --transport http unreal http://127.0.0.1:8000/mcp
   ```
3. **Start**: tell Claude "read age_guide and get started". After that, just ask.
   - "Summarize what BP_Enemy does"
   - "If I rename this C++ function, what breaks?"
   - "What changed in the StateTree since the last commit?"

To use it with the editor closed, tell Claude "I want to use it without the editor" and Claude starts the server. If you get stuck, tell Claude "read `Plugins/AssetGraphExporterMcp/Resources/AI_GUIDE.md` and help me". Other install methods and all options are in the [Guide](Documentation/Guide.md).

## Why use it

Compared with the built-in Unreal MCP on the same assets (details in [ROADMAP.md](ROADMAP.md)):

| | Built-in Unreal MCP | This plugin |
|---|---|---|
| Editor closed | Not possible | Possible, with a background server |
| Read one StateTree | 120 calls, 49K chars | 1 call, 3.1K chars |
| Read one Blueprint | 13 calls, 20K chars | 2.3K chars |
| Niagara Simulation Stages | Cannot read | Reads them (25 in a fluid system) |
| AnimBP transition rules | No dedicated tool | As expressions (`if (bIsStrafing AND (Speed2D < 10))`) |

- **Project-wide questions**: who uses this asset, which BPs call this C++ function, what changed in this BP since the last commit.
- **The same asset always gives the same JSON**: so diffs and caching work (incremental export 13.0 s → 0.57 s).
- **Generic**: no project names in the code, so it drops into any project as is.

## Tools

| Tool | What it does |
|---|---|
| `age_blueprint_overview` · `age_animbp_overview` | BP summary: variables and defaults, execution flow one line at a time. For AnimBPs also the pose flow and state machines |
| `age_statetree_overview` · `age_statetree_bindings` | StateTree states, conditions, tasks, transitions and bindings |
| `age_niagara_overview` | A short outline of every stack (including Simulation Stages); with `emitter` or `full`, the modules and input values |
| `age_references` | What this asset uses, and what uses this asset |
| `age_find_usages` | BP nodes that use a C++ class, function or variable |
| `age_diff` | Differences between an older git revision and now |
| `age_find_assets` · `age_get_asset` | Find assets, pull out part of the structure JSON |
| `age_level_actors` | What is where in the open level: the actors in a box or radius with location, rotation, scale, bounds and mesh (editor only) |
| `age_status` · `age_shutdown` | Check status (including whether an editing plugin is installed), stop the background server |
| `age_open_window` | Open the window that shows what the AI is doing |
| `age_guide` · `age_feedback` | The guide the AI reads first; report a problem (opens a pre-filled GitHub issue page, you press Submit) |

## Requirements

- Unreal Engine 5.8, Win64 (checked on 5.8.4). The release zip contains a build for 5.8.4 Win64. For other engine versions, add it to a C++ project and build from source.
- MCP tools: the Unreal MCP plugin that ships with the engine (`ModelContextProtocol`, Experimental).
- Optional: the StateTree and Niagara plugins (without them, only that handler is missing). `age_diff` needs git.
- Not done yet: analyzing level contents. Other limits are in [Known limitations](Documentation/Guide.md#known-limitations).

## Notices

- This is an independent project. It is not affiliated with, sponsored or endorsed by Epic Games, Inc. Unreal, Unreal Engine and Unreal MCP are trademarks or registered trademarks of Epic Games, Inc. in the United States and elsewhere.
- This tool sends project data to the connected AI through Unreal MCP. Under the Unreal Engine EULA, it is your responsibility to make sure the AI service you use does not use that data for training.
- The software is provided "as is", without warranty of any kind. See [LICENSE](LICENSE).

## License

[PolyForm Strict 1.0.0](LICENSE) © 2026 HanFlag. You may use it for noncommercial purposes: personal study, testing, hobby projects, and the noncommercial organizations the license lists. Changing it, making new works based on it, and sharing or selling it are not allowed. **Commercial use** (for example, in a game you plan to sell, or at a company) needs written permission from [HanFlag](https://github.com/HanFlag), usually as a paid license.
