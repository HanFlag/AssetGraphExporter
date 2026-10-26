// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

/** An asset as it was in an earlier git revision, for age_diff. Game thread only. */
namespace AGE::Mcp::Revision
{
	struct FLoaded
	{
		UObject* Asset = nullptr;
		/** e.g. git 76acbc8 (2026-10-07 "Limit the boss ...") */
		FString Label;
		/** The package the revision was loaded as. Its name stands in for the original's inside the asset. */
		FString TempPackageName;
		FString Error;
	};

	/**
	 * Loads the asset's file as it was at Revision (anything git accepts: a hash, HEAD~2, a branch) under a temporary package
	 * mounted at /AGEDiff/, much as the editor's "Diff against depot" loads one: references to the original package go to the
	 * temporary one, Blueprints are not compiled. Git LFS files are read from the local LFS store only; nothing is downloaded.
	 * The file is written to -AGEDiffDir=<dir>, by default <Project>/Saved/AssetGraphExporter/Diff.
	 */
	FLoaded Load(const FAssetData& Asset, const FString& Revision);

	/** Closes the temporary package's file. The package itself goes with the next garbage collection. */
	void Release(const FLoaded& Loaded);
}
