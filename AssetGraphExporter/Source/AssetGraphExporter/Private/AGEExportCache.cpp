// Asset Graph Exporter

#include "AGEExportCache.h"
#include "AGEAssetExporter.h"
#include "AGEExportContext.h"
#include "AGELog.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IO/IoHash.h"
#include "Misc/AssetRegistryInterface.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"

namespace AGE::ExportCacheDetail
{
	FString HashText(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		return FSHA1::HashBuffer(Utf8.Get(), Utf8.Length()).ToString();
	}

	/**
	 * Number and newest file time of the DLLs that can change what is exported: the project's modules, its plugins' modules and
	 * this plugin's modules. The engine's BuildId cannot be used: with an installed engine it stays the same when the project is rebuilt.
	 */
	FString CodeStamp()
	{
		TArray<FString> Directories = { FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries")), FPaths::ProjectPluginsDir() };
		const FString OwnModule = FModuleManager::Get().GetModuleFilename(TEXT("AssetGraphExporter"));
		if (!OwnModule.IsEmpty())
		{
			Directories.Add(FPaths::GetPath(OwnModule));
		}

		int32 Count = 0;
		FDateTime Newest = FDateTime::MinValue();
		for (const FString& Directory : Directories)
		{
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*.dll"), true, false);
			for (const FString& File : Files)
			{
				++Count;
				Newest = FMath::Max(Newest, IFileManager::Get().GetTimeStamp(*File));
			}
		}
		return FString::Printf(TEXT("%d dlls, newest %lld"), Count, Newest.GetTicks());
	}
}

FAGEExportCache::FAGEExportCache(bool bFull, const FString& Culture)
{
	using namespace AGE::ExportCacheDetail;
	SettingsKey = FString::Printf(TEXT("exporter %s|schema %d|full %d|culture %s|engine %s|code %s"),
		AGE::ExporterVersion, AGE::SchemaVersion, bFull ? 1 : 0, *Culture, *FEngineVersion::Current().ToString(), *CodeStamp());
}

const FString& FAGEExportCache::PackageHash(FName PackageName)
{
	if (const FString* Known = PackageHashes.Find(PackageName))
	{
		return *Known;
	}

	FString Hash;
	const TOptional<FAssetPackageData> Data = IAssetRegistry::GetChecked().GetAssetPackageDataCopy(PackageName);
	if (Data.IsSet() && !Data->GetPackageSavedHash().IsZero())
	{
		Hash = LexToString(Data->GetPackageSavedHash());
	}
	else
	{
		FString Filename;
		Hash = FPackageName::DoesPackageExist(PackageName.ToString(), &Filename)
			? FString::Printf(TEXT("size %lld, time %lld"), IFileManager::Get().FileSize(*Filename), IFileManager::Get().GetTimeStamp(*Filename).GetTicks())
			: FString(TEXT("missing"));
	}
	return PackageHashes.Add(PackageName, MoveTemp(Hash));
}

FString FAGEExportCache::AssetKey(const FAssetData& Asset, int32* OutPackageCount)
{
	const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();

	// Every package reached through hard dependencies. C++ modules (/Script/...) are covered by the code stamp instead.
	TSet<FName> Packages = { Asset.PackageName };
	TArray<FName> Pending = { Asset.PackageName };
	TArray<FName> Dependencies;
	while (!Pending.IsEmpty())
	{
		Dependencies.Reset();
		AssetRegistry.GetDependencies(Pending.Pop(), Dependencies, UE::AssetRegistry::EDependencyCategory::Package,
			UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard));
		for (const FName Dependency : Dependencies)
		{
			bool bAlreadyIn = false;
			if (!FPackageName::IsScriptPackage(Dependency.ToString()))
			{
				Packages.Add(Dependency, &bAlreadyIn);
				if (!bAlreadyIn)
				{
					Pending.Add(Dependency);
				}
			}
		}
	}

	TArray<FName> Sorted = Packages.Array();
	Sorted.Sort(FNameLexicalLess());
	FString Text = SettingsKey + TEXT("\n") + Asset.GetSoftObjectPath().ToString();
	for (const FName Package : Sorted)
	{
		Text += FString::Printf(TEXT("\n%s=%s"), *Package.ToString(), *PackageHash(Package));
	}
	if (OutPackageCount)
	{
		*OutPackageCount = Sorted.Num();
	}
	return AGE::ExportCacheDetail::HashText(Text);
}

