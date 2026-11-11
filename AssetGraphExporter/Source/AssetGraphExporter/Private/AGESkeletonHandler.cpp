// Asset Graph Exporter

#include "AGESkeletonHandler.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

namespace
{
	/** One entry per bone in the engine's order, where a parent comes before its children; the root has no parent. */
	TArray<TSharedPtr<FJsonValue>> BonesJson(const FReferenceSkeleton& Skeleton)
	{
		const TArray<FMeshBoneInfo>& Bones = Skeleton.GetRawRefBoneInfo();
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Reserve(Bones.Num());
		for (const FMeshBoneInfo& Bone : Bones)
		{
			const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("name"), Bone.Name.ToString());
			if (Bones.IsValidIndex(Bone.ParentIndex))
			{
				Json->SetStringField(TEXT("parent"), Bones[Bone.ParentIndex].Name.ToString());
			}
			Out.Add(MakeShared<FJsonValueObject>(Json));
		}
		return Out;
	}
}

bool FAGESkeletonHandler::CanHandle(const UObject& Asset) const
{
	return Asset.IsA<USkeleton>() || Asset.IsA<USkeletalMesh>();
}

void FAGESkeletonHandler::Export(const UObject& Asset, FAGEExportContext& Context, FJsonObject& OutAssetJson) const
{
	if (const USkeleton* Skeleton = Cast<USkeleton>(&Asset))
	{
		OutAssetJson.SetArrayField(TEXT("bones"), BonesJson(Skeleton->GetReferenceSkeleton()));
		return;
	}
	const USkeletalMesh& Mesh = *CastChecked<USkeletalMesh>(&Asset);
	const FReferenceSkeleton& MeshBones = Mesh.GetRefSkeleton();
	OutAssetJson.SetArrayField(TEXT("bones"), BonesJson(MeshBones));
	// A skeleton shared by several meshes can have bones this mesh lacks; animating those does nothing on it.
	if (const USkeleton* Skeleton = Mesh.GetSkeleton())
	{
		TArray<TSharedPtr<FJsonValue>> Missing;
		for (const FMeshBoneInfo& Bone : Skeleton->GetReferenceSkeleton().GetRawRefBoneInfo())
		{
			if (MeshBones.FindRawBoneIndex(Bone.Name) == INDEX_NONE)
			{
				Missing.Add(MakeShared<FJsonValueString>(Bone.Name.ToString()));
			}
		}
		OutAssetJson.SetArrayField(TEXT("skeletonBonesNotInMesh"), Missing);
	}
}
