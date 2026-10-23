// Asset Graph Exporter

#include "AGEHandlerRegistry.h"

FAGEHandlerRegistry& FAGEHandlerRegistry::Get()
{
	static FAGEHandlerRegistry Instance;
	return Instance;
}

void FAGEHandlerRegistry::Register(const TSharedRef<IAGEAssetHandler>& Handler)
{
	Unregister(Handler->GetHandlerName());
	Handlers.Add(Handler);
}

void FAGEHandlerRegistry::Unregister(FName HandlerName)
{
	Handlers.RemoveAll([HandlerName](const TSharedRef<IAGEAssetHandler>& Handler)
	{
		return Handler->GetHandlerName() == HandlerName;
	});
}

const IAGEAssetHandler* FAGEHandlerRegistry::FindHandler(const UObject& Asset) const
{
	const IAGEAssetHandler* Best = nullptr;
	for (const TSharedRef<IAGEAssetHandler>& Handler : Handlers)
	{
		if (Handler->CanHandle(Asset) && (!Best || Handler->GetPriority() > Best->GetPriority()))
		{
			Best = &Handler.Get();
		}
	}
	return Best;
}
