// Asset Graph Exporter

#include "AGEPropertyWriter.h"
#include "AGEExportContext.h"
#include "AnimStateNodeBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "ObjectEditorUtils.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/EnumProperty.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StrProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

namespace
{
	// Compare owned sub-objects by content, not by pointer, so an untouched component template does not count as a change.
	constexpr uint32 ComparePortFlags = PPF_DeepComparison | PPF_DeepCompareInstances;

	int32 GetStaticArrayDim(const FProperty& Property)
	{
		const int32 ElementSize = Property.GetElementSize();
		return ElementSize > 0 ? Property.GetSize() / ElementSize : 1;
	}

	TSharedPtr<FJsonValue> MakeString(const FString& Value)
	{
		return MakeShared<FJsonValueString>(Value);
	}

	TSharedPtr<FJsonValue> MakeNumber(double Value)
	{
		// JSON has no NaN / Infinity.
		if (!FMath::IsFinite(Value))
		{
			return MakeShared<FJsonValueString>(LexToString(Value));
		}
		return MakeShared<FJsonValueNumber>(Value);
	}

	FString ExportAsText(const FProperty& Property, const void* Value, const UObject* Owner)
	{
		FString Text;
		Property.ExportText_Direct(Text, Value, nullptr, const_cast<UObject*>(Owner), PPF_None);
		return Text;
	}

	/** Matched by name: these structs' StaticStruct() is not exported from CoreUObject. */
	bool IsCoreUObjectStruct(const UScriptStruct& Struct, const TCHAR* Name)
	{
		static const FName CoreUObjectPackage(TEXT("/Script/CoreUObject"));
		return Struct.GetFName() == FName(Name) && Struct.GetPackage()->GetFName() == CoreUObjectPackage;
	}

	/**
	 * Values that change without the content changing, so writing them would make the same asset export differently.
	 * UMovieSceneSignedObject::Signature is a fresh GUID on every MarkAsChanged ("a GUID and not a hash", MovieSceneSignedObject.h);
	 * animation data models and sequences reload with new ones. Matched by name to avoid a MovieScene module dependency.
	 */
	bool IsVolatileProperty(const FProperty& Property, const UObject* Owner)
	{
		static const FName SignatureName(TEXT("Signature"));
		static const FName SignedObjectName(TEXT("MovieSceneSignedObject"));
		static const FName MovieScenePackage(TEXT("/Script/MovieScene"));
		static const FName GuidName(TEXT("Guid"));
		static const FName NotifyEventName(TEXT("AnimNotifyEvent"));
		static const FName EnginePackage(TEXT("/Script/Engine"));
		if (Property.GetFName() == SignatureName)
		{
			const UClass* Class = Property.GetOwnerClass();
			return Class && Class->GetFName() == SignedObjectName && Class->GetPackage()->GetFName() == MovieScenePackage;
		}
		// FAnimNotifyEvent::PostSerialize gives an empty Guid a fresh one whenever the event is read back, undo included (AnimTypes.cpp).
		// The events of a state or transition (StateEntered, TransitionStart, ...) start empty, so an undo changed their export.
		// An animation's notifies keep theirs: they get one when added, and editing tools name notifies by it.
		// Matched by name: FAnimNotifyEvent::StaticStruct() is not exported from Engine.
		if (Property.GetFName() == GuidName && Owner && Owner->IsA<UAnimStateNodeBase>())
		{
			const UStruct* Struct = Property.GetOwnerStruct();
			return Struct && Struct->GetFName() == NotifyEventName && Struct->GetPackage()->GetFName() == EnginePackage;
		}
		return false;
	}

	/**
	 * Vectors, rotators, colors and other structs of at most four plain numbers. They are written whole when written at all:
	 * {"Y":-1} for (0,-1,0) reads as "only Y is set".
	 */
	bool IsSmallNumericStruct(const UStruct& Struct)
	{
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(&Struct); It; ++It)
		{
			const FNumericProperty* Numeric = CastField<FNumericProperty>(*It);
			if (!Numeric || Numeric->IsEnum() || GetStaticArrayDim(*Numeric) != 1 || ++Count > 4)
			{
				return false;
			}
		}
		return Count > 0;
	}

	/** The section title the Details panel shows for the property ("SkeletalControl" -> "Skeletal Control"); sub-categories joined by '|'. */
	FString DetailsCategory(const FProperty& Property)
	{
		const FName Category = FObjectEditorUtils::GetCategoryFName(&Property);
		if (Category.IsNone())
		{
			return FString();
		}
		TArray<FString> Parts;
		Category.ToString().ParseIntoArray(Parts, TEXT("|"), true);
		for (FString& Part : Parts)
		{
			Part = FObjectEditorUtils::GetCategoryText(FName(*Part.TrimStartAndEnd())).ToString();
		}
		return FString::Join(Parts, TEXT("|"));
	}

	void AppendFields(FJsonObject& Target, const FJsonObject& Source)
	{
		// auto: the key type is UE::FSharedString in 5.8 (FString only with UE_JSONOBJECT_LEGACY_STRING_KEYS=1).
		for (const auto& Pair : Source.Values)
		{
			Target.Values.Add(Pair.Key, Pair.Value);
		}
	}
}

