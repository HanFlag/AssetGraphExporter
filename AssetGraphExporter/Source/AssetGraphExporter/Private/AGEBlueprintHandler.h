// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "AGEGraphWriter.h"
#include "IAGEAssetHandler.h"

/** Blueprints: parent class, variables, components, class defaults (CDO) and every graph. */
class FAGEBlueprintHandler : public IAGEAssetHandler
{
public:
	virtual FName GetHandlerName() const override { return TEXT("Blueprint"); }
	virtual int32 GetPriority() const override { return 10; }
	virtual bool CanHandle(const UObject& Asset) const override;
	virtual bool ShouldExportAssetProperty(const FProperty& Property) const override;
	virtual void Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const override;

protected:
	virtual const IAGENodeDecorator* GetNodeDecorator() const { return nullptr; }
};

/** Writes AnimGraph pin bindings (property access / function bindings) on AnimGraph nodes. */
class FAGEAnimNodeDecorator : public IAGENodeDecorator
{
public:
	virtual void Decorate(const UEdGraphNode& Node, FAGEExportContext& Context, FJsonObject& NodeJson) const override;
	virtual bool SkipProperty(const UEdGraphNode& Node, const FProperty& Property) const override;
};

/** Animation Blueprints: everything a Blueprint has, plus AnimGraph bindings. */
class FAGEAnimBlueprintHandler : public FAGEBlueprintHandler
{
public:
	virtual FName GetHandlerName() const override { return TEXT("AnimBlueprint"); }
	virtual int32 GetPriority() const override { return 20; }
	virtual bool CanHandle(const UObject& Asset) const override;

protected:
	virtual const IAGENodeDecorator* GetNodeDecorator() const override { return &NodeDecorator; }

private:
	FAGEAnimNodeDecorator NodeDecorator;
};
