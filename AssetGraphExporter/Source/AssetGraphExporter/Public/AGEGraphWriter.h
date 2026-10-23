// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProperty;
class UEdGraph;
class UEdGraphNode;
struct FEdGraphPinType;
struct FAGEExportContext;

/**
 * Adds type-specific data to a node's JSON and may hide node properties it already wrote in a cleaner form.
 * Return true from SkipProperty for properties that must not appear in the node's "properties".
 */
class IAGENodeDecorator
{
public:
	virtual ~IAGENodeDecorator() = default;
	virtual void Decorate(const UEdGraphNode& Node, FAGEExportContext& Context, FJsonObject& NodeJson) const = 0;
	virtual bool SkipProperty(const UEdGraphNode& Node, const FProperty& Property) const { return false; }
};

/** Writes an EdGraph (nodes, pins, links) and its sub-graphs as a tree. */
class ASSETGRAPHEXPORTER_API FAGEGraphWriter
{
public:
	FAGEGraphWriter(FAGEExportContext& InContext, const IAGENodeDecorator* InDecorator = nullptr);

	/** Kind is a free label for where the graph came from (Ubergraph, Function, Macro, ...). */
	TSharedRef<FJsonObject> WriteGraph(const UEdGraph& Graph, const FString& Kind);

	static TSharedRef<FJsonObject> WritePinType(const FEdGraphPinType& PinType);

private:
	TSharedRef<FJsonObject> WriteNode(const UEdGraphNode& Node);

	FAGEExportContext& Context;
	const IAGENodeDecorator* Decorator;
	TSet<const UEdGraph*> WrittenGraphs;
};
