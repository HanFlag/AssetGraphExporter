// Asset Graph Exporter

#include "AGEGraphWriter.h"
#include "AGEExportContext.h"
#include "AGEPropertyWriter.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"

namespace
{
	const TCHAR* ContainerTypeToString(EPinContainerType ContainerType)
	{
		switch (ContainerType)
		{
		case EPinContainerType::Array: return TEXT("array");
		case EPinContainerType::Set: return TEXT("set");
		case EPinContainerType::Map: return TEXT("map");
		default: return TEXT("none");
		}
	}

	void SetIfNotEmpty(FJsonObject& Json, const TCHAR* Key, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Json.SetStringField(Key, Value);
		}
	}

	TSharedRef<FJsonObject> WritePin(const UEdGraphPin& Pin)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		// Only pins something else points at need an id (links are stored on both ends; split pins point at their parent).
		// Some pins are rebuilt with a fresh GUID on every load (e.g. hidden ErrorTolerance on comparison nodes), so writing
		// every id would make identical assets export differently.
		if (Pin.LinkedTo.Num() > 0 || Pin.SubPins.Num() > 0)
		{
			Json->SetStringField(TEXT("id"), AGE::GuidToString(Pin.PinId));
		}
		Json->SetStringField(TEXT("name"), Pin.PinName.ToString());
		if (!Pin.PinFriendlyName.IsEmpty() && Pin.PinFriendlyName.ToString() != Pin.PinName.ToString())
		{
			Json->SetStringField(TEXT("displayName"), Pin.PinFriendlyName.ToString());
		}
		Json->SetStringField(TEXT("dir"), Pin.Direction == EGPD_Input ? TEXT("in") : TEXT("out"));
		Json->SetObjectField(TEXT("type"), FAGEGraphWriter::WritePinType(Pin.PinType));
		SetIfNotEmpty(*Json, TEXT("default"), Pin.GetDefaultAsString());
		if (Pin.bHidden)
		{
			Json->SetBoolField(TEXT("hidden"), true);
		}
		if (Pin.bOrphanedPin)
		{
			Json->SetBoolField(TEXT("orphaned"), true);
		}
		if (Pin.ParentPin)
		{
			// Split struct pins (e.g. a Vector split into X/Y/Z) point back to the pin they came from.
			Json->SetStringField(TEXT("parentPin"), AGE::GuidToString(Pin.ParentPin->PinId));
		}

		TArray<TSharedPtr<FJsonValue>> Links;
		for (const UEdGraphPin* Linked : Pin.LinkedTo)
		{
			const UEdGraphNode* LinkedNode = Linked ? Linked->GetOwningNodeUnchecked() : nullptr;
			if (!LinkedNode)
			{
				continue;
			}
			TSharedRef<FJsonObject> Link = MakeShared<FJsonObject>();
			Link->SetStringField(TEXT("node"), AGE::GuidToString(LinkedNode->NodeGuid));
			Link->SetStringField(TEXT("pin"), AGE::GuidToString(Linked->PinId));
			Link->SetStringField(TEXT("pinName"), Linked->PinName.ToString());
			Links.Add(MakeShared<FJsonValueObject>(Link));
		}
		if (Links.Num() > 0)
		{
			Json->SetArrayField(TEXT("links"), Links);
		}
		return Json;
	}
}

FAGEGraphWriter::FAGEGraphWriter(FAGEExportContext& InContext, const IAGENodeDecorator* InDecorator)
	: Context(InContext)
	, Decorator(InDecorator)
{
}

TSharedRef<FJsonObject> FAGEGraphWriter::WritePinType(const FEdGraphPinType& PinType)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("category"), PinType.PinCategory.ToString());
	if (!PinType.PinSubCategory.IsNone())
	{
		Json->SetStringField(TEXT("subCategory"), PinType.PinSubCategory.ToString());
	}
	if (const UObject* SubCategoryObject = PinType.PinSubCategoryObject.Get())
	{
		Json->SetStringField(TEXT("object"), SubCategoryObject->GetPathName());
	}
	if (!PinType.PinSubCategoryMemberReference.MemberName.IsNone())
	{
		Json->SetStringField(TEXT("member"), PinType.PinSubCategoryMemberReference.MemberName.ToString());
	}
	if (PinType.ContainerType != EPinContainerType::None)
	{
		Json->SetStringField(TEXT("container"), ContainerTypeToString(PinType.ContainerType));
	}
	if (PinType.ContainerType == EPinContainerType::Map)
	{
		TSharedRef<FJsonObject> ValueType = MakeShared<FJsonObject>();
		ValueType->SetStringField(TEXT("category"), PinType.PinValueType.TerminalCategory.ToString());
		if (!PinType.PinValueType.TerminalSubCategory.IsNone())
		{
			ValueType->SetStringField(TEXT("subCategory"), PinType.PinValueType.TerminalSubCategory.ToString());
		}
		if (const UObject* ValueObject = PinType.PinValueType.TerminalSubCategoryObject.Get())
		{
			ValueType->SetStringField(TEXT("object"), ValueObject->GetPathName());
		}
		Json->SetObjectField(TEXT("valueType"), ValueType);
	}
	if (PinType.bIsReference)
	{
		Json->SetBoolField(TEXT("byRef"), true);
	}
	if (PinType.bIsConst)
	{
		Json->SetBoolField(TEXT("const"), true);
	}
	return Json;
}

