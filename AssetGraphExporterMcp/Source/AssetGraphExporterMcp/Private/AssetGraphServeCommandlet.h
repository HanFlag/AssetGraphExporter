// Asset Graph Exporter

#pragma once

#include "Commandlets/Commandlet.h"
#include "AssetGraphServeCommandlet.generated.h"

/**
 * UnrealEditor-Cmd.exe <Project>.uproject -run=AssetGraphServe [-IdleMinutes=30] [-IgnoreEditor]
 *
 * Keeps the engine's Unreal MCP server running without the editor, with the age_* tools added, until age_shutdown,
 * Ctrl+C, IdleMinutes without an age_* call (0 = never), or an Unreal Editor starting (it serves the same port).
 * Assets are read from the saved files; a file saved again while this runs is reloaded on the next request.
 * Port and URL path come from the Unreal MCP settings (-ModelContextProtocolPort=N overrides the port).
 */
UCLASS()
class UAssetGraphServeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAssetGraphServeCommandlet();

	virtual int32 Main(const FString& Params) override;
};
