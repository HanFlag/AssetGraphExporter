// Asset Graph Exporter

#include "AGEMcpPreview.h"
#include "AdvancedPreviewScene.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "AssetCompilingManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Misc/PackageName.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "UnrealClient.h"

namespace AGE::McpPreviewDetail
{
	/** A narrow lens, so shapes look as they are and not stretched toward the edges. */
	constexpr float FieldOfView = 30.0f;
	/** The first pictures of a scene come before its shaders, textures and shadows have settled. */
	constexpr int32 WarmUpCaptures = 3;

	UObject* LoadAsset(const FString& Path)
	{
		FString PackageName = Path;
		int32 Dot = INDEX_NONE;
		if (PackageName.FindChar(TEXT('.'), Dot))
		{
			PackageName.LeftInline(Dot);
		}
		if (!Path.StartsWith(TEXT("/")) || !FPackageName::DoesPackageExist(PackageName))
		{
			return nullptr;
		}
		return FSoftObjectPath(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName)).TryLoad();
	}

	bool ReadTarget(UTextureRenderTarget2D& Target, AGE::Mcp::FCaptureFrame& OutFrame)
	{
		FTextureRenderTargetResource* Resource = Target.GameThread_GetRenderTargetResource();
		if (!Resource || !Resource->ReadPixels(OutFrame.Pixels) || OutFrame.Pixels.Num() != Target.SizeX * Target.SizeY)
		{
			return false;
		}
		OutFrame.Width = Target.SizeX;
		OutFrame.Height = Target.SizeY;
		for (FColor& Pixel : OutFrame.Pixels)
		{
			Pixel.A = 255;
		}
		return true;
	}
}

