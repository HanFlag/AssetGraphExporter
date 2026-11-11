// Asset Graph Exporter

#pragma once

#include "IAGEAssetHandler.h"

/** Skeletons and skeletal meshes: the bone hierarchy, which is not a reflected property (FReferenceSkeleton). */
class FAGESkeletonHandler : public IAGEAssetHandler
{
public:
	virtual FName GetHandlerName() const override { return TEXT("Skeleton"); }
	virtual int32 GetPriority() const override { return 10; }
	virtual bool CanHandle(const UObject& Asset) const override;
	virtual void Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const override;
};
