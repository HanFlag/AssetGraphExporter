// Asset Graph Exporter

#pragma once

#include "IAGEAssetHandler.h"

/**
 * Customizable Objects (the engine's Mutable plugin): the node graph they are built from, written like any other graph.
 *
 * The graph is found through reflection (the asset's "Source" property), so this module does not depend on the Mutable plugin:
 * a dependency would turn that plugin on in every project that uses this one (it is off by default).
 */
class FAGECustomizableObjectHandler : public IAGEAssetHandler
{
public:
	virtual FName GetHandlerName() const override { return TEXT("CustomizableObject"); }
	virtual int32 GetPriority() const override { return 10; }
	virtual bool CanHandle(const UObject& Asset) const override;
	virtual void Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const override;
};
