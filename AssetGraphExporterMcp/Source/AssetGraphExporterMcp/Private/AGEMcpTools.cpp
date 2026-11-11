// Asset Graph Exporter

#include "AGEMcpTools.h"
#include "AGEAssetQuery.h"
#include "AGEAssetExporter.h"
#include "AGEExportCache.h"
#include "AGEExportContext.h"
#include "AGEJsonDiff.h"
#include "AGEJsonQuery.h"
#include "AGEMcpActivity.h"
#include "AGEMcpLevel.h"
#include "AGEMcpRevision.h"
#include "AGEMcpState.h"
#include "AGESummary.h"
#include "AGEUsages.h"
#include "IModelContextProtocolTool.h"
#include "ModelContextProtocolToolResults.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Regex.h"
#include "Misc/App.h"
#include "Misc/AssetRegistryInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

// Named (not anonymous) so these helpers cannot clash with other files in a unity build.
namespace AGE::McpToolsDetail
{
	/** age_get_asset returns an outline instead of JSON longer than this, unless the caller raises maxChars. */
	constexpr int32 DefaultMaxChars = 40000;
	constexpr int32 DefaultFindLimit = 100;
	constexpr int32 MaxFindLimit = 1000;
	constexpr int32 DefaultReferenceLimit = 200;
	constexpr int32 MaxReferenceLimit = 2000;
	constexpr int32 MaxReferenceDepth = 3;

	using FToolFunction = TFunction<FModelContextProtocolToolResult(const FJsonObject& Arguments)>;

	/** One age_* tool: name, description, argument schema and the function that answers it. */
	class FTool : public IModelContextProtocolTool
	{
	public:
		FTool(const TCHAR* InName, const TCHAR* InDescription, const TSharedRef<FJsonObject>& InSchema, FToolFunction InFunction)
			: Name(InName)
			, Description(InDescription)
			, Schema(InSchema)
			, Function(MoveTemp(InFunction))
		{
		}

		virtual FString GetName() const override { return Name; }
		virtual FString GetDescription() const override { return Description; }
		virtual TSharedPtr<FJsonObject> GetInputJsonSchema() const override { return Schema; }

		virtual FModelContextProtocolToolResult Run(const TSharedPtr<FJsonObject>& Params) override
		{
			AGE::Mcp::NoteActivity();
			const double Start = FPlatformTime::Seconds();
			const TSharedRef<FJsonObject> NoArguments = MakeShared<FJsonObject>();
			const FJsonObject& Arguments = Params.IsValid() ? *Params : *NoArguments;
			FModelContextProtocolToolResult Result = Function(Arguments);
			const double Milliseconds = (FPlatformTime::Seconds() - Start) * 1000.0;
			UE_LOG(LogAssetGraphExporterMcp, Display, TEXT("%s (%.0f ms)"), *Name, Milliseconds);
			AGE::Mcp::Activity::RecordToolCall(Name, Arguments, Result, Milliseconds);
			return Result;
		}

	private:
		FString Name;
		FString Description;
		TSharedPtr<FJsonObject> Schema;
		FToolFunction Function;
	};

	/** Builds the JSON Schema of a tool's arguments. */
	class FSchema
	{
	public:
		FSchema& Add(const TCHAR* Name, const TCHAR* Type, const TCHAR* Text, bool bRequired = false)
		{
			TSharedRef<FJsonObject> Property = MakeShared<FJsonObject>();
			Property->SetStringField(TEXT("type"), Type);
			Property->SetStringField(TEXT("description"), Text);
			Properties->SetObjectField(Name, Property);
			if (bRequired)
			{
				Required.Add(MakeShared<FJsonValueString>(Name));
			}
			return *this;
		}

		TSharedRef<FJsonObject> Build() const
		{
			TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			Schema->SetObjectField(TEXT("properties"), Properties);
			if (Required.Num() > 0)
			{
				Schema->SetArrayField(TEXT("required"), Required);
			}
			return Schema;
		}

	private:
		TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Required;
	};

	FString StringArgument(const FJsonObject& Arguments, const TCHAR* Key)
	{
		FString Value;
		Arguments.TryGetStringField(Key, Value);
		return Value.TrimStartAndEnd();
	}

	bool BoolArgument(const FJsonObject& Arguments, const TCHAR* Key, bool bDefault)
	{
		bool bValue = bDefault;
		Arguments.TryGetBoolField(Key, bValue);
		return bValue;
	}

	int32 IntArgument(const FJsonObject& Arguments, const TCHAR* Key, int32 Default)
	{
		int32 Value = Default;
		Arguments.TryGetNumberField(Key, Value);
		return Value;
	}

	FModelContextProtocolToolResult TextResult(const FString& Text)
	{
		return UE::ModelContextProtocol::MakeTextResult(Text);
	}

	FModelContextProtocolToolResult ErrorResult(const FString& Text)
	{
		// Every failure carries the feedback hint, so the assistant sees it exactly when a report is most useful.
		return UE::ModelContextProtocol::MakeErrorResult(Text + TEXT("\n(If this blocks the task or looks wrong, tell the user and report it with age_feedback.)"));
	}

	// ---- Loading ----

	/** File time of every package the serve commandlet has read, so a package saved again later is reloaded. */
	TMap<FName, FDateTime>& PackageReadTimes()
	{
		static TMap<FName, FDateTime> Times;
		return Times;
	}

	struct FLoadedAsset
	{
		UObject* Asset = nullptr;
		/** First line of every answer: where the data came from. */
		FString Source;
		FString Error;
	};

