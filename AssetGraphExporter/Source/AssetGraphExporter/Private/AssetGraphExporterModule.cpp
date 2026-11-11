// Asset Graph Exporter

#include "AGEBlueprintHandler.h"
#include "AGEHandlerRegistry.h"
#include "AGELog.h"
#include "AGESkeletonHandler.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogAssetGraphExporter);

class FAssetGraphExporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FAGEHandlerRegistry::Get().Register(MakeShared<FAGEBlueprintHandler>());
		FAGEHandlerRegistry::Get().Register(MakeShared<FAGEAnimBlueprintHandler>());
		FAGEHandlerRegistry::Get().Register(MakeShared<FAGESkeletonHandler>());
	}

	virtual void ShutdownModule() override
	{
		FAGEHandlerRegistry::Get().Unregister(TEXT("Blueprint"));
		FAGEHandlerRegistry::Get().Unregister(TEXT("AnimBlueprint"));
		FAGEHandlerRegistry::Get().Unregister(TEXT("Skeleton"));
	}
};

IMPLEMENT_MODULE(FAssetGraphExporterModule, AssetGraphExporter)
