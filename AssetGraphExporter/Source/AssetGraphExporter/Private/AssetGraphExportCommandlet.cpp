// Asset Graph Exporter

#include "AssetGraphExportCommandlet.h"
#include "AGEAssetQuery.h"
#include "AGEAssetExporter.h"
#include "AGEExportCache.h"
#include "AGEExportContext.h"
#include "AGELog.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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
		TEXT("Usage: -run=AssetGraphExport [-Assets=/Game/A,/Game/B] [-Path=/Game/Folder] [-Class=Blueprint,StateTree] [-Out=<dir>] [-Full] [-Incremental]");

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

	/** Asset entries of the index.json an earlier run left in the output folder, by object path. Empty when there is none. */
	TMap<FString, TSharedPtr<FJsonObject>> ReadPreviousIndex(const FString& IndexPath)
	{
		TMap<FString, TSharedPtr<FJsonObject>> Entries;
		FString Text;
		TSharedPtr<FJsonObject> Index;
		if (!FFileHelper::LoadFileToString(Text, *IndexPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Index) || !Index.IsValid())
		{
			return Entries;
		}
		const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
		if (Index->TryGetArrayField(TEXT("assets"), Assets))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Assets)
			{
				const TSharedPtr<FJsonObject>* Entry = nullptr;
				FString Asset;
				if (Value->TryGetObject(Entry) && (*Entry)->TryGetStringField(TEXT("asset"), Asset))
				{
					Entries.Add(Asset, *Entry);
				}
			}
		}
		return Entries;
	}

	/** A file name an earlier index.json may name for deletion: a relative .json path that stays inside the output folder. */
	bool IsOwnOutputFile(const FString& RelativePath)
	{
		return RelativePath.EndsWith(TEXT(".json")) && RelativePath != TEXT("index.json") && FPaths::IsRelative(RelativePath) && !RelativePath.Contains(TEXT(".."));
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
	// Reuse the files of unchanged assets from the earlier run into the same -Out folder (keys in its index.json).
	const bool bIncremental = FParse::Param(*Params, TEXT("Incremental"));

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
	UE_LOG(LogAssetGraphExporter, Display, TEXT("Exporting %d asset(s) to %s%s"), Assets.Num(), *OutDir, bIncremental ? TEXT(" (incremental)") : TEXT(""));

	const FString IndexPath = FPaths::Combine(OutDir, TEXT("index.json"));
	const TMap<FString, TSharedPtr<FJsonObject>> PreviousEntries = bIncremental ? ReadPreviousIndex(IndexPath) : TMap<FString, TSharedPtr<FJsonObject>>();
	FAGEExportCache Keys(bFull, Culture);
	double TotalKeySeconds = 0.0;
	int64 TotalKeyPackages = 0;
	int32 Reused = 0;

	TArray<TSharedPtr<FJsonValue>> IndexEntries;
	TSet<FString> WrittenFiles;
	int32 Failures = 0;
	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		const FAssetData& AssetData = Assets[Index];
		const FString RelativePath = RelativeOutputPath(AssetData);
		const FString AssetPath = AssetData.GetSoftObjectPath().ToString();
		WrittenFiles.Add(RelativePath);

		const double KeyStart = FPlatformTime::Seconds();
		int32 KeyPackages = 0;
		const FString Key = Keys.AssetKey(AssetData, &KeyPackages);
		TotalKeySeconds += FPlatformTime::Seconds() - KeyStart;
		TotalKeyPackages += KeyPackages;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset"), AssetPath);
		Entry->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
		Entry->SetStringField(TEXT("file"), RelativePath);
		Entry->SetStringField(TEXT("key"), Key);

		// Same key, same file name and an earlier success: the file on disk is what this run would write.
		const TSharedPtr<FJsonObject>* Previous = PreviousEntries.Find(AssetPath);
		FString PreviousKey, PreviousFile, PreviousStatus;
		if (Previous && (*Previous)->TryGetStringField(TEXT("key"), PreviousKey) && PreviousKey == Key
			&& (*Previous)->TryGetStringField(TEXT("file"), PreviousFile) && PreviousFile == RelativePath
			&& (*Previous)->TryGetStringField(TEXT("status"), PreviousStatus) && PreviousStatus == TEXT("ok")
			&& IFileManager::Get().FileExists(*FPaths::Combine(OutDir, RelativePath)))
		{
			int32 PreviousWarnings = 0;
			if ((*Previous)->TryGetNumberField(TEXT("warnings"), PreviousWarnings))
			{
				Entry->SetNumberField(TEXT("warnings"), PreviousWarnings);
			}
			Entry->SetStringField(TEXT("status"), TEXT("ok"));
			IndexEntries.Add(MakeShared<FJsonValueObject>(Entry));
			++Reused;
			UE_LOG(LogAssetGraphExporter, Display, TEXT("[%d/%d] %s (unchanged)"), Index + 1, Assets.Num(), *RelativePath);
			continue;
		}

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
	SaveJson(IndexPath, FAGEAssetExporter::ToJsonString(IndexJson));

	// Files of assets the earlier run had and this one does not (deleted, renamed, or no longer matched by the query).
	// Only files that index named are touched.
	int32 Removed = 0;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Previous : PreviousEntries)
	{
		FString PreviousFile;
		if (Previous.Value->TryGetStringField(TEXT("file"), PreviousFile) && !WrittenFiles.Contains(PreviousFile) && IsOwnOutputFile(PreviousFile)
			&& IFileManager::Get().Delete(*FPaths::Combine(OutDir, PreviousFile), false, false, true))
		{
			++Removed;
			UE_LOG(LogAssetGraphExporter, Display, TEXT("Removed %s (no longer exported)"), *PreviousFile);
		}
	}

	UE_LOG(LogAssetGraphExporter, Display, TEXT("Done: %d exported, %d failed, %d query error(s)."), Assets.Num() - Failures, Failures, Errors.Num());
	if (bIncremental)
	{
		UE_LOG(LogAssetGraphExporter, Display, TEXT("Incremental: %d unchanged (reused), %d exported again, %d old file(s) removed."), Reused, Assets.Num() - Reused, Removed);
	}
	UE_LOG(LogAssetGraphExporter, Display, TEXT("[Timing] Keys %.2f s (%.1f packages per asset), load %.2f s, export %.2f s, commandlet total %.2f s (process total %.2f s)"),
		TotalKeySeconds, Assets.Num() > 0 ? double(TotalKeyPackages) / Assets.Num() : 0.0,
		TotalLoadSeconds, TotalExportSeconds, FPlatformTime::Seconds() - MainStart, FPlatformTime::Seconds() - GStartTime);
	return (Failures > 0 || Errors.Num() > 0) ? 1 : 0;
}
