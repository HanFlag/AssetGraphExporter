// Asset Graph Exporter

#include "AGEMcpActivity.h"
#include "AGEMcpState.h"
#include "IModelContextProtocolModule.h"
#include "ModelContextProtocolServer.h"
#include "ModelContextProtocolToolResults.h"
#include "Containers/StringConv.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/CriticalSection.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpPath.h"
#include "HttpRequestHandler.h"
#include "HttpRouteHandle.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Named (not anonymous) so these helpers cannot clash with other files in a unity build.
namespace AGE::McpActivityDetail
{
	constexpr int32 MaxEntries = 200;
	constexpr int32 MaxTextLength = 300;
	constexpr const TCHAR* PagePath = TEXT("/age/activity");
	constexpr const TCHAR* PluginName = TEXT("AssetGraphExporterMcp");

	TAutoConsoleVariable<int32> CVarOpenPage(
		TEXT("AGE.ActivityWindow"),
		1,
		TEXT("1: open the Asset Graph Exporter activity page in the browser when an MCP client first connects or calls a tool. 0: never open it by itself (AGE.OpenActivity still does)."));

	struct FEntry
	{
		int64 Id = 0;
		FDateTime Time;
		/** "tool" for age_* calls, "engineTool" for other tools (name only), "connect" when a client starts a session. */
		FString Kind;
		FString Tool;
		TSharedPtr<FJsonObject> Arguments;
		/** "file", "editor" or "editorUnsaved"; empty when the answer did not say. */
		FString Source;
		/** First line of the answer after the source line (e.g. the summary header). */
		FString Line;
		int32 Chars = 0;
		double Milliseconds = -1.0;
		FString Error;
	};

	struct FState
	{
		FCriticalSection Lock;
		TArray<FEntry> Entries;
		int64 NextId = 1;
		/** Set by any record (from any thread); the ticker opens the page once on the game thread. */
		bool bOpenRequested = false;
		bool bOpened = false;

		TSharedPtr<IHttpRouter> Router;
		FHttpRouteHandle Route;
		uint32 RoutePort = 0;

		TUniquePtr<FOutputDevice> LogCapture;
		FTSTicker::FDelegateHandle Ticker;
		IConsoleObject* OpenCommand = nullptr;
	};

	FState& State()
	{
		static FState Instance;
		return Instance;
	}

	FString Shorten(const FString& Text)
	{
		return Text.Len() <= MaxTextLength ? Text : Text.Left(MaxTextLength) + TEXT("...");
	}

	void Add(FEntry&& Entry)
	{
		FState& S = State();
		FScopeLock Guard(&S.Lock);
		Entry.Id = S.NextId++;
		Entry.Time = FDateTime::Now();
		S.Entries.Add(MoveTemp(Entry));
		if (S.Entries.Num() > MaxEntries)
		{
			S.Entries.RemoveAt(0, S.Entries.Num() - MaxEntries);
		}
		S.bOpenRequested = true;
	}

