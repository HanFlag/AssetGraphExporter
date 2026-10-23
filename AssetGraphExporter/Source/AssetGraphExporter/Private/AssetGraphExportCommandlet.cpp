// Asset Graph Exporter

#include "AssetGraphExportCommandlet.h"
#include "AGEAssetQuery.h"
#include "AGEAssetExporter.h"
#include "AGEExportContext.h"
#include "AGELog.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/CommandLine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetGraphExportCommandlet)

namespace
{
	const TCHAR* Usage =
		TEXT("Usage: -run=AssetGraphExport [-Assets=/Game/A,/Game/B] [-Path=/Game/Folder] [-Class=Blueprint,StateTree] [-Out=<dir>] [-Full]");

	/** Garbage is collected every N assets so a large -Path run does not keep every loaded asset in memory. */
	constexpr int32 GarbageCollectInterval = 20;

	TArray<FString> ParseList(const FString& Params, const TCHAR* Switch)
	{
		FString Value;
		TArray<FString> Items;
		// false: do not stop at ',' so the whole comma-separated list is read.
		if (FParse::Value(*Params, Switch, Value, false))
		{
			Value.ParseIntoArray(Items, TEXT(","), true);
			for (FString& Item : Items)
			{
				Item.TrimStartAndEndInline();
			}
		}
		return Items;
	}

	/** Game/Folder/Asset.json for /Game/Folder/Asset. */
	FString RelativeOutputPath(const FAssetData& Asset)
	{
		FString Path = Asset.PackageName.ToString();
		Path.RemoveFromStart(TEXT("/"));
		// Only when a package holds more than one top-level asset with a different name.
		if (Asset.AssetName != FName(*FPackageName::GetShortName(Asset.PackageName)))
		{
			Path += TEXT(".") + Asset.AssetName.ToString();
		}
		return Path + TEXT(".json");
	}

	bool SaveJson(const FString& FilePath, const FString& Text)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
		return FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TArray<TSharedPtr<FJsonValue>> ToJsonStrings(const TArray<FString>& Strings)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& String : Strings)
		{
			Values.Add(MakeShared<FJsonValueString>(String));
		}
		return Values;
	}
}

