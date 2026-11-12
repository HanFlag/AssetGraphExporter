// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FProperty;
class UObject;
class UScriptStruct;
class UStruct;
struct FAGEExportContext;

/** Returns false for properties that must not be written. */
using FAGEPropertyFilter = TFunction<bool(const FProperty&)>;

/**
 * Reflection-based writer: turns any UObject / struct into JSON by walking its FProperties.
 *
 * Object references:
 *  - objects owned by the object being written (outer == owner, or an Instanced property) are expanded inline
 *    as {"$class": ..., "$name": ..., ...properties};
 *  - everything else (other assets, classes, graphs, nodes) is written as a path string.
 * FInstancedStruct and FInstancedPropertyBag are opened, since plain reflection cannot see inside them.
 * Structs of at most four plain numbers (vectors, rotators, colors) are written whole whenever they are written.
 */
class ASSETGRAPHEXPORTER_API FAGEPropertyWriter
{
public:
	explicit FAGEPropertyWriter(FAGEExportContext& InContext);

	/** The object's properties. In diff mode only values that differ from the archetype (its CDO or template). */
	TSharedRef<FJsonObject> WriteObjectProperties(const UObject& Object, const FAGEPropertyFilter& Filter = nullptr);

	/** {"$class", "$name", ...properties} for an object the asset owns. */
	TSharedRef<FJsonObject> WriteOwnedObject(const UObject& Object, const FAGEPropertyFilter& Filter = nullptr);

	/** Struct fields. Defaults == null writes every field; otherwise only fields that differ from Defaults. */
	TSharedRef<FJsonObject> WriteStruct(const UStruct& Struct, const void* Data, const void* Defaults, const UObject* Owner, const FAGEPropertyFilter& Filter = nullptr);

	/** Struct fields, diffed against a default-constructed struct when the context is in diff mode. */
	TSharedRef<FJsonObject> WriteStructAgainstDefaults(const UScriptStruct& Struct, const void* Data, const UObject* Owner, const FAGEPropertyFilter& Filter = nullptr);

	/** {"$type": struct path, ...fields}, or JSON null when the instanced struct is empty. */
	TSharedPtr<FJsonValue> WriteInstancedStruct(const UScriptStruct* Struct, const void* Memory, const UObject* Owner);

	/** One property value. Default (may be null) is the diff base for nested structs. */
	TSharedPtr<FJsonValue> WriteValue(const FProperty& Property, const void* Value, const void* Default, const UObject* Owner);

	/** One field looked up by name (useful for enums and types whose headers are private or not exported). Null if missing. */
	TSharedPtr<FJsonValue> WriteField(const UStruct& Type, const void* Container, FName FieldName, const UObject* Owner);

	static FString ObjectPath(const UObject* Object);

private:
	/** DefaultsStruct is the type Defaults points at; fields not in it (e.g. variables a child class added) are always written. */
	TSharedRef<FJsonObject> WriteFields(const UStruct& Struct, const void* Data, const void* Defaults, const UStruct* DefaultsStruct, const UObject* Owner, const FAGEPropertyFilter& Filter);
	TSharedPtr<FJsonValue> WriteStructValue(const UScriptStruct& Struct, const FProperty& Property, const void* Value, const void* Default, const UObject* Owner);
	TSharedPtr<FJsonValue> WriteObjectReference(const UObject* Object, const UObject* Owner, bool bInstancedProperty);
	bool ShouldExpand(const UObject& Object, const UObject* Owner, bool bInstancedProperty) const;

	FAGEExportContext& Context;
	int32 Depth = 0;
};
