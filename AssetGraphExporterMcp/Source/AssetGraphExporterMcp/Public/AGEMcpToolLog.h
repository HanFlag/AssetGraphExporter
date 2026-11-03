// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "ModelContextProtocolToolResults.h"

/** For tools that other plugins add to the same MCP server, so they show in the activity page and window like the age_* tools here. */
namespace AGE::Mcp
{
	/** Call when the tool starts: the window shows work in progress. */
	ASSETGRAPHEXPORTERMCP_API void NoteExternalToolStart();

	/** Call once the tool has answered: the call is listed with its result. */
	ASSETGRAPHEXPORTERMCP_API void RecordExternalToolCall(const FString& Tool, const FJsonObject& Arguments, const FModelContextProtocolToolResult& Result, double Milliseconds);
}
