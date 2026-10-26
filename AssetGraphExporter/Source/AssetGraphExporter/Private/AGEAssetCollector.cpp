// Asset Graph Exporter

#include "AGEAssetQuery.h"
#include "AGELog.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"

UClass* AGE::ResolveClass(const FString& Name)
{
	if (Name.Contains(TEXT("/")))
	{
		return LoadObject<UClass>(nullptr, *Name);
	}
	UClass* Class = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst);
	// Accept C++ spellings too: UGameplayAbility, AActor.
	if (!Class && Name.Len() > 1 && (Name[0] == TEXT('U') || Name[0] == TEXT('A')))
	{
		Class = FindFirstObject<UClass>(*Name.RightChop(1), EFindFirstObjectOptions::NativeFirst);
	}
	return Class;
}

namespace
{
	bool IsChildOfAny(const UClass* Class, const TArray<UClass*>& Classes)
	{
		if (!Class)
		{
			return false;
		}
		for (const UClass* Filter : Classes)
		{
			if (Class->IsChildOf(Filter))
			{
				return true;
			}
		}
		return false;
	}

	UClass* ClassFromTag(const FAssetData& Asset, FName Tag)
	{
		FString Value;
		if (!Asset.GetTagValue(Tag, Value) || Value.IsEmpty())
		{
			return nullptr;
		}
		return LoadObject<UClass>(nullptr, *FPackageName::ExportTextPathToObjectPath(Value));
	}

	bool MatchesClassFilter(const FAssetData& Asset, const TArray<UClass*>& Classes)
	{
		if (Classes.IsEmpty())
		{
			return true;
		}
		// The asset's own class: -Class=Blueprint, -Class=StateTree
		if (IsChildOfAny(FindObject<UClass>(nullptr, *Asset.AssetClassPath.ToString()), Classes))
		{
			return true;
		}
		// Blueprints by what they derive from: -Class=GameplayAbility finds GA blueprints.
		// Registry tags written by UBlueprint; the native parent is checked first because it needs no load.
		static const FName NativeParentClassTag(TEXT("NativeParentClass"));
		static const FName ParentClassTag(TEXT("ParentClass"));
		return IsChildOfAny(ClassFromTag(Asset, NativeParentClassTag), Classes)
			|| IsChildOfAny(ClassFromTag(Asset, ParentClassTag), Classes);
	}

	FString TrimFolder(FString Path)
	{
		Path.TrimStartAndEndInline();
		while (Path.Len() > 1 && Path.EndsWith(TEXT("/")))
		{
			Path.LeftChopInline(1);
		}
		return Path;
	}
}

TArray<FAssetData> AGE::CollectAssets(const FAGEAssetQuery& Query, TArray<FString>& OutErrors)
{
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	if (Query.bRescan)
	{
		// Commandlets do not scan in the background; make sure the registry is complete before querying.
		const double ScanStart = FPlatformTime::Seconds();
		AssetRegistry.SearchAllAssets(true);
		UE_LOG(LogAssetGraphExporter, Display, TEXT("[Timing] Asset registry scan: %.2f s"), FPlatformTime::Seconds() - ScanStart);
	}
	else if (AssetRegistry.IsLoadingAssets())
	{
		OutErrors.Add(TEXT("The asset registry is still scanning (editor just started); results may be incomplete."));
	}

	TArray<UClass*> Classes;
	for (const FString& ClassName : Query.Classes)
	{
		if (UClass* Class = AGE::ResolveClass(ClassName))
		{
			Classes.Add(Class);
		}
		else
		{
			OutErrors.Add(FString::Printf(TEXT("Class not found: %s"), *ClassName));
		}
	}
	if (!Query.Classes.IsEmpty() && Classes.IsEmpty())
	{
		return {};
	}

	TArray<FAssetData> Candidates;
	for (const FString& AssetArg : Query.Assets)
	{
		const FString PackageName = FPackageName::ObjectPathToPackageName(TrimFolder(AssetArg));
		TArray<FAssetData> Found;
		AssetRegistry.GetAssetsByPackageName(FName(*PackageName), Found);
		Found.RemoveAll([](const FAssetData& Asset) { return !Asset.IsTopLevelAsset(); });
		if (Found.IsEmpty())
		{
			OutErrors.Add(FString::Printf(TEXT("Asset not found: %s"), *AssetArg));
		}
		Candidates.Append(Found);
	}
	for (const FString& PathArg : Query.Paths)
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*TrimFolder(PathArg)));
		Filter.bRecursivePaths = true;
		TArray<FAssetData> Found;
		AssetRegistry.GetAssets(Filter, Found);
		Found.RemoveAll([](const FAssetData& Asset) { return !Asset.IsTopLevelAsset(); });
		if (Found.IsEmpty())
		{
			OutErrors.Add(FString::Printf(TEXT("No assets under: %s"), *PathArg));
		}
		Candidates.Append(Found);
	}

	TSet<FSoftObjectPath> Seen;
	TArray<FAssetData> Result;
	for (const FAssetData& Asset : Candidates)
	{
		bool bAlreadySeen = false;
		Seen.Add(Asset.GetSoftObjectPath(), &bAlreadySeen);
		if (!bAlreadySeen && MatchesClassFilter(Asset, Classes))
		{
			Result.Add(Asset);
		}
	}
	// Stable order so repeated runs produce the same index.
	Result.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString();
	});
	return Result;
}
