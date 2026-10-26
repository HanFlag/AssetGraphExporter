// Asset Graph Exporter

#include "AssetGraphServeCommandlet.h"
#include "AGEMcpActivity.h"
#include "AGEMcpState.h"
#include "IModelContextProtocolModule.h"
#include "ModelContextProtocolSettings.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UObjectGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetGraphServeCommandlet)

// Named (not anonymous) so these constants cannot clash with other files in a unity build.
namespace AGE::ServeDetail
{
	/** An editor serves the same MCP port, so the background server gives way to it. */
	const TCHAR* EditorProcessName = TEXT("UnrealEditor.exe");
	constexpr double EditorCheckSeconds = 5.0;
	/** Loaded assets are released this often (only after there were requests), so memory does not keep growing. */
	constexpr double GarbageCollectSeconds = 120.0;
	/** age_shutdown keeps serving this long so its own reply is sent before the server stops. */
	constexpr double ShutdownGraceSeconds = 1.0;
	constexpr int32 DefaultIdleMinutes = 30;
	constexpr float LoopSleepSeconds = 0.005f;
}

UAssetGraphServeCommandlet::UAssetGraphServeCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UAssetGraphServeCommandlet::Main(const FString& Params)
{
	using namespace AGE::ServeDetail;

	const bool bIgnoreEditor = FParse::Param(*Params, TEXT("IgnoreEditor"));
	if (!bIgnoreEditor && FPlatformProcess::IsApplicationRunning(EditorProcessName))
	{
		UE_LOG(LogAssetGraphExporterMcp, Error, TEXT("An Unreal Editor is running. Use the MCP server inside the editor instead; both would use the same port. Pass -IgnoreEditor to start anyway."));
		return 1;
	}

	int32 IdleMinutes = DefaultIdleMinutes;
	FParse::Value(*Params, TEXT("IdleMinutes="), IdleMinutes);

	// Same text language as the AssetGraphExport commandlet, so answers do not depend on the machine's editor language.
	FString CultureOverride;
	if (!FParse::Value(FCommandLine::Get(), TEXT("culture="), CultureOverride) && !FParse::Value(FCommandLine::Get(), TEXT("language="), CultureOverride))
	{
		FInternationalization::Get().SetCurrentLanguageAndLocale(TEXT("en"));
	}

	// Commandlets do not scan in the background.
	const double ScanStart = FPlatformTime::Seconds();
	IAssetRegistry::GetChecked().SearchAllAssets(true);
	UE_LOG(LogAssetGraphExporterMcp, Display, TEXT("[Timing] Asset registry scan: %.2f s"), FPlatformTime::Seconds() - ScanStart);

	AGE::Mcp::EnterServeMode(IdleMinutes);

	// Normally started here. With -ModelContextProtocolStartServer the Unreal MCP plugin has already started it.
	IModelContextProtocolModule& Mcp = IModelContextProtocolModule::GetChecked();
	const uint32 Port = UE::ModelContextProtocol::GetServerPortNumber();
	const FString UrlPath = UE::ModelContextProtocol::GetServerUrlPath();
	if (!Mcp.GetServer())
	{
		Mcp.StartServer(Port, UrlPath);
	}
	AGE::Mcp::Activity::EnsurePage();
	UE_LOG(LogAssetGraphExporterMcp, Display, TEXT("MCP server ready: http://127.0.0.1:%u%s (%d top-level tools). Stop with age_shutdown or Ctrl+C%s."),
		Port, *UrlPath, Mcp.GetTools().Num(),
		IdleMinutes > 0 ? *FString::Printf(TEXT(", or it stops after %d idle minutes"), IdleMinutes) : TEXT(""));
	UE_LOG(LogAssetGraphExporterMcp, Display, TEXT("[Timing] Ready %.2f s after process start"), FPlatformTime::Seconds() - GStartTime);

	double LastTick = FPlatformTime::Seconds();
	double NextEditorCheck = LastTick + EditorCheckSeconds;
	double NextGarbageCollect = LastTick + GarbageCollectSeconds;
	double ActivityAtLastCollect = AGE::Mcp::GetLastActivityTime();
	double ShutdownAt = 0.0;
	FString StopReason;
	while (!IsEngineExitRequested())
	{
		const double Now = FPlatformTime::Seconds();
		const float DeltaTime = static_cast<float>(Now - LastTick);
		LastTick = Now;

		// What the editor's main loop would do: game-thread tasks (tool results hop here), then tickers (HTTP and MCP servers).
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FTSTicker::GetCoreTicker().Tick(DeltaTime);
		FThreadManager::Get().Tick();

		if (AGE::Mcp::IsShutdownRequested())
		{
			if (ShutdownAt == 0.0)
			{
				ShutdownAt = Now + ShutdownGraceSeconds;
			}
			else if (Now >= ShutdownAt)
			{
				StopReason = TEXT("age_shutdown");
				break;
			}
		}
		if (IdleMinutes > 0 && Now - AGE::Mcp::GetLastActivityTime() > IdleMinutes * 60.0)
		{
			StopReason = FString::Printf(TEXT("no age_* request for %d minutes"), IdleMinutes);
			break;
		}
		if (!bIgnoreEditor && Now >= NextEditorCheck)
		{
			NextEditorCheck = Now + EditorCheckSeconds;
			if (FPlatformProcess::IsApplicationRunning(EditorProcessName))
			{
				StopReason = TEXT("an Unreal Editor started");
				break;
			}
		}
		if (Now >= NextGarbageCollect)
		{
			NextGarbageCollect = Now + GarbageCollectSeconds;
			if (AGE::Mcp::GetLastActivityTime() != ActivityAtLastCollect)
			{
				ActivityAtLastCollect = AGE::Mcp::GetLastActivityTime();
				CollectGarbage(RF_NoFlags);
			}
		}

		FPlatformProcess::Sleep(LoopSleepSeconds);
	}

	if (StopReason.IsEmpty())
	{
		StopReason = TEXT("engine exit requested (Ctrl+C)");
	}
	UE_LOG(LogAssetGraphExporterMcp, Display, TEXT("Stopping the MCP server: %s"), *StopReason);
	Mcp.StopServer();
	return 0;
}
