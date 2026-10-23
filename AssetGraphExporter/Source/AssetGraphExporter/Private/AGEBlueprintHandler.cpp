// Asset Graph Exporter

#include "AGEBlueprintHandler.h"
#include "AGEExportContext.h"
#include "AGEPropertyWriter.h"
#include "AnimGraphNode_Base.h"
#include "Animation/AnimBlueprint.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"

namespace
{
	struct FFlagName
	{
		EPropertyFlags Flag;
		const TCHAR* Name;
	};

	// Variable flags a reader cares about (what the Details panel shows as checkboxes).
	const FFlagName VariableFlagNames[] =
	{
		{ CPF_Edit, TEXT("Edit") },
		{ CPF_DisableEditOnInstance, TEXT("DisableEditOnInstance") },
		{ CPF_BlueprintVisible, TEXT("BlueprintVisible") },
		{ CPF_BlueprintReadOnly, TEXT("BlueprintReadOnly") },
		{ CPF_ExposeOnSpawn, TEXT("ExposeOnSpawn") },
		{ CPF_Net, TEXT("Replicated") },
		{ CPF_RepNotify, TEXT("RepNotify") },
		{ CPF_Transient, TEXT("Transient") },
		{ CPF_SaveGame, TEXT("SaveGame") },
		{ CPF_Config, TEXT("Config") },
		{ CPF_Interp, TEXT("Interp") },
		{ CPF_AdvancedDisplay, TEXT("AdvancedDisplay") },
	};

	TSharedRef<FJsonObject> WriteVariable(const FBPVariableDescription& Variable)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("name"), Variable.VarName.ToString());
		Json->SetStringField(TEXT("guid"), AGE::GuidToString(Variable.VarGuid));
		Json->SetObjectField(TEXT("type"), FAGEGraphWriter::WritePinType(Variable.VarType));
		if (!Variable.Category.IsEmpty())
		{
			Json->SetStringField(TEXT("category"), Variable.Category.ToString());
		}
		if (!Variable.DefaultValue.IsEmpty())
		{
			Json->SetStringField(TEXT("default"), Variable.DefaultValue);
		}

		TArray<TSharedPtr<FJsonValue>> Flags;
		const EPropertyFlags PropertyFlags = static_cast<EPropertyFlags>(Variable.PropertyFlags);
		for (const FFlagName& FlagName : VariableFlagNames)
		{
			if (EnumHasAnyFlags(PropertyFlags, FlagName.Flag))
			{
				Flags.Add(MakeShared<FJsonValueString>(FlagName.Name));
			}
		}
		Json->SetArrayField(TEXT("flags"), Flags);

		if (!Variable.RepNotifyFunc.IsNone())
		{
			Json->SetStringField(TEXT("repNotify"), Variable.RepNotifyFunc.ToString());
		}
		if (Variable.MetaDataArray.Num() > 0)
		{
			TSharedRef<FJsonObject> MetaData = MakeShared<FJsonObject>();
			for (const FBPVariableMetaDataEntry& Entry : Variable.MetaDataArray)
			{
				MetaData->SetStringField(Entry.DataKey.ToString(), Entry.DataValue);
			}
			Json->SetObjectField(TEXT("metaData"), MetaData);
		}
		return Json;
	}

	TArray<TSharedPtr<FJsonValue>> WriteComponents(const USimpleConstructionScript& Script, FAGEExportContext& Context)
	{
		TMap<const USCS_Node*, const USCS_Node*> ParentOf;
		for (const USCS_Node* Node : Script.GetAllNodes())
		{
			if (!Node)
			{
				continue;
			}
			for (const USCS_Node* Child : Node->GetChildNodes())
			{
				ParentOf.Add(Child, Node);
			}
		}

		FAGEPropertyWriter PropertyWriter(Context);
		TArray<TSharedPtr<FJsonValue>> Components;
		for (const USCS_Node* Node : Script.GetAllNodes())
		{
			if (!Node)
			{
				continue;
			}
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			Json->SetStringField(TEXT("class"), FAGEPropertyWriter::ObjectPath(Node->ComponentClass));
			if (const USCS_Node* const* Parent = ParentOf.Find(Node))
			{
				Json->SetStringField(TEXT("parent"), (*Parent)->GetVariableName().ToString());
			}
			else if (!Node->ParentComponentOrVariableName.IsNone())
			{
				// Attached to a component declared in a parent class.
				Json->SetStringField(TEXT("parent"), Node->ParentComponentOrVariableName.ToString());
			}
			if (!Node->AttachToName.IsNone())
			{
				Json->SetStringField(TEXT("attachSocket"), Node->AttachToName.ToString());
			}
			if (Node->ComponentTemplate)
			{
				TSharedRef<FJsonObject> Template = PropertyWriter.WriteObjectProperties(*Node->ComponentTemplate);
				if (Template->Values.Num() > 0)
				{
					Json->SetObjectField(TEXT("properties"), Template);
				}
			}
			Components.Add(MakeShared<FJsonValueObject>(Json));
		}
		return Components;
	}
}

