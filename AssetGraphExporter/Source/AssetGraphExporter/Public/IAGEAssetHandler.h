// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FProperty;
class UObject;
struct FAGEExportContext;

/**
 * Adds asset-type specific sections to an exported asset.
 * Every asset first gets the generic "properties" section; a handler only adds what reflection alone cannot show well
 * (graphs, state hierarchies, bindings resolved to readable names).
 */
class IAGEAssetHandler
{
public:
	virtual ~IAGEAssetHandler() = default;

	/** Unique name, used to unregister and written to the JSON as "handler". */
	virtual FName GetHandlerName() const = 0;

	/** When several handlers accept an asset, the highest priority wins (e.g. AnimBlueprint over Blueprint). */
	virtual int32 GetPriority() const = 0;

	virtual bool CanHandle(const UObject& Asset) const = 0;

	/** Return false to leave a top-level asset property out of "properties" (e.g. because Export writes it in a cleaner form). */
	virtual bool ShouldExportAssetProperty(const FProperty& Property) const { return true; }

	/** Adds handler-specific sections to the asset JSON. */
	virtual void Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const = 0;
};
