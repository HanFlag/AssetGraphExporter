// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "IAGEAssetHandler.h"

/**
 * StateTree assets: the editor data (state hierarchy, tasks, conditions, transitions, bindings), not the compiled runtime data.
 * GUIDs used by transitions and bindings are resolved to readable paths such as "Root/Combat/Attack".
 */
class FAGEStateTreeHandler : public IAGEAssetHandler
{
public:
	static const FName HandlerName;

	virtual FName GetHandlerName() const override { return HandlerName; }
	virtual int32 GetPriority() const override { return 10; }
	virtual bool CanHandle(const UObject& Asset) const override;
	/** The asset's own properties are compiled data or EditorData, which is written as "stateTree". */
	virtual bool ShouldExportAssetProperty(const FProperty& Property) const override { return false; }
	virtual void Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const override;
};