	/** Serve mode only: reload the package if its file changed since this process read it. */
	FDateTime RefreshFromDisk(const FAssetData& Data)
	{
		const FString PackageName = Data.PackageName.ToString();
		FString Filename;
		const FDateTime FileTime = FPackageName::DoesPackageExist(PackageName, &Filename) ? IFileManager::Get().GetTimeStamp(*Filename) : FDateTime::MinValue();

		if (UPackage* Package = FindPackage(nullptr, *PackageName))
		{
			const FDateTime* ReadTime = PackageReadTimes().Find(Data.PackageName);
			// Not read by a tool yet but already in memory (e.g. loaded as a dependency): trust it only if the file is older than this process.
			const bool bStale = ReadTime ? FileTime != *ReadTime : FileTime > AGE::Mcp::GetServeStartUtc();
			if (bStale)
			{
				FText ReloadError;
				UPackageTools::ReloadPackages({ Package }, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
				if (!ReloadError.IsEmpty())
				{
					UE_LOG(LogAssetGraphExporterMcp, Warning, TEXT("Reloading %s: %s"), *PackageName, *ReloadError.ToString());
				}
				else
				{
					UE_LOG(LogAssetGraphExporterMcp, Display, TEXT("Reloaded %s (the file changed on disk)"), *PackageName);
				}
			}
		}
		PackageReadTimes().Add(Data.PackageName, FileTime);
		return FileTime;
	}

	FLoadedAsset LoadAsset(const FString& AssetArgument)
	{
		FLoadedAsset Loaded;
		if (AssetArgument.IsEmpty())
		{
			Loaded.Error = TEXT("'asset' is required, e.g. /Game/Folder/BP_Thing. Use age_find_assets to look up paths.");
			return Loaded;
		}

		FAGEAssetQuery Query;
		Query.Assets.Add(AssetArgument);
		Query.bRescan = false;
		TArray<FString> Errors;
		TArray<FAssetData> Found = AGE::CollectAssets(Query, Errors);
		if (Found.IsEmpty() && AGE::Mcp::IsServeMode())
		{
			// The file may have been added after the server scanned the project.
			Query.bRescan = true;
			Errors.Reset();
			Found = AGE::CollectAssets(Query, Errors);
		}
		if (Found.IsEmpty())
		{
			Loaded.Error = FString::Printf(TEXT("Asset not found: %s. Use age_find_assets to look up paths."), *AssetArgument);
			return Loaded;
		}

		const FAssetData& Data = Found[0];
		if (AGE::Mcp::IsServeMode())
		{
			const FDateTime FileTime = RefreshFromDisk(Data);
			Loaded.Source = FString::Printf(TEXT("source: saved file (%s UTC)"), *FileTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
		}

		Loaded.Asset = Data.GetAsset();
		if (!Loaded.Asset)
		{
			Loaded.Error = FString::Printf(TEXT("Failed to load %s."), *Data.GetSoftObjectPath().ToString());
			return Loaded;
		}
		if (!AGE::Mcp::IsServeMode())
		{
			const UPackage* Package = Loaded.Asset->GetPackage();
			Loaded.Source = Package && Package->IsDirty() ? TEXT("source: editor (has unsaved changes)") : TEXT("source: editor");
		}
		return Loaded;
	}

	TSharedRef<FJsonObject> ExportAsset(const UObject& Asset, bool bFull)
	{
		FAGEExportContext Context;
		Context.bDiffOnly = !bFull;
		return FAGEAssetExporter::ExportAsset(Asset, Context);
	}

	FString HandlerOf(const FJsonObject& Export)
	{
		FString Handler;
		Export.TryGetStringField(TEXT("handler"), Handler);
		return Handler;
	}

	/** Shared body of the overview tools: load, export, check the asset kind, summarize. */
	FModelContextProtocolToolResult SummaryResult(const FJsonObject& Arguments, const TCHAR* Handler, const TCHAR* KindText, TFunctionRef<FString(const FJsonObject&)> Summarize)
	{
		const FLoadedAsset Loaded = LoadAsset(StringArgument(Arguments, TEXT("asset")));
		if (!Loaded.Asset)
		{
			return ErrorResult(Loaded.Error);
		}
		const TSharedRef<FJsonObject> Export = ExportAsset(*Loaded.Asset, false);
		if (HandlerOf(*Export) != Handler)
		{
			return ErrorResult(FString::Printf(TEXT("%s is a %s, not %s."), *Loaded.Asset->GetPathName(), *Loaded.Asset->GetClass()->GetName(), KindText));
		}
		return TextResult(Loaded.Source + TEXT("\n") + Summarize(*Export));
	}

	// ---- Tools ----

	FModelContextProtocolToolResult FindAssets(const FJsonObject& Arguments)
	{
		const FString Name = StringArgument(Arguments, TEXT("name"));
		FString Path = StringArgument(Arguments, TEXT("path"));
		if (Path.IsEmpty())
		{
			Path = TEXT("/Game");
		}
		const int32 Limit = FMath::Clamp(IntArgument(Arguments, TEXT("limit"), DefaultFindLimit), 1, MaxFindLimit);

		FAGEAssetQuery Query;
		Query.Paths.Add(Path);
		StringArgument(Arguments, TEXT("class")).ParseIntoArray(Query.Classes, TEXT(","), true);
		for (FString& Class : Query.Classes)
		{
			Class.TrimStartAndEndInline();
		}
		// The editor keeps the registry current; the serve commandlet rescans so new files show up.
		Query.bRescan = AGE::Mcp::IsServeMode();

		TArray<FString> Errors;
		TArray<FAssetData> Assets = AGE::CollectAssets(Query, Errors);
		if (!Name.IsEmpty())
		{
			Assets.RemoveAll([&Name](const FAssetData& Asset)
			{
				return !Asset.AssetName.ToString().Contains(Name, ESearchCase::IgnoreCase);
			});
		}

		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("%d asset(s) under %s%s%s"), Assets.Num(), *Path,
			Name.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named *%s*"), *Name),
			Query.Classes.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" of class %s"), *FString::Join(Query.Classes, TEXT(", ")))));
		const int32 Shown = FMath::Min(Assets.Num(), Limit);
		for (int32 Index = 0; Index < Shown; ++Index)
		{
			Lines.Add(FString::Printf(TEXT("%s  [%s]"), *Assets[Index].GetSoftObjectPath().ToString(), *Assets[Index].AssetClassPath.GetAssetName().ToString()));
		}
		if (Assets.Num() > Shown)
		{
			Lines.Add(FString::Printf(TEXT("... %d more. Narrow with name/path/class or raise limit."), Assets.Num() - Shown));
		}
		for (const FString& Error : Errors)
		{
			Lines.Add(TEXT("note: ") + Error);
		}
		return TextResult(FString::Join(Lines, TEXT("\n")));
	}

	FModelContextProtocolToolResult GetAsset(const FJsonObject& Arguments)
	{
		const FLoadedAsset Loaded = LoadAsset(StringArgument(Arguments, TEXT("asset")));
		if (!Loaded.Asset)
		{
			return ErrorResult(Loaded.Error);
		}
		const FString SelectPath = StringArgument(Arguments, TEXT("select"));
		const int32 MaxChars = FMath::Max(IntArgument(Arguments, TEXT("maxChars"), DefaultMaxChars), 1000);

		const TSharedPtr<FJsonValue> Root = MakeShared<FJsonValueObject>(ExportAsset(*Loaded.Asset, BoolArgument(Arguments, TEXT("full"), false)));
		FString SelectError;
		const TSharedPtr<FJsonValue> Selected = AGE::JsonQuery::Select(Root, SelectPath, SelectError);
		if (!Selected.IsValid())
		{
			return ErrorResult(SelectError);
		}

		const FString Json = AGE::JsonQuery::ToCompactString(Selected);
		if (Json.Len() <= MaxChars)
		{
			return TextResult(Loaded.Source + TEXT("\n") + Json);
		}
		return TextResult(FString::Printf(
			TEXT("%s\nThe result is %d chars (maxChars %d), so this is an outline. Call again with 'select' set to one of these paths, or raise maxChars.\n%s"),
			*Loaded.Source, Json.Len(), MaxChars, *AGE::JsonQuery::Outline(Selected, SelectPath)));
	}

	FModelContextProtocolToolResult StateTreeOverview(const FJsonObject& Arguments)
	{
		return SummaryResult(Arguments, TEXT("StateTree"), TEXT("a StateTree"), [](const FJsonObject& Export)
		{
			return AGE::Summary::StateTreeOverview(Export);
		});
	}

	FModelContextProtocolToolResult StateTreeBindings(const FJsonObject& Arguments)
	{
		const FString Filter = StringArgument(Arguments, TEXT("filter"));
		return SummaryResult(Arguments, TEXT("StateTree"), TEXT("a StateTree"), [&Filter](const FJsonObject& Export)
		{
			return AGE::Summary::StateTreeBindings(Export, Filter);
		});
	}

	FModelContextProtocolToolResult BlueprintOverview(const FJsonObject& Arguments)
	{
		return SummaryResult(Arguments, TEXT("Blueprint"), TEXT("a Blueprint (for Animation Blueprints use age_animbp_overview)"), [](const FJsonObject& Export)
		{
			return AGE::Summary::BlueprintOverview(Export);
		});
	}

	FModelContextProtocolToolResult AnimBlueprintOverview(const FJsonObject& Arguments)
	{
		return SummaryResult(Arguments, TEXT("AnimBlueprint"), TEXT("an Animation Blueprint"), [](const FJsonObject& Export)
		{
			return AGE::Summary::AnimBlueprintOverview(Export);
		});
	}

	FModelContextProtocolToolResult NiagaraOverview(const FJsonObject& Arguments)
	{
		const FString Emitter = StringArgument(Arguments, TEXT("emitter"));
		const bool bBrief = Emitter.IsEmpty() && !BoolArgument(Arguments, TEXT("full"), false);
		return SummaryResult(Arguments, TEXT("NiagaraSystem"), TEXT("a Niagara System"), [&Emitter, bBrief](const FJsonObject& Export)
		{
			return AGE::Summary::NiagaraOverview(Export, Emitter, bBrief);
		});
	}

	// ---- References ----

	/** Direct package dependencies (or referencers) of one package, sorted by name, with the properties of all links to each package combined. */
	TMap<FName, UE::AssetRegistry::EDependencyProperty> DirectLinks(const IAssetRegistry& AssetRegistry, FName PackageName, bool bReferencers)
	{
		TArray<FAssetDependency> Links;
		const FAssetIdentifier Id(PackageName);
		if (bReferencers)
		{
			AssetRegistry.GetReferencers(Id, Links, UE::AssetRegistry::EDependencyCategory::Package);
		}
		else
		{
			AssetRegistry.GetDependencies(Id, Links, UE::AssetRegistry::EDependencyCategory::Package);
		}

		TMap<FName, UE::AssetRegistry::EDependencyProperty> Result;
		for (const FAssetDependency& Link : Links)
		{
			if (Link.AssetId.PackageName != PackageName)
			{
				Result.FindOrAdd(Link.AssetId.PackageName) |= Link.Properties;
			}
		}
		Result.KeySort(FNameLexicalLess());
		return Result;
	}

	/** Class of the package's main asset (the one named like the package), or of its first asset. */
	FString PackageClass(const IAssetRegistry& AssetRegistry, FName PackageName)
	{
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPackageName(PackageName, Assets, true);
		if (Assets.IsEmpty())
		{
			return TEXT("not found");
		}
		const FName ShortName(FPackageName::GetShortName(PackageName));
		const FAssetData* Main = Assets.FindByPredicate([ShortName](const FAssetData& Asset) { return Asset.AssetName == ShortName; });
		return (Main ? *Main : Assets[0]).AssetClassPath.GetAssetName().ToString();
	}

	FString LinkText(UE::AssetRegistry::EDependencyProperty Properties)
	{
		FString Text = EnumHasAnyFlags(Properties, UE::AssetRegistry::EDependencyProperty::Hard) ? TEXT("hard") : TEXT("soft");
		if (!EnumHasAnyFlags(Properties, UE::AssetRegistry::EDependencyProperty::Game))
		{
			Text += TEXT(", editor-only");
		}
		return Text;
	}

	/** Writes one direction of age_references as an indented tree; packages already written are only named again. */
	struct FReferenceTree
	{
		const IAssetRegistry& AssetRegistry;
		bool bReferencers = false;
		int32 MaxDepth = 1;
		int32 MaxLines = DefaultReferenceLimit;
		TArray<FString>& Lines;
		int32 CutLines = 0;
		TSet<FName> Listed;

		void Add(FString Line)
		{
			if (Lines.Num() < MaxLines)
			{
				Lines.Add(MoveTemp(Line));
			}
			else
			{
				++CutLines;
			}
		}

		void Write(FName PackageName, int32 Depth)
		{
			const FString Indent = FString::ChrN(Depth * 2, TEXT(' '));
			TArray<FString> Native;
			for (const TPair<FName, UE::AssetRegistry::EDependencyProperty>& Link : DirectLinks(AssetRegistry, PackageName, bReferencers))
			{
				const FString Name = Link.Key.ToString();
				// C++ modules go on one line: otherwise /Script/Engine and the like push the assets out of view.
				if (FPackageName::IsScriptPackage(Name))
				{
					Native.Add(Name);
					continue;
				}
				bool bAlreadyListed = false;
				Listed.Add(Link.Key, &bAlreadyListed);
				Add(FString::Printf(TEXT("%s%s  [%s]  %s%s"), *Indent, *Name, *PackageClass(AssetRegistry, Link.Key), *LinkText(Link.Value),
					bAlreadyListed ? TEXT("  (listed above)") : TEXT("")));
				if (!bAlreadyListed && Depth < MaxDepth)
				{
					Write(Link.Key, Depth + 1);
				}
			}
			if (!Native.IsEmpty())
			{
				Add(Indent + TEXT("native: ") + FString::Join(Native, TEXT(", ")));
			}
		}
	};

	FModelContextProtocolToolResult References(const FJsonObject& Arguments)
	{
		const FString AssetArgument = StringArgument(Arguments, TEXT("asset"));
		if (AssetArgument.IsEmpty())
		{
			return ErrorResult(TEXT("'asset' is required, e.g. /Game/Folder/BP_Thing or /Script/ModuleName. Use age_find_assets to look up paths."));
		}
		FString Direction = StringArgument(Arguments, TEXT("direction")).ToLower();
		if (Direction.IsEmpty())
		{
			Direction = TEXT("both");
		}
		if (Direction != TEXT("both") && Direction != TEXT("uses") && Direction != TEXT("usedby"))
		{
			return ErrorResult(TEXT("'direction' must be uses, usedBy or both."));
		}
		const int32 MaxDepth = FMath::Clamp(IntArgument(Arguments, TEXT("depth"), 1), 1, MaxReferenceDepth);
		const int32 Limit = FMath::Clamp(IntArgument(Arguments, TEXT("limit"), DefaultReferenceLimit), 1, MaxReferenceLimit);

		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		if (AGE::Mcp::IsServeMode())
		{
			// Pick up files saved since the last scan, as age_find_assets does. The editor keeps the registry current itself.
			AssetRegistry.SearchAllAssets(true);
		}

		FName PackageName;
		FString Title;
		if (FPackageName::IsScriptPackage(AssetArgument))
		{
			PackageName = FName(*AssetArgument);
			Title = AssetArgument + TEXT("  [native module]");
		}
		else
		{
			FAGEAssetQuery Query;
			Query.Assets.Add(AssetArgument);
			Query.bRescan = false;
			TArray<FString> Errors;
			const TArray<FAssetData> Found = AGE::CollectAssets(Query, Errors);
			if (Found.IsEmpty())
			{
				return ErrorResult(FString::Printf(TEXT("Asset not found: %s. Use age_find_assets to look up paths."), *AssetArgument));
			}
			PackageName = Found[0].PackageName;
			Title = FString::Printf(TEXT("%s  [%s]"), *PackageName.ToString(), *Found[0].AssetClassPath.GetAssetName().ToString());
		}

		TArray<FString> Lines;
		Lines.Add(TEXT("source: asset registry (saved files; unsaved editor changes are not included)"));
		Lines.Add(Title);
		int32 CutLines = 0;
		for (const bool bReferencers : { false, true })
		{
			if (Direction != TEXT("both") && Direction != (bReferencers ? TEXT("usedby") : TEXT("uses")))
			{
				continue;
			}
			int32 Direct = 0;
			int32 Native = 0;
			for (const TPair<FName, UE::AssetRegistry::EDependencyProperty>& Link : DirectLinks(AssetRegistry, PackageName, bReferencers))
			{
				++(FPackageName::IsScriptPackage(Link.Key.ToString()) ? Native : Direct);
			}
			Lines.Add(FString::Printf(TEXT("%s (%d%s):"), bReferencers ? TEXT("used by") : TEXT("uses"), Direct,
				Native > 0 ? *FString::Printf(TEXT(" + %d native modules"), Native) : TEXT("")));

			FReferenceTree Tree{ AssetRegistry, bReferencers, MaxDepth, Limit, Lines };
			Tree.Listed.Add(PackageName);
			Tree.Write(PackageName, 1);
			CutLines += Tree.CutLines;
		}
		if (CutLines > 0)
		{
			Lines.Add(FString::Printf(TEXT("... %d more lines. Raise limit, lower depth or pick one direction."), CutLines));
		}
		return TextResult(FString::Join(Lines, TEXT("\n")));
	}

	// ---- Usages ----

	/** Folder age_find_usages exports into and reuses: -AGECacheDir=<dir> on the command line, else <Project>/Saved/AssetGraphExporter/Cache. */
	FString UsageCacheDir()
	{
		FString Dir;
		if (!FParse::Value(FCommandLine::Get(), TEXT("AGECacheDir="), Dir))
		{
			Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetGraphExporter"), TEXT("Cache"));
		}
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Dir);
	}

	TSharedPtr<FJsonObject> ReadJsonFile(const FString& Filename)
	{
		FString Text;
		TSharedPtr<FJsonObject> Json;
		if (FFileHelper::LoadFileToString(Text, *Filename))
		{
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		}
		return Json;
	}

	FModelContextProtocolToolResult FindUsages(const FJsonObject& Arguments)
	{
		FAGEUsageSymbol Symbol;
		FString SymbolError;
		if (!AGE::Usages::ResolveSymbol(StringArgument(Arguments, TEXT("symbol")), Symbol, SymbolError))
		{
			return ErrorResult(SymbolError);
		}
		FString Path = StringArgument(Arguments, TEXT("path"));
		if (Path.IsEmpty())
		{
			Path = TEXT("/Game");
		}
		Path.RemoveFromEnd(TEXT("/"));
		const int32 Limit = FMath::Clamp(IntArgument(Arguments, TEXT("limit"), DefaultReferenceLimit), 1, MaxReferenceLimit);
		const double Start = FPlatformTime::Seconds();

		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		if (AGE::Mcp::IsServeMode())
		{
			AssetRegistry.SearchAllAssets(true);
		}

		// Every Blueprint under the path: the registry's dependencies on C++ modules are incomplete (in tested assets saved with 5.8,
		// /Script/Engine and /Script/CoreUObject were missing although the graphs call their functions), and member uses live in graphs.
		FAGEAssetQuery BlueprintQuery;
		BlueprintQuery.Paths.Add(Path);
		BlueprintQuery.Classes.Add(TEXT("Blueprint"));
		BlueprintQuery.bRescan = false;
		TArray<FString> QueryErrors;
		TArray<FAssetData> Candidates = AGE::CollectAssets(BlueprintQuery, QueryErrors);
		const int32 BlueprintCount = Candidates.Num();

		// Other assets only when the registry says they depend on the module. Maps are skipped (loading a level is slow), and so are
		// actors a World Partition level keeps in their own packages: they are not top-level assets and do not load on their own.
		TSet<FSoftObjectPath> Known;
		for (const FAssetData& Candidate : Candidates)
		{
			Known.Add(Candidate.GetSoftObjectPath());
		}
		TArray<FName> Referencers;
		AssetRegistry.GetReferencers(Symbol.ModulePackage, Referencers, UE::AssetRegistry::EDependencyCategory::Package);
		const FTopLevelAssetPath WorldClass(TEXT("/Script/Engine.World"));
		int32 SkippedMaps = 0;
		for (const FName Referencer : Referencers)
		{
			if (!Referencer.ToString().StartsWith(Path + TEXT("/")))
			{
				continue;
			}
			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByPackageName(Referencer, Assets, true);
			for (const FAssetData& Asset : Assets)
			{
				if (Asset.AssetClassPath == WorldClass || !Asset.IsTopLevelAsset())
				{
					++SkippedMaps;
				}
				else if (!Known.Contains(Asset.GetSoftObjectPath()))
				{
					Candidates.Add(Asset);
				}
			}
		}
		Candidates.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString(); });

		// In the editor, assets with unsaved changes are read from memory and not written to the cache.
		TArray<FAssetData> FromCache;
		TArray<FAssetData> Unsaved;
		for (const FAssetData& Candidate : Candidates)
		{
			const UPackage* Package = AGE::Mcp::IsServeMode() ? nullptr : FindPackage(nullptr, *Candidate.PackageName.ToString());
			(Package && Package->IsDirty() ? Unsaved : FromCache).Add(Candidate);
		}

		FAGEExportRun Run;
		Run.OutDir = UsageCacheDir();
		Run.bReuse = true;
		Run.Culture = FInternationalization::Get().GetCurrentLanguage()->GetName();
		Run.Load = [](const FAssetData& Data) -> UObject*
		{
			if (AGE::Mcp::IsServeMode())
			{
				RefreshFromDisk(Data);
			}
			return Data.GetAsset();
		};
		AGE::ExportCache::Export(FromCache, Run);

		// The cache's index keeps every asset any search exported, not only this search's candidates.
		TMap<FString, TSharedPtr<FJsonObject>> Entries = Run.PreviousEntries;
		for (const TSharedPtr<FJsonValue>& Value : Run.Entries)
		{
			Entries.Add(Value->AsObject()->GetStringField(TEXT("asset")), Value->AsObject());
		}
		Entries.KeySort(TLess<FString>());
		TArray<TSharedPtr<FJsonValue>> IndexAssets;
		for (const TPair<FString, TSharedPtr<FJsonObject>>& Entry : Entries)
		{
			IndexAssets.Add(MakeShared<FJsonValueObject>(Entry.Value));
		}
		const TSharedRef<FJsonObject> IndexJson = MakeShared<FJsonObject>();
		IndexJson->SetNumberField(TEXT("schemaVersion"), AGE::SchemaVersion);
		IndexJson->SetArrayField(TEXT("assets"), IndexAssets);
		AGE::ExportCache::SaveJson(FPaths::Combine(Run.OutDir, TEXT("index.json")), FAGEAssetExporter::ToJsonString(IndexJson));

		TArray<FString> Found;
		int32 Uses = 0;
		int32 AssetsWithUses = 0;
		auto AddUses = [&](const FAssetData& Asset, const TArray<FString>& AssetLines)
		{
			if (!AssetLines.IsEmpty())
			{
				++AssetsWithUses;
				Uses += AssetLines.Num();
				Found.Add(FString::Printf(TEXT("  %s  [%s]"), *Asset.PackageName.ToString(), *Asset.AssetClassPath.GetAssetName().ToString()));
				for (const FString& Line : AssetLines)
				{
					Found.Add(TEXT("    ") + Line);
				}
			}
		};
		for (int32 Index = 0; Index < FromCache.Num(); ++Index)
		{
			FString Status;
			Run.Entries[Index]->AsObject()->TryGetStringField(TEXT("status"), Status);
			const TSharedPtr<FJsonObject> Export = Status == TEXT("ok")
				? ReadJsonFile(FPaths::Combine(Run.OutDir, AGE::ExportCache::RelativeOutputPath(FromCache[Index]))) : nullptr;
			if (Export.IsValid())
			{
				AddUses(FromCache[Index], AGE::Usages::FindInExport(*Export, Symbol));
			}
		}
		for (const FAssetData& Asset : Unsaved)
		{
			if (const UObject* Object = Asset.GetAsset())
			{
				AddUses(Asset, AGE::Usages::FindInExport(*ExportAsset(*Object, false), Symbol));
			}
		}

		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("source: saved files through the cache %s (%d reused, %d exported, %d failed, %.1f s)%s"),
			*Run.OutDir, Run.Reused, FromCache.Num() - Run.Reused, Run.Failures, FPlatformTime::Seconds() - Start,
			Unsaved.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; %d asset(s) with unsaved changes read from the editor"), Unsaved.Num())));
		Lines.Add(TEXT("symbol: ") + Symbol.Describe());
		Lines.Add(FString::Printf(TEXT("searched: %d Blueprint(s) under %s, plus %d other asset(s) that depend on %s%s"), BlueprintCount, *Path,
			Candidates.Num() - BlueprintCount, *Symbol.ModulePackage.ToString(), SkippedMaps > 0 ? *FString::Printf(TEXT(" (%d map(s) and level actor(s) skipped)"), SkippedMaps) : TEXT("")));
		Lines.Add(Uses > 0 ? FString::Printf(TEXT("found %d use(s) in %d asset(s):"), Uses, AssetsWithUses) : FString(TEXT("no uses found")));
		const int32 Shown = FMath::Min(Found.Num(), FMath::Max(Limit - Lines.Num(), 0));
		Lines.Append(Found.GetData(), Shown);
		if (Found.Num() > Shown)
		{
			Lines.Add(FString::Printf(TEXT("... %d more lines. Narrow 'path' or raise limit."), Found.Num() - Shown));
		}
		return TextResult(FString::Join(Lines, TEXT("\n")));
	}

	// ---- Revisions ----

	/** The overview tool's text for an export, split into lines; empty for asset kinds without one. */
	TArray<FString> OverviewLines(const FJsonObject& Export)
	{
		const FString Handler = HandlerOf(Export);
		FString Text;
		if (Handler == TEXT("Blueprint"))
		{
			Text = AGE::Summary::BlueprintOverview(Export);
		}
		else if (Handler == TEXT("AnimBlueprint"))
		{
			Text = AGE::Summary::AnimBlueprintOverview(Export);
		}
		else if (Handler == TEXT("StateTree"))
		{
			Text = AGE::Summary::StateTreeOverview(Export);
		}
		else if (Handler == TEXT("NiagaraSystem"))
		{
			Text = AGE::Summary::NiagaraOverview(Export);
		}
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		return Lines;
	}

	/** Export of a loaded revision with its temporary package name put back to the original, so both sides compare equal where unchanged. */
	TSharedPtr<FJsonObject> ExportRevision(const UObject& Asset, const FString& TempPackageName, const FString& PackageName)
	{
		FString Text = FAGEAssetExporter::ToJsonString(ExportAsset(Asset, false));
		if (!TempPackageName.IsEmpty())
		{
			Text.ReplaceInline(*TempPackageName, *PackageName, ESearchCase::CaseSensitive);
		}
		TSharedPtr<FJsonObject> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		return Json;
	}

	FModelContextProtocolToolResult Diff(const FJsonObject& Arguments)
	{
		FString From = StringArgument(Arguments, TEXT("from"));
		if (From.IsEmpty())
		{
			From = TEXT("HEAD");
		}
		const FString To = StringArgument(Arguments, TEXT("to"));
		const FString Detail = StringArgument(Arguments, TEXT("detail")).ToLower();
		if (!Detail.IsEmpty() && Detail != TEXT("summary") && Detail != TEXT("json"))
		{
			return ErrorResult(TEXT("'detail' must be summary or json."));
		}
		const int32 Limit = FMath::Clamp(IntArgument(Arguments, TEXT("limit"), DefaultReferenceLimit), 1, MaxReferenceLimit);

		// The current asset: the saved file (serve mode) or the editor's copy, as the other tools read it.
		const FLoadedAsset Current = LoadAsset(StringArgument(Arguments, TEXT("asset")));
		if (!Current.Asset)
		{
			return ErrorResult(Current.Error);
		}
		const FAssetData Data(Current.Asset);
		const FString PackageName = Data.PackageName.ToString();

		const AGE::Mcp::Revision::FLoaded Old = AGE::Mcp::Revision::Load(Data, From);
		if (!Old.Asset)
		{
			AGE::Mcp::Revision::Release(Old);
			return ErrorResult(Old.Error);
		}
		AGE::Mcp::Revision::FLoaded New;
		if (To.IsEmpty())
		{
			New.Asset = Current.Asset;
			New.Label = Current.Source;
			New.Label.RemoveFromStart(TEXT("source: "));
		}
		else
		{
			New = AGE::Mcp::Revision::Load(Data, To);
			if (!New.Asset)
			{
				AGE::Mcp::Revision::Release(Old);
				AGE::Mcp::Revision::Release(New);
				return ErrorResult(New.Error);
			}
		}

		const TSharedPtr<FJsonObject> OldJson = ExportRevision(*Old.Asset, Old.TempPackageName, PackageName);
		const TSharedPtr<FJsonObject> NewJson = ExportRevision(*New.Asset, New.TempPackageName, PackageName);
		AGE::Mcp::Revision::Release(Old);
		AGE::Mcp::Revision::Release(New);
		if (!OldJson.IsValid() || !NewJson.IsValid())
		{
			return ErrorResult(TEXT("Could not read back an export for comparison."));
		}

		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("source: %s vs %s"), *Old.Label, *New.Label));
		Lines.Add(FString::Printf(TEXT("%s  [%s]"), *PackageName, *Data.AssetClassPath.GetAssetName().ToString()));

		const TArray<FString> OldOverview = OverviewLines(*OldJson);
		const TArray<FString> NewOverview = OverviewLines(*NewJson);
		if (!OldOverview.IsEmpty() || !NewOverview.IsEmpty())
		{
			const TArray<FString> OverviewDiff = AGE::JsonDiff::DiffLines(OldOverview, NewOverview);
			if (OverviewDiff.IsEmpty())
			{
				Lines.Add(TEXT("overview: no change"));
			}
			else
			{
				Lines.Add(TEXT("overview diff (- old, + new):"));
				Lines.Append(OverviewDiff);
			}
		}

		const TArray<AGE::JsonDiff::FChange> Changes = AGE::JsonDiff::Compare(
			MakeShared<FJsonValueObject>(OldJson), MakeShared<FJsonValueObject>(NewJson));
		int32 Changed = 0, Added = 0, Removed = 0;
		for (const AGE::JsonDiff::FChange& Change : Changes)
		{
			Changed += Change.Kind == TEXT("changed") ? 1 : 0;
			Added += Change.Kind == TEXT("added") ? 1 : 0;
			Removed += Change.Kind == TEXT("removed") ? 1 : 0;
		}
		Lines.Add(FString::Printf(TEXT("json: %d changed, %d added, %d removed%s"), Changed, Added, Removed,
			(Detail == TEXT("json") || Changes.IsEmpty()) ? TEXT("") : TEXT(" (detail=json lists them)")));
		if (Detail == TEXT("json"))
		{
			for (const AGE::JsonDiff::FChange& Change : Changes)
			{
				if (Change.Kind == TEXT("changed"))
				{
					Lines.Add(FString::Printf(TEXT("  ~ %s: %s -> %s"), *Change.Path, *Change.Before, *Change.After));
				}
				else
				{
					Lines.Add(FString::Printf(TEXT("  %s %s: %s"), Change.Kind == TEXT("added") ? TEXT("+") : TEXT("-"), *Change.Path,
						Change.Kind == TEXT("added") ? *Change.After : *Change.Before));
				}
			}
		}

		if (Lines.Num() > Limit)
		{
			const int32 Cut = Lines.Num() - Limit;
			Lines.SetNum(Limit);
			Lines.Add(FString::Printf(TEXT("... %d more lines. Raise limit."), Cut));
		}
		return TextResult(FString::Join(Lines, TEXT("\n")));
	}

	// ---- Guide and feedback ----

	const TCHAR* const McpPluginName = TEXT("AssetGraphExporterMcp");
	/** An optional plugin that adds editing tools; it is looked up by name only, with no dependency on it. */
	const TCHAR* const EditPluginName = TEXT("AssetGraphExporterEdit");

	const TCHAR* const EditMarkOpen = TEXT("<!-- age:edit ");
	const TCHAR* const EditMarkClose = TEXT("<!-- /age:edit -->");

	/** The text after each <!-- age:edit KEY --> mark of the editing plugin's guide, up to the next mark. Text before the first mark is a note for its authors. */
	TMap<FString, FString> ReadEditBlocks(const FString& EditGuide)
	{
		TMap<FString, FString> Blocks;
		int32 Mark = EditGuide.Find(EditMarkOpen, ESearchCase::CaseSensitive);
		while (Mark != INDEX_NONE)
		{
			const int32 KeyStart = Mark + FCString::Strlen(EditMarkOpen);
			const int32 KeyEnd = EditGuide.Find(TEXT("-->"), ESearchCase::CaseSensitive, ESearchDir::FromStart, KeyStart);
			if (KeyEnd == INDEX_NONE)
			{
				break;
			}
			const int32 TextStart = KeyEnd + 3;
			const int32 Next = EditGuide.Find(EditMarkOpen, ESearchCase::CaseSensitive, ESearchDir::FromStart, TextStart);
			Blocks.Add(EditGuide.Mid(KeyStart, KeyEnd - KeyStart).TrimStartAndEnd(),
				EditGuide.Mid(TextStart, (Next == INDEX_NONE ? EditGuide.Len() : Next) - TextStart).TrimStartAndEnd());
			Mark = Next;
		}
		return Blocks;
	}

	/**
	 * AI_GUIDE.md marks what is only true without the editing plugin as <!-- age:edit KEY -->text<!-- /age:edit -->.
	 * The marks are always removed; the text is replaced by the editing plugin's block of the same key when there is one.
	 */
	FString ResolveEditMarks(const FString& Guide, const TMap<FString, FString>& Blocks)
	{
		FString Result;
		int32 Done = 0;
		for (;;)
		{
			const int32 Mark = Guide.Find(EditMarkOpen, ESearchCase::CaseSensitive, ESearchDir::FromStart, Done);
			const int32 KeyStart = Mark == INDEX_NONE ? INDEX_NONE : Mark + FCString::Strlen(EditMarkOpen);
			const int32 KeyEnd = KeyStart == INDEX_NONE ? INDEX_NONE : Guide.Find(TEXT("-->"), ESearchCase::CaseSensitive, ESearchDir::FromStart, KeyStart);
			const int32 Close = KeyEnd == INDEX_NONE ? INDEX_NONE : Guide.Find(EditMarkClose, ESearchCase::CaseSensitive, ESearchDir::FromStart, KeyEnd);
			if (Close == INDEX_NONE)
			{
				Result += Guide.Mid(Done);
				return Result;
			}
			Result += Guide.Mid(Done, Mark - Done);
			const FString Key = Guide.Mid(KeyStart, KeyEnd - KeyStart).TrimStartAndEnd();
			const FString* Block = Blocks.Find(Key);
			Result += Block ? *Block : Guide.Mid(KeyEnd + 3, Close - KeyEnd - 3);
			Done = Close + FCString::Strlen(EditMarkClose);
		}
	}

	FModelContextProtocolToolResult Guide(const FJsonObject& Arguments)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(McpPluginName);
		const FString File = Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("AI_GUIDE.md")) : FString();
		FString Text;
		if (File.IsEmpty() || !FFileHelper::LoadFileToString(Text, *File))
		{
			return UE::ModelContextProtocol::MakeErrorResult(FString::Printf(TEXT("AI_GUIDE.md was not found next to the plugin (%s)."), *File));
		}
		// With the editing plugin, its guide replaces the marked sentences and adds a section at the end ("append").
		TMap<FString, FString> Blocks;
		if (const TSharedPtr<IPlugin> EditPlugin = IPluginManager::Get().FindEnabledPlugin(EditPluginName))
		{
			FString EditGuide;
			if (FFileHelper::LoadFileToString(EditGuide, *FPaths::Combine(EditPlugin->GetBaseDir(), TEXT("Resources"), TEXT("AI_GUIDE_EDIT.md"))))
			{
				Blocks = ReadEditBlocks(EditGuide);
			}
		}
		Text = ResolveEditMarks(Text, Blocks);
		if (const FString* Append = Blocks.Find(TEXT("append")))
		{
			Text.TrimEndInline();
			Text += TEXT("\n\n") + *Append + TEXT("\n");
		}
		return TextResult(Text);
	}

	/** Folder for saved reports: -AGEFeedbackDir=<dir> on the command line, else <Project>/Saved/AssetGraphExporter/Feedback. */
	FString FeedbackDir()
	{
		FString Dir;
		if (!FParse::Value(FCommandLine::Get(), TEXT("AGEFeedbackDir="), Dir))
		{
			Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetGraphExporter"), TEXT("Feedback"));
		}
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Dir);
	}

	/** Project asset paths and user folders replaced by <path> in text that may leave the computer. /Script/ (engine and C++ classes) stays. */
	FString Scrub(const FString& Text)
	{
		FString Result = Text;
		for (const TCHAR* Pattern : { TEXT("/Game/[^\\s'\"(),;\\]]+"), TEXT("[A-Za-z]:[\\\\/]Users[\\\\/][^\\s'\"(),;]+") })
		{
			const FRegexPattern Regex(Pattern);
			FRegexMatcher Matcher(Regex, Result);
			TArray<TPair<int32, int32>> Spans;
			while (Matcher.FindNext())
			{
				Spans.Emplace(Matcher.GetMatchBeginning(), Matcher.GetMatchEnding());
			}
			for (int32 Index = Spans.Num() - 1; Index >= 0; --Index)
			{
				Result = Result.Left(Spans[Index].Key) + TEXT("<path>") + Result.Mid(Spans[Index].Value);
			}
		}
		return Result;
	}

	/** Percent-encoding of the UTF-8 bytes, for a URL query value. */
	FString UrlEncode(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		FString Encoded;
		for (int32 Index = 0; Index < Utf8.Length(); ++Index)
		{
			const uint8 Byte = static_cast<uint8>(Utf8.Get()[Index]);
			if ((Byte >= '0' && Byte <= '9') || (Byte >= 'A' && Byte <= 'Z') || (Byte >= 'a' && Byte <= 'z') || Byte == '-' || Byte == '_' || Byte == '.' || Byte == '~')
			{
				Encoded.AppendChar(TCHAR(Byte));
			}
			else
			{
				Encoded += FString::Printf(TEXT("%%%02X"), Byte);
			}
		}
		return Encoded;
	}

	FModelContextProtocolToolResult Feedback(const FJsonObject& Arguments)
	{
		const FString Summary = StringArgument(Arguments, TEXT("summary"));
		if (Summary.IsEmpty())
		{
			return UE::ModelContextProtocol::MakeErrorResult(TEXT("'summary' is required: one line on what went wrong or what was missing."));
		}
		FString Kind = StringArgument(Arguments, TEXT("kind"));
		if (Kind.IsEmpty())
		{
			Kind = TEXT("problem");
		}
		const FString Details = StringArgument(Arguments, TEXT("details"));
		const FString Tool = StringArgument(Arguments, TEXT("tool"));
		const bool bOpen = BoolArgument(Arguments, TEXT("open"), true);

		const FString Title = Scrub(FString::Printf(TEXT("[%s] %s"), *Kind, *Summary));
		TArray<FString> Body;
		Body.Add(TEXT("### What happened"));
		Body.Add(Scrub(Summary));
		if (!Details.IsEmpty())
		{
			Body.Add(FString());
			Body.Add(Scrub(Details));
		}
		Body.Add(FString());
		Body.Add(TEXT("### Environment"));
		Body.Add(FString::Printf(TEXT("- AssetGraphExporter %s, schema %d"), AGE::ExporterVersion, AGE::SchemaVersion));
		Body.Add(FString::Printf(TEXT("- Unreal Engine %s"), *FEngineVersion::Current().ToString()));
		Body.Add(FString::Printf(TEXT("- answered by: %s"), AGE::Mcp::IsServeMode() ? TEXT("background server") : TEXT("editor")));
		if (!Tool.IsEmpty())
		{
			Body.Add(FString::Printf(TEXT("- tool: %s"), *Scrub(Tool)));
		}
		Body.Add(FString());
		Body.Add(TEXT("_Prepared by an AI assistant with age_feedback. Asset paths were removed; please check the text before submitting._"));
		const FString BodyText = FString::Join(Body, TEXT("\n"));

		// Saved on this computer first, so nothing is lost if the page is not submitted.
		const FString Dir = FeedbackDir();
		const FString File = FPaths::Combine(Dir, TEXT("feedback.md"));
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Entry = FString::Printf(TEXT("## %s UTC - %s\n\n%s\n\n"), *FDateTime::UtcNow().ToString(TEXT("%Y-%m-%d %H:%M:%S")), *Title, *BodyText);
		const bool bSaved = FFileHelper::SaveStringToFile(Entry, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);

		// The issue page comes from the plugin's SupportURL, so a fork reports to its own repository.
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(McpPluginName);
		const FString IssuesUrl = Plugin.IsValid() ? Plugin->GetDescriptor().SupportURL : FString();
		TArray<FString> Lines;
		Lines.Add(bSaved ? FString::Printf(TEXT("Saved to %s."), *File) : FString::Printf(TEXT("Could not save to %s."), *File));
		if (IssuesUrl.IsEmpty())
		{
			Lines.Add(TEXT("The plugin has no SupportURL, so no issue page was opened. Show the user the saved report."));
		}
		else
		{
			// Stay well below common URL length limits; the full report is in the saved file.
			FString Url = FString::Printf(TEXT("%s?title=%s&body=%s"), *IssuesUrl, *UrlEncode(Title), *UrlEncode(BodyText));
			if (Url.Len() > 7500)
			{
				Url = FString::Printf(TEXT("%s?title=%s&body=%s"), *IssuesUrl, *UrlEncode(Title), *UrlEncode(BodyText.Left(1500) + TEXT("\n\n(cut; the full report is in feedback.md)")));
			}
			FString LaunchError;
			if (bOpen)
			{
				FPlatformProcess::LaunchURL(*Url, nullptr, &LaunchError);
			}
			Lines.Add(!bOpen ? FString(TEXT("Issue page link (not opened):"))
				: LaunchError.IsEmpty() ? FString(TEXT("Opened a prefilled GitHub issue page in the browser. Tell the user to review it and click Submit (it needs a GitHub account with access to the repository). Link:"))
				: FString::Printf(TEXT("Could not open the browser (%s). Give the user this link:"), *LaunchError));
			Lines.Add(Url);
		}
		return TextResult(FString::Join(Lines, TEXT("\n")));
	}

	FModelContextProtocolToolResult Status(const FJsonObject& Arguments)
	{
		TArray<FString> Lines;
		if (AGE::Mcp::IsServeMode())
		{
			const double Now = FPlatformTime::Seconds();
			Lines.Add(TEXT("mode: background server (AssetGraphServe commandlet). Reads the saved .uasset files; the editor is not running."));
			Lines.Add(FString::Printf(TEXT("running since %s UTC, idle %d min"),
				*AGE::Mcp::GetServeStartUtc().ToString(TEXT("%Y-%m-%d %H:%M:%S")), FMath::FloorToInt32((Now - AGE::Mcp::GetLastActivityTime()) / 60.0)));
			Lines.Add(AGE::Mcp::GetIdleMinutes() > 0
				? FString::Printf(TEXT("stops by itself after %d idle minutes, when an Unreal Editor starts, or on age_shutdown"), AGE::Mcp::GetIdleMinutes())
				: FString(TEXT("stops when an Unreal Editor starts or on age_shutdown")));
			Lines.Add(TEXT("while running it keeps the project's DLLs loaded: call age_shutdown before building C++"));
		}
		else
		{
			Lines.Add(TEXT("mode: editor. Answers include unsaved changes."));
		}
		Lines.Add(FString::Printf(TEXT("project: %s"), FApp::GetProjectName()));
		Lines.Add(FString::Printf(TEXT("engine: %s"), *FEngineVersion::Current().ToString()));
		Lines.Add(FString::Printf(TEXT("exporter: AssetGraphExporter %s (schema %d)"), AGE::ExporterVersion, AGE::SchemaVersion));
		// Without it, the name of the editing plugin is not mentioned.
		const TSharedPtr<IPlugin> EditPlugin = IPluginManager::Get().FindEnabledPlugin(EditPluginName);
		Lines.Add(EditPlugin.IsValid()
			? FString::Printf(TEXT("editing: %s %s is installed; age_guide explains the editing tools and confirm/auto mode"), *EditPlugin->GetName(), *EditPlugin->GetDescriptor().VersionName)
			: FString(TEXT("editing: no editing plugin installed; the age_* tools only read")));
		return TextResult(FString::Join(Lines, TEXT("\n")));
	}

	FModelContextProtocolToolResult Shutdown(const FJsonObject& Arguments)
	{
		if (!AGE::Mcp::IsServeMode())
		{
			return ErrorResult(TEXT("This server runs inside the editor; age_shutdown only stops the background server started with Scripts\\Serve.bat."));
		}
		AGE::Mcp::RequestShutdown();
		return TextResult(TEXT("The background server stops in a few seconds. Start it again with Scripts\\Serve.bat (startup can take up to a minute, mostly on the first start after a reboot)."));
	}

	FModelContextProtocolToolResult LevelActorsTool(const FJsonObject& Arguments)
	{
		const AGE::Mcp::FLevelActorsAnswer Answer = AGE::Mcp::LevelActors(Arguments);
		return Answer.bError ? ErrorResult(Answer.Text) : TextResult(Answer.Text);
	}

	FModelContextProtocolToolResult OpenWindow(const FJsonObject& Arguments)
	{
		if (!AGE::Mcp::Activity::OpenWindow())
		{
			return ErrorResult(TEXT("Could not add the activity page to the MCP server, so the window has nothing to read."));
		}
		return TextResult(TEXT("The activity window is open (or was brought to the front). It stays open when the server stops, until the user closes it."));
	}
}

