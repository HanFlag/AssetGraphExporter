// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

/** Which assets to work on: what the commandlet gets on its command line, or what an MCP tool gets as arguments. */
struct FAGEAssetQuery
{
	/** Package or object paths: /Game/Folder/Asset or /Game/Folder/Asset.Asset */
	TArray<FString> Assets;
	/** Folders, searched recursively: /Game/Folder */
	TArray<FString> Paths;
	/** Class filter: short native names (Blueprint, StateTree, GameplayAbility) or full paths (/Script/Engine.Blueprint, /Game/BP_Base.BP_Base_C) */
	TArray<FString> Classes;
	/**
	 * true: scan all asset files first and wait for the scan (commandlets, which do not scan in the background).
	 * false: use the registry as it is (the editor keeps it up to date while running).
	 */
	bool bRescan = true;
};

namespace AGE
{
	/** Resolves the query through the Asset Registry without loading the assets. Sorted by object path. Problems are appended to OutErrors. */
	ASSETGRAPHEXPORTER_API TArray<FAssetData> CollectAssets(const FAGEAssetQuery& Query, TArray<FString>& OutErrors);
}
