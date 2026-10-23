// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

/** Picking parts out of an exported document, for front-ends that cannot return the whole thing at once. */
namespace AGE::JsonQuery
{
	/**
	 * Follows a path such as "stateTree.bindings[3].source" or "blueprint.graphs[EventGraph].nodes".
	 * Keys are separated by '.'. "[3]" is an array index; "[Text]" is the first array item whose "name" (or "path", "title")
	 * is Text. An empty path returns Root. On failure returns nullptr and says why in OutError.
	 */
	ASSETGRAPHEXPORTER_API TSharedPtr<FJsonValue> Select(const TSharedPtr<FJsonValue>& Root, const FString& Path, FString& OutError);

	/** JSON without whitespace. */
	ASSETGRAPHEXPORTER_API FString ToCompactString(const TSharedPtr<FJsonValue>& Value);

	/**
	 * A table of contents for Value: one line per key (or array item, up to MaxItems) with its type and size,
	 * written as the path to pass to Select. Used instead of the value itself when it is too large.
	 */
	ASSETGRAPHEXPORTER_API FString Outline(const TSharedPtr<FJsonValue>& Value, const FString& Path, int32 MaxItems = 60);
}
