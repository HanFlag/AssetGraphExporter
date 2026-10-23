// Asset Graph Exporter

#include "AGEAssetExporter.h"
#include "AGEExportContext.h"
#include "AGEHandlerRegistry.h"
#include "AGEPropertyWriter.h"
#include "Misc/EngineVersion.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

TSharedRef<FJsonObject> FAGEAssetExporter::ExportAsset(const UObject& Asset, FAGEExportContext& Context)
{
	Context.RootAsset = &Asset;

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), AGE::SchemaVersion);

	TSharedRef<FJsonObject> Exporter = MakeShared<FJsonObject>();
	Exporter->SetStringField(TEXT("name"), TEXT("AssetGraphExporter"));
	Exporter->SetStringField(TEXT("version"), AGE::ExporterVersion);
	Exporter->SetBoolField(TEXT("diffOnly"), Context.bDiffOnly);
	Root->SetObjectField(TEXT("exporter"), Exporter);
	Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));

	TSharedRef<FJsonObject> AssetInfo = MakeShared<FJsonObject>();
	AssetInfo->SetStringField(TEXT("path"), Asset.GetPathName());
	AssetInfo->SetStringField(TEXT("class"), Asset.GetClass()->GetPathName());
	Root->SetObjectField(TEXT("asset"), AssetInfo);

	const IAGEAssetHandler* Handler = FAGEHandlerRegistry::Get().FindHandler(Asset);
	if (Handler)
	{
		Root->SetStringField(TEXT("handler"), Handler->GetHandlerName().ToString());
	}

	FAGEPropertyWriter PropertyWriter(Context);
	TSharedRef<FJsonObject> Properties = PropertyWriter.WriteObjectProperties(Asset, [Handler](const FProperty& Property)
	{
		return !Handler || Handler->ShouldExportAssetProperty(Property);
	});
	if (Properties->Values.Num() > 0)
	{
		Root->SetObjectField(TEXT("properties"), Properties);
	}

	if (Handler)
	{
		Handler->Export(Asset, Context, *Root);
	}

	if (Context.Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Warnings;
		for (const FString& Warning : Context.Warnings)
		{
			Warnings.Add(MakeShared<FJsonValueString>(Warning));
		}
		Root->SetArrayField(TEXT("warnings"), Warnings);
	}
	return Root;
}

FString FAGEAssetExporter::ToJsonString(const TSharedRef<FJsonObject>& Json)
{
	FString Output;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Json, Writer);
	return Output;
}