TArray<TSharedRef<IModelContextProtocolTool>> AGE::Mcp::CreateTools()
{
	using namespace AGE::McpToolsDetail;
	const TCHAR* AssetText = TEXT("Asset path, e.g. /Game/Folder/ST_Tree (the .ST_Tree suffix is optional).");

	TArray<TSharedRef<IModelContextProtocolTool>> Tools;
	Tools.Add(MakeShared<FTool>(TEXT("age_guide"),
		TEXT("Call this first when you start using the age_* tools: how to use them well, what to tell the user (what the tools do, "
			"confirm or auto edit mode), setup steps, troubleshooting, and how to report problems with age_feedback."),
		FSchema().Build(),
		&Guide));

	Tools.Add(MakeShared<FTool>(TEXT("age_feedback"),
		TEXT("Report a problem with the age_* tools, a wrong or oversized answer, or something the user needed that no tool could do. "
			"Saves the report on the user's computer and opens a prefilled GitHub issue page for the user to review and submit. "
			"Tell the user in one line before calling it. Describe what you tried, what happened and what you expected; "
			"do not include the user's asset contents or code (project asset paths are removed automatically)."),
		FSchema()
			.Add(TEXT("summary"), TEXT("string"), TEXT("One line: what went wrong or what was missing."), true)
			.Add(TEXT("kind"), TEXT("string"), TEXT("problem, wrong_result, too_large, missing_feature, guide or other. Default problem."))
			.Add(TEXT("details"), TEXT("string"), TEXT("What you tried, what happened, what you expected. Optional."))
			.Add(TEXT("tool"), TEXT("string"), TEXT("The age_* or built-in tool involved. Optional."))
			.Add(TEXT("open"), TEXT("boolean"), TEXT("Open the issue page in the browser. Default true."))
			.Build(),
		&Feedback));

	Tools.Add(MakeShared<FTool>(TEXT("age_find_assets"),
		TEXT("Find assets in the Unreal project by name, folder and class without loading them. One line per asset: object path and class. "
			"Use the paths with the other age_* tools."),
		FSchema()
			.Add(TEXT("name"), TEXT("string"), TEXT("Part of the asset name, case-insensitive. Optional."))
			.Add(TEXT("path"), TEXT("string"), TEXT("Folder searched recursively. Default /Game."))
			.Add(TEXT("class"), TEXT("string"), TEXT("Comma-separated classes, e.g. Blueprint, AnimBlueprint, StateTree, NiagaraSystem. A parent class such as GameplayAbility also finds Blueprints derived from it. Optional."))
			.Add(TEXT("limit"), TEXT("integer"), TEXT("Maximum lines, default 100, at most 1000."))
			.Build(),
		&FindAssets));

	Tools.Add(MakeShared<FTool>(TEXT("age_references"),
		TEXT("What an asset uses and what uses it, from the Asset Registry without loading anything (saved files only). "
			"One line per package: path, class, and 'hard' (loaded with it) or 'soft' (loaded on demand), plus 'editor-only' when the game does not need it. "
			"C++ modules (/Script/...) are gathered on one 'native:' line. Pass a module such as /Script/MyGame as 'asset' to see which assets use it. "
			"With depth > 1 each package's own links are indented under it; a package already written is marked (listed above)."),
		FSchema()
			.Add(TEXT("asset"), TEXT("string"), TEXT("Asset path, e.g. /Game/Folder/BP_Thing, or a C++ module, e.g. /Script/MyGame."), true)
			.Add(TEXT("direction"), TEXT("string"), TEXT("uses, usedBy or both. Default both."))
			.Add(TEXT("depth"), TEXT("integer"), TEXT("How many links to follow, 1 to 3. Default 1."))
			.Add(TEXT("limit"), TEXT("integer"), TEXT("Maximum lines, default 200, at most 2000."))
			.Build(),
		&References));

	Tools.Add(MakeShared<FTool>(TEXT("age_find_usages"),
		TEXT("Where Blueprints and other assets use a C++ class, UFUNCTION or UPROPERTY. "
			"For a member: every graph node that calls the function, gets or sets the property, overrides the event or binds the delegate, "
			"on the declaring class or a subclass (calls on self are judged by the Blueprint's native parent class); one line per node: graph, node type and title. "
			"For a class: every place the exported JSON names it (parent class, casts, pin types, class defaults). "
			"Every Blueprint under 'path' is searched, plus other assets that depend on the class's C++ module; maps and their actors are skipped. "
			"Assets are exported once into a cache folder and re-exported only when they or their dependencies change, so the first search over many assets is slow."),
		FSchema()
			.Add(TEXT("symbol"), TEXT("string"), TEXT("Class::Member or Class.Member, e.g. GameplayAbility::GetAvatarActorFromActorInfo or ACharacter.JumpMaxCount; or a class alone, e.g. UGameplayAbility or /Script/MyGame.MyActor."), true)
			.Add(TEXT("path"), TEXT("string"), TEXT("Folder searched recursively. Default /Game."))
			.Add(TEXT("limit"), TEXT("integer"), TEXT("Maximum lines, default 200, at most 2000."))
			.Build(),
		&FindUsages));

	Tools.Add(MakeShared<FTool>(TEXT("age_diff"),
		TEXT("What changed in an asset between a git revision and now (or between two revisions). The old file is taken from git "
			"(Git LFS files from the local store only, nothing is downloaded) and loaded under a temporary package, like the editor's Diff tool. "
			"For Blueprints, Animation Blueprints, StateTrees and Niagara Systems the overview texts are compared line by line (- old, + new); "
			"for every asset the exported JSON is compared, with graph nodes paired by GUID. detail=json lists each changed JSON path. "
			"The old revision is read with today's C++ classes, so members removed since then show as missing. Maps are not supported."),
		FSchema()
			.Add(TEXT("asset"), TEXT("string"), AssetText, true)
			.Add(TEXT("from"), TEXT("string"), TEXT("Older git revision: a commit hash, HEAD~2, a branch. Default HEAD."))
			.Add(TEXT("to"), TEXT("string"), TEXT("Newer git revision. Default: the current asset (saved file, or the editor's copy in the editor)."))
			.Add(TEXT("detail"), TEXT("string"), TEXT("summary (default) or json."))
			.Add(TEXT("limit"), TEXT("integer"), TEXT("Maximum lines, default 200, at most 2000."))
			.Build(),
		&Diff));

	Tools.Add(MakeShared<FTool>(TEXT("age_get_asset"),
		TEXT("Structure of one asset as compact JSON: properties, Blueprint variables/components/graphs (nodes, pins, links), AnimBP bindings, StateTree states and bindings, Niagara emitters and stacks. "
			"By default only values that differ from defaults are written. Use 'select' for one part, e.g. 'stateTree.bindings', 'blueprint.graphs[EventGraph]' or 'blueprint.graphs[0].nodes[3]'. "
			"When the result is longer than maxChars an outline (paths, item counts, sizes) is returned instead, so you can narrow 'select'. "
			"For Blueprints, StateTrees, Animation Blueprints and Niagara Systems, start with age_blueprint_overview / age_statetree_overview / age_animbp_overview / age_niagara_overview, which are much shorter."),
		FSchema()
			.Add(TEXT("asset"), TEXT("string"), AssetText, true)
			.Add(TEXT("select"), TEXT("string"), TEXT("Path inside the JSON: keys separated by '.', [n] for an array index, [Name] for the item whose name/path/title is Name. Optional."))
			.Add(TEXT("full"), TEXT("boolean"), TEXT("Write every value, including defaults. Default false."))
			.Add(TEXT("maxChars"), TEXT("integer"), TEXT("Longest JSON returned before switching to an outline. Default 40000."))
			.Build(),
		&GetAsset));

	Tools.Add(MakeShared<FTool>(TEXT("age_statetree_overview"),
		TEXT("StateTree as an indented outline: schema, global tasks and evaluators, then every state with its enter conditions, tasks and transitions (target, trigger, conditions). "
			"Condition and task texts are the editor's descriptions, which name bound values (e.g. 'Track Player.Distance to Player < 1,000.0'). "
			"Use age_statetree_bindings for the exact data bindings."),
		FSchema().Add(TEXT("asset"), TEXT("string"), AssetText, true).Build(),
		&StateTreeOverview));

	Tools.Add(MakeShared<FTool>(TEXT("age_statetree_bindings"),
		TEXT("Property bindings of a StateTree, one per line: 'source -> target'. Ends are written as owner path plus property path, "
			"e.g. 'GlobalTasks/Track Player.Player Actor -> Root/Combat/Approach/Tasks/Move To.Target Actor'. 'Context:Name' is schema context data."),
		FSchema()
			.Add(TEXT("asset"), TEXT("string"), AssetText, true)
			.Add(TEXT("filter"), TEXT("string"), TEXT("Keep only lines containing this text, case-insensitive. Optional."))
			.Build(),
		&StateTreeBindings));

	Tools.Add(MakeShared<FTool>(TEXT("age_blueprint_overview"),
		TEXT("Blueprint summary (Actor, GameplayAbility, Widget and other Blueprints): parent class, interfaces, variables with their default values, "
			"components, class defaults this Blueprint changed, then the execution flow of the EventGraph, functions, macros and collapsed graphs. "
			"Flow: from each event or function entry, one line per node along the exec wires; Set nodes as 'Var = expression' "
			"(e.g. 'AimTurnRate = (NormalizeAxis((Yaw - LastYaw)) / DeltaTime)'); Branch as 'if condition'; nodes with several exec outputs "
			"(Branch, Sequence, Is Valid, loops) list each connected output in [brackets] with what runs from it. "
			"For Animation Blueprints use age_animbp_overview; for raw nodes and pins use age_get_asset with select, e.g. 'blueprint.graphs[EventGraph]'."),
		FSchema().Add(TEXT("asset"), TEXT("string"), AssetText, true).Build(),
		&BlueprintOverview));

	Tools.Add(MakeShared<FTool>(TEXT("age_animbp_overview"),
		TEXT("Animation Blueprint summary: variables with their default values, AnimGraph pose flow (with the values and property-access bindings feeding each node), "
			"every state machine with each state's pose and each transition rule written as an expression, e.g. "
			"'Strafe -> TurnInPlace if (bIsStrafing AND (Speed2D < 10))', "
			"then the execution flow of the EventGraph and functions in the same form as age_blueprint_overview."),
		FSchema().Add(TEXT("asset"), TEXT("string"), AssetText, true).Build(),
		&AnimBlueprintOverview));

	Tools.Add(MakeShared<FTool>(TEXT("age_niagara_overview"),
		TEXT("Niagara System summary. Without options it is a short outline: user parameters with their values, system stacks, then each emitter (CPU/GPU, parent) with every stack "
			"in execution order, including event handlers and simulation stages (iteration source, data interface, iterations), as module names only, and the renderer kinds. "
			"With emitter set to the name of one emitter it is that emitter in detail, and with full=true the whole system in detail (large: tens of thousands of characters for a fluid system): "
			"emitter settings, renderer settings and one line per module with the inputs that were set: constants, linked parameters (e.g. User.WorldSpaceSize), dynamic inputs as nested calls, "
			"data interfaces with their changed settings, custom HLSL; enum values by their editor display names. "
			"Inputs left at their default are named at the end of the line ({default: ...}); a set input hidden by the current static switches is marked inactive. "
			"For one module's full data or the raw graphs, use age_get_asset with select, e.g. 'niagara.emitters[Name].stacks' or 'niagara.graphs[Name]'."),
		FSchema()
			.Add(TEXT("asset"), TEXT("string"), AssetText, true)
			.Add(TEXT("emitter"), TEXT("string"), TEXT("Exact name of one emitter to show in detail. Optional."))
			.Add(TEXT("full"), TEXT("boolean"), TEXT("Every emitter in detail instead of the outline. Default false."))
			.Build(),
		&NiagaraOverview));

	Tools.Add(MakeShared<FTool>(TEXT("age_level_actors"),
		TEXT("What is where in the level open in the editor (unsaved changes included; not available from the background server): "
			"the actors inside a box (min, max) or a sphere (center or around, with radius), or the whole level, in one call. "
			"An actor counts when its bounds overlap the range (one without bounds, such as a light: when its location is inside). "
			"One line per actor, nearest to the range's center first: label [object name] (the name alone when both are the same), class, "
			"'at' location, 'rot' (pitch, yaw, roll) and 'scale' when not 0 and 1, 'box' min-max (world-axis bounds, so a rotated mesh shows the space it really takes), "
			"'mesh' static or skeletal mesh asset(s), 'hidden' when hidden in the editor. "
			"World Partition actors in unloaded regions are listed as 'unloaded' with their saved bounds only; nothing is loaded. "
			"Outliner rules decide which actors are listed (no WorldSettings or builder brush)."),
		FSchema()
			.Add(TEXT("min"), TEXT("array"), TEXT("Box range: lowest corner [x, y, z]. Use with max."))
			.Add(TEXT("max"), TEXT("array"), TEXT("Box range: highest corner [x, y, z]. Use with min."))
			.Add(TEXT("center"), TEXT("array"), TEXT("Sphere range: center [x, y, z]. Use with radius."))
			.Add(TEXT("around"), TEXT("string"), TEXT("Sphere range around an actor: its exact label or object name; the center is the middle of its box. Use with radius, instead of center."))
			.Add(TEXT("radius"), TEXT("number"), TEXT("Sphere radius in cm."))
			.Add(TEXT("class"), TEXT("string"), TEXT("Comma-separated classes; parents count, e.g. StaticMeshActor, Light, BP_Door. Optional."))
			.Add(TEXT("name"), TEXT("string"), TEXT("Part of the label or object name, case-insensitive. Optional."))
			.Add(TEXT("bounds"), TEXT("string"), TEXT("visible (default): components that exist in the game, skipping editor-only helpers such as sprites and a camera's frustum, which can make a character's box many meters wide; "
				"collision: only those with collision; all: every component, as the engine's GetActorBounds."))
			.Add(TEXT("limit"), TEXT("integer"), TEXT("Maximum lines, default 100, at most 1000."))
			.Build(),
		&LevelActorsTool));

	Tools.Add(MakeShared<FTool>(TEXT("age_status"),
		TEXT("Where the age_* answers come from (the running editor, including unsaved changes, or the background server reading saved files), plus project, engine and exporter versions."),
		FSchema().Build(),
		&Status));

	Tools.Add(MakeShared<FTool>(TEXT("age_shutdown"),
		TEXT("Stops the background server started with Scripts\\Serve.bat. Call it before building C++ code: while it runs, the project's DLLs stay loaded and the build cannot overwrite them. Does nothing inside the editor."),
		FSchema().Build(),
		&Shutdown));

	Tools.Add(MakeShared<FTool>(TEXT("age_open_window"),
		TEXT("Opens the activity window that shows the user what you are doing, or brings it to the front. It normally starts by itself; call this when the user asks for it, e.g. after closing it."),
		FSchema().Build(),
		&OpenWindow));
	return Tools;
}