bool AGE::Mcp::RenderPreview(const FPreviewRequest& Request, TArray<FCaptureFrame>& OutFrames, FString& OutName, FString& OutError)
{
	using namespace AGE::McpPreviewDetail;

	UAnimSequenceBase* Animation = nullptr;
	if (!Request.Animation.IsEmpty())
	{
		Animation = Cast<UAnimSequenceBase>(LoadAsset(Request.Animation));
		if (!Animation || !Animation->GetSkeleton())
		{
			OutError = FString::Printf(TEXT("'animation' must be the path of an animation sequence or montage; '%s' is not one."), *Request.Animation);
			return false;
		}
		if (Request.Time < 0.0 || Request.Time + (Request.Frames - 1) * Request.Interval > Animation->GetPlayLength() + UE_KINDA_SMALL_NUMBER)
		{
			OutError = FString::Printf(TEXT("%s is %.3f s long; 'time' (and time + (frames - 1) x interval) must be within it."), *Request.Animation, Animation->GetPlayLength());
			return false;
		}
	}
	else if (Request.Frames > 1)
	{
		OutError = TEXT("Several pictures of a preview show an animation over time: give 'animation', or leave 'frames' out.");
		return false;
	}

	TArray<UObject*> Assets;
	for (const FString& Path : Request.Assets)
	{
		UObject* Asset = LoadAsset(Path);
		if (!Asset || (!Asset->IsA<UStaticMesh>() && !Asset->IsA<USkeletalMesh>()))
		{
			OutError = FString::Printf(TEXT("'assets' takes static meshes and skeletal meshes; '%s' is %s."), *Path, Asset ? *FString::Printf(TEXT("a %s"), *Asset->GetClass()->GetName()) : TEXT("not an asset that exists"));
			return false;
		}
		Assets.Add(Asset);
	}
	if (Assets.IsEmpty() && Animation)
	{
		USkeletalMesh* Mesh = Animation->GetPreviewMesh(/*bFindIfNotSet=*/true);
		if (!Mesh)
		{
			Mesh = Animation->GetSkeleton()->GetPreviewMesh(/*bFindIfNotSet=*/true);
		}
		if (!Mesh)
		{
			OutError = FString::Printf(TEXT("No skeletal mesh was found for the skeleton of %s; name one in 'assets'."), *Request.Animation);
			return false;
		}
		Assets.Add(Mesh);
	}
	if (Assets.IsEmpty())
	{
		OutError = TEXT("A preview needs 'assets' (static or skeletal mesh paths), 'animation', or both.");
		return false;
	}

	FAdvancedPreviewScene Scene{ FPreviewScene::ConstructionValues() };
	// Directly, so the user's own preview settings are left as they are.
	Scene.SetFloorVisibility(true, /*bDirect=*/true);
	Scene.SetEnvironmentVisibility(true, /*bDirect=*/true);

	// Side by side along X, each standing on the floor, the first one's middle at X = 0.
	TArray<USkeletalMeshComponent*> Posed;
	TArray<UPrimitiveComponent*> Components;
	TArray<FString> Names;
	bool bAnimationUsed = false;
	double Cursor = 0.0;
	for (UObject* Asset : Assets)
	{
		UPrimitiveComponent* Component = nullptr;
		FBoxSphereBounds Bounds;
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
		{
			UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(GetTransientPackage());
			MeshComponent->SetStaticMesh(StaticMesh);
			Bounds = StaticMesh->GetBounds();
			Component = MeshComponent;
		}
		else
		{
			USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(Asset);
			USkeletalMeshComponent* MeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage());
			MeshComponent->SetSkeletalMeshAsset(SkeletalMesh);
			MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Bounds = SkeletalMesh->GetBounds();
			Component = MeshComponent;
			if (Animation && SkeletalMesh->GetSkeleton() && SkeletalMesh->GetSkeleton()->IsCompatibleForEditor(Animation->GetSkeleton()))
			{
				Posed.Add(MeshComponent);
				bAnimationUsed = true;
			}
		}
		const FVector Extent = Bounds.BoxExtent;
		const FVector Location(Cursor + (Components.IsEmpty() ? 0.0 : Extent.X) - Bounds.Origin.X, -Bounds.Origin.Y, Extent.Z - Bounds.Origin.Z);
		Cursor += (Components.IsEmpty() ? Extent.X : 2.0 * Extent.X) + FMath::Max(10.0, Extent.X * 0.3);
		Scene.AddComponent(Component, FTransform(Location));
		Components.Add(Component);
		Names.Add(Asset->GetOutermost()->GetName());
	}
	if (Animation && !bAnimationUsed)
	{
		OutError = FString::Printf(TEXT("%s fits none of the skeletal meshes in 'assets' (another skeleton)."), *Request.Animation);
		return false;
	}
	for (USkeletalMeshComponent* Component : Posed)
	{
		Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Component->SetAnimation(Animation);
	}
	const auto Pose = [&Posed](double Time)
	{
		for (USkeletalMeshComponent* Component : Posed)
		{
			// Without a tick function the pose is worked out here and now, not on a worker thread.
			Component->SetPosition(static_cast<float>(Time), /*bFireNotifies=*/false);
			Component->TickAnimation(0.0f, /*bNeedsValidRootMotion=*/false);
			Component->RefreshBoneTransforms();
			Component->UpdateBounds();
		}
	};

	// Everything the pictures will show, over all the poses, so one camera fits them all.
	FBox Box(ForceInit);
	for (int32 Index = 0; Index < Request.Frames; ++Index)
	{
		Pose(Request.Time + Index * Request.Interval);
		for (const UPrimitiveComponent* Component : Components)
		{
			Box += Component->Bounds.GetBox();
		}
	}
	const FVector Center = Box.GetCenter();
	const double Radius = FMath::Max(1.0, Box.GetExtent().Size());
	const double Distance = Radius / FMath::Sin(FMath::DegreesToRadians(FieldOfView * 0.5)) * 1.05;
	const FVector FromCenter = FRotator(Request.Pitch, 90.0 + Request.Yaw, 0.0).Vector();
	const FVector CameraLocation = Center + FromCenter * Distance;

	const TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
	Target->RenderTargetFormat = RTF_RGBA8;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(Request.Size, Request.Size);
	Target->UpdateResourceImmediate(/*bClearRenderTarget=*/true);

	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(GetTransientPackage());
	Capture->TextureTarget = Target.Get();
	// What the screen would show: tone mapped, in the colors of a picture file.
	Capture->CaptureSource = SCS_FinalColorLDR;
	Capture->FOVAngle = FieldOfView;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	// Shadows and lighting build up over the warm-up pictures.
	Capture->bAlwaysPersistRenderingState = true;
	// One still picture at a time: nothing to blur between them, and no slow drift of the brightness.
	Capture->ShowFlags.SetMotionBlur(false);
	Capture->ShowFlags.SetEyeAdaptation(false);
	Scene.AddComponent(Capture, FTransform((-FromCenter).Rotation(), CameraLocation));

	for (int32 Index = 0; Index < Request.Frames; ++Index)
	{
		const double Time = Request.Time + Index * Request.Interval;
		Pose(Time);
		for (int32 Pass = 0; Pass < (Index == 0 ? WarmUpCaptures : 1); ++Pass)
		{
			Capture->CaptureScene();
			if (Index == 0 && Pass == 0)
			{
				// The first picture asked for shaders, meshes and textures: have them all before the ones that count.
				FAssetCompilingManager::Get().FinishAllCompilation();
				IStreamingManager::Get().StreamAllResources(0.0f);
			}
		}
		FCaptureFrame Frame;
		if (!ReadTarget(*Target, Frame))
		{
			OutError = TEXT("The preview was drawn but its picture could not be read back from the graphics card.");
			return false;
		}
		Frame.Seconds = Time;
		OutFrames.Add(MoveTemp(Frame));
	}

	OutName = FString::Printf(TEXT("a preview of %s%s, camera %s deg around and %s deg up"), *FString::Join(Names, TEXT(", ")),
		Animation ? *FString::Printf(TEXT(" in %s"), *Animation->GetOutermost()->GetName()) : TEXT(""),
		*FString::SanitizeFloat(Request.Yaw), *FString::SanitizeFloat(Request.Pitch));
	return true;
}