FAGEPropertyWriter::FAGEPropertyWriter(FAGEExportContext& InContext)
	: Context(InContext)
{
}

FString FAGEPropertyWriter::ObjectPath(const UObject* Object)
{
	return Object ? Object->GetPathName() : FString();
}

TSharedRef<FJsonObject> FAGEPropertyWriter::WriteObjectProperties(const UObject& Object, const FAGEPropertyFilter& Filter)
{
	const UObject* Archetype = Context.bDiffOnly ? Object.GetArchetype() : nullptr;
	// A CDO's archetype is the parent class CDO, so it may lack fields the object has.
	return WriteFields(*Object.GetClass(), &Object, Archetype, Archetype ? Archetype->GetClass() : nullptr, &Object, Filter);
}

TSharedRef<FJsonObject> FAGEPropertyWriter::WriteOwnedObject(const UObject& Object, const FAGEPropertyFilter& Filter)
{
	Context.ExpandedObjects.Add(&Object);

	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("$class"), ObjectPath(Object.GetClass()));
	Json->SetStringField(TEXT("$name"), Object.GetName());

	++Depth;
	AppendFields(*Json, *WriteObjectProperties(Object, Filter));
	--Depth;
	return Json;
}

TSharedRef<FJsonObject> FAGEPropertyWriter::WriteStruct(const UStruct& Struct, const void* Data, const void* Defaults, const UObject* Owner, const FAGEPropertyFilter& Filter)
{
	return WriteFields(Struct, Data, Defaults, Defaults ? &Struct : nullptr, Owner, Filter);
}

TSharedRef<FJsonObject> FAGEPropertyWriter::WriteStructAgainstDefaults(const UScriptStruct& Struct, const void* Data, const UObject* Owner, const FAGEPropertyFilter& Filter)
{
	const void* Defaults = Context.bDiffOnly ? Context.GetStructDefaults(Struct) : nullptr;
	return WriteStruct(Struct, Data, Defaults, Owner, Filter);
}

TSharedRef<FJsonObject> FAGEPropertyWriter::WriteFields(const UStruct& Struct, const void* Data, const void* Defaults, const UStruct* DefaultsStruct, const UObject* Owner, const FAGEPropertyFilter& Filter)
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	// The fields of a vector or color are not rows of their own in the Details panel.
	const bool bWriteDetails = Context.bWriteDetails && !IsSmallNumericStruct(Struct);
	TSharedRef<FJsonObject> Details = MakeShared<FJsonObject>();

	for (TFieldIterator<FProperty> It(&Struct); It; ++It)
	{
		const FProperty& Property = **It;
		if (Property.HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated) || IsVolatileProperty(Property, Owner))
		{
			continue;
		}
		if (Filter && !Filter(Property))
		{
			continue;
		}

		const bool bHasDefault = Defaults && DefaultsStruct && Property.IsInContainer(DefaultsStruct);
		// Authored name drops the GUID suffix that user-defined struct fields carry.
		const FString Key = Property.GetAuthoredName();
		const int32 ArrayDim = GetStaticArrayDim(Property);

		if (ArrayDim == 1)
		{
			const void* Value = Property.ContainerPtrToValuePtr<void>(Data);
			const void* Default = bHasDefault ? Property.ContainerPtrToValuePtr<void>(Defaults) : nullptr;
			if (Default && Property.Identical(Value, Default, ComparePortFlags))
			{
				continue;
			}
			const TSharedPtr<FJsonValue> Written = WriteValue(Property, Value, Default, Owner);
			// A struct that differs only in fields that are not written (transient, volatile) would read as an empty change.
			const TSharedPtr<FJsonObject>* WrittenObject = nullptr;
			if (Default && Property.IsA<FStructProperty>() && Written->TryGetObject(WrittenObject) && (*WrittenObject)->Values.IsEmpty())
			{
				continue;
			}
			Json->SetField(Key, Written);
		}
		else
		{
			// C-style fixed array (UPROPERTY() int32 Values[4]).
			bool bAllIdentical = bHasDefault;
			TArray<TSharedPtr<FJsonValue>> Items;
			for (int32 Index = 0; Index < ArrayDim; ++Index)
			{
				const void* Value = Property.ContainerPtrToValuePtr<void>(Data, Index);
				const void* Default = bHasDefault ? Property.ContainerPtrToValuePtr<void>(Defaults, Index) : nullptr;
				if (!Default || !Property.Identical(Value, Default, ComparePortFlags))
				{
					bAllIdentical = false;
				}
				Items.Add(WriteValue(Property, Value, Default, Owner));
			}
			if (bAllIdentical)
			{
				continue;
			}
			Json->SetArrayField(Key, Items);
		}

		if (bWriteDetails && Property.HasAnyPropertyFlags(CPF_Edit))
		{
			// The same functions the Details panel uses, so the text follows the editor's language settings like the panel does.
			TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
			Detail->SetStringField(TEXT("displayName"), Property.GetDisplayNameText().ToString());
			const FString Category = DetailsCategory(Property);
			if (!Category.IsEmpty())
			{
				Detail->SetStringField(TEXT("category"), Category);
			}
			Details->SetObjectField(Key, Detail);
		}
	}
	if (Details->Values.Num() > 0)
	{
		Json->SetObjectField(TEXT("$details"), Details);
	}
	return Json;
}

