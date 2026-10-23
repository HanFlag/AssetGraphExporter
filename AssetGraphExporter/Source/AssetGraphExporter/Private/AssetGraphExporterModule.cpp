// Asset Graph Exporter

#include "AGEBlueprintHandler.h"
#include "AGEHandlerRegistry.h"
#include "AGELog.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAssetGraphExporter);

class FAssetGraphExporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FAGEHandlerRegistry::Get().Register(MakeShared<FAGEBlueprintHandler>());
		FAGEHandlerRegistry::Get().Register(MakeShared<FAGEAnimBlueprintHandler>());
	}

	virtual void ShutdownModule() override
	{
		FAGEHandlerRegistry::Get().Unregister(TEXT("Blueprint"));
		FAGEHandlerRegistry::Get().Unregister(TEXT("AnimBlueprint"));
	}
};

IMPLEMENT_MODULE(FAssetGraphExporterModule, AssetGraphExporter)
