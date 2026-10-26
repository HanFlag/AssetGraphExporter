// Asset Graph Exporter

#include "AGENiagaraSystemHandler.h"
#include "AGEExportContext.h"
#include "AGEGraphWriter.h"
#include "AGEPropertyWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphPin.h"
#include "NiagaraCommon.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterBase.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterBinding.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

const FName FAGENiagaraSystemHandler::HandlerName(TEXT("NiagaraSystem"));

// Named (not anonymous) so these helpers cannot clash with other files in a unity build.
namespace AGE::NiagaraDetail
{
	using FJsonArray = TArray<TSharedPtr<FJsonValue>>;

	/** Dynamic inputs nested deeper than this are cut (also guards against graphs that loop). */
	constexpr int32 MaxDynamicInputDepth = 16;
	/** Longest parameter map chain followed back from an output node. */
	constexpr int32 MaxChainLength = 4096;
	/** Nested structs inside one value are expanded down to this depth. */
	constexpr int32 MaxValueDepth = 4;

	FAGEPropertyFilter ExcludeNames(TSet<FName> Names)
	{
		return [Names = MoveTemp(Names)](const FProperty& Property)
		{
			return !Names.Contains(Property.GetFName());
		};
	}

	/**
	 * Struct types are taken from the reflected properties that hold them, because
	 * FVersionedNiagaraEmitterData::StaticStruct() and friends are not exported from the Niagara module.
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

	/** For private members and node classes whose functions are not exported from NiagaraEditor. */
	const UObject* ReadObjectProperty(const UObject& Owner, FName PropertyName)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Owner.GetClass(), PropertyName);
		return Property ? Property->GetObjectPropertyValue_InContainer(&Owner) : nullptr;
	}

	FString ReadStringProperty(const UObject& Owner, FName PropertyName)
	{
		const FStrProperty* Property = FindFProperty<FStrProperty>(Owner.GetClass(), PropertyName);
		return Property ? Property->GetPropertyValue_InContainer(&Owner) : FString();
	}

	/** Node classes that are private to NiagaraEditor (e.g. the parameter map get/set nodes) are recognized by name. */
	bool IsNodeOfClass(const UEdGraphNode& Node, FName ClassName)
	{
		for (const UClass* Class = Node.GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == ClassName)
			{
				return true;
			}
		}
		return false;
	}

	bool IsParameterMapSet(const UEdGraphNode& Node)
	{
		static const FName ClassName(TEXT("NiagaraNodeParameterMapSet"));
		return IsNodeOfClass(Node, ClassName);
	}

	bool IsParameterMapGet(const UEdGraphNode& Node)
	{
		static const FName ClassName(TEXT("NiagaraNodeParameterMapGet"));
		return IsNodeOfClass(Node, ClassName);
	}

	bool IsParameterMapPin(const UEdGraphPin& Pin)
	{
		return Pin.PinType.PinSubCategoryObject.Get() == FNiagaraTypeDefinition::GetParameterMapStruct();
	}

	const UEdGraphPin* ParameterMapPin(const UEdGraphNode& Node, EEdGraphPinDirection Direction)
	{
		for (const UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin && Pin->Direction == Direction && IsParameterMapPin(*Pin))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** In a stack every node's parameter map input is fed by exactly one node; this returns that node. */
	const UEdGraphNode* PreviousInStack(const UEdGraphNode& Node)
	{
		const UEdGraphPin* Input = ParameterMapPin(Node, EGPD_Input);
		return Input && Input->LinkedTo.Num() == 1 && Input->LinkedTo[0] ? Input->LinkedTo[0]->GetOwningNode() : nullptr;
	}

	int32 CountValueOutputs(const UEdGraphNode& Node)
	{
		int32 Count = 0;
		for (const UEdGraphPin* Pin : Node.Pins)
		{
			Count += Pin && Pin->Direction == EGPD_Output && !IsParameterMapPin(*Pin) ? 1 : 0;
		}
		return Count;
	}

	const UNiagaraGraph* GraphOf(const UNiagaraScriptSourceBase* Source)
	{
		const UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(Source);
		return ScriptSource ? ScriptSource->NodeGraph.Get() : nullptr;
	}

	// ---- Values ----

	/** "NiagaraFloat" -> "float", "Vector3f" stays, "NiagaraDataInterfaceGrid3DCollection" -> "Grid3DCollection". */
	FString ObjectTypeLabel(const UObject* TypeObject)
	{
		if (!TypeObject)
		{
			return TEXT("?");
		}
		FString Name = TypeObject->GetName();
		if (Name == TEXT("NiagaraFloat"))
		{
			return TEXT("float");
		}
		if (Name == TEXT("NiagaraInt32"))
		{
			return TEXT("int");
		}
		if (Name == TEXT("NiagaraBool"))
		{
			return TEXT("bool");
		}
		Name.RemoveFromStart(TEXT("NiagaraDataInterface"));
		Name.RemoveFromStart(TEXT("Niagara"));
		return Name;
	}

	FString TypeLabel(const FNiagaraTypeDefinition& Type)
	{
		if (!Type.IsValid())
		{
			return TEXT("?");
		}
		return Type.IsEnum() ? Type.GetEnum()->GetName() : ObjectTypeLabel(Type.GetStruct());
	}

	FString FormatStruct(const UStruct& Struct, const uint8* Data, int32 Depth);

	FString FormatProperty(const FProperty& Property, const uint8* Value, int32 Depth)
	{
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(&Property))
		{
			return BoolProperty->GetPropertyValue(Value) ? TEXT("true") : TEXT("false");
		}
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(&Property))
		{
			const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
			const UEnum* Enum = EnumProperty->GetEnum();
			return Enum ? Enum->GetNameStringByValue(EnumValue) : LexToString(EnumValue);
		}
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(&Property))
		{
			if (const UEnum* Enum = NumericProperty->GetIntPropertyEnum())
			{
				return Enum->GetNameStringByValue(NumericProperty->GetSignedIntPropertyValue(Value));
			}
			if (NumericProperty->IsFloatingPoint())
			{
				return FString::SanitizeFloat(NumericProperty->GetFloatingPointPropertyValue(Value), 0);
			}
			return LexToString(NumericProperty->GetSignedIntPropertyValue(Value));
		}
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			return Depth < MaxValueDepth ? FormatStruct(*StructProperty->Struct, Value, Depth + 1) : FString(TEXT("(...)"));
		}
		FString Text;
		Property.ExportTextItem_Direct(Text, Value, nullptr, nullptr, PPF_None);
		return Text;
	}

	bool IsPlainNumber(const FProperty& Property)
	{
		const FNumericProperty* NumericProperty = CastField<FNumericProperty>(&Property);
		return NumericProperty && !NumericProperty->GetIntPropertyEnum();
	}

	/** (1,2,3) for all-number structs such as vectors and colors, (A=1, B=x) otherwise. A one-field struct is just its value. */
	FString FormatStruct(const UStruct& Struct, const uint8* Data, int32 Depth)
	{
		TArray<const FProperty*> Fields;
		for (TFieldIterator<FProperty> It(&Struct); It; ++It)
		{
			Fields.Add(*It);
		}
		if (Fields.Num() == 1 && Fields[0]->ArrayDim == 1)
		{
			return FormatProperty(*Fields[0], Fields[0]->ContainerPtrToValuePtr<uint8>(Data), Depth);
		}

		bool bAllNumbers = Fields.Num() > 0;
		for (const FProperty* Field : Fields)
		{
			bAllNumbers = bAllNumbers && IsPlainNumber(*Field);
		}
		TArray<FString> Parts;
		for (const FProperty* Field : Fields)
		{
			for (int32 Index = 0; Index < Field->ArrayDim; ++Index)
			{
				const FString Value = FormatProperty(*Field, Field->ContainerPtrToValuePtr<uint8>(Data, Index), Depth);
				if (bAllNumbers)
				{
					Parts.Add(Value);
				}
				else
				{
					const FString Name = Field->ArrayDim > 1 ? FString::Printf(TEXT("%s[%d]"), *Field->GetName(), Index) : Field->GetName();
					Parts.Add(Name + TEXT("=") + Value);
				}
			}
		}
		return TEXT("(") + FString::Join(Parts, bAllNumbers ? TEXT(",") : TEXT(", ")) + TEXT(")");
	}

	/**
	 * An enum value's display name when it says more than the value name. Niagara's own enums are user-defined enums
	 * whose value names are NewEnumerator0, NewEnumerator1, ... and whose display names are what the editor shows.
	 */
	void AddEnumDisplayName(FJsonObject& Entry, const UEnum& Enum, int64 Value)
	{
		const int32 Index = Enum.GetIndexByValue(Value);
		if (Index == INDEX_NONE)
		{
			return;
		}
		const FString DisplayName = Enum.GetDisplayNameTextByIndex(Index).ToString();
		if (!DisplayName.IsEmpty() && DisplayName != Enum.GetNameStringByIndex(Index))
		{
			Entry.SetStringField(TEXT("displayName"), DisplayName);
		}
	}

	/** A value stored in a Niagara parameter store, e.g. "96", "true", "(200,200,350)". */
	FString FormatStoredValue(const FNiagaraParameterStore& Store, const FNiagaraVariableWithOffset& Variable)
	{
		const FNiagaraTypeDefinition& Type = Variable.GetType();
		const uint8* Data = Variable.Offset != INDEX_NONE ? Store.GetParameterData(Variable) : nullptr;
		if (!Data || !Type.IsValid())
		{
			return TEXT("?");
		}
		// FNiagaraBool stores 0 / -1 in an int.
		if (Type.IsSameBaseDefinition(FNiagaraTypeDefinition::GetBoolDef()))
		{
			return *reinterpret_cast<const int32*>(Data) != 0 ? TEXT("true") : TEXT("false");
		}
		if (Type.IsEnum())
		{
			const int32 Value = *reinterpret_cast<const int32*>(Data);
			const FString Name = Type.GetEnum()->GetNameStringByValue(Value);
			return Name.IsEmpty() ? LexToString(Value) : Name;
		}
		const UStruct* Struct = Type.GetStruct();
		if (!Struct || Type.IsUObject())
		{
			return TEXT("?");
		}
		// Structs with large world coordinate members (e.g. FVector) are stored in a float layout;
		// the store keeps a converter back to the struct's own layout (see FNiagaraParameterStore::CopyParameterData).
		if (Variable.StructConverter.IsValid())
		{
			TArray<uint8> Converted;
			Converted.SetNumZeroed(Struct->GetStructureSize());
			Variable.StructConverter.ConvertDataFromSimulation(Converted.GetData(), Data);
			return FormatStruct(*Struct, Converted.GetData(), 0);
		}
		return FormatStruct(*Struct, Data, 0);
	}

	/** "value", plus "displayName" for enums. */
	void WriteStoredValue(FJsonObject& Entry, const FNiagaraParameterStore& Store, const FNiagaraVariableWithOffset& Variable)
	{
		Entry.SetStringField(TEXT("value"), FormatStoredValue(Store, Variable));
		const FNiagaraTypeDefinition& Type = Variable.GetType();
		const uint8* Data = Variable.Offset != INDEX_NONE ? Store.GetParameterData(Variable) : nullptr;
		if (Data && Type.IsEnum() && Type.GetEnum())
		{
			AddEnumDisplayName(Entry, *Type.GetEnum(), *reinterpret_cast<const int32*>(Data));
		}
	}

	/**
	 * Node properties that change every time the asset is loaded, left out of the graphs so exports stay comparable.
	 * UNiagaraNodeFunctionCall::CachedChangeId is "adjusted every time that we compile this script" (NiagaraNodeFunctionCall.h).
	 */
	class FSkipVolatileNodeProperties : public IAGENodeDecorator
	{
	public:
		virtual void Decorate(const UEdGraphNode& Node, FAGEExportContext& Context, FJsonObject& NodeJson) const override {}

		virtual bool SkipProperty(const UEdGraphNode& Node, const FProperty& Property) const override
		{
			static const FName CachedChangeId(TEXT("CachedChangeId"));
			return Property.GetFName() == CachedChangeId;
		}
	};

	/** Reflection writes an FNiagaraVariableBase as {"Name": ..., "TypeDefHandle": {...}}; the name is what a reader needs. */
	TSharedPtr<FJsonValue> SimplifyVariables(const TSharedPtr<FJsonValue>& Value);

	void SimplifyVariablesIn(FJsonObject& Object)
	{
		for (auto& Pair : Object.Values)
		{
			Pair.Value = SimplifyVariables(Pair.Value);
		}
	}

	TSharedPtr<FJsonValue> SimplifyVariables(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return Value;
		}
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Value->TryGetObject(Object) && Object && Object->IsValid())
		{
			if ((*Object)->HasField(TEXT("TypeDefHandle")))
			{
				FString Name;
				(*Object)->TryGetStringField(TEXT("Name"), Name);
				return MakeShared<FJsonValueString>(Name.IsEmpty() ? FString(TEXT("None")) : Name);
			}
			SimplifyVariablesIn(**Object);
			return Value;
		}
		const FJsonArray* Array = nullptr;
		if (Value->TryGetArray(Array) && Array)
		{
			FJsonArray Items;
			for (const TSharedPtr<FJsonValue>& Item : *Array)
			{
				Items.Add(SimplifyVariables(Item));
			}
			return MakeShared<FJsonValueArray>(Items);
		}
		return Value;
	}

	// ---- Stacks ----

	/** Where one stack lives and where its constant input values are stored. */
	struct FStackSource
	{
		const UNiagaraGraph* Graph = nullptr;
		ENiagaraScriptUsage Usage = ENiagaraScriptUsage::Module;
		FGuid UsageId;
		/** Script whose RapidIterationParameters hold the constant input values of this stack. */
		const UNiagaraScript* Script = nullptr;
		/** Unique emitter name used in rapid iteration parameter names; empty for system stacks. */
		FString EmitterName;
		/** The emitter of an emitter stack (unset for system stacks); resolves static switches when listing a function's inputs. */
		FVersionedNiagaraEmitter Emitter;
	};

	class FSystemWriter
	{
	public:
		FSystemWriter(const UNiagaraSystem& InSystem, FAGEExportContext& InContext)
			: System(InSystem)
			, Context(InContext)
			, Writer(InContext)
			, GraphWriter(InContext, &NodeFilter)
		{
			EmitterDataStruct = StructOfProperty(*UNiagaraEmitter::StaticClass(), TEXT("VersionData"));
			EventStruct = EmitterDataStruct ? StructOfProperty(*EmitterDataStruct, TEXT("EventHandlerScriptProps")) : nullptr;
		}

		TSharedRef<FJsonObject> Write()
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetArrayField(TEXT("userParameters"), WriteUserParameters());
			Json->SetObjectField(TEXT("system"), WriteSystemStacks());

			FJsonArray Emitters;
			for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
			{
				Emitters.Add(MakeShared<FJsonValueObject>(WriteEmitter(Handle)));
			}
			Json->SetArrayField(TEXT("emitters"), Emitters);

			// Last, so data interfaces already written inline above appear in the graphs as paths only.
			Json->SetArrayField(TEXT("graphs"), WriteGraphs());
			return Json;
		}

	private:
		struct FGraphEntry
		{
			const UNiagaraGraph* Graph = nullptr;
			FString Label;
			FString Kind;
		};

		TSharedRef<FJsonObject> Owned(const UObject& Object, const FAGEPropertyFilter& Filter = nullptr)
		{
			TSharedRef<FJsonObject> Json = Writer.WriteOwnedObject(Object, Filter);
			SimplifyVariablesIn(*Json);
			return Json;
		}

		void AddGraph(const UNiagaraGraph* Graph, const FString& Label, const TCHAR* Kind)
		{
			if (Graph && !Graphs.ContainsByPredicate([Graph](const FGraphEntry& Entry) { return Entry.Graph == Graph; }))
			{
				Graphs.Add({ Graph, Label, Kind });
			}
		}

		FJsonArray WriteGraphs()
		{
			FJsonArray Json;
			for (const FGraphEntry& Entry : Graphs)
			{
				TSharedRef<FJsonObject> GraphJson = GraphWriter.WriteGraph(*Entry.Graph, Entry.Kind);
				// Graph objects are named NiagaraGraph_N; the label (system, emitter or scratch pad name) is what select needs.
				GraphJson->SetStringField(TEXT("object"), Entry.Graph->GetName());
				GraphJson->SetStringField(TEXT("name"), Entry.Label);
				Json.Add(MakeShared<FJsonValueObject>(GraphJson));
			}
			return Json;
		}

		FJsonArray WriteUserParameters()
		{
			FJsonArray Json;
			const FNiagaraUserRedirectionParameterStore& Store = System.GetExposedParameters();
			for (const FNiagaraVariableWithOffset& Variable : Store.ReadParameterVariables())
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Variable.GetName().ToString());
				Entry->SetStringField(TEXT("type"), TypeLabel(Variable.GetType()));
				if (Variable.IsDataInterface())
				{
					if (const UNiagaraDataInterface* DataInterface = Store.GetDataInterface(Variable.Offset))
					{
						Entry->SetObjectField(TEXT("dataInterface"), Owned(*DataInterface));
					}
				}
				else if (Variable.IsUObject())
				{
					Entry->SetStringField(TEXT("object"), FAGEPropertyWriter::ObjectPath(Store.GetUObject(Variable.Offset).Get()));
				}
				else
				{
					WriteStoredValue(*Entry, Store, Variable);
				}
				Json.Add(MakeShared<FJsonValueObject>(Entry));
			}
			return Json;
		}

		TSharedRef<FJsonObject> WriteSystemStacks()
		{
			const UNiagaraScript* SpawnScript = System.GetSystemSpawnScript();
			const UNiagaraScript* UpdateScript = System.GetSystemUpdateScript();

			FStackSource Source;
			Source.Graph = SpawnScript ? GraphOf(SpawnScript->GetLatestSource()) : nullptr;
			AddGraph(Source.Graph, TEXT("System"), TEXT("NiagaraSystem"));

			FJsonArray Stacks;
			Source.Usage = ENiagaraScriptUsage::SystemSpawnScript;
			Source.Script = SpawnScript;
			Stacks.Add(MakeShared<FJsonValueObject>(WriteStack(Source, TEXT("SystemSpawn"))));
			Source.Usage = ENiagaraScriptUsage::SystemUpdateScript;
			Source.Script = UpdateScript;
			Stacks.Add(MakeShared<FJsonValueObject>(WriteStack(Source, TEXT("SystemUpdate"))));

			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetArrayField(TEXT("stacks"), Stacks);
			return Json;
		}

		const FNiagaraEmitterHandle* FindHandle(const FGuid& Id) const
		{
			return System.GetEmitterHandles().FindByPredicate([&Id](const FNiagaraEmitterHandle& Handle) { return Handle.GetId() == Id; });
		}

		TSharedRef<FJsonObject> WriteEmitter(const FNiagaraEmitterHandle& Handle)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			const FString Name = Handle.GetName().ToString();
			Json->SetStringField(TEXT("name"), Name);
			Json->SetStringField(TEXT("id"), AGE::GuidToString(Handle.GetId()));
			if (!Handle.GetIsEnabled())
			{
				Json->SetBoolField(TEXT("enabled"), false);
			}

			// Lightweight emitters have no graph or stacks; their modules are plain reflected objects.
			if (Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless)
			{
				Json->SetStringField(TEXT("mode"), TEXT("Stateless"));
				if (const UNiagaraEmitterBase* EmitterBase = Handle.GetEmitterBase())
				{
					Json->SetObjectField(TEXT("properties"), Owned(*EmitterBase));
				}
				return Json;
			}

			const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
			const FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			if (!Instance.Emitter || !Data)
			{
				Json->SetStringField(TEXT("error"), TEXT("The emitter or its data could not be loaded."));
				Context.Warn(FString::Printf(TEXT("Niagara emitter %s: the emitter or its data could not be loaded."), *Name));
				return Json;
			}
			const UNiagaraEmitter& Emitter = *Instance.Emitter;

			Json->SetStringField(TEXT("simTarget"), Data->SimTarget == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPU") : TEXT("CPU"));
			const FVersionedNiagaraEmitter Parent = Data->GetParent();
			if (Parent.Emitter)
			{
				Json->SetStringField(TEXT("parent"), FAGEPropertyWriter::ObjectPath(Parent.Emitter.Get()));
			}
			if (EmitterDataStruct)
			{
				TSharedRef<FJsonObject> Settings = Writer.WriteStructAgainstDefaults(*EmitterDataStruct, Data, &Emitter, SettingsFilter());
				SimplifyVariablesIn(*Settings);
				Json->SetObjectField(TEXT("settings"), Settings);
			}

			FStackSource Base;
			Base.Graph = GraphOf(Data->GraphSource.Get());
			Base.EmitterName = Emitter.GetUniqueEmitterName();
			Base.Emitter = Instance;
			AddGraph(Base.Graph, Name, TEXT("NiagaraEmitter"));

			FJsonArray Stacks;
			const auto AddScriptStack = [this, &Stacks, &Base](ENiagaraScriptUsage Usage, const UNiagaraScript* Script, const TCHAR* Label)
			{
				FStackSource Source = Base;
				Source.Usage = Usage;
				Source.Script = Script;
				Stacks.Add(MakeShared<FJsonValueObject>(WriteStack(Source, Label)));
			};
			AddScriptStack(ENiagaraScriptUsage::EmitterSpawnScript, Data->EmitterSpawnScriptProps.Script.Get(), TEXT("EmitterSpawn"));
			AddScriptStack(ENiagaraScriptUsage::EmitterUpdateScript, Data->EmitterUpdateScriptProps.Script.Get(), TEXT("EmitterUpdate"));
			AddScriptStack(ENiagaraScriptUsage::ParticleSpawnScript, Data->SpawnScriptProps.Script.Get(), TEXT("ParticleSpawn"));
			AddScriptStack(ENiagaraScriptUsage::ParticleUpdateScript, Data->UpdateScriptProps.Script.Get(), TEXT("ParticleUpdate"));

			for (const FNiagaraEventScriptProperties& Event : Data->GetEventHandlers())
			{
				FStackSource Source = Base;
				Source.Usage = ENiagaraScriptUsage::ParticleEventScript;
				Source.Script = Event.Script.Get();
				Source.UsageId = Event.Script ? Event.Script->GetUsageId() : FGuid();
				TSharedRef<FJsonObject> Stack = MakeShared<FJsonObject>();
				Stack->SetStringField(TEXT("usage"), TEXT("Event"));
				Stack->SetStringField(TEXT("name"), Event.SourceEventName.ToString());
				if (const FNiagaraEmitterHandle* SourceHandle = FindHandle(Event.SourceEmitterID))
				{
					Stack->SetStringField(TEXT("sourceEmitter"), SourceHandle->GetName().ToString());
				}
				WriteModules(*Stack, Source);
				if (EventStruct)
				{
					TSharedRef<FJsonObject> Properties = Writer.WriteStructAgainstDefaults(*EventStruct, &Event, &Emitter, ExcludeNames({ TEXT("Script") }));
					SimplifyVariablesIn(*Properties);
					Stack->SetObjectField(TEXT("properties"), Properties);
				}
				Stacks.Add(MakeShared<FJsonValueObject>(Stack));
			}

			for (const UNiagaraSimulationStageBase* Stage : Data->GetSimulationStages())
			{
				if (!Stage)
				{
					continue;
				}
				FStackSource Source = Base;
				Source.Usage = ENiagaraScriptUsage::ParticleSimulationStageScript;
				Source.Script = Stage->Script.Get();
				Source.UsageId = Stage->Script ? Stage->Script->GetUsageId() : FGuid();
				TSharedRef<FJsonObject> Stack = MakeShared<FJsonObject>();
				Stack->SetStringField(TEXT("usage"), TEXT("SimulationStage"));
				Stack->SetStringField(TEXT("name"), Stage->SimulationStageName.ToString());
				if (!Stage->bEnabled)
				{
					Stack->SetBoolField(TEXT("enabled"), false);
				}
				WriteStageHeader(*Stack, *Stage);
				WriteModules(*Stack, Source);
				Stack->SetObjectField(TEXT("properties"), Owned(*Stage, ExcludeNames({ TEXT("Script"), TEXT("SimulationStageName"), TEXT("bEnabled") })));
				Stacks.Add(MakeShared<FJsonValueObject>(Stack));
			}
			Json->SetArrayField(TEXT("stacks"), Stacks);

			FJsonArray Renderers;
			for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
			{
				if (Renderer)
				{
					Renderers.Add(MakeShared<FJsonValueObject>(Owned(*Renderer)));
				}
			}
			Json->SetArrayField(TEXT("renderers"), Renderers);
			return Json;
		}

		/** Emitter settings without the parts written elsewhere (stacks, renderers, graph) and without version, editor and compiled data. */
		static FAGEPropertyFilter SettingsFilter()
		{
			return ExcludeNames({
				// Versioning and Python upgrade scripts
				TEXT("Version"), TEXT("VersionChangeDescription"), TEXT("UpdateScriptExecution"), TEXT("PythonUpdateScript"), TEXT("ScriptAsset"),
				TEXT("bDeprecated"), TEXT("DeprecationMessage"),
				// Written as "stacks" and "renderers"
				TEXT("SpawnScriptProps"), TEXT("UpdateScriptProps"), TEXT("EmitterSpawnScriptProps"), TEXT("EmitterUpdateScriptProps"),
				TEXT("EventHandlerScriptProps"), TEXT("SimulationStages"), TEXT("RendererProperties"),
				// Graph, scratch pads, parent (written as "parent")
				TEXT("GraphSource"), TEXT("ScratchPads"), TEXT("ParentScratchPads"), TEXT("VersionedParent"), TEXT("VersionedParentAtLastMerge"),
				// Derived, compiled or editor-only data
				TEXT("RendererBindings"), TEXT("RendererBindingsExternalObjects"), TEXT("ResolvedDIBindings"), TEXT("GPUComputeScript"),
				TEXT("SharedEventGeneratorIds"), TEXT("CurrentScalabilitySettings"), TEXT("SimStageExecutionLoopEditorData"),
				TEXT("EditorData"), TEXT("EditorParameters"), TEXT("AddEmitterDefaultViewState"),
			});
		}

		/** What a generic simulation stage iterates over and how often, so a reader does not have to dig through "properties". */
		void WriteStageHeader(FJsonObject& Stack, const UNiagaraSimulationStageBase& Stage)
		{
			const UNiagaraSimulationStageGeneric* Generic = Cast<UNiagaraSimulationStageGeneric>(&Stage);
			if (!Generic)
			{
				return;
			}
			FString Source;
			const TSharedPtr<FJsonValue> SourceValue = Writer.WriteField(*Generic->GetClass(), Generic, TEXT("IterationSource"), Generic);
			if (SourceValue.IsValid() && SourceValue->TryGetString(Source))
			{
				Stack.SetStringField(TEXT("iterationSource"), Source);
			}
			if (Source.EndsWith(TEXT("DataInterface")))
			{
				Stack.SetStringField(TEXT("dataInterface"), Generic->DataInterface.BoundVariable.GetName().ToString());
			}
			Stack.SetStringField(TEXT("iterations"), BindingText(Generic->NumIterations));
		}

		/** A bound parameter's name, otherwise the constant (e.g. "1"). */
		static FString BindingText(const FNiagaraParameterBindingWithValue& Binding)
		{
			if (!Binding.IsSetToDefault())
			{
				return Binding.AliasedParameter.GetName().ToString();
			}
			const TConstArrayView<uint8> Value = Binding.GetDefaultValueArray();
			if (Value.Num() == sizeof(int32))
			{
				return LexToString(*reinterpret_cast<const int32*>(Value.GetData()));
			}
			return Binding.ToString();
		}

		TSharedRef<FJsonObject> WriteStack(const FStackSource& Source, const TCHAR* Usage)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("usage"), Usage);
			WriteModules(*Json, Source);
			return Json;
		}

		/** The stack's modules in execution order: from the output node back along the parameter map chain. */
		void WriteModules(FJsonObject& Stack, const FStackSource& Source)
		{
			const UNiagaraNodeOutput* Output = Source.Graph ? Source.Graph->FindEquivalentOutputNode(Source.Usage, Source.UsageId) : nullptr;
			if (!Output)
			{
				Stack.SetStringField(TEXT("error"), TEXT("No output node for this stack in the graph."));
				Context.Warn(FString::Printf(TEXT("Niagara stack %s (usage %d) has no output node in its graph."), *Stack.GetStringField(TEXT("usage")), static_cast<int32>(Source.Usage)));
				return;
			}

			// Rapid iteration parameter names use the output node's usage, as the editor's stack does.
			FStackSource Resolved = Source;
			Resolved.Usage = Output->GetUsage();

			TArray<const UNiagaraNodeFunctionCall*> Modules;
			int32 Steps = 0;
			for (const UEdGraphNode* Node = PreviousInStack(*Output); Node && Steps < MaxChainLength; Node = PreviousInStack(*Node), ++Steps)
			{
				if (const UNiagaraNodeFunctionCall* Module = Cast<UNiagaraNodeFunctionCall>(Node))
				{
					Modules.Insert(Module, 0);
				}
			}

			FJsonArray Json;
			for (const UNiagaraNodeFunctionCall* Module : Modules)
			{
				Json.Add(MakeShared<FJsonValueObject>(WriteFunctionCall(*Module, Resolved, 0)));
			}
			Stack.SetArrayField(TEXT("modules"), Json);
		}

		bool IsScratchPad(const UNiagaraScript& Script) const
		{
			return Script.GetOutermost() == System.GetOutermost();
		}

		/** Prefix of this function's rapid iteration parameters, built with the engine's own naming rule. */
		static FString RapidIterationPrefix(const FStackSource& Source, const FString& FunctionName)
		{
			const FString Placeholder = FunctionName + TEXT(".X");
			const FString Name = FNiagaraUtilities::CreateRapidIterationConstantName(
				FName(*Placeholder), Source.EmitterName.IsEmpty() ? nullptr : *Source.EmitterName, Source.Usage);
			return Name.LeftChop(1);
		}

		/** A module, or a dynamic input when Depth > 0: its name, script, switches and the inputs that were set. */
		TSharedRef<FJsonObject> WriteFunctionCall(const UNiagaraNodeFunctionCall& Node, const FStackSource& Source, int32 Depth)
		{
			TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			const FString FunctionName = Node.GetFunctionName();
			Json->SetStringField(TEXT("name"), FunctionName);
			const UNiagaraScript* FunctionScript = Node.FunctionScript.Get();
			if (FunctionScript)
			{
				Json->SetStringField(TEXT("script"), FAGEPropertyWriter::ObjectPath(FunctionScript));
			}

			const bool bCustomHlsl = Node.IsA<UNiagaraNodeCustomHlsl>();
			if (bCustomHlsl)
			{
				Json->SetStringField(TEXT("kind"), TEXT("customHlsl"));
				Json->SetStringField(TEXT("hlsl"), ReadStringProperty(Node, TEXT("CustomHlsl")));
			}
			else if (Node.IsA<UNiagaraNodeAssignment>())
			{
				Json->SetStringField(TEXT("kind"), TEXT("setParameters"));
			}
			else if (FunctionScript && IsScratchPad(*FunctionScript))
			{
				Json->SetStringField(TEXT("kind"), TEXT("scratchPad"));
				AddGraph(GraphOf(FunctionScript->GetLatestSource()), FunctionScript->GetName(), TEXT("ScratchPad"));
			}
			if (!Node.IsNodeEnabled())
			{
				Json->SetBoolField(TEXT("enabled"), false);
			}
			Json->SetStringField(TEXT("node"), AGE::GuidToString(Node.NodeGuid));

			TMap<FString, TSharedRef<FJsonObject>> Written;
			FJsonArray Switches;
			FJsonArray Inputs;

			// Pins on the call node itself are its static switches (on a custom HLSL node: its own inputs).
			// A switch still at its default is left out, like any other default input.
			for (const UEdGraphPin* Pin : Node.Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || IsParameterMapPin(*Pin) || Pin->bOrphanedPin)
				{
					continue;
				}
				if (Pin->LinkedTo.Num() == 0 && Pin->DoesDefaultValueMatchAutogenerated())
				{
					continue;
				}
				const FString InputName = Pin->PinName.ToString();
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), InputName);
				WritePinValue(*Entry, *Pin, Source, Depth);
				(bCustomHlsl ? Inputs : Switches).Add(MakeShared<FJsonValueObject>(Entry));
				Written.Add(InputName, Entry);
			}

			// Inputs set in the stack are pins named "<FunctionName>.<Input>" on the parameter map set node in front of the call.
			// Dynamic inputs share such override nodes with their siblings, so the name prefix decides which pins belong here.
			const UEdGraphNode* OverrideNode = PreviousInStack(Node);
			if (OverrideNode && IsParameterMapSet(*OverrideNode))
			{
				const FString Prefix = FunctionName + TEXT(".");
				for (const UEdGraphPin* Pin : OverrideNode->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Input || IsParameterMapPin(*Pin))
					{
						continue;
					}
					const FString PinName = Pin->PinName.ToString();
					if (!PinName.StartsWith(Prefix))
					{
						continue;
					}
					const FString InputName = PinName.RightChop(Prefix.Len());
					TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("name"), InputName);
					if (Pin->LinkedTo.Num() == 0)
					{
						Entry->SetStringField(TEXT("type"), ObjectTypeLabel(Pin->PinType.PinSubCategoryObject.Get()));
					}
					WritePinValue(*Entry, *Pin, Source, Depth);
					Inputs.Add(MakeShared<FJsonValueObject>(Entry));
					Written.Add(InputName, Entry);
				}
			}

			// Constant values typed into the stack are kept in the owning script's rapid iteration parameters.
			if (Source.Script)
			{
				const FString Prefix = RapidIterationPrefix(Source, FunctionName);
				const FNiagaraParameterStore& Store = Source.Script->RapidIterationParameters;
				for (const FNiagaraVariableWithOffset& Variable : Store.ReadParameterVariables())
				{
					const FString ParameterName = Variable.GetName().ToString();
					if (!ParameterName.StartsWith(Prefix))
					{
						continue;
					}
					const FString InputName = ParameterName.RightChop(Prefix.Len());
					const FNiagaraTypeDefinition& Type = Variable.GetType();
					if (Written.Contains(InputName) || Type.IsStatic() || Type.IsDataInterface() || Type.IsUObject())
					{
						continue;
					}
					TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("name"), InputName);
					Entry->SetStringField(TEXT("type"), TypeLabel(Type));
					WriteStoredValue(*Entry, Store, Variable);
					Inputs.Add(MakeShared<FJsonValueObject>(Entry));
					Written.Add(InputName, Entry);
				}
			}

			if (Switches.Num() > 0)
			{
				Json->SetArrayField(TEXT("switches"), Switches);
			}
			if (Inputs.Num() > 0)
			{
				Json->SetArrayField(TEXT("inputs"), Inputs);
			}
			if (!bCustomHlsl && !Node.IsA<UNiagaraNodeAssignment>())
			{
				WriteUnsetInputs(*Json, Node, Source, Written);
			}
			return Json;
		}

		FCompileConstantResolver ResolverFor(const FStackSource& Source) const
		{
			return Source.Emitter.Emitter
				? FCompileConstantResolver(Source.Emitter, Source.Usage)
				: FCompileConstantResolver(&System, Source.Usage);
		}

		/**
		 * "defaults": inputs the stack shows that nobody set (they keep the module's default value).
		 * Inputs hidden by the current static switch values are not listed; an input that was set but is hidden gets "inactive": true.
		 * The input list comes from the editor's own stack code (FNiagaraStackGraphUtilities): static switches, then module inputs.
		 */
		void WriteUnsetInputs(FJsonObject& Json, const UNiagaraNodeFunctionCall& Node, const FStackSource& Source, const TMap<FString, TSharedRef<FJsonObject>>& Written) const
		{
			FCompileConstantResolver Resolver = ResolverFor(Source);
			TArray<FString> Defaults;
			TSet<FString> Seen;
			const auto Visit = [&Defaults, &Seen, &Written](const FString& Name, bool bHidden)
			{
				bool bAlreadySeen = false;
				Seen.Add(Name, &bAlreadySeen);
				if (bAlreadySeen)
				{
					return;
				}
				if (const TSharedRef<FJsonObject>* Entry = Written.Find(Name))
				{
					if (bHidden)
					{
						(*Entry)->SetBoolField(TEXT("inactive"), true);
					}
				}
				else if (!bHidden)
				{
					Defaults.Add(Name);
				}
			};

			TArray<UEdGraphPin*> SwitchPins;
			TSet<UEdGraphPin*> HiddenSwitchPins;
			FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(Node, SwitchPins, HiddenSwitchPins, Resolver);
			for (UEdGraphPin* Pin : SwitchPins)
			{
				if (Pin)
				{
					Visit(Pin->PinName.ToString(), HiddenSwitchPins.Contains(Pin));
				}
			}

			TArray<FNiagaraVariable> ModuleInputs;
			TSet<FNiagaraVariable> HiddenInputs;
			FNiagaraStackGraphUtilities::GetStackFunctionInputs(Node, ModuleInputs, HiddenInputs, Resolver,
				FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);
			for (const FNiagaraVariable& Input : ModuleInputs)
			{
				FString Name = Input.GetName().ToString();
				Name.RemoveFromStart(TEXT("Module."));
				Visit(Name, HiddenInputs.Contains(Input));
			}

			if (Defaults.Num() > 0)
			{
				FJsonArray Names;
				for (const FString& Name : Defaults)
				{
					Names.Add(MakeShared<FJsonValueString>(Name));
				}
				Json.SetArrayField(TEXT("defaults"), Names);
			}
		}

		/** What feeds an input pin: a linked parameter, a dynamic input, a data interface, an object, or the pin's own value. */
		void WritePinValue(FJsonObject& Entry, const UEdGraphPin& Pin, const FStackSource& Source, int32 Depth)
		{
			const UEdGraphPin* Linked = Pin.LinkedTo.Num() > 0 ? Pin.LinkedTo[0] : nullptr;
			const UEdGraphNode* Node = Linked ? Linked->GetOwningNode() : nullptr;
			if (!Node)
			{
				Entry.SetStringField(TEXT("value"), Pin.DefaultObject ? FAGEPropertyWriter::ObjectPath(Pin.DefaultObject.Get()) : Pin.DefaultValue);
				if (const UEnum* Enum = Cast<UEnum>(Pin.PinType.PinSubCategoryObject.Get()))
				{
					const int64 Value = Enum->GetValueByNameString(Pin.DefaultValue);
					if (Value != INDEX_NONE)
					{
						AddEnumDisplayName(Entry, *Enum, Value);
					}
				}
				return;
			}
			if (IsParameterMapGet(*Node))
			{
				Entry.SetStringField(TEXT("linked"), Linked->PinName.ToString());
				return;
			}
			if (const UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node))
			{
				if (Depth >= MaxDynamicInputDepth)
				{
					Entry.SetStringField(TEXT("error"), FString::Printf(TEXT("Dynamic input %s is nested more than %d levels deep."), *Call->GetFunctionName(), MaxDynamicInputDepth));
					return;
				}
				TSharedRef<FJsonObject> Dynamic = WriteFunctionCall(*Call, Source, Depth + 1);
				if (CountValueOutputs(*Call) > 1)
				{
					Dynamic->SetStringField(TEXT("output"), Linked->PinName.ToString());
				}
				Entry.SetObjectField(TEXT("dynamic"), Dynamic);
				return;
			}
			if (const UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(Node))
			{
				if (const UObject* DataInterface = ReadObjectProperty(*InputNode, TEXT("DataInterface")))
				{
					Entry.SetObjectField(TEXT("dataInterface"), Owned(*DataInterface));
					return;
				}
				if (const UObject* Object = ReadObjectProperty(*InputNode, TEXT("ObjectAsset")))
				{
					Entry.SetStringField(TEXT("object"), FAGEPropertyWriter::ObjectPath(Object));
					return;
				}
				Entry.SetStringField(TEXT("linked"), InputNode->Input.GetName().ToString());
				return;
			}
			Entry.SetStringField(TEXT("linkedNode"), FString::Printf(TEXT("%s.%s"), *Node->GetClass()->GetName(), *Linked->PinName.ToString()));
		}

		const UNiagaraSystem& System;
		FAGEExportContext& Context;
		FAGEPropertyWriter Writer;
		FSkipVolatileNodeProperties NodeFilter;
		FAGEGraphWriter GraphWriter;
		const UScriptStruct* EmitterDataStruct = nullptr;
		const UScriptStruct* EventStruct = nullptr;
		TArray<FGraphEntry> Graphs;
	};
}

