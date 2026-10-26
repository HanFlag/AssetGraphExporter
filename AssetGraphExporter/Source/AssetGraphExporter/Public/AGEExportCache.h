// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Templates/Function.h"

/**
 * Keys for reusing earlier exports. An asset's key changes when its saved file changes, when any package it hard-depends on
 * (followed to the end) changes, or when anything that changes every export changes: plugin and schema version, -Full,
 * text culture, engine version, and the C++ code (DLL files of the project, its plugins and this plugin).
 * The asset's JSON depends on those packages too: values are written as differences from the parent class, and Niagara
 * stacks read their module scripts.
 */
class ASSETGRAPHEXPORTER_API FAGEExportCache
{
public:
	FAGEExportCache(bool bFull, const FString& Culture);

	/** Hex key of one asset. OutPackageCount: how many packages went into it (the asset and its dependencies). */
	FString AssetKey(const FAssetData& Asset, int32* OutPackageCount = nullptr);

private:
	/** Saved hash of a package's file, or its size and file time when the package has no saved hash. */
	const FString& PackageHash(FName PackageName);

	FString SettingsKey;
	TMap<FName, FString> PackageHashes;
};

/** One export of a list of assets into a folder (<Out>/Game/Folder/Asset.json): settings in, index entries and counts out. */
struct ASSETGRAPHEXPORTER_API FAGEExportRun
{
	FString OutDir;
	bool bFull = false;
	/** Keep the file of an asset whose key, file name and earlier status in the folder's index.json match, instead of loading it. */
	bool bReuse = false;
	FString Culture;
	/** Loads one asset. Default: FAssetData::GetAsset. The MCP server first reloads packages that changed on disk. */
	TFunction<UObject*(const FAssetData&)> Load;

	/** This run's index entries, one per asset in the given order: asset, class, file, key, warnings, status, error. */
	TArray<TSharedPtr<FJsonValue>> Entries;
	/** The folder's earlier index.json entries by object path; read only when bReuse. */
	TMap<FString, TSharedPtr<FJsonObject>> PreviousEntries;
	int32 Reused = 0;
	int32 Failures = 0;
	double KeySeconds = 0.0;
	double LoadSeconds = 0.0;
	double ExportSeconds = 0.0;
	int64 KeyPackages = 0;
};

namespace AGE::ExportCache
{
	/** Exports (or, with bReuse, keeps) every asset and fills Run's entries and counts. Does not write index.json. */
	ASSETGRAPHEXPORTER_API void Export(const TArray<FAssetData>& Assets, FAGEExportRun& Run);

	/** Game/Folder/Asset.json for /Game/Folder/Asset. */
	ASSETGRAPHEXPORTER_API FString RelativeOutputPath(const FAssetData& Asset);

	/** Asset entries of an index.json by object path. Empty when the file is missing or unreadable. */
	ASSETGRAPHEXPORTER_API TMap<FString, TSharedPtr<FJsonObject>> ReadIndex(const FString& IndexPath);

	/** UTF-8 without BOM, creating the folder first. */
	ASSETGRAPHEXPORTER_API bool SaveJson(const FString& FilePath, const FString& Text);
}
