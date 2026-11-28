// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"

struct IModelContextProtocolTool;

namespace AGE::Mcp
{
	/** age_capture: pictures of the editor (level viewport, a window, an asset's editor) for the assistant to look at and to show the user. */
	TSharedRef<IModelContextProtocolTool> CreateCaptureTool();
}
