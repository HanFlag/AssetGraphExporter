// Asset Graph Exporter

#include "AssetGraphExportCommandlet.h"
#include "AGEAssetQuery.h"
#include "AGEAssetExporter.h"
#include "AGEExportCache.h"
#include "AGEExportContext.h"
#include "AGELog.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
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

	TArray<TSharedPtr<FJsonValue>> ToJsonStrings(const TArray<FString>& Strings)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& String : Strings)
		{
			Values.Add(MakeShared<FJsonValueString>(String));
		}
		return Values;
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
	for (const FString& Error : Errors)
	{
		UE_LOG(LogAssetGraphExporter, Error, TEXT("%s"), *Error);
	}
	UE_LOG(LogAssetGraphExporter, Display, TEXT("Exporting %d asset(s) to %s%s"), Assets.Num(), *OutDir, bIncremental ? TEXT(" (incremental)") : TEXT(""));

	FAGEExportRun Run;
	Run.OutDir = OutDir;
	Run.bFull = bFull;
	Run.bReuse = bIncremental;
	Run.Culture = Culture;
	AGE::ExportCache::Export(Assets, Run);

	TSharedRef<FJsonObject> IndexJson = MakeShared<FJsonObject>();
	IndexJson->SetNumberField(TEXT("schemaVersion"), AGE::SchemaVersion);
	TSharedRef<FJsonObject> QueryJson = MakeShared<FJsonObject>();
	QueryJson->SetArrayField(TEXT("assets"), ToJsonStrings(Query.Assets));
	QueryJson->SetArrayField(TEXT("paths"), ToJsonStrings(Query.Paths));
	QueryJson->SetArrayField(TEXT("classes"), ToJsonStrings(Query.Classes));
	QueryJson->SetBoolField(TEXT("full"), bFull);
	QueryJson->SetStringField(TEXT("culture"), Culture);
	IndexJson->SetObjectField(TEXT("query"), QueryJson);
	IndexJson->SetArrayField(TEXT("assets"), Run.Entries);
	if (Errors.Num() > 0)
	{
		IndexJson->SetArrayField(TEXT("errors"), ToJsonStrings(Errors));
	}
	AGE::ExportCache::SaveJson(FPaths::Combine(OutDir, TEXT("index.json")), FAGEAssetExporter::ToJsonString(IndexJson));

	// Files of assets the earlier run had and this one does not (deleted, renamed, or no longer matched by the query).
	// Only files that index named are touched.
	TSet<FString> WrittenFiles;
	for (const FAssetData& Asset : Assets)
	{
		WrittenFiles.Add(AGE::ExportCache::RelativeOutputPath(Asset));
	}
	int32 Removed = 0;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Previous : Run.PreviousEntries)
	{
		FString PreviousFile;
		if (Previous.Value->TryGetStringField(TEXT("file"), PreviousFile) && !WrittenFiles.Contains(PreviousFile) && IsOwnOutputFile(PreviousFile)
			&& IFileManager::Get().Delete(*FPaths::Combine(OutDir, PreviousFile), false, false, true))
		{
			++Removed;
			UE_LOG(LogAssetGraphExporter, Display, TEXT("Removed %s (no longer exported)"), *PreviousFile);
		}
	}

	UE_LOG(LogAssetGraphExporter, Display, TEXT("Done: %d exported, %d failed, %d query error(s)."), Assets.Num() - Run.Failures, Run.Failures, Errors.Num());
	if (bIncremental)
	{
		UE_LOG(LogAssetGraphExporter, Display, TEXT("Incremental: %d unchanged (reused), %d exported again, %d old file(s) removed."), Run.Reused, Assets.Num() - Run.Reused, Removed);
	}
	UE_LOG(LogAssetGraphExporter, Display, TEXT("[Timing] Keys %.2f s (%.1f packages per asset), load %.2f s, export %.2f s, commandlet total %.2f s (process total %.2f s)"),
		Run.KeySeconds, Assets.Num() > 0 ? double(Run.KeyPackages) / Assets.Num() : 0.0,
		Run.LoadSeconds, Run.ExportSeconds, FPlatformTime::Seconds() - MainStart, FPlatformTime::Seconds() - GStartTime);
	return (Run.Failures > 0 || Errors.Num() > 0) ? 1 : 0;
}
