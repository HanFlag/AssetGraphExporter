// Asset Graph Exporter

#include "AGEMcpAnim.h"
#include "AnimPose.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"

namespace AGE::McpAnimDetail
{
	constexpr int32 DefaultRows = 10;
	constexpr int32 MaxRows = 500;
	constexpr int32 MaxFolderAnimations = 500;

	double Clean(double Value)
	{
		return FMath::Abs(Value) < 0.05 ? 0.0 : Value;
	}

	FString VectorText(const FVector& Vector)
	{
		return FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Clean(Vector.X), Clean(Vector.Y), Clean(Vector.Z));
	}

	/** Up to three decimals, without trailing zeros. */
	FString Seconds(double Value)
	{
		FString Text = FString::Printf(TEXT("%.3f"), Value);
		while (Text.EndsWith(TEXT("0")))
		{
			Text.LeftChopInline(1);
		}
		Text.RemoveFromEnd(TEXT("."));
		return Text;
	}

	UAnimSequenceBase* LoadAnimation(const FString& Path, FString& OutError)
	{
		const FString ObjectPath = Path.Contains(TEXT(".")) ? Path : FString::Printf(TEXT("%s.%s"), *Path, *FPackageName::GetShortName(Path));
		UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		UAnimSequenceBase* Animation = Cast<UAnimSequenceBase>(Object);
		if (!Animation)
		{
			OutError = Object ? FString::Printf(TEXT("%s is a %s, not an animation sequence, montage or composite."), *Path, *Object->GetClass()->GetName())
				: FString::Printf(TEXT("Asset not found: %s. Use age_find_assets with class AnimSequence to look up paths."), *Path);
		}
		return Animation;
	}

	struct FInfo
	{
		const UAnimSequenceBase* Animation = nullptr;
		double Length = 0.0;
		int32 Frames = 0;
		FFrameRate Rate;
		TArray<FName> Tracks;
	};

	FInfo InfoOf(const UAnimSequenceBase& Animation)
	{
		FInfo Info;
		Info.Animation = &Animation;
		Info.Length = Animation.GetPlayLength();
		const IAnimationDataModel* Model = Animation.GetDataModel();
		Info.Rate = Model ? Model->GetFrameRate() : Animation.GetSamplingFrameRate();
		Info.Frames = Model ? Model->GetNumberOfFrames() : FMath::RoundToInt32(Info.Rate.AsDecimal() * Info.Length);
		if (Model)
		{
			Model->GetBoneTrackNames(Info.Tracks);
		}
		return Info;
	}

	FString SummaryLine(const FInfo& Info)
	{
		const UAnimSequenceBase& Animation = *Info.Animation;
		const USkeleton* Skeleton = Animation.GetSkeleton();
		const UAnimSequence* Sequence = Cast<UAnimSequence>(&Animation);
		TArray<FString> Flags;
		if (Animation.HasRootMotion())
		{
			Flags.Add(TEXT("root motion on"));
		}
		if (Animation.IsValidAdditive())
		{
			Flags.Add(TEXT("additive"));
		}
		return FString::Printf(TEXT("%s (%s): %s s, %d frames at %s fps, skeleton %s, tracks for %d of %d bones%s"), *Animation.GetPackage()->GetName(), *Animation.GetClass()->GetName(),
			*Seconds(Info.Length), Info.Frames, *Seconds(Info.Rate.AsDecimal()), Skeleton ? *Skeleton->GetPackage()->GetName() : TEXT("(none)"),
			Info.Tracks.Num(), Skeleton ? Skeleton->GetReferenceSkeleton().GetRawBoneNum() : 0,
			Flags.IsEmpty() ? TEXT("") : *(TEXT(", ") + FString::Join(Flags, TEXT(", "))));
	}

	/** Each bone must be in the animation's skeleton; an unknown one is reported with the names that contain it. */
	bool CheckBones(const UAnimSequenceBase& Animation, const TArray<FName>& Bones, FString& OutError)
	{
		const USkeleton* Skeleton = Animation.GetSkeleton();
		if (!Skeleton)
		{
			OutError = FString::Printf(TEXT("%s has no skeleton."), *Animation.GetPackage()->GetName());
			return false;
		}
		const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
		for (const FName& Bone : Bones)
		{
			if (Reference.FindRawBoneIndex(Bone) != INDEX_NONE)
			{
				continue;
			}
			TArray<FString> Similar;
			for (int32 Index = 0; Index < Reference.GetRawBoneNum() && Similar.Num() < 10; ++Index)
			{
				const FString Name = Reference.GetBoneName(Index).ToString();
				if (Name.Contains(Bone.ToString()) || Bone.ToString().Contains(Name))
				{
					Similar.Add(Name);
				}
			}
			OutError = FString::Printf(TEXT("%s's skeleton has no bone '%s'%s. age_get_asset on the skeleton lists its bones."), *Animation.GetPackage()->GetName(), *Bone.ToString(),
				Similar.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (similar: %s)"), *FString::Join(Similar, TEXT(", "))));
			return false;
		}
		return true;
	}

	/** Component-space positions of the bones, as the engine evaluates the pose (raw data, root motion kept in the pose, additive as a full pose). */
	TArray<FVector> Positions(const UAnimSequenceBase& Animation, const TArray<FName>& Bones, double Time)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Animation, Time, FAnimPoseEvaluationOptions(), Pose);
		TArray<FVector> Out;
		for (const FName& Bone : Bones)
		{
			Out.Add(UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World).GetLocation());
		}
		return Out;
	}

	/** Fastest movement of each bone between two consecutive frames: speed in cm/s and the time it starts at. */
	TArray<FString> PeakSpeeds(const FInfo& Info, const TArray<FName>& Bones)
	{
		TArray<double> Peak;
		TArray<int32> PeakFrame;
		Peak.Init(0.0, Bones.Num());
		PeakFrame.Init(0, Bones.Num());
		TArray<FVector> Previous;
		for (int32 Frame = 0; Frame <= Info.Frames; ++Frame)
		{
			const double Time = FMath::Min(Info.Rate.AsSeconds(Frame), Info.Length);
			const TArray<FVector> Now = Positions(*Info.Animation, Bones, Time);
			for (int32 Index = 0; Frame > 0 && Index < Bones.Num(); ++Index)
			{
				const double Speed = FVector::Dist(Previous[Index], Now[Index]) * Info.Rate.AsDecimal();
				if (Speed > Peak[Index])
				{
					Peak[Index] = Speed;
					PeakFrame[Index] = Frame - 1;
				}
			}
			Previous = Now;
		}
		TArray<FString> Items;
		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			Items.Add(FString::Printf(TEXT("%s %.1f cm/s at %s s (frame %d to %d)"), *Bones[Index].ToString(), Peak[Index],
				*Seconds(Info.Rate.AsSeconds(PeakFrame[Index])), PeakFrame[Index], PeakFrame[Index] + 1));
		}
		return Items;
	}

	TArray<double> RowTimes(double Length, double Step)
	{
		TArray<double> Times;
		for (int32 Row = 0; Row < MaxRows; ++Row)
		{
			const double Time = Row * Step;
			if (Time > Length - 1e-4)
			{
				break;
			}
			Times.Add(Time);
		}
		Times.Add(Length);
		return Times;
	}

	bool ReadBones(const FJsonObject& Arguments, TArray<FName>& OutBones, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Arguments.TryGetField(TEXT("bones"));
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			return true;
		}
		TArray<FString> Names;
		if (Value->Type == EJson::String)
		{
			Value->AsString().ParseIntoArray(Names, TEXT(","));
		}
		else if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				Names.Add(Item->Type == EJson::String ? Item->AsString() : FString());
			}
		}
		for (FString& Name : Names)
		{
			Name.TrimStartAndEndInline();
			if (Name.IsEmpty())
			{
				OutError = TEXT("'bones' must be bone names, e.g. [\"hand_l\", \"foot_r\"].");
				return false;
			}
			OutBones.Add(*Name);
		}
		return true;
	}

	/** One line per animation under a folder, with the bones' peak speeds when asked. */
	AGE::Mcp::FAnimOverviewAnswer FolderOverview(const FString& Folder, const TArray<FName>& Bones)
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(*Folder);
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
		TArray<FAssetData> Assets;
		IAssetRegistry::GetChecked().GetAssets(Filter, Assets);
		if (Assets.IsEmpty())
		{
			return { FString::Printf(TEXT("No AnimSequence asset or folder at %s. Use age_find_assets with class AnimSequence to look up paths."), *Folder), true };
		}
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("%s: %d AnimSequence(s)%s"), *Folder, Assets.Num(),
			Assets.Num() > MaxFolderAnimations ? *FString::Printf(TEXT(", first %d shown"), MaxFolderAnimations) : TEXT("")));
		for (int32 Index = 0; Index < Assets.Num() && Index < MaxFolderAnimations; ++Index)
		{
			const UAnimSequenceBase* Animation = Cast<UAnimSequenceBase>(Assets[Index].GetAsset());
			if (!Animation)
			{
				Lines.Add(FString::Printf(TEXT("%s: could not be loaded"), *Assets[Index].PackageName.ToString()));
				continue;
			}
			const FInfo Info = InfoOf(*Animation);
			FString Line = SummaryLine(Info);
			FString Error;
			if (!Bones.IsEmpty())
			{
				Line += CheckBones(*Animation, Bones, Error) ? TEXT("; peak speed: ") + FString::Join(PeakSpeeds(Info, Bones), TEXT("; ")) : TEXT("; ") + Error;
			}
			Lines.Add(Line);
		}
		return { FString::Join(Lines, TEXT("\n")), false };
	}
}

