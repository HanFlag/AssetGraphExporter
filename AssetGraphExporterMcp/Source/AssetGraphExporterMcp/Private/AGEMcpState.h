// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAssetGraphExporterMcp, Log, All);

/** Process-wide state shared by the age_* tools and the AssetGraphServe commandlet. Game thread only. */
namespace AGE::Mcp
{
	/** true inside the AssetGraphServe commandlet: no editor UI, assets are read from the saved files. */
	bool IsServeMode();

	/** Called once by the serve commandlet before it starts answering. IdleMinutes 0 = never stop on its own. */
	void EnterServeMode(int32 IdleMinutes);

	/** When serve mode started (UTC). Packages loaded before then may have changed on disk since. */
	FDateTime GetServeStartUtc();

	int32 GetIdleMinutes();

	/** Every age_* call marks activity; the serve loop stops after the idle limit. */
	void NoteActivity();

	/** FPlatformTime::Seconds() of the last age_* call (serve start if none yet). */
	double GetLastActivityTime();

	/** age_shutdown: the serve loop stops a moment later, after the reply has been sent. */
	void RequestShutdown();
	bool IsShutdownRequested();
}