TSharedRef<FJsonObject> FAGEGraphWriter::WriteGraph(const UEdGraph& Graph, const FString& Kind)
{
	WrittenGraphs.Add(&Graph);

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("name"), Graph.GetName());
	Json->SetStringField(TEXT("kind"), Kind);
	Json->SetStringField(TEXT("class"), Graph.GetClass()->GetName());
	if (Graph.Schema)
	{
		Json->SetStringField(TEXT("schema"), Graph.Schema->GetName());
	}
	// State machine, state, transition and collapsed graphs are created inside the node that owns them.
	if (const UEdGraphNode* OwnerNode = Cast<UEdGraphNode>(Graph.GetOuter()))
	{
		Json->SetStringField(TEXT("ownerNode"), AGE::GuidToString(OwnerNode->NodeGuid));
	}

	TArray<TSharedPtr<FJsonValue>> Nodes;
	for (const TObjectPtr<UEdGraphNode>& Node : Graph.Nodes)
	{
		if (Node)
		{
			Nodes.Add(MakeShared<FJsonValueObject>(WriteNode(*Node)));
		}
	}
	Json->SetArrayField(TEXT("nodes"), Nodes);

	TArray<TSharedPtr<FJsonValue>> SubGraphs;
	for (const TObjectPtr<UEdGraph>& SubGraph : Graph.SubGraphs)
	{
		if (SubGraph && !WrittenGraphs.Contains(SubGraph.Get()))
		{
			SubGraphs.Add(MakeShared<FJsonValueObject>(WriteGraph(*SubGraph, TEXT("SubGraph"))));
		}
	}
	if (SubGraphs.Num() > 0)
	{
		Json->SetArrayField(TEXT("subGraphs"), SubGraphs);
	}
	return Json;
}

TSharedRef<FJsonObject> FAGEGraphWriter::WriteNode(const UEdGraphNode& Node)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("guid"), AGE::GuidToString(Node.NodeGuid));
	Json->SetStringField(TEXT("class"), Node.GetClass()->GetName());
	Json->SetStringField(TEXT("title"), Node.GetNodeTitle(ENodeTitleType::FullTitle).ToString());

	TArray<TSharedPtr<FJsonValue>> Position;
	Position.Add(MakeShared<FJsonValueNumber>(Node.NodePosX));
	Position.Add(MakeShared<FJsonValueNumber>(Node.NodePosY));
	Json->SetArrayField(TEXT("pos"), Position);

	if (Node.GetDesiredEnabledState() != ENodeEnabledState::Enabled)
	{
		Json->SetStringField(TEXT("enabledState"), LexToString(Node.GetDesiredEnabledState()));
	}
	if (Node.IsAutomaticallyPlacedGhostNode())
	{
		// Disabled default event placed by the engine. Its comment is engine boilerplate saved in the language
		// of whoever created the Blueprint, so it is replaced by this flag.
		Json->SetBoolField(TEXT("ghost"), true);
	}
	else
	{
		SetIfNotEmpty(*Json, TEXT("comment"), Node.NodeComment);
	}

	if (Decorator)
	{
		Decorator->Decorate(Node, Context, *Json);
	}

	// Base UEdGraphNode fields (position, size, comment bubble, error state) are layout noise or written above.
	const UClass* EdGraphNodeClass = UEdGraphNode::StaticClass();
	FAGEPropertyWriter PropertyWriter(Context);
	TSharedRef<FJsonObject> Properties = PropertyWriter.WriteObjectProperties(Node, [this, &Node, EdGraphNodeClass](const FProperty& Property)
	{
		return Property.GetOwnerClass() != EdGraphNodeClass && !(Decorator && Decorator->SkipProperty(Node, Property));
	});
	if (Properties->Values.Num() > 0)
	{
		Json->SetObjectField(TEXT("properties"), Properties);
	}

	TArray<TSharedPtr<FJsonValue>> Pins;
	for (const UEdGraphPin* Pin : Node.Pins)
	{
		if (Pin)
		{
			Pins.Add(MakeShared<FJsonValueObject>(WritePin(*Pin)));
		}
	}
	Json->SetArrayField(TEXT("pins"), Pins);
	return Json;
}
