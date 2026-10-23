// Asset Graph Exporter

#pragma once

#include "Commandlets/Commandlet.h"
#include "AssetGraphExportCommandlet.generated.h"

/**
 * UnrealEditor-Cmd.exe <Project>.uproject -run=AssetGraphExport
 *     [-Assets=/Game/A,/Game/B.B] [-Path=/Game/Folder] [-Class=Blueprint,StateTree] [-Out=<dir>] [-Full]
 *
 * Writes one JSON file per asset under <Out> (default <Project>/Saved/AssetExport), mirroring the package path,
 * plus index.json. Assets are only read, never saved. Exit code 0 = every asset exported.
 */
UCLASS()
class UAssetGraphExportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UAssetGraphExportCommandlet();

	virtual int32 Main(const FString& Params) override;
};