FString AGE::ExportCache::RelativeOutputPath(const FAssetData& Asset)
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

bool AGE::ExportCache::SaveJson(const FString& FilePath, const FString& Text)
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
	return FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

TMap<FString, TSharedPtr<FJsonObject>> AGE::ExportCache::ReadIndex(const FString& IndexPath)
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

void AGE::ExportCache::Export(const TArray<FAssetData>& Assets, FAGEExportRun& Run)
{
	// Garbage is collected every N assets so a large run does not keep every loaded asset in memory.
	constexpr int32 GarbageCollectInterval = 20;

	if (Run.bReuse)
	{
		Run.PreviousEntries = ReadIndex(FPaths::Combine(Run.OutDir, TEXT("index.json")));
	}
	FAGEExportCache Keys(Run.bFull, Run.Culture);

	for (int32 Index = 0; Index < Assets.Num(); ++Index)
	{
		const FAssetData& AssetData = Assets[Index];
		const FString RelativePath = RelativeOutputPath(AssetData);
		const FString AssetPath = AssetData.GetSoftObjectPath().ToString();

		const double KeyStart = FPlatformTime::Seconds();
		int32 KeyPackages = 0;
		const FString Key = Keys.AssetKey(AssetData, &KeyPackages);
		Run.KeySeconds += FPlatformTime::Seconds() - KeyStart;
		Run.KeyPackages += KeyPackages;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset"), AssetPath);
		Entry->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
		Entry->SetStringField(TEXT("file"), RelativePath);
		Entry->SetStringField(TEXT("key"), Key);

		// Same key, same file name and an earlier success: the file on disk is what this run would write.
		const TSharedPtr<FJsonObject>* Previous = Run.PreviousEntries.Find(AssetPath);
		FString PreviousKey, PreviousFile, PreviousStatus;
		if (Previous && (*Previous)->TryGetStringField(TEXT("key"), PreviousKey) && PreviousKey == Key
			&& (*Previous)->TryGetStringField(TEXT("file"), PreviousFile) && PreviousFile == RelativePath
			&& (*Previous)->TryGetStringField(TEXT("status"), PreviousStatus) && PreviousStatus == TEXT("ok")
			&& IFileManager::Get().FileExists(*FPaths::Combine(Run.OutDir, RelativePath)))
		{
			int32 PreviousWarnings = 0;
			if ((*Previous)->TryGetNumberField(TEXT("warnings"), PreviousWarnings))
			{
				Entry->SetNumberField(TEXT("warnings"), PreviousWarnings);
			}
			Entry->SetStringField(TEXT("status"), TEXT("ok"));
			Run.Entries.Add(MakeShared<FJsonValueObject>(Entry));
			++Run.Reused;
			UE_LOG(LogAssetGraphExporter, Display, TEXT("[%d/%d] %s (unchanged)"), Index + 1, Assets.Num(), *RelativePath);
			continue;
		}

		FString Error;
		const double LoadStart = FPlatformTime::Seconds();
		const UObject* Asset = Run.Load ? Run.Load(AssetData) : AssetData.GetAsset();
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
			Context.bDiffOnly = !Run.bFull;
			const TSharedRef<FJsonObject> Json = FAGEAssetExporter::ExportAsset(*Asset, Context);
			if (!SaveJson(FPaths::Combine(Run.OutDir, RelativePath), FAGEAssetExporter::ToJsonString(Json)))
			{
				Error = TEXT("Failed to write file");
			}
			ExportSeconds = FPlatformTime::Seconds() - ExportStart;
			Run.ExportSeconds += ExportSeconds;
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
			++Run.Failures;
			Entry->SetStringField(TEXT("status"), TEXT("failed"));
			Entry->SetStringField(TEXT("error"), Error);
			UE_LOG(LogAssetGraphExporter, Error, TEXT("[%d/%d] %s: %s"), Index + 1, Assets.Num(), *AssetPath, *Error);
		}
		Run.Entries.Add(MakeShared<FJsonValueObject>(Entry));
		Run.LoadSeconds += LoadSeconds;

		if ((Index + 1) % GarbageCollectInterval == 0)
		{
			CollectGarbage(RF_NoFlags);
		}
	}
}
