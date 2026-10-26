// Asset Graph Exporter

#include "AGEExportCache.h"
#include "AGEExportContext.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "IO/IoHash.h"
#include "Misc/AssetRegistryInterface.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"

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
