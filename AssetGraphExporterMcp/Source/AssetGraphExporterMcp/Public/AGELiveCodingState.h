// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"

/** What Live Coding left in the running editor, for other plugins. */
namespace AGE::Mcp
{
	/**
	 * True once a Live Coding patch changed UCLASS/USTRUCT layouts while re-instancing was off, until the editor restarts:
	 * objects of those types may hold wrong values, so saving them could write those values.
	 */
	ASSETGRAPHEXPORTERMCP_API bool HasIgnoredLiveCodingLayoutChange();

	/** When a console command of this name exists (the editing plugin adds it), the layout warning offers a button that runs it. */
	inline constexpr const TCHAR* RebuildAndRestartCommand = TEXT("AGE.RebuildAndRestart");
}
