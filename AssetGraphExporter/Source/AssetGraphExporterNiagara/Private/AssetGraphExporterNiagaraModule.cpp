// Asset Graph Exporter

#include "AGEHandlerRegistry.h"
#include "AGENiagaraSystemHandler.h"
#include "Modules/ModuleManager.h"

class FAssetGraphExporterNiagaraModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FAGEHandlerRegistry::Get().Register(MakeShared<FAGENiagaraSystemHandler>());
	}

	virtual void ShutdownModule() override
	{
		FAGEHandlerRegistry::Get().Unregister(FAGENiagaraSystemHandler::HandlerName);
	}
};

IMPLEMENT_MODULE(FAssetGraphExporterNiagaraModule, AssetGraphExporterNiagara)
