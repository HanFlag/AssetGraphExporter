// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

/** What to export, as given on the command line. */
struct FAGEAssetQuery
{
	/** Package or object paths: /Game/Folder/Asset or /Game/Folder/Asset.Asset */
	TArray<FString> Assets;
	/** Folders, searched recursively: /Game/Folder */
	TArray<FString> Paths;
	/** Class filter: short native names (Blueprint, StateTree, GameplayAbility) or full paths (/Script/Engine.Blueprint, /Game/BP_Base.BP_Base_C) */
	TArray<FString> Classes;
};

namespace AGE
{
	/** Resolves the query through the Asset Registry without loading the assets. Problems are appended to OutErrors. */
	TArray<FAssetData> CollectAssets(const FAGEAssetQuery& Query, TArray<FString>& OutErrors);
}
