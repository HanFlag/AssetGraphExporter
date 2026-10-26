// Asset Graph Exporter

#include "AGEMcpActivity.h"
#include "AGEMcpState.h"
#include "AGEMcpTools.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "IModelContextProtocolModule.h"
#include "IModelContextProtocolTool.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "ModelContextProtocolServer.h"
#include "ModelContextProtocolSettings.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAssetGraphExporterMcp);

/** Adds the age_* tools to the Unreal MCP server (inside the editor, or inside the AssetGraphServe commandlet), and the activity page. */
class FAssetGraphExporterMcpModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		IModelContextProtocolModule& Mcp = IModelContextProtocolModule::GetChecked();
		AddTools(Mcp);
		// ModelContextProtocol.RefreshTools drops every tool and asks providers to add theirs again.
		RefreshToolsHandle = Mcp.OnRefreshTools().AddRaw(this, &FAssetGraphExporterMcpModule::HandleRefreshTools);
		AGE::Mcp::Activity::Startup();

		// In the editor, start the Unreal MCP server ourselves so users need not find its per-user "Auto Start Server"
		// setting. The AssetGraphServe commandlet starts it on its own; -AGENoServerAutoStart turns this off.
		if (GIsEditor && !IsRunningCommandlet() && !FParse::Param(FCommandLine::Get(), TEXT("AGENoServerAutoStart")))
		{
			PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FAssetGraphExporterMcpModule::BeginServerAutoStart);
		}
	}

	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		FTSTicker::RemoveTicker(AutoStartTicker);
		AGE::Mcp::Activity::Shutdown();
		if (IModelContextProtocolModule* Mcp = IModelContextProtocolModule::Get())
		{
			Mcp->OnRefreshTools().Remove(RefreshToolsHandle);
			for (const TSharedRef<IModelContextProtocolTool>& Tool : Tools)
			{
				Mcp->RemoveTool(Tool);
			}
		}
		Tools.Reset();
	}

private:
	/** Attempts once at engine init, then every few seconds for a while: a background server that is shutting down because the
	 *  editor started may still hold the port. Does nothing when the server is already running (e.g. the engine setting is on). */
	void BeginServerAutoStart()
	{
		AutoStartAttempts = 0;
		if (TryStartServer())
		{
			AutoStartTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FAssetGraphExporterMcpModule::TryStartServer), 2.0f);
		}
	}

	/** Returns true while another attempt is needed. */
	bool TryStartServer(float DeltaTime = 0.0f)
	{
		constexpr int32 MaxAttempts = 15;
		IModelContextProtocolModule* Mcp = IModelContextProtocolModule::Get();
		if (!Mcp)
		{
			return false;
		}
		const FModelContextProtocolServer* Server = Mcp->GetServer();
		if (Server && Server->IsServerRunning())
		{
			if (AutoStartAttempts > 0)
			{
				UE_LOG(LogAssetGraphExporterMcp, Display, TEXT("Started the Unreal MCP server: http://127.0.0.1:%u%s"),
					UE::ModelContextProtocol::GetServerPortNumber(), *UE::ModelContextProtocol::GetServerUrlPath());
			}
			return false;
		}
		if (AutoStartAttempts >= MaxAttempts)
		{
			UE_LOG(LogAssetGraphExporterMcp, Warning, TEXT("Could not start the Unreal MCP server on port %u (is another program using it?). Start it with ModelContextProtocol.StartServer."),
				UE::ModelContextProtocol::GetServerPortNumber());
			return false;
		}
		++AutoStartAttempts;
		if (Server)
		{
			Mcp->StopServer();
		}
		Mcp->StartServer(UE::ModelContextProtocol::GetServerPortNumber(), UE::ModelContextProtocol::GetServerUrlPath());
		const FModelContextProtocolServer* Started = Mcp->GetServer();
		if (Started && Started->IsServerRunning())
		{
			UE_LOG(LogAssetGraphExporterMcp, Display, TEXT("Started the Unreal MCP server: http://127.0.0.1:%u%s (turn off with -AGENoServerAutoStart)"),
				UE::ModelContextProtocol::GetServerPortNumber(), *UE::ModelContextProtocol::GetServerUrlPath());
			return false;
		}
		return true;
	}

	void HandleRefreshTools()
	{
		if (IModelContextProtocolModule* Mcp = IModelContextProtocolModule::Get())
		{
			AddTools(*Mcp);
		}
	}

	void AddTools(IModelContextProtocolModule& Mcp)
	{
		Tools = AGE::Mcp::CreateTools();
		int32 Added = 0;
		for (const TSharedRef<IModelContextProtocolTool>& Tool : Tools)
		{
			if (Mcp.AddTool(Tool))
			{
				++Added;
			}
			else
			{
				UE_LOG(LogAssetGraphExporterMcp, Warning, TEXT("Could not add MCP tool %s (a tool with that name already exists)."), *Tool->GetName());
			}
		}
		UE_LOG(LogAssetGraphExporterMcp, Log, TEXT("Added %d Asset Graph Exporter tools to the Unreal MCP server."), Added);
	}

	TArray<TSharedRef<IModelContextProtocolTool>> Tools;
	FDelegateHandle RefreshToolsHandle;
	FDelegateHandle PostEngineInitHandle;
	FTSTicker::FDelegateHandle AutoStartTicker;
	int32 AutoStartAttempts = 0;
};

IMPLEMENT_MODULE(FAssetGraphExporterMcpModule, AssetGraphExporterMcp)