TSharedPtr<FJsonValue> FAGEPropertyWriter::WriteInstancedStruct(const UScriptStruct* Struct, const void* Memory, const UObject* Owner)
{
	if (!Struct || !Memory)
	{
		return MakeShared<FJsonValueNull>();
	}
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("$type"), ObjectPath(Struct));
	AppendFields(*Json, *WriteStructAgainstDefaults(*Struct, Memory, Owner));
	return MakeShared<FJsonValueObject>(Json);
}

TSharedPtr<FJsonValue> FAGEPropertyWriter::WriteStructValue(const UScriptStruct& Struct, const FProperty& Property, const void* Value, const void* Default, const UObject* Owner)
{
	// Containers whose content is invisible to plain reflection.
	if (IsCoreUObjectStruct(Struct, TEXT("InstancedStruct")))
	{
		const FInstancedStruct& Instanced = *static_cast<const FInstancedStruct*>(Value);
		return WriteInstancedStruct(Instanced.GetScriptStruct(), Instanced.GetMemory(), Owner);
	}
	if (IsCoreUObjectStruct(Struct, TEXT("InstancedPropertyBag")))
	{
		// Every value in a bag is user data, so it is always written in full.
		const FConstStructView Bag = static_cast<const FInstancedPropertyBag*>(Value)->GetValue();
		if (!Bag.IsValid())
		{
			return MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
		}
		return MakeShared<FJsonValueObject>(WriteStruct(*Bag.GetScriptStruct(), Bag.GetMemory(), nullptr, Owner));
	}

	// Structs with a native text form (FGuid, FSoftObjectPath, FGameplayTag, ...) read better as one string.
	if (Struct.StructFlags & STRUCT_ExportTextItemNative)
	{
		return MakeString(ExportAsText(Property, Value, Owner));
	}
	if (IsSmallNumericStruct(Struct))
	{
		return MakeShared<FJsonValueObject>(WriteStruct(Struct, Value, nullptr, Owner));
	}

	const void* StructDefaults = Default ? Default : (Context.bDiffOnly ? Context.GetStructDefaults(Struct) : nullptr);
	return MakeShared<FJsonValueObject>(WriteStruct(Struct, Value, StructDefaults, Owner));
}

