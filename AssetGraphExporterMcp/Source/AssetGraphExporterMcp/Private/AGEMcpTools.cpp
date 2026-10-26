// Asset Graph Exporter

#include "AGEMcpTools.h"
#include "AGEAssetQuery.h"
#include "AGEAssetExporter.h"
#include "AGEExportContext.h"
#include "AGEJsonQuery.h"
#include "AGEMcpActivity.h"
#include "AGEMcpState.h"
#include "AGESummary.h"
#include "IModelContextProtocolTool.h"
#include "ModelContextProtocolToolResults.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/AssetRegistryInterface.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
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
		return UE::ModelContextProtocol::MakeErrorResult(Text);
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
		return SummaryResult(Arguments, TEXT("NiagaraSystem"), TEXT("a Niagara System"), [](const FJsonObject& Export)
		{
			return AGE::Summary::NiagaraOverview(Export);
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

	FModelContextProtocolToolResult Status(const FJsonObject& Arguments)
	{
		TArray<FString> Lines;
		if (AGE::Mcp::IsServeMode())
		{
			const double Now = FPlatformTime::Seconds();
			Lines.Add(TEXT("mode: background server (AssetGraphServe commandlet). Reads the saved .uasset files; the editor is not running."));
			Lines.Add(FString::Printf(TEXT("running since %s UTC, idle %d min"),
				*AGE::Mcp::GetServeStartUtc().ToString(TEXT("%Y-%m-%d %H:%M:%S")), FMath::FloorToInt((Now - AGE::Mcp::GetLastActivityTime()) / 60.0)));
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
}

TArray<TSharedRef<IModelContextProtocolTool>> AGE::Mcp::CreateTools()
{
	using namespace AGE::McpToolsDetail;
	const TCHAR* AssetText = TEXT("Asset path, e.g. /Game/Folder/ST_Boss (the .ST_Boss suffix is optional).");

	TArray<TSharedRef<IModelContextProtocolTool>> Tools;
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
			"'CombatMove -> TurnInPlace if (bIsCombatMove AND (Speed2D < 10))', "
			"then the execution flow of the EventGraph and functions in the same form as age_blueprint_overview."),
		FSchema().Add(TEXT("asset"), TEXT("string"), AssetText, true).Build(),
		&AnimBlueprintOverview));

	Tools.Add(MakeShared<FTool>(TEXT("age_niagara_overview"),
		TEXT("Niagara System summary: user parameters, system stacks, then each emitter (CPU/GPU, parent) with every stack in execution order, "
			"including event handlers and simulation stages (iteration source, data interface, iterations), and renderers. "
			"One line per module with the inputs that were set: constants, linked parameters (e.g. User.WorldSpaceSize), dynamic inputs as nested calls, "
			"data interfaces with their changed settings, custom HLSL; enum values by their editor display names. "
			"Inputs left at their default are named at the end of the line ({default: ...}); a set input hidden by the current static switches is marked inactive. "
			"For one module's full data or the raw graphs, use age_get_asset with select, e.g. 'niagara.emitters[Name].stacks' or 'niagara.graphs[Name]'."),
		FSchema().Add(TEXT("asset"), TEXT("string"), AssetText, true).Build(),
		&NiagaraOverview));

	Tools.Add(MakeShared<FTool>(TEXT("age_status"),
		TEXT("Where the age_* answers come from (the running editor, including unsaved changes, or the background server reading saved files), plus project, engine and exporter versions."),
		FSchema().Build(),
		&Status));

	Tools.Add(MakeShared<FTool>(TEXT("age_shutdown"),
		TEXT("Stops the background server started with Scripts\\Serve.bat. Call it before building C++ code: while it runs, the project's DLLs stay loaded and the build cannot overwrite them. Does nothing inside the editor."),
		FSchema().Build(),
		&Shutdown));
	return Tools;
}
