// Asset Graph Exporter

#include "AGEHandlerRegistry.h"
#include "AGEStateTreeHandler.h"
#include "Modules/ModuleManager.h"

class FAssetGraphExporterStateTreeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FAGEHandlerRegistry::Get().Register(MakeShared<FAGEStateTreeHandler>());
	}

	virtual void ShutdownModule() override
	{
		FAGEHandlerRegistry::Get().Unregister(FAGEStateTreeHandler::HandlerName);
	}
};

IMPLEMENT_MODULE(FAssetGraphExporterStateTreeModule, AssetGraphExporterStateTree)
