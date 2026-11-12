// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "UObject/StructOnScope.h"

namespace AGE
{
	/** Bumped when the JSON layout changes in a way readers must know about. */
	inline constexpr int32 SchemaVersion = 2;
	inline constexpr const TCHAR* ExporterVersion = TEXT("0.3.1");

	/** GUIDs are written as 8-4-4-4-12 hex everywhere so they can be matched across files. */
	ASSETGRAPHEXPORTER_API FString GuidToString(const FGuid& Guid);
}

/** State shared by every writer while one asset is exported. */
struct ASSETGRAPHEXPORTER_API FAGEExportContext
{
	/** true: only values that differ from the archetype / struct defaults are written. -Full turns it off. */
	bool bDiffOnly = true;

	/** true: each written object also gets "$details" (Details-panel display name and category of its editable properties). */
	bool bWriteDetails = false;

	/** Limit for nested owned-object expansion, so a broken object graph cannot recurse forever. */
	int32 MaxObjectDepth = 12;

	/** The asset being exported. */
	const UObject* RootAsset = nullptr;

	/** Owned objects already written once; a second reference is written as a path. */
	TSet<const UObject*> ExpandedObjects;

	/** Problems that did not stop the export. Written to the asset file and the log. */
	TArray<FString> Warnings;

	void Warn(const FString& Message);

	/** Default-initialized memory for a struct type (cached), used as the diff base for struct values. */
	const void* GetStructDefaults(const UScriptStruct& Struct);

private:
	TMap<const UScriptStruct*, TSharedPtr<FStructOnScope>> StructDefaults;
};
