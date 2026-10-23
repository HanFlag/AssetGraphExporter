// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Short plain-text views of an exported asset (the document built by FAGEAssetExporter::ExportAsset).
 * They read only the exported JSON, so any front-end (MCP tools, files next to the export) gets the same text.
 * Each returns an empty string when the export does not contain the section it needs.
 */
namespace AGE::Summary
{
	/** State hierarchy with enter conditions, tasks and transitions, one line each. Needs the "stateTree" section. */
	ASSETGRAPHEXPORTER_API FString StateTreeOverview(const FJsonObject& Export);

	/** One line per property binding: "source -> target". Filter keeps lines containing it (case-insensitive); empty keeps all. */
	ASSETGRAPHEXPORTER_API FString StateTreeBindings(const FJsonObject& Export, const FString& Filter);

	/**
	 * Variables, AnimGraph pose flow and state machines (state contents, transition rules written as expressions).
	 * Needs the "blueprint" section; other graphs (EventGraph, functions) are only counted.
	 */
	ASSETGRAPHEXPORTER_API FString AnimBlueprintOverview(const FJsonObject& Export);
}
