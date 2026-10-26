// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

/** Comparing two exports of the same asset (e.g. two git revisions). */
namespace AGE::JsonDiff
{
	struct FChange
	{
		/** changed, added or removed */
		FString Kind;
		/** Path as age_get_asset's select reads it; array steps are [Name] or the item's index on the side that has it. */
		FString Path;
		/** Short text of the old and new value; empty for the side that does not exist. */
		FString Before;
		FString After;
	};

	/**
	 * Differences between two JSON values. Array items are paired by "guid" (graph nodes), else by "name", else by position,
	 * so inserting one node does not report every later node as changed.
	 */
	ASSETGRAPHEXPORTER_API TArray<FChange> Compare(const TSharedPtr<FJsonValue>& Before, const TSharedPtr<FJsonValue>& After);

	/**
	 * Line diff of two texts: "- old", "+ new", and up to Context unchanged lines ("  same") around each change; "  ..." between
	 * separate places. Empty when the texts are equal. A single "(too long to compare line by line)" line when both have
	 * so many lines that the comparison table would be too large.
	 */
	ASSETGRAPHEXPORTER_API TArray<FString> DiffLines(const TArray<FString>& Before, const TArray<FString>& After, int32 Context = 1);
}
