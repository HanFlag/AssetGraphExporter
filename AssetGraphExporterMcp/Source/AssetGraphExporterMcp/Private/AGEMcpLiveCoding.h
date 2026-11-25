// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"

/** Watches Live Coding patches in the editor and says when one changed C++ class or struct layouts (usually a header edit). Game thread only. */
namespace AGE::Mcp::LiveCoding
{
	void Startup();
	void Shutdown();

	/** age_status lines; none before the first patch. */
	void AppendStatus(TArray<FString>& Lines);
}
