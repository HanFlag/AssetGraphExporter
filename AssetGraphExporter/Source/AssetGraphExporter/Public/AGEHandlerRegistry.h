// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "IAGEAssetHandler.h"

/** Handlers register here from their module's StartupModule; optional modules (e.g. StateTree) add theirs only when loaded. */
class ASSETGRAPHEXPORTER_API FAGEHandlerRegistry
{
public:
	static FAGEHandlerRegistry& Get();

	void Register(const TSharedRef<IAGEAssetHandler>& Handler);
	void Unregister(FName HandlerName);

	/** Highest-priority handler that accepts the asset, or null (then only generic properties are written). */
	const IAGEAssetHandler* FindHandler(const UObject& Asset) const;

private:
	TArray<TSharedRef<IAGEAssetHandler>> Handlers;
};
