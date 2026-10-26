// Asset Graph Exporter

#include "AGEMcpActivity.h"
#include "AGEMcpState.h"
#include "AGEMcpTools.h"
#include "IModelContextProtocolModule.h"
#include "IModelContextProtocolTool.h"
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
	}

	virtual void ShutdownModule() override
	{
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
};

IMPLEMENT_MODULE(FAssetGraphExporterMcpModule, AssetGraphExporterMcp)