TSharedPtr<FJsonValue> FAGEPropertyWriter::WriteValue(const FProperty& Property, const void* Value, const void* Default, const UObject* Owner)
{
	if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(&Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(Value));
	}
	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(&Property))
	{
		const int64 EnumValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
		const UEnum* Enum = EnumProperty->GetEnum();
		return MakeString(Enum ? Enum->GetValueOrBitfieldAsString(EnumValue) : LexToString(EnumValue));
	}
	if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(&Property))
	{
		if (const UEnum* Enum = NumericProperty->GetIntPropertyEnum())
		{
			return MakeString(Enum->GetValueOrBitfieldAsString(NumericProperty->GetSignedIntPropertyValue(Value)));
		}
		if (NumericProperty->IsFloatingPoint())
		{
			return MakeNumber(NumericProperty->GetFloatingPointPropertyValue(Value));
		}
		if (Property.IsA<FUInt64Property>())
		{
			return MakeNumber(static_cast<double>(NumericProperty->GetUnsignedIntPropertyValue(Value)));
		}
		return MakeNumber(static_cast<double>(NumericProperty->GetSignedIntPropertyValue(Value)));
	}
	if (Property.IsA<FNameProperty>())
	{
		return MakeString(static_cast<const FName*>(Value)->ToString());
	}
	if (Property.IsA<FStrProperty>())
	{
		return MakeString(*static_cast<const FString*>(Value));
	}
	if (Property.IsA<FTextProperty>())
	{
		return MakeString(static_cast<const FText*>(Value)->ToString());
	}
	if (Property.IsA<FSoftObjectProperty>())
	{
		// Do not load the target; the path is what matters.
		const FSoftObjectPath Path = static_cast<const FSoftObjectPtr*>(Value)->ToSoftObjectPath();
		return Path.IsNull() ? MakeShared<FJsonValueNull>() : MakeString(Path.ToString());
	}
	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(&Property))
	{
		return WriteObjectReference(ObjectProperty->GetObjectPropertyValue(Value), Owner, Property.HasAnyPropertyFlags(CPF_InstancedReference));
	}
	if (Property.IsA<FInterfaceProperty>())
	{
		return WriteObjectReference(static_cast<const FScriptInterface*>(Value)->GetObject(), Owner, false);
	}
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
	{
		return WriteStructValue(*StructProperty->Struct, Property, Value, Default, Owner);
	}
	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(&Property))
	{
		FScriptArrayHelper Helper(ArrayProperty, Value);
		TArray<TSharedPtr<FJsonValue>> Items;
		Items.Reserve(Helper.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			Items.Add(WriteValue(*ArrayProperty->Inner, Helper.GetRawPtr(Index), nullptr, Owner));
		}
		return MakeShared<FJsonValueArray>(Items);
	}
	if (const FSetProperty* SetProperty = CastField<FSetProperty>(&Property))
	{
		FScriptSetHelper Helper(SetProperty, Value);
		TArray<TSharedPtr<FJsonValue>> Items;
		for (FScriptSetHelper::FIterator It(Helper); It; ++It)
		{
			Items.Add(WriteValue(*SetProperty->GetElementProperty(), Helper.GetElementPtr(It), nullptr, Owner));
		}
		return MakeShared<FJsonValueArray>(Items);
	}
	if (const FMapProperty* MapProperty = CastField<FMapProperty>(&Property))
	{
		// Keys become JSON object keys in their text form (names and strings as-is, objects as paths).
		FScriptMapHelper Helper(MapProperty, Value);
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		for (FScriptMapHelper::FIterator It(Helper); It; ++It)
		{
			const FProperty& KeyProperty = *MapProperty->GetKeyProperty();
			const void* KeyPtr = Helper.GetKeyPtr(It);
			FString Key;
			if (KeyProperty.IsA<FNameProperty>())
			{
				Key = static_cast<const FName*>(KeyPtr)->ToString();
			}
			else if (KeyProperty.IsA<FStrProperty>())
			{
				Key = *static_cast<const FString*>(KeyPtr);
			}
			else
			{
				Key = ExportAsText(KeyProperty, KeyPtr, Owner);
			}
			Json->SetField(Key, WriteValue(*MapProperty->GetValueProperty(), Helper.GetValuePtr(It), nullptr, Owner));
		}
		return MakeShared<FJsonValueObject>(Json);
	}

	// Delegates, field paths, optionals and anything newer: the engine's own text form.
	return MakeString(ExportAsText(Property, Value, Owner));
}

TSharedPtr<FJsonValue> FAGEPropertyWriter::WriteField(const UStruct& Type, const void* Container, FName FieldName, const UObject* Owner)
{
	const FProperty* Property = FindFProperty<FProperty>(&Type, FieldName);
	if (!Property)
	{
		Context.Warn(FString::Printf(TEXT("Field %s not found on %s"), *FieldName.ToString(), *Type.GetName()));
		return nullptr;
	}
	return WriteValue(*Property, Property->ContainerPtrToValuePtr<void>(Container), nullptr, Owner);
}

bool FAGEPropertyWriter::ShouldExpand(const UObject& Object, const UObject* Owner, bool bInstancedProperty) const
{
	// Types, graphs and nodes are referenced, never inlined (graphs are written by FAGEGraphWriter).
	if (Object.IsA<UField>() || Object.IsA<UPackage>() || Object.IsA<UEdGraph>() || Object.IsA<UEdGraphNode>())
	{
		return false;
	}
	if (Context.ExpandedObjects.Contains(&Object) || Depth >= Context.MaxObjectDepth)
	{
		return false;
	}
	// Only objects stored inside the exported asset's package; references into other assets stay paths.
	if (!Context.RootAsset || Object.GetPackage() != Context.RootAsset->GetPackage())
	{
		return false;
	}
	return bInstancedProperty || Object.GetOuter() == Owner;
}

TSharedPtr<FJsonValue> FAGEPropertyWriter::WriteObjectReference(const UObject* Object, const UObject* Owner, bool bInstancedProperty)
{
	if (!Object)
	{
		return MakeShared<FJsonValueNull>();
	}
	if (ShouldExpand(*Object, Owner, bInstancedProperty))
	{
		return MakeShared<FJsonValueObject>(WriteOwnedObject(*Object));
	}
	return MakeString(ObjectPath(Object));
}