AGE::Mcp::FAnimOverviewAnswer AGE::Mcp::AnimOverview(const FJsonObject& Arguments)
{
	using namespace AGE::McpAnimDetail;
	FString Path;
	FString ComparePath;
	Arguments.TryGetStringField(TEXT("asset"), Path);
	Arguments.TryGetStringField(TEXT("compare"), ComparePath);
	Path.TrimStartAndEndInline();
	Path.RemoveFromEnd(TEXT("/"));
	ComparePath.TrimStartAndEndInline();
	TArray<FName> Bones;
	FString Error;
	if (!ReadBones(Arguments, Bones, Error))
	{
		return { Error, true };
	}
	double Step = 0.0;
	if (Arguments.TryGetNumberField(TEXT("step"), Step) && Step <= 0.0)
	{
		return { TEXT("'step' is in seconds and must be above 0."), true };
	}

	if (!Path.Contains(TEXT(".")) && !FPackageName::DoesPackageExist(Path))
	{
		if (!ComparePath.IsEmpty())
		{
			return { TEXT("'compare' works with one animation in 'asset', not a folder."), true };
		}
		return FolderOverview(Path, Bones);
	}
	const UAnimSequenceBase* Animation = LoadAnimation(Path, Error);
	const UAnimSequenceBase* Other = Animation && !ComparePath.IsEmpty() ? LoadAnimation(ComparePath, Error) : nullptr;
	if (!Animation || (!ComparePath.IsEmpty() && !Other))
	{
		return { Error, true };
	}
	if (!CheckBones(*Animation, Bones, Error) || (Other && !CheckBones(*Other, Bones, Error)))
	{
		return { Error, true };
	}

	const FInfo Info = InfoOf(*Animation);
	TArray<FString> Lines;
	Lines.Add(SummaryLine(Info));
	TArray<FString> TrackNames;
	for (const FName& Track : Info.Tracks)
	{
		TrackNames.Add(Track.ToString());
	}
	Lines.Add(TEXT("tracks: ") + (TrackNames.IsEmpty() ? FString(TEXT("(none)")) : FString::Join(TrackNames, TEXT(", "))));
	TOptional<FInfo> OtherInfo;
	if (Other)
	{
		OtherInfo = InfoOf(*Other);
		Lines.Add(TEXT("compare: ") + SummaryLine(*OtherInfo));
		TArray<FString> OnlyA;
		TArray<FString> OnlyB;
		for (const FName& Track : Info.Tracks)
		{
			if (!OtherInfo->Tracks.Contains(Track))
			{
				OnlyA.Add(Track.ToString());
			}
		}
		for (const FName& Track : OtherInfo->Tracks)
		{
			if (!Info.Tracks.Contains(Track))
			{
				OnlyB.Add(Track.ToString());
			}
		}
		Lines.Add(FString::Printf(TEXT("tracks only in the first: %s; only in the second: %s"), OnlyA.IsEmpty() ? TEXT("none") : *FString::Join(OnlyA, TEXT(", ")),
			OnlyB.IsEmpty() ? TEXT("none") : *FString::Join(OnlyB, TEXT(", "))));
	}
	if (Bones.IsEmpty())
	{
		Lines.Add(TEXT("Give 'bones' for their component-space positions over time and their peak speed."));
		return { FString::Join(Lines, TEXT("\n")), false };
	}

	if (Step <= 0.0)
	{
		Step = Info.Length > 0.0 ? Info.Length / DefaultRows : 1.0;
	}
	const TArray<double> Times = RowTimes(Info.Length, Step);
	const bool bSameLength = !OtherInfo.IsSet() || FMath::IsNearlyEqual(Info.Length, OtherInfo->Length, 1e-3);
	Lines.Add(FString::Printf(TEXT("component-space positions in cm (raw data, root motion kept in the pose), every %s s%s:"), *Seconds(Step),
		bSameLength ? TEXT("") : TEXT("; the lengths differ, so the second is read at the same fraction of its length")));
	for (const double Time : Times)
	{
		const TArray<FVector> Here = Positions(*Animation, Bones, Time);
		TArray<FString> Items;
		if (!OtherInfo.IsSet())
		{
			for (int32 Index = 0; Index < Bones.Num(); ++Index)
			{
				Items.Add(FString::Printf(TEXT("%s %s"), *Bones[Index].ToString(), *VectorText(Here[Index])));
			}
		}
		else
		{
			const double OtherTime = Info.Length > 0.0 ? Time / Info.Length * OtherInfo->Length : 0.0;
			const TArray<FVector> There = Positions(*Other, Bones, OtherTime);
			for (int32 Index = 0; Index < Bones.Num(); ++Index)
			{
				Items.Add(FString::Printf(TEXT("%s %s vs %s, %.1f apart"), *Bones[Index].ToString(), *VectorText(Here[Index]), *VectorText(There[Index]), FVector::Dist(Here[Index], There[Index])));
			}
		}
		Lines.Add(FString::Printf(TEXT("%s s: %s"), *Seconds(Time), *FString::Join(Items, TEXT("; "))));
	}
	Lines.Add(TEXT("peak speed: ") + FString::Join(PeakSpeeds(Info, Bones), TEXT("; ")));
	if (OtherInfo.IsSet())
	{
		Lines.Add(TEXT("peak speed of the second: ") + FString::Join(PeakSpeeds(*OtherInfo, Bones), TEXT("; ")));
	}
	return { FString::Join(Lines, TEXT("\n")), false };
}
