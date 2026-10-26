// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "IAGEAssetHandler.h"

/**
 * Niagara systems: user parameters, the system stacks, and for each emitter its stacks in execution order
 * (emitter spawn/update, particle spawn/update, event handlers, simulation stages) with module inputs, then renderers.
 *
 * Stacks are read from the script graphs directly: from each stack's output node back along the parameter map chain,
 * the same rule the editor's stack uses. The editor's stack view model is not built, so this also works in a commandlet.
 * A module input value comes from its override pin (linked parameter, dynamic input, data interface, local value)
 * or, for constants, from the script's rapid iteration parameters. Inputs found in neither are at their module default and are left out.
 */
class FAGENiagaraSystemHandler : public IAGEAssetHandler
{
public:
	static const FName HandlerName;

	virtual FName GetHandlerName() const override { return HandlerName; }
	virtual int32 GetPriority() const override { return 10; }
	virtual bool CanHandle(const UObject& Asset) const override;
	/** Emitters, scripts, user parameters and compiled data are left out of "properties"; "niagara" writes them readably. */
	virtual bool ShouldExportAssetProperty(const FProperty& Property) const override;
	virtual void Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const override;
};