	/** The Unreal MCP server logs "Running tool: 'name'" for every call and "Session initialized: id" when a client connects. */
	class FMcpLogCapture : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			static const FName McpCategory(TEXT("LogModelContextProtocol"));
			if (Category != McpCategory || !Text)
			{
				return;
			}
			const FString Message(Text);
			static const FString RunningTool(TEXT("Running tool: '"));
			if (Message.StartsWith(RunningTool))
			{
				FString Name = Message.RightChop(RunningTool.Len());
				Name.RemoveFromEnd(TEXT("'"));
				// age_* calls are recorded with their arguments by RecordToolCall.
				if (!Name.StartsWith(TEXT("age_")))
				{
					FEntry Entry;
					Entry.Kind = TEXT("engineTool");
					Entry.Tool = Name;
					Add(MoveTemp(Entry));
				}
			}
			else if (Message.StartsWith(TEXT("Session initialized")))
			{
				FEntry Entry;
				Entry.Kind = TEXT("connect");
				Add(MoveTemp(Entry));
			}
		}

		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
	};

	FString PageHtml()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
		FString Html;
		const FString File = Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("Activity.html")) : FString();
		if (File.IsEmpty() || !FFileHelper::LoadFileToString(Html, *File))
		{
			Html = FString::Printf(TEXT("<!doctype html><meta charset=\"utf-8\"><title>Asset Graph Exporter</title><p>Activity.html was not found next to the plugin (%s).</p>"), *File);
		}
		return Html;
	}

	FString EntriesJson(int64 After)
	{
		FState& S = State();
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("project"), FApp::GetProjectName());
		Root->SetStringField(TEXT("mode"), AGE::Mcp::IsServeMode() ? TEXT("serve") : TEXT("editor"));

		TArray<TSharedPtr<FJsonValue>> Items;
		{
			FScopeLock Guard(&S.Lock);
			Root->SetNumberField(TEXT("last"), static_cast<double>(S.NextId - 1));
			for (const FEntry& Entry : S.Entries)
			{
				if (Entry.Id <= After)
				{
					continue;
				}
				const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetNumberField(TEXT("id"), static_cast<double>(Entry.Id));
				Item->SetStringField(TEXT("time"), Entry.Time.ToString(TEXT("%H:%M:%S")));
				Item->SetStringField(TEXT("kind"), Entry.Kind);
				if (!Entry.Tool.IsEmpty())
				{
					Item->SetStringField(TEXT("tool"), Entry.Tool);
				}
				if (Entry.Arguments.IsValid() && Entry.Arguments->Values.Num() > 0)
				{
					Item->SetObjectField(TEXT("args"), Entry.Arguments);
				}
				if (!Entry.Source.IsEmpty())
				{
					Item->SetStringField(TEXT("source"), Entry.Source);
				}
				if (!Entry.Line.IsEmpty())
				{
					Item->SetStringField(TEXT("line"), Entry.Line);
				}
				if (Entry.Chars > 0)
				{
					Item->SetNumberField(TEXT("chars"), Entry.Chars);
				}
				if (Entry.Milliseconds >= 0.0)
				{
					Item->SetNumberField(TEXT("ms"), FMath::RoundToDouble(Entry.Milliseconds));
				}
				if (!Entry.Error.IsEmpty())
				{
					Item->SetStringField(TEXT("error"), Entry.Error);
				}
				Items.Add(MakeShared<FJsonValueObject>(Item));
			}
		}
		Root->SetArrayField(TEXT("entries"), Items);

		FString Json;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		FJsonSerializer::Serialize(Root, Writer);
		return Json;
	}

	TUniquePtr<FHttpServerResponse> Utf8Response(const FString& Text, const TCHAR* ContentType)
	{
		const FTCHARToUTF8 Converted(*Text);
		TArray<uint8> Bytes(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(MoveTemp(Bytes), ContentType);
		Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-store") });
		return Response;
	}

	/** GET /age/activity: the page. GET /age/activity?after=N: entries newer than N as JSON (the page polls this). */
	bool HandleRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
	{
		if (const FString* After = Request.QueryParams.Find(TEXT("after")))
		{
			OnComplete(Utf8Response(EntriesJson(FCString::Atoi64(**After)), TEXT("application/json; charset=utf-8")));
		}
		else
		{
			OnComplete(Utf8Response(PageHtml(), TEXT("text/html; charset=utf-8")));
		}
		return true;
	}

	void UnbindPage()
	{
		FState& S = State();
		if (S.Router.IsValid() && S.Route.IsValid())
		{
			S.Router->UnbindRoute(S.Route);
		}
		S.Router.Reset();
		S.Route.Reset();
		S.RoutePort = 0;
	}

	void LaunchPage(const FString& Url)
	{
		FString Error;
		FPlatformProcess::LaunchURL(*Url, nullptr, &Error);
		if (!Error.IsEmpty())
		{
			UE_LOG(LogAssetGraphExporterMcp, Warning, TEXT("Could not open %s: %s"), *Url, *Error);
		}
	}

	bool ShouldOpenByItself()
	{
		return CVarOpenPage.GetValueOnGameThread() != 0 && !FParse::Param(FCommandLine::Get(), TEXT("AGENoActivityWindow"));
	}

	/** Game thread: after new records, makes sure the page exists and opens it once (first time only). */
	bool Tick(float DeltaTime)
	{
		FState& S = State();
		bool bNewRecords = false;
		bool bOpenedBefore = false;
		{
			FScopeLock Guard(&S.Lock);
			bNewRecords = S.bOpenRequested;
			bOpenedBefore = S.bOpened;
			S.bOpenRequested = false;
		}
		if (!bNewRecords)
		{
			return true;
		}
		const FString Url = AGE::Mcp::Activity::EnsurePage();
		if (!Url.IsEmpty() && !bOpenedBefore && ShouldOpenByItself())
		{
			LaunchPage(Url);
			FScopeLock Guard(&S.Lock);
			S.bOpened = true;
		}
		return true;
	}

	/** Pulls the answer's text apart: the "source: ..." line, the next line, the length; or the error text. */
	void ReadResult(const FModelContextProtocolToolResult& Result, FEntry& Entry)
	{
		const TSharedPtr<FJsonObject>& Json = Result.JsonObject;
		if (!Json.IsValid())
		{
			Entry.Error = TEXT("(no result)");
			return;
		}
		FString Text;
		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		if (Json->TryGetArrayField(TEXT("content"), Content) && Content)
		{
			for (const TSharedPtr<FJsonValue>& Item : *Content)
			{
				const TSharedPtr<FJsonObject>* ItemObject = nullptr;
				FString ItemText;
				if (Item.IsValid() && Item->TryGetObject(ItemObject) && ItemObject && (*ItemObject)->TryGetStringField(TEXT("text"), ItemText))
				{
					Text += ItemText;
				}
			}
		}
		bool bError = false;
		Json->TryGetBoolField(TEXT("isError"), bError);
		if (bError)
		{
			Entry.Error = Shorten(Text);
			return;
		}

		Entry.Chars = Text.Len();
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		int32 Next = 0;
		if (Lines.Num() > 0 && Lines[0].StartsWith(TEXT("source: ")))
		{
			const FString& Source = Lines[0];
			Entry.Source = Source.StartsWith(TEXT("source: saved file")) ? TEXT("file")
				: Source.Contains(TEXT("unsaved")) ? TEXT("editorUnsaved")
				: TEXT("editor");
			Next = 1;
		}
		if (Lines.IsValidIndex(Next))
		{
			Entry.Line = Shorten(Lines[Next]);
		}
	}
}

