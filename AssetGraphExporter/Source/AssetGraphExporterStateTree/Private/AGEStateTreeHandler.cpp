// Asset Graph Exporter

#include "AGEStateTreeHandler.h"
#include "AGEExportContext.h"
#include "AGEPropertyWriter.h"
#include "PropertyBindingBindableStructDescriptor.h"
#include "PropertyBindingBinding.h"
#include "PropertyBindingPath.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeSchema.h"
#include "StateTreeState.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"

const FName FAGEStateTreeHandler::HandlerName(TEXT("StateTree"));

namespace
{
	/**
	 * Struct types are taken from the reflected properties that hold them, because
	 * FStateTreeTransition::StaticStruct() and friends are not exported from the StateTree modules.
	 */
	const UScriptStruct* StructOfProperty(const UStruct& Owner, FName PropertyName)
	{
		const FProperty* Property = FindFProperty<FProperty>(&Owner, PropertyName);
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			Property = ArrayProperty->Inner;
		}
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		return StructProperty ? StructProperty->Struct.Get() : nullptr;
	}

	/**
	 * Path segments for a list of siblings: names that appear more than once get an occurrence index
	 * ("Float Compare[0]", "Float Compare[1]"); unique names are left as they are.
	 */
	TArray<FString> DisambiguateNames(const TArray<FString>& Names)
	{
		TMap<FString, int32> Counts;
		for (const FString& Name : Names)
		{
			++Counts.FindOrAdd(Name);
		}
		TMap<FString, int32> Seen;
		TArray<FString> Segments;
		for (const FString& Name : Names)
		{
			Segments.Add(Counts[Name] > 1 ? FString::Printf(TEXT("%s[%d]"), *Name, Seen.FindOrAdd(Name)++) : Name);
		}
		return Segments;
	}

	FAGEPropertyFilter ExcludeNames(TSet<FName> Names)
	{
		return [Names = MoveTemp(Names)](const FProperty& Property)
		{
			return !Names.Contains(Property.GetFName());
		};
	}

	class FStateTreeWriter
	{
	public:
		FStateTreeWriter(const UStateTreeEditorData& InEditorData, FAGEExportContext& InContext)
			: EditorData(InEditorData)
			, Context(InContext)
			, Writer(InContext)
		{
			StateClass = UStateTreeState::StaticClass();
			EditorNodeStruct = StructOfProperty(*StateClass, TEXT("Tasks"));
			TransitionStruct = StructOfProperty(*StateClass, TEXT("Transitions"));
			StateLinkStruct = TransitionStruct ? StructOfProperty(*TransitionStruct, TEXT("State")) : nullptr;

			// Context data declared by the schema (Actor, AIController, ...) can be a binding source.
			if (EditorData.Schema)
			{
				for (const FStateTreeExternalDataDesc& Desc : EditorData.Schema->GetContextDataDescs())
				{
					ContextNames.Add(Desc.ID, Desc.Name.ToString());
				}
			}
		}

		TSharedRef<FJsonObject> Write()
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			if (!EditorNodeStruct || !TransitionStruct || !StateLinkStruct)
			{
				Context.Warn(TEXT("StateTree reflection layout not recognized; this engine version may need handler updates."));
				return Json;
			}

			// Pass 1: state paths, so transitions can name states that appear later in the tree.
			IndexStates(EditorData.SubTrees, FString());

			if (EditorData.Schema)
			{
				Json->SetObjectField(TEXT("schema"), Writer.WriteOwnedObject(*EditorData.Schema));
			}

			IdToPath.Add(EditorData.GetRootParametersGuid(), TEXT("Parameters"));
			TSharedRef<FJsonObject> Parameters = MakeShared<FJsonObject>();
			Parameters->SetStringField(TEXT("id"), AGE::GuidToString(EditorData.GetRootParametersGuid()));
			Parameters->SetObjectField(TEXT("values"), WriteBag(EditorData.GetRootParametersPropertyBag()));
			Json->SetObjectField(TEXT("parameters"), Parameters);

			Json->SetArrayField(TEXT("evaluators"), WriteNodes(EditorData.Evaluators, TEXT("Evaluators"), false));
			Json->SetArrayField(TEXT("globalTasks"), WriteNodes(EditorData.GlobalTasks, TEXT("GlobalTasks"), false));
			SetField(*Json, TEXT("globalTasksCompletion"), Writer.WriteField(*UStateTreeEditorData::StaticClass(), &EditorData, TEXT("GlobalTasksCompletion"), &EditorData));

			TArray<TSharedPtr<FJsonValue>> SubTrees;
			for (const TObjectPtr<UStateTreeState>& State : EditorData.SubTrees)
			{
				if (State)
				{
					SubTrees.Add(MakeShared<FJsonValueObject>(WriteState(*State)));
				}
			}
			Json->SetArrayField(TEXT("states"), SubTrees);

			// Last: every node, state and parameter ID is known by now.
			Json->SetArrayField(TEXT("bindings"), WriteBindings());
			return Json;
		}

	private:
		static void SetField(FJsonObject& Json, const TCHAR* Key, const TSharedPtr<FJsonValue>& Value)
		{
			if (Value.IsValid())
			{
				Json.SetField(Key, Value);
			}
		}

		void IndexStates(const TArray<TObjectPtr<UStateTreeState>>& Siblings, const FString& ParentPath)
		{
			TArray<const UStateTreeState*> States;
			TArray<FString> Names;
			for (const TObjectPtr<UStateTreeState>& State : Siblings)
			{
				if (State)
				{
					States.Add(State.Get());
					Names.Add(State->Name.ToString());
				}
			}
			const TArray<FString> Segments = DisambiguateNames(Names);
			for (int32 Index = 0; Index < States.Num(); ++Index)
			{
				const FString Path = ParentPath.IsEmpty() ? Segments[Index] : ParentPath / Segments[Index];
				IdToPath.Add(States[Index]->ID, Path);
				IndexStates(States[Index]->Children, Path);
			}
		}

		FString PathOf(const FGuid& ID) const
		{
			const FString* Path = IdToPath.Find(ID);
			return Path ? *Path : FString();
		}

		TSharedRef<FJsonObject> WriteBag(const FInstancedPropertyBag& Bag)
		{
			const FConstStructView Value = Bag.GetValue();
			if (!Value.IsValid())
			{
				return MakeShared<FJsonObject>();
			}
			// Parameter values are user data: always written in full.
			return Writer.WriteStruct(*Value.GetScriptStruct(), Value.GetMemory(), nullptr, &EditorData);
		}

		TArray<TSharedPtr<FJsonValue>> WriteNodes(const TArray<FStateTreeEditorNode>& Nodes, const FString& OwnerPath, bool bIsCondition)
		{
			TArray<const FStateTreeEditorNode*> ValidNodes;
			TArray<FString> Names;
			for (const FStateTreeEditorNode& Node : Nodes)
			{
				if (Node.Node.IsValid())
				{
					ValidNodes.Add(&Node);
					Names.Add(Node.GetName().ToString());
				}
			}
			const TArray<FString> Segments = DisambiguateNames(Names);
			TArray<TSharedPtr<FJsonValue>> Result;
			for (int32 Index = 0; Index < ValidNodes.Num(); ++Index)
			{
				Result.Add(MakeShared<FJsonValueObject>(WriteNode(*ValidNodes[Index], OwnerPath / Segments[Index], bIsCondition)));
			}
			return Result;
		}

		/** Path is the node's full path, already made unique among its siblings. */
		TSharedRef<FJsonObject> WriteNode(const FStateTreeEditorNode& Node, const FString& Path, bool bIsCondition)
		{
			const FString Name = Node.GetName().ToString();
			IdToPath.Add(Node.ID, Path);
			if (Node.GetInstanceDataID() != Node.ID)
			{
				IdToPath.Add(Node.GetInstanceDataID(), Path);
			}

			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), AGE::GuidToString(Node.ID));
			Json->SetStringField(TEXT("name"), Name);
			Json->SetStringField(TEXT("type"), FAGEPropertyWriter::ObjectPath(Node.Node.GetScriptStruct()));
			// The same one-line summary the StateTree editor shows for the node.
			const FString Description = EditorData.GetNodeDescription(Node).ToString();
			if (!Description.IsEmpty())
			{
				Json->SetStringField(TEXT("description"), Description);
			}
			if (bIsCondition)
			{
				SetField(*Json, TEXT("operand"), Writer.WriteField(*EditorNodeStruct, &Node, TEXT("ExpressionOperand"), &EditorData));
				Json->SetNumberField(TEXT("indent"), Node.ExpressionIndent);
			}

			Json->SetObjectField(TEXT("node"), Writer.WriteStructAgainstDefaults(*Node.Node.GetScriptStruct(), Node.Node.GetMemory(), &EditorData));
			if (Node.Instance.IsValid())
			{
				Json->SetObjectField(TEXT("instance"), Writer.WriteStructAgainstDefaults(*Node.Instance.GetScriptStruct(), Node.Instance.GetMemory(), &EditorData));
			}
			if (Node.InstanceObject)
			{
				// Blueprint tasks/conditions/evaluators keep their values in an object instead of a struct.
				Json->SetObjectField(TEXT("instanceObject"), Writer.WriteOwnedObject(*Node.InstanceObject));
			}
			return Json;
		}

		TSharedRef<FJsonObject> WriteStateLink(const FStateTreeStateLink& Link)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			SetField(*Json, TEXT("type"), Writer.WriteField(*StateLinkStruct, &Link, TEXT("LinkType"), &EditorData));
			if (Link.ID.IsValid())
			{
				Json->SetStringField(TEXT("state"), PathOf(Link.ID));
				Json->SetStringField(TEXT("id"), AGE::GuidToString(Link.ID));
			}
			return Json;
		}

		TSharedRef<FJsonObject> WriteTransition(const FStateTreeTransition& Transition, const FString& Path)
		{
			IdToPath.Add(Transition.ID, Path);

			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), AGE::GuidToString(Transition.ID));
			Json->SetBoolField(TEXT("enabled"), Transition.bTransitionEnabled);
			SetField(*Json, TEXT("trigger"), Writer.WriteField(*TransitionStruct, &Transition, TEXT("Trigger"), &EditorData));
			Json->SetObjectField(TEXT("target"), WriteStateLink(Transition.State));
			SetField(*Json, TEXT("priority"), Writer.WriteField(*TransitionStruct, &Transition, TEXT("Priority"), &EditorData));
			if (Transition.bDelayTransition)
			{
				TSharedRef<FJsonObject> Delay = MakeShared<FJsonObject>();
				Delay->SetNumberField(TEXT("duration"), Transition.DelayDuration);
				Delay->SetNumberField(TEXT("randomVariance"), Transition.DelayRandomVariance);
				Json->SetObjectField(TEXT("delay"), Delay);
			}
			Json->SetArrayField(TEXT("conditions"), WriteNodes(Transition.Conditions, Path / TEXT("Conditions"), true));

			// Everything else that differs from a new transition (required event, delegate listener, reactivation rule, ...).
			TSharedRef<FJsonObject> Other = Writer.WriteStructAgainstDefaults(*TransitionStruct, &Transition, &EditorData, ExcludeNames(
			{
				TEXT("ID"), TEXT("bTransitionEnabled"), TEXT("Trigger"), TEXT("State"), TEXT("Priority"),
				TEXT("bDelayTransition"), TEXT("DelayDuration"), TEXT("DelayRandomVariance"), TEXT("Conditions"),
			}));
			if (Other->Values.Num() > 0)
			{
				Json->SetObjectField(TEXT("properties"), Other);
			}
			return Json;
		}

		TSharedRef<FJsonObject> WriteState(const UStateTreeState& State)
		{
			const FString Path = PathOf(State.ID);

			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), AGE::GuidToString(State.ID));
			Json->SetStringField(TEXT("name"), State.Name.ToString());
			Json->SetStringField(TEXT("path"), Path);
			Json->SetBoolField(TEXT("enabled"), State.bEnabled);
			SetField(*Json, TEXT("type"), Writer.WriteField(*StateClass, &State, TEXT("Type"), &EditorData));
			SetField(*Json, TEXT("selectionBehavior"), Writer.WriteField(*StateClass, &State, TEXT("SelectionBehavior"), &EditorData));
			SetField(*Json, TEXT("tasksCompletion"), Writer.WriteField(*StateClass, &State, TEXT("TasksCompletion"), &EditorData));
			if (State.Type == EStateTreeStateType::Linked)
			{
				Json->SetObjectField(TEXT("linkedSubtree"), WriteStateLink(State.LinkedSubtree));
			}
			if (State.Type == EStateTreeStateType::LinkedAsset)
			{
				Json->SetStringField(TEXT("linkedAsset"), FAGEPropertyWriter::ObjectPath(State.LinkedAsset.Get()));
			}

			IdToPath.Add(State.Parameters.ID, Path / TEXT("Parameters"));
			if (State.Parameters.Parameters.GetValue().IsValid())
			{
				TSharedRef<FJsonObject> Parameters = MakeShared<FJsonObject>();
				Parameters->SetStringField(TEXT("id"), AGE::GuidToString(State.Parameters.ID));
				Parameters->SetObjectField(TEXT("values"), WriteBag(State.Parameters.Parameters));
				Json->SetObjectField(TEXT("parameters"), Parameters);
			}

			Json->SetArrayField(TEXT("enterConditions"), WriteNodes(State.EnterConditions, Path / TEXT("EnterConditions"), true));
			if (State.Considerations.Num() > 0)
			{
				Json->SetArrayField(TEXT("considerations"), WriteNodes(State.Considerations, Path / TEXT("Considerations"), true));
			}
			Json->SetArrayField(TEXT("tasks"), WriteNodes(State.Tasks, Path / TEXT("Tasks"), false));
			if (State.SingleTask.Node.IsValid())
			{
				// Used instead of Tasks when the schema allows only one task per state.
				Json->SetObjectField(TEXT("singleTask"), WriteNode(State.SingleTask, Path / TEXT("SingleTask") / State.SingleTask.GetName().ToString(), false));
			}

			TArray<TSharedPtr<FJsonValue>> Transitions;
			for (int32 Index = 0; Index < State.Transitions.Num(); ++Index)
			{
				const FString TransitionPath = FString::Printf(TEXT("%s/Transitions[%d]"), *Path, Index);
				Transitions.Add(MakeShared<FJsonValueObject>(WriteTransition(State.Transitions[Index], TransitionPath)));
			}
			Json->SetArrayField(TEXT("transitions"), Transitions);

			// Remaining state settings that differ from a new state (tag, description, required event, weight, tick rate, ...).
			TSharedRef<FJsonObject> Other = Writer.WriteObjectProperties(State, ExcludeNames(
			{
				TEXT("ID"), TEXT("Name"), TEXT("bEnabled"), TEXT("Type"), TEXT("SelectionBehavior"), TEXT("TasksCompletion"),
				TEXT("LinkedSubtree"), TEXT("LinkedAsset"), TEXT("Parameters"), TEXT("EnterConditions"), TEXT("Considerations"),
				TEXT("Tasks"), TEXT("SingleTask"), TEXT("Transitions"), TEXT("Children"), TEXT("Parent"), TEXT("bExpanded"),
			}));
			if (Other->Values.Num() > 0)
			{
				Json->SetObjectField(TEXT("properties"), Other);
			}

			TArray<TSharedPtr<FJsonValue>> Children;
			for (const TObjectPtr<UStateTreeState>& Child : State.Children)
			{
				if (Child)
				{
					Children.Add(MakeShared<FJsonValueObject>(WriteState(*Child)));
				}
			}
			Json->SetArrayField(TEXT("children"), Children);
			return Json;
		}

		TSharedRef<FJsonObject> WriteBindingEnd(const FPropertyBindingPath& BindingPath)
		{
			const FGuid& ID = BindingPath.GetStructID();
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("id"), AGE::GuidToString(ID));

			FString Owner = PathOf(ID);
			if (Owner.IsEmpty())
			{
				if (const FString* ContextName = ContextNames.Find(ID))
				{
					Owner = TEXT("Context:") + *ContextName;
				}
			}
			if (Owner.IsEmpty())
			{
				// Some other bindable struct outside the tree (e.g. a property function): the engine's display name.
				TInstancedStruct<FPropertyBindingBindableStructDescriptor> Descriptor;
				if (EditorData.GetBindableStructByID(ID, Descriptor) && Descriptor.GetMemory())
				{
					Owner = reinterpret_cast<const FPropertyBindingBindableStructDescriptor*>(Descriptor.GetMemory())->Name.ToString();
				}
			}
			if (!Owner.IsEmpty())
			{
				Json->SetStringField(TEXT("owner"), Owner);
			}
			Json->SetStringField(TEXT("path"), BindingPath.ToString());
			return Json;
		}

		TArray<TSharedPtr<FJsonValue>> WriteBindings()
		{
			TArray<TSharedPtr<FJsonValue>> Bindings;
			const FStateTreeEditorPropertyBindings* EditorBindings = EditorData.GetPropertyEditorBindings();
			if (!EditorBindings)
			{
				return Bindings;
			}
			EditorBindings->ForEachBinding([this, &Bindings](const FPropertyBindingBinding& Binding)
			{
				TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
				Json->SetObjectField(TEXT("source"), WriteBindingEnd(Binding.GetSourcePath()));
				Json->SetObjectField(TEXT("target"), WriteBindingEnd(Binding.GetTargetPath()));
				// Property functions compute the value instead of copying it.
				const FConstStructView Function = Binding.GetPropertyFunctionNode();
				if (Function.IsValid())
				{
					Json->SetField(TEXT("function"), Writer.WriteInstancedStruct(Function.GetScriptStruct(), Function.GetMemory(), &EditorData));
				}
				Bindings.Add(MakeShared<FJsonValueObject>(Json));
			});
			return Bindings;
		}

		const UStateTreeEditorData& EditorData;
		FAGEExportContext& Context;
		FAGEPropertyWriter Writer;
		const UClass* StateClass = nullptr;
		const UScriptStruct* EditorNodeStruct = nullptr;
		const UScriptStruct* TransitionStruct = nullptr;
		const UScriptStruct* StateLinkStruct = nullptr;
		TMap<FGuid, FString> IdToPath;
		TMap<FGuid, FString> ContextNames;
	};
}

bool FAGEStateTreeHandler::CanHandle(const UObject& Asset) const
{
	return Asset.IsA<UStateTree>();
}

void FAGEStateTreeHandler::Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const
{
	const UStateTree& StateTree = *CastChecked<UStateTree>(&Asset);
	const UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree.EditorData);
	if (!EditorData)
	{
		Context.Warn(TEXT("StateTree has no editor data (cooked asset?); nothing to export."));
		return;
	}
	OutAssetJson.SetObjectField(TEXT("stateTree"), FStateTreeWriter(*EditorData, Context).Write());
}
