// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"

struct IModelContextProtocolTool;

namespace AGE::Mcp
{
	/** The age_* tools, ready to add to the Unreal MCP server. A new set each call. */
	TArray<TSharedRef<IModelContextProtocolTool>> CreateTools();
}