void AGE::Mcp::Activity::Startup()
{
	using namespace AGE::McpActivityDetail;
	FState& S = State();
	S.LogCapture = MakeUnique<FMcpLogCapture>();
	GLog->AddOutputDevice(S.LogCapture.Get());
	S.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), 0.25f);
	S.OpenCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("AGE.OpenActivity"),
		TEXT("Opens the Asset Graph Exporter activity page (what the MCP client asked) in the browser. Needs the Unreal MCP server running."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const FString Url = EnsurePage();
			if (Url.IsEmpty())
			{
				UE_LOG(LogAssetGraphExporterMcp, Warning, TEXT("The Unreal MCP server is not running, so there is no activity page yet. Start it with ModelContextProtocol.StartServer."));
				return;
			}
			LaunchPage(Url);
		}),
		ECVF_Default);
}

void AGE::Mcp::Activity::Shutdown()
{
	using namespace AGE::McpActivityDetail;
	FState& S = State();
	if (S.OpenCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(S.OpenCommand);
		S.OpenCommand = nullptr;
	}
	if (S.Ticker.IsValid())
	{
		FTSTicker::RemoveTicker(S.Ticker);
		S.Ticker.Reset();
	}
	if (S.LogCapture.IsValid())
	{
		if (GLog)
		{
			GLog->RemoveOutputDevice(S.LogCapture.Get());
		}
		S.LogCapture.Reset();
	}
	UnbindPage();
}

void AGE::Mcp::Activity::RecordToolCall(const FString& Tool, const FJsonObject& Arguments, const FModelContextProtocolToolResult& Result, double Milliseconds)
{
	using namespace AGE::McpActivityDetail;
	FEntry Entry;
	Entry.Kind = TEXT("tool");
	Entry.Tool = Tool;
	Entry.Milliseconds = Milliseconds;

	// Only short text arguments; nothing the page does not show.
	const TSharedRef<FJsonObject> Shown = MakeShared<FJsonObject>();
	for (const TCHAR* Key : { TEXT("asset"), TEXT("select"), TEXT("name"), TEXT("path"), TEXT("class"), TEXT("filter") })
	{
		FString Value;
		if (Arguments.TryGetStringField(Key, Value) && !Value.TrimStartAndEnd().IsEmpty())
		{
			Shown->SetStringField(Key, Shorten(Value.TrimStartAndEnd()));
		}
	}
	bool bFull = false;
	if (Arguments.TryGetBoolField(TEXT("full"), bFull) && bFull)
	{
		Shown->SetBoolField(TEXT("full"), true);
	}
	Entry.Arguments = Shown;
	ReadResult(Result, Entry);
	Add(MoveTemp(Entry));
}

FString AGE::Mcp::Activity::EnsurePage()
{
	using namespace AGE::McpActivityDetail;
	FState& S = State();
	IModelContextProtocolModule* Mcp = IModelContextProtocolModule::Get();
	FModelContextProtocolServer* Server = Mcp ? Mcp->GetServer() : nullptr;
	if (!Server || !Server->IsServerRunning() || !FHttpServerModule::IsAvailable())
	{
		return FString();
	}
	const uint32 Port = Server->GetServerPort();
	if (S.RoutePort != Port)
	{
		UnbindPage();
		S.Router = FHttpServerModule::Get().GetHttpRouter(Port);
		if (!S.Router.IsValid())
		{
			return FString();
		}
		S.Route = S.Router->BindRoute(FHttpPath(PagePath), EHttpServerRequestVerbs::VERB_GET, FHttpRequestHandler::CreateStatic(&HandleRequest));
		if (!S.Route.IsValid())
		{
			UE_LOG(LogAssetGraphExporterMcp, Warning, TEXT("Could not add %s to the MCP server on port %u (the path is already in use)."), PagePath, Port);
			S.Router.Reset();
			return FString();
		}
		S.RoutePort = Port;
		UE_LOG(LogAssetGraphExporterMcp, Display, TEXT("Activity page: http://127.0.0.1:%u%s"), Port, PagePath);
	}
	return FString::Printf(TEXT("http://127.0.0.1:%u%s"), Port, PagePath);
}