UAssetGraphExportCommandlet::UAssetGraphExportCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UAssetGraphExportCommandlet::Main(const FString& Params)
{
	// [Timing] lines show where the run time goes: engine startup, registry scan, loading, exporting.
	const double MainStart = FPlatformTime::Seconds();
	UE_LOG(LogAssetGraphExporter, Display, TEXT("[Timing] Engine startup until commandlet: %.2f s"), MainStart - GStartTime);

	FAGEAssetQuery Query;
	Query.Assets = ParseList(Params, TEXT("-Assets="));
	Query.Paths = ParseList(Params, TEXT("-Path="));
	Query.Classes = ParseList(Params, TEXT("-Class="));
	const bool bFull = FParse::Param(*Params, TEXT("Full"));

	// Node titles, pin names and StateTree descriptions are generated in the current language. Fix it to English so
	// exports from machines with different editor languages compare equal. An explicit engine -culture=/-language= wins.
	FString CultureOverride;
	if (!FParse::Value(FCommandLine::Get(), TEXT("culture="), CultureOverride) && !FParse::Value(FCommandLine::Get(), TEXT("language="), CultureOverride))
	{
		FInternationalization::Get().SetCurrentLanguageAndLocale(TEXT("en"));
	}
	const FString Culture = FInternationalization::Get().GetCurrentLanguage()->GetName();
	UE_LOG(LogAssetGraphExporter, Display, TEXT("Text culture: %s"), *Culture);

	if (Query.Assets.IsEmpty() && Query.Paths.IsEmpty())
	{
		UE_LOG(LogAssetGraphExporter, Error, TEXT("Nothing to export: give -Assets= and/or -Path=. %s"), Usage);
		return 1;
	}

	FString OutDir;
	if (!FParse::Value(*Params, TEXT("-Out="), OutDir))
	{
		OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetExport"));
	}
	OutDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), OutDir);

	TArray<FString> Errors;
	const double CollectStart = FPlatformTime::Seconds();
	const TArray<FAssetData> Assets = AGE::CollectAssets(Query, Errors);
	UE_LOG(LogAssetGraphExporter, Display, TEXT("[Timing] Asset collection (scan + query + class filter): %.2f s"), FPlatformTime::Seconds() - CollectStart);
	double TotalLoadSeconds = 0.0;
	double TotalExportSeconds = 0.0;
	for (const FString& Error : Errors)
	{
		UE_LOG(LogAssetGraphExporter, Error, TEXT("%s"), *Error);
	}
	UE_LOG(LogAssetGraphExporter, Display, TEXT("Exporting %d asset(s) to %s"), Assets.Num(), *OutDir);

	TArray<TSharedPtr<FJsonValue>> IndexEntries;
	int32 Failures = 0;
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		const FAssetData& AssetData = Assets[Index];
		const FString RelativePath = RelativeOutputPath(AssetData);

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset"), AssetData.GetSoftObjectPath().ToString());
		Entry->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
		Entry->SetStringField(TEXT("file"), RelativePath);

		FString Error;
		const double LoadStart = FPlatformTime::Seconds();
		const UObject* Asset = AssetData.GetAsset();
		const double LoadSeconds = FPlatformTime::Seconds() - LoadStart;
		double ExportSeconds = 0.0;
		if (!Asset)
		{
			Error = TEXT("Failed to load");
		}
		else
		{
			const double ExportStart = FPlatformTime::Seconds();
			FAGEExportContext Context;
			Context.bDiffOnly = !bFull;
			const TSharedRef<FJsonObject> Json = FAGEAssetExporter::ExportAsset(*Asset, Context);
			if (!SaveJson(FPaths::Combine(OutDir, RelativePath), FAGEAssetExporter::ToJsonString(Json)))
			{
				Error = TEXT("Failed to write file");
			}
			ExportSeconds = FPlatformTime::Seconds() - ExportStart;
			TotalExportSeconds += ExportSeconds;
			if (Context.Warnings.Num() > 0)
			{
				Entry->SetNumberField(TEXT("warnings"), Context.Warnings.Num());
			}
		}

		if (Error.IsEmpty())
		{
			Entry->SetStringField(TEXT("status"), TEXT("ok"));
			UE_LOG(LogAssetGraphExporter, Display, TEXT("[%d/%d] %s (load %.2f s, export %.2f s)"), Index + 1, Assets.Num(), *RelativePath, LoadSeconds, ExportSeconds);
		}
		else
		{
			++Failures;
			Entry->SetStringField(TEXT("status"), TEXT("failed"));
			Entry->SetStringField(TEXT("error"), Error);
			UE_LOG(LogAssetGraphExporter, Error, TEXT("[%d/%d] %s: %s"), Index + 1, Assets.Num(), *AssetData.GetSoftObjectPath().ToString(), *Error);
		}
		IndexEntries.Add(MakeShared<FJsonValueObject>(Entry));
		TotalLoadSeconds += LoadSeconds;

		if ((Index + 1) % GarbageCollectInterval == 0)
		{
			CollectGarbage(RF_NoFlags);
		}
	}

	TSharedRef<FJsonObject> IndexJson = MakeShared<FJsonObject>();
	IndexJson->SetNumberField(TEXT("schemaVersion"), AGE::SchemaVersion);
	TSharedRef<FJsonObject> QueryJson = MakeShared<FJsonObject>();
	QueryJson->SetArrayField(TEXT("assets"), ToJsonStrings(Query.Assets));
	QueryJson->SetArrayField(TEXT("paths"), ToJsonStrings(Query.Paths));
	QueryJson->SetArrayField(TEXT("classes"), ToJsonStrings(Query.Classes));
	QueryJson->SetBoolField(TEXT("full"), bFull);
	QueryJson->SetStringField(TEXT("culture"), Culture);
	IndexJson->SetObjectField(TEXT("query"), QueryJson);
	IndexJson->SetArrayField(TEXT("assets"), IndexEntries);
	if (Errors.Num() > 0)
	{
		IndexJson->SetArrayField(TEXT("errors"), ToJsonStrings(Errors));
	}
	SaveJson(FPaths::Combine(OutDir, TEXT("index.json")), FAGEAssetExporter::ToJsonString(IndexJson));

	UE_LOG(LogAssetGraphExporter, Display, TEXT("Done: %d exported, %d failed, %d query error(s)."), Assets.Num() - Failures, Failures, Errors.Num());
	UE_LOG(LogAssetGraphExporter, Display, TEXT("[Timing] Load %.2f s, export %.2f s, commandlet total %.2f s (process total %.2f s)"),
		TotalLoadSeconds, TotalExportSeconds, FPlatformTime::Seconds() - MainStart, FPlatformTime::Seconds() - GStartTime);
	return (Failures > 0 || Errors.Num() > 0) ? 1 : 0;
}
