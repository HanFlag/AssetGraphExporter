// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FAGEExportContext;

/** Builds the full JSON document for one asset. Kept separate from the commandlet so other front-ends (e.g. an MCP server) can reuse it. */
class ASSETGRAPHEXPORTER_API FAGEAssetExporter
{
public:
	static TSharedRef<FJsonObject> ExportAsset(const UObject& Asset, FAGEExportContext& Context);

	/** Pretty-printed JSON text. */
	static FString ToJsonString(const TSharedRef<FJsonObject>& Json);
};
