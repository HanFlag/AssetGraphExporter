// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

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
