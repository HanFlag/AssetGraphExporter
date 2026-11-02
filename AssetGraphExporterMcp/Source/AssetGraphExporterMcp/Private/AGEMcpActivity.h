// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FModelContextProtocolToolResult;

/**
 * A short record of what the MCP client (e.g. Claude) asked, shown on a page served by the MCP server itself:
 * http://127.0.0.1:<port>/age/activity, and in the window that reads the same data (Source/ActivityWindow, its own process).
 * Kept in memory only (the last MaxEntries calls); nothing is written to disk.
 *
 * age_* calls are recorded with their arguments and answer. Other tools (the engine's own) are only known by name,
 * from the "Running tool" line the Unreal MCP server logs for every call.
 */
namespace AGE::Mcp::Activity
{
	/** Module startup / shutdown: log capture, the AGE.OpenActivity console command, the page route. */
	void Startup();
	void Shutdown();

	/** One age_* call. Game thread. */
	void RecordToolCall(const FString& Tool, const FJsonObject& Arguments, const FModelContextProtocolToolResult& Result, double Milliseconds);

	/** Adds the page to the running MCP server (once per port). Returns its URL, or an empty string while no server runs. */
	FString EnsurePage();

	/** Opens the activity window, or brings it to the front. False while no server runs. Game thread. */
	bool OpenWindow();
}