bool FAGEBlueprintHandler::CanHandle(const UObject& Asset) const
{
	return Asset.IsA<UBlueprint>();
}

bool FAGEBlueprintHandler::ShouldExportAssetProperty(const FProperty& Property) const
{
	// Written below in a cleaner form, or editor bookkeeping with no meaning for a reader.
	static const TSet<FName> Skipped =
	{
		TEXT("ParentClass"), TEXT("GeneratedClass"), TEXT("SkeletonGeneratedClass"),
		TEXT("UbergraphPages"), TEXT("FunctionGraphs"), TEXT("DelegateSignatureGraphs"), TEXT("MacroGraphs"),
		TEXT("IntermediateGeneratedGraphs"), TEXT("EventGraphs"),
		TEXT("NewVariables"), TEXT("SimpleConstructionScript"), TEXT("ImplementedInterfaces"),
		TEXT("LastEditedDocuments"), TEXT("CategorySorting"), TEXT("BlueprintGuid"),
	};
	return !Skipped.Contains(Property.GetFName());
}

void FAGEBlueprintHandler::Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const
{
	const UBlueprint& Blueprint = *CastChecked<UBlueprint>(&Asset);
	TSharedRef<FJsonObject> Section = MakeShared<FJsonObject>();

	FAGEPropertyWriter PropertyWriter(Context);
	if (TSharedPtr<FJsonValue> Type = PropertyWriter.WriteField(*UBlueprint::StaticClass(), &Blueprint, TEXT("BlueprintType"), &Blueprint))
	{
		Section->SetField(TEXT("type"), Type);
	}
	Section->SetStringField(TEXT("parentClass"), FAGEPropertyWriter::ObjectPath(Blueprint.ParentClass.Get()));
	for (const UClass* Class = Blueprint.ParentClass.Get(); Class; Class = Class->GetSuperClass())
	{
		if (Class->HasAnyClassFlags(CLASS_Native))
		{
			Section->SetStringField(TEXT("nativeParentClass"), Class->GetPathName());
			break;
		}
	}
	Section->SetStringField(TEXT("generatedClass"), FAGEPropertyWriter::ObjectPath(Blueprint.GeneratedClass.Get()));

	TArray<TSharedPtr<FJsonValue>> Interfaces;
	for (const FBPInterfaceDescription& Interface : Blueprint.ImplementedInterfaces)
	{
		Interfaces.Add(MakeShared<FJsonValueString>(FAGEPropertyWriter::ObjectPath(Interface.Interface.Get())));
	}
	Section->SetArrayField(TEXT("interfaces"), Interfaces);

	TArray<TSharedPtr<FJsonValue>> Variables;
	for (const FBPVariableDescription& Variable : Blueprint.NewVariables)
	{
		Variables.Add(MakeShared<FJsonValueObject>(WriteVariable(Variable)));
	}
	Section->SetArrayField(TEXT("variables"), Variables);

	if (Blueprint.SimpleConstructionScript)
	{
		Section->SetArrayField(TEXT("components"), WriteComponents(*Blueprint.SimpleConstructionScript, Context));
	}

	// Class Defaults panel: CDO values that differ from the parent class.
	if (const UClass* GeneratedClass = Blueprint.GeneratedClass.Get())
	{
		if (const UObject* DefaultObject = GeneratedClass->GetDefaultObject(false))
		{
			Section->SetObjectField(TEXT("classDefaults"), PropertyWriter.WriteObjectProperties(*DefaultObject));
		}
		else
		{
			Context.Warn(TEXT("Generated class has no default object; classDefaults skipped."));
		}
	}
	else
	{
		Context.Warn(TEXT("Blueprint has no generated class (not compiled?); classDefaults skipped."));
	}

	FAGEGraphWriter GraphWriter(Context, GetNodeDecorator());
	TArray<TSharedPtr<FJsonValue>> Graphs;
	auto AddGraphs = [&Graphs, &GraphWriter](const TArray<TObjectPtr<UEdGraph>>& Source, const TCHAR* Kind)
	{
		for (const TObjectPtr<UEdGraph>& Graph : Source)
		{
			if (Graph)
			{
				Graphs.Add(MakeShared<FJsonValueObject>(GraphWriter.WriteGraph(*Graph, Kind)));
			}
		}
	};
	AddGraphs(Blueprint.UbergraphPages, TEXT("Ubergraph"));
	AddGraphs(Blueprint.FunctionGraphs, TEXT("Function"));
	AddGraphs(Blueprint.MacroGraphs, TEXT("Macro"));
	AddGraphs(Blueprint.DelegateSignatureGraphs, TEXT("DelegateSignature"));
	Section->SetArrayField(TEXT("graphs"), Graphs);

	OutAssetJson.SetObjectField(TEXT("blueprint"), Section);
}

bool FAGEAnimBlueprintHandler::CanHandle(const UObject& Asset) const
{
	return Asset.IsA<UAnimBlueprint>();
}

