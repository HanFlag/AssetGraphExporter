// Asset Graph Exporter

#include "AGECustomizableObjectHandler.h"
#include "AGEGraphWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
	/** The editor graph of a Customizable Object (UCustomizableObject::Source), or null for any other asset. */
	const UEdGraph* SourceGraph(const UObject& Asset)
	{
		for (const UClass* Class = Asset.GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (Class->GetPathName() == TEXT("/Script/CustomizableObject.CustomizableObject"))
			{
				const FObjectProperty* Property = FindFProperty<FObjectProperty>(Class, TEXT("Source"));
				return Property ? Cast<UEdGraph>(Property->GetObjectPropertyValue_InContainer(&Asset)) : nullptr;
			}
		}
		return nullptr;
	}
}

bool FAGECustomizableObjectHandler::CanHandle(const UObject& Asset) const
{
	return SourceGraph(Asset) != nullptr;
}

void FAGECustomizableObjectHandler::Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const
{
	FAGEGraphWriter GraphWriter(Context);
	TArray<TSharedPtr<FJsonValue>> Graphs;
	Graphs.Add(MakeShared<FJsonValueObject>(GraphWriter.WriteGraph(*SourceGraph(Asset), TEXT("Source"))));
	const TSharedRef<FJsonObject> Section = MakeShared<FJsonObject>();
	Section->SetArrayField(TEXT("graphs"), Graphs);
	OutAssetJson.SetObjectField(TEXT("customizableObject"), Section);
}
