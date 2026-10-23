// Asset Graph Exporter

#include "AGEExportContext.h"
#include "AGELog.h"

FString AGE::GuidToString(const FGuid& Guid)
{
	return Guid.ToString(EGuidFormats::DigitsWithHyphens);
}

void FAGEExportContext::Warn(const FString& Message)
{
	UE_LOG(LogAssetGraphExporter, Warning, TEXT("%s: %s"), RootAsset ? *RootAsset->GetPathName() : TEXT("?"), *Message);
	Warnings.Add(Message);
}

const void* FAGEExportContext::GetStructDefaults(const UScriptStruct& Struct)
{
	TSharedPtr<FStructOnScope>& Defaults = StructDefaults.FindOrAdd(&Struct);
	if (!Defaults.IsValid())
	{
		Defaults = MakeShared<FStructOnScope>(&Struct);
	}
	return Defaults->GetStructMemory();
}