namespace
{
	const TCHAR* BindingTypeToString(EAnimGraphNodePropertyBindingType Type)
	{
		switch (Type)
		{
		case EAnimGraphNodePropertyBindingType::Property: return TEXT("Property");
		case EAnimGraphNodePropertyBindingType::Function: return TEXT("Function");
		default: return TEXT("None");
		}
	}

	/**
	 * Reads a TMap<FName, FAnimGraphNodePropertyBinding> property by name.
	 * The map lives on UAnimGraphNodeBinding_Base, which is private to the AnimGraph module, so it is reached through reflection.
	 * Returns false when the property is missing or has an unexpected type.
	 */
	bool ReadBindingMap(const UObject& Owner, FName PropertyName, TArray<TSharedPtr<FJsonValue>>& OutBindings)
	{
		const FMapProperty* MapProperty = FindFProperty<FMapProperty>(Owner.GetClass(), PropertyName);
		if (!MapProperty || !MapProperty->GetKeyProperty()->IsA<FNameProperty>())
		{
			return false;
		}
		const FStructProperty* ValueProperty = CastField<FStructProperty>(MapProperty->GetValueProperty());
		// Compared by name: the struct's StaticStruct() is not exported from the AnimGraph module.
		if (!ValueProperty || ValueProperty->Struct->GetFName() != TEXT("AnimGraphNodePropertyBinding"))
		{
			return false;
		}

		FScriptMapHelper Helper(MapProperty, MapProperty->ContainerPtrToValuePtr<void>(&Owner));
		for (FScriptMapHelper::FIterator It(Helper); It; ++It)
		{
			const FName& PinProperty = *reinterpret_cast<const FName*>(Helper.GetKeyPtr(It));
			const FAnimGraphNodePropertyBinding& Binding = *reinterpret_cast<const FAnimGraphNodePropertyBinding*>(Helper.GetValuePtr(It));

			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("property"), PinProperty.ToString());
			Json->SetStringField(TEXT("type"), BindingTypeToString(Binding.Type));
			Json->SetBoolField(TEXT("bound"), Binding.bIsBound);
			Json->SetStringField(TEXT("path"), FString::Join(Binding.PropertyPath, TEXT(".")));
			if (!Binding.PathAsText.IsEmpty())
			{
				Json->SetStringField(TEXT("pathText"), Binding.PathAsText.ToString());
			}
			Json->SetObjectField(TEXT("pinType"), FAGEGraphWriter::WritePinType(Binding.PinType));
			if (Binding.bIsPromotion)
			{
				Json->SetObjectField(TEXT("promotedFrom"), FAGEGraphWriter::WritePinType(Binding.PromotedPinType));
			}
			if (Binding.ArrayIndex != INDEX_NONE)
			{
				Json->SetNumberField(TEXT("arrayIndex"), Binding.ArrayIndex);
			}
			if (!Binding.ContextId.IsNone())
			{
				Json->SetStringField(TEXT("context"), Binding.ContextId.ToString());
			}
			if (Binding.bOnlyUpdateWhenActive)
			{
				Json->SetBoolField(TEXT("onlyUpdateWhenActive"), true);
			}
			OutBindings.Add(MakeShared<FJsonValueObject>(Json));
		}
		return true;
	}
}

void FAGEAnimNodeDecorator::Decorate(const UEdGraphNode& Node, FAGEExportContext& Context, FJsonObject& NodeJson) const
{
	const UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(&Node);
	if (!AnimNode)
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Bindings;
	// Current storage: an instanced binding object on the node.
	if (const FObjectProperty* BindingProperty = FindFProperty<FObjectProperty>(UAnimGraphNode_Base::StaticClass(), TEXT("Binding")))
	{
		if (const UObject* BindingObject = BindingProperty->GetObjectPropertyValue_InContainer(AnimNode))
		{
			if (!ReadBindingMap(*BindingObject, TEXT("PropertyBindings"), Bindings))
			{
				// A custom binding implementation: fall back to plain reflection.
				NodeJson.SetObjectField(TEXT("binding"), FAGEPropertyWriter(Context).WriteOwnedObject(*BindingObject));
			}
		}
	}
	// Assets saved before bindings moved off the node still carry the old map.
	ReadBindingMap(*AnimNode, TEXT("PropertyBindings_DEPRECATED"), Bindings);

	if (Bindings.Num() > 0)
	{
		NodeJson.SetArrayField(TEXT("animBindings"), Bindings);
	}
}

bool FAGEAnimNodeDecorator::SkipProperty(const UEdGraphNode& Node, const FProperty& Property) const
{
	// Bindings are written as "animBindings"; ShowPinForProperties repeats what the pin list already shows.
	static const TSet<FName> Skipped = { TEXT("Binding"), TEXT("PropertyBindings_DEPRECATED"), TEXT("ShowPinForProperties") };
	return Node.IsA<UAnimGraphNode_Base>() && Skipped.Contains(Property.GetFName());
}