bool FAGENiagaraSystemHandler::CanHandle(const UObject& Asset) const
{
	return Asset.IsA<UNiagaraSystem>();
}

bool FAGENiagaraSystemHandler::ShouldExportAssetProperty(const FProperty& Property) const
{
	static const TSet<FName> Excluded = {
		// Written as "niagara"
		TEXT("EmitterHandles"), TEXT("SystemSpawnScript"), TEXT("SystemUpdateScript"), TEXT("ExposedParameters"), TEXT("ScratchPadScripts"),
		// Compiled, cached or editor-only data
		TEXT("SystemCompiledData"), TEXT("ScriptRuntimeCompiledDataForEditor"), TEXT("StaleRuntimeCompiledDataForEditor"), TEXT("ScriptRuntimeCookedDataMap"),
		TEXT("EditorData"), TEXT("EditorParameters"), TEXT("EditorOnlyAddedParameters"), TEXT("UpdateContext"), TEXT("TypeContainer"),
		TEXT("ThumbnailImage"), TEXT("MessageStore"), TEXT("SystemStateDataStruct"), TEXT("BakerGeneratedSettings"),
	};
	return !Excluded.Contains(Property.GetFName());
}

void FAGENiagaraSystemHandler::Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const
{
	using namespace AGE::NiagaraDetail;
	const UNiagaraSystem* System = Cast<UNiagaraSystem>(&Asset);
	if (!System)
	{
		return;
	}
	FSystemWriter Writer(*System, Context);
	OutAssetJson.SetObjectField(TEXT("niagara"), Writer.Write());
}
