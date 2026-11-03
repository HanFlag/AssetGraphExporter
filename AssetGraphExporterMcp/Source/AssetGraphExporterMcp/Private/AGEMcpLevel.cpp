// Asset Graph Exporter

#include "AGEMcpLevel.h"
#include "AGEActorBounds.h"
#include "AGEMcpState.h"
#include "ActorEditorUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Math/Box.h"
#include "UObject/Package.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"

FBox AGE::Mcp::ActorBounds(const AActor& Actor, EActorBoundsMode Mode)
{
	// The engine's GetComponentsBoundingBox (Actor.cpp), minus editor-only components for Visible and Collision:
	// a camera's frustum and proxy mesh alone can make a character's box many meters wide.
	FBox Box(ForceInit);
	Actor.ForEachComponent<UPrimitiveComponent>(false, [&Box, Mode](const UPrimitiveComponent* Component)
	{
		if (!Component->IsRegistered()
			|| (Mode != EActorBoundsMode::All && Component->IsEditorOnly())
			|| (Mode == EActorBoundsMode::Collision && !Component->IsCollisionEnabled()))
		{
			return;
		}
		Box += Component->Bounds.GetBox();
	});
	return Box;
}

namespace AGE::McpLevelDetail
{
	constexpr int32 DefaultLimit = 100;
	constexpr int32 MaxLimit = 1000;
	constexpr int32 MaxMeshesShown = 2;
	constexpr int32 MaxSimilarShown = 5;

	/** Rounds away what %.1f would print as -0.0 or as noise around zero. */
	double Clean(double Value)
	{
		return FMath::Abs(Value) < 0.05 ? 0.0 : Value;
	}

	FString VectorText(const FVector& Vector)
	{
		return FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Clean(Vector.X), Clean(Vector.Y), Clean(Vector.Z));
	}

	/** Up to three decimals, without trailing zeros: 1, 2.5, 0.125. */
	FString ShortNumber(double Value)
	{
		FString Text = FString::Printf(TEXT("%.3f"), Value);
		while (Text.EndsWith(TEXT("0")))
		{
			Text.LeftChopInline(1);
		}
		Text.RemoveFromEnd(TEXT("."));
		return Text == TEXT("-0") ? TEXT("0") : Text;
	}

	FString ScaleText(const FVector& Scale)
	{
		if (Scale.Equals(FVector::OneVector, 0.0005))
		{
			return FString();
		}
		if (FMath::IsNearlyEqual(Scale.X, Scale.Y, 0.0005) && FMath::IsNearlyEqual(Scale.X, Scale.Z, 0.0005))
		{
			return TEXT(" scale ") + ShortNumber(Scale.X);
		}
		return FString::Printf(TEXT(" scale (%s, %s, %s)"), *ShortNumber(Scale.X), *ShortNumber(Scale.Y), *ShortNumber(Scale.Z));
	}

	FString RotationText(const FRotator& Rotation)
	{
		if (Clean(Rotation.Pitch) == 0.0 && Clean(Rotation.Yaw) == 0.0 && Clean(Rotation.Roll) == 0.0)
		{
			return FString();
		}
		return FString::Printf(TEXT(" rot (%.1f, %.1f, %.1f)"), Clean(Rotation.Pitch), Clean(Rotation.Yaw), Clean(Rotation.Roll));
	}

	/** Static and skeletal meshes the actor shows in the game, by package path. */
	FString MeshText(const AActor& Actor)
	{
		TArray<FString> Meshes;
		Actor.ForEachComponent<UPrimitiveComponent>(false, [&Meshes](const UPrimitiveComponent* Component)
		{
			if (Component->IsEditorOnly())
			{
				return;
			}
			const UObject* Mesh = nullptr;
			if (const UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component))
			{
				Mesh = StaticMesh->GetStaticMesh();
			}
			else if (const USkinnedMeshComponent* SkinnedMesh = Cast<USkinnedMeshComponent>(Component))
			{
				Mesh = SkinnedMesh->GetSkinnedAsset();
			}
			if (Mesh)
			{
				Meshes.AddUnique(Mesh->GetPackage()->GetName());
			}
		});
		if (Meshes.IsEmpty())
		{
			return FString();
		}
		Meshes.Sort();
		const int32 Extra = Meshes.Num() - MaxMeshesShown;
		Meshes.SetNum(FMath::Min(Meshes.Num(), MaxMeshesShown));
		return TEXT(" mesh ") + FString::Join(Meshes, TEXT(", ")) + (Extra > 0 ? FString::Printf(TEXT(" +%d more"), Extra) : FString());
	}

	/** "BP_Door_C" and "BP_Door" name the same class; "AStaticMeshActor" is the C++ spelling of "StaticMeshActor". */
	bool ClassNameMatches(FString ClassName, FString Filter)
	{
		ClassName.RemoveFromEnd(TEXT("_C"));
		Filter.RemoveFromEnd(TEXT("_C"));
		if (ClassName.Equals(Filter, ESearchCase::IgnoreCase))
		{
			return true;
		}
		return Filter.Len() > 1 && (Filter[0] == TEXT('A') || Filter[0] == TEXT('U')) && FChar::IsUpper(Filter[1])
			&& ClassName.Equals(Filter.Mid(1), ESearchCase::IgnoreCase);
	}

	bool ClassMatches(const UClass* Class, const TArray<FString>& Filters)
	{
		for (; Class; Class = Class->GetSuperClass())
		{
			for (const FString& Filter : Filters)
			{
				if (ClassNameMatches(Class->GetName(), Filter))
				{
					return true;
				}
			}
		}
		return false;
	}

	struct FRange
	{
		enum class EKind : uint8 { Level, Box, Sphere };
		EKind Kind = EKind::Level;
		FBox Box = FBox(ForceInit);
		FVector Center = FVector::ZeroVector;
		double Radius = 0.0;

		bool Overlaps(const FBox& Bounds) const
		{
			switch (Kind)
			{
			case EKind::Box: return Box.Intersect(Bounds);
			case EKind::Sphere: return FMath::SphereAABBIntersection(Center, Radius * Radius, Bounds);
			default: return true;
			}
		}

		bool Contains(const FVector& Point) const
		{
			switch (Kind)
			{
			case EKind::Box: return Box.IsInsideOrOn(Point);
			case EKind::Sphere: return FVector::DistSquared(Center, Point) <= Radius * Radius;
			default: return true;
			}
		}

		/** Box that holds the whole range, for the World Partition query. */
		FBox QueryBox() const
		{
			return Kind == EKind::Sphere ? FBox(Center - FVector(Radius), Center + FVector(Radius)) : Box;
		}
	};

	struct FEntry
	{
		FString Line;
		double Distance = 0.0;
		FString Name;
		FString Path;
	};

	/** [x, y, z] of JSON numbers only: a string such as "100" is refused rather than read as a number. */
	bool ReadVector(const FJsonObject& Arguments, const TCHAR* Key, TOptional<FVector>& OutVector, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Arguments.TryGetField(Key);
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			return true;
		}
		if (Value->Type == EJson::Array && Value->AsArray().Num() == 3)
		{
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			if (Items[0]->Type == EJson::Number && Items[1]->Type == EJson::Number && Items[2]->Type == EJson::Number)
			{
				OutVector = FVector(Items[0]->AsNumber(), Items[1]->AsNumber(), Items[2]->AsNumber());
				return true;
			}
		}
		OutError = FString::Printf(TEXT("'%s' must be an array of three numbers, e.g. [0, 0, 100]."), Key);
		return false;
	}

	bool ReadNumber(const FJsonObject& Arguments, const TCHAR* Key, TOptional<double>& OutNumber, FString& OutError)
	{
		const TSharedPtr<FJsonValue> Value = Arguments.TryGetField(Key);
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			return true;
		}
		if (Value->Type != EJson::Number)
		{
			OutError = FString::Printf(TEXT("'%s' must be a number."), Key);
			return false;
		}
		OutNumber = Value->AsNumber();
		return true;
	}

	FString StringArgument(const FJsonObject& Arguments, const TCHAR* Key)
	{
		FString Value;
		Arguments.TryGetStringField(Key, Value);
		return Value.TrimStartAndEnd();
	}

	FString LabelOf(const AActor& Actor)
	{
		const FString& Label = Actor.GetActorLabel(false);
		return Label.IsEmpty() ? Actor.GetName() : Label;
	}

	FString NameText(const FString& Label, const FString& Name)
	{
		return Label == Name ? Name : FString::Printf(TEXT("%s [%s]"), *Label, *Name);
	}

	/** One displayable actor by exact label or object name, as age_* tools that take an actor name find it. */
	AActor* FindOneActor(UWorld& World, const FString& Query, FString& OutError)
	{
		TArray<AActor*> Matches;
		TArray<FString> Similar;
		int32 SimilarCount = 0;
		for (TActorIterator<AActor> It(&World); It; ++It)
		{
			if (!FActorEditorUtils::IsActorDisplayable(*It))
			{
				continue;
			}
			const FString Label = LabelOf(**It);
			if (Label == Query || It->GetName() == Query)
			{
				Matches.Add(*It);
			}
			else if ((Label.Contains(Query) || It->GetName().Contains(Query)) && ++SimilarCount <= MaxSimilarShown)
			{
				Similar.Add(NameText(Label, It->GetName()));
			}
		}
		if (Matches.Num() == 1)
		{
			return Matches[0];
		}
		if (Matches.IsEmpty())
		{
			OutError = FString::Printf(TEXT("'around': no loaded actor is labeled or named '%s'."), *Query);
			if (!Similar.IsEmpty())
			{
				Similar.Sort();
				OutError += FString::Printf(TEXT(" Similar: %s%s."), *FString::Join(Similar, TEXT(", ")),
					SimilarCount > MaxSimilarShown ? *FString::Printf(TEXT(" and %d more"), SimilarCount - MaxSimilarShown) : TEXT(""));
			}
			return nullptr;
		}
		TArray<FString> Names;
		for (const AActor* Match : Matches)
		{
			Names.Add(Match->GetName());
		}
		Names.Sort();
		OutError = FString::Printf(TEXT("'around': '%s' matches %d actors: %s. Use the object name."), *Query, Matches.Num(), *FString::Join(Names, TEXT(", ")));
		return nullptr;
	}
}

FString AGE::Mcp::BoxText(const FBox& Box)
{
	using namespace AGE::McpLevelDetail;
	return Box.IsValid ? VectorText(Box.Min) + TEXT("-") + VectorText(Box.Max) : FString(TEXT("none"));
}

AGE::Mcp::FLevelActorsAnswer AGE::Mcp::LevelActors(const FJsonObject& Arguments)
{
	using namespace AGE::McpLevelDetail;
	auto Error = [](const FString& Text) { return FLevelActorsAnswer{ Text, true }; };

	if (IsServeMode() || !GEditor)
	{
		return Error(TEXT("age_level_actors reads the level open in the editor, and this is the background server, which reads saved files only. "
			"Ask the user to open the project in the Unreal editor, then call it again."));
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return Error(TEXT("The editor has no level open."));
	}

	FString ArgumentError;
	TOptional<FVector> Min, Max, Center;
	TOptional<double> Radius, LimitNumber;
	if (!ReadVector(Arguments, TEXT("min"), Min, ArgumentError) || !ReadVector(Arguments, TEXT("max"), Max, ArgumentError)
		|| !ReadVector(Arguments, TEXT("center"), Center, ArgumentError)
		|| !ReadNumber(Arguments, TEXT("radius"), Radius, ArgumentError) || !ReadNumber(Arguments, TEXT("limit"), LimitNumber, ArgumentError))
	{
		return Error(ArgumentError);
	}
	const FString Around = StringArgument(Arguments, TEXT("around"));
	const FString NameFilter = StringArgument(Arguments, TEXT("name"));
	TArray<FString> ClassFilters;
	StringArgument(Arguments, TEXT("class")).ParseIntoArray(ClassFilters, TEXT(","));
	for (FString& Filter : ClassFilters)
	{
		Filter.TrimStartAndEndInline();
	}
	ClassFilters.RemoveAll([](const FString& Filter) { return Filter.IsEmpty(); });

	const FString BoundsName = StringArgument(Arguments, TEXT("bounds"));
	EActorBoundsMode Mode = EActorBoundsMode::Visible;
	if (BoundsName.Equals(TEXT("collision"), ESearchCase::IgnoreCase))
	{
		Mode = EActorBoundsMode::Collision;
	}
	else if (BoundsName.Equals(TEXT("all"), ESearchCase::IgnoreCase))
	{
		Mode = EActorBoundsMode::All;
	}
	else if (!BoundsName.IsEmpty() && !BoundsName.Equals(TEXT("visible"), ESearchCase::IgnoreCase))
	{
		return Error(FString::Printf(TEXT("'bounds' is visible, collision or all, not '%s'."), *BoundsName));
	}
	const TCHAR* ModeName = Mode == EActorBoundsMode::Visible ? TEXT("visible") : Mode == EActorBoundsMode::Collision ? TEXT("collision") : TEXT("all");

	int32 Limit = DefaultLimit;
	if (LimitNumber.IsSet())
	{
		if (*LimitNumber < 1 || *LimitNumber > MaxLimit || *LimitNumber != FMath::RoundToDouble(*LimitNumber))
		{
			return Error(FString::Printf(TEXT("'limit' must be a whole number from 1 to %d."), MaxLimit));
		}
		Limit = static_cast<int32>(*LimitNumber);
	}

	FRange Range;
	FString RangeText = TEXT("whole level");
	const bool bBox = Min.IsSet() || Max.IsSet();
	const bool bSphere = Center.IsSet() || !Around.IsEmpty() || Radius.IsSet();
	if (bBox && bSphere)
	{
		return Error(TEXT("Give either a box (min and max) or a sphere (center or around, with radius), not both."));
	}
	if (bBox)
	{
		if (!Min.IsSet() || !Max.IsSet())
		{
			return Error(TEXT("A box needs both 'min' and 'max'."));
		}
		if (Min->X > Max->X || Min->Y > Max->Y || Min->Z > Max->Z)
		{
			return Error(TEXT("Each value of 'min' must be at most the same value of 'max'."));
		}
		Range.Kind = FRange::EKind::Box;
		Range.Box = FBox(*Min, *Max);
		Range.Center = Range.Box.GetCenter();
		RangeText = TEXT("box ") + BoxText(Range.Box);
	}
	else if (bSphere)
	{
		if (Center.IsSet() == !Around.IsEmpty())
		{
			return Error(TEXT("A sphere needs 'center' ([x, y, z]) or 'around' (an actor's label or name), one of the two, and 'radius'."));
		}
		if (!Radius.IsSet() || *Radius <= 0.0)
		{
			return Error(TEXT("A sphere needs 'radius', a number above 0."));
		}
		Range.Kind = FRange::EKind::Sphere;
		Range.Radius = *Radius;
		if (Center.IsSet())
		{
			Range.Center = *Center;
			RangeText = FString::Printf(TEXT("sphere %s radius %s"), *VectorText(Range.Center), *ShortNumber(Range.Radius));
		}
		else
		{
			FString FindError;
			const AActor* Anchor = FindOneActor(*World, Around, FindError);
			if (!Anchor)
			{
				return Error(FindError);
			}
			const FBox AnchorBox = ActorBounds(*Anchor, Mode);
			Range.Center = AnchorBox.IsValid ? AnchorBox.GetCenter() : Anchor->GetActorLocation();
			RangeText = FString::Printf(TEXT("sphere %s radius %s (center of %s)"), *VectorText(Range.Center), *ShortNumber(Range.Radius),
				*NameText(LabelOf(*Anchor), Anchor->GetName()));
		}
	}

	const UPackage* MapPackage = World->GetPackage();
	// In World Partition each actor has its own package, so the map's mark alone misses unsaved actor changes.
	bool bUnsaved = MapPackage->IsDirty();
	TArray<FEntry> Entries;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const AActor& Actor = **It;
		// What the Outliner lists (FActorMode::IsActorDisplayable), without its view options.
		if (!FActorEditorUtils::IsActorDisplayable(&Actor) || Actor.HasAnyFlags(RF_Transient))
		{
			continue;
		}
		if (!ClassFilters.IsEmpty() && !ClassMatches(Actor.GetClass(), ClassFilters))
		{
			continue;
		}
		const FString Label = LabelOf(Actor);
		if (!NameFilter.IsEmpty() && !Label.Contains(NameFilter) && !Actor.GetName().Contains(NameFilter))
		{
			continue;
		}
		const FBox Box = ActorBounds(Actor, Mode);
		const FVector Location = Actor.GetActorLocation();
		if (Box.IsValid ? !Range.Overlaps(Box) : !Range.Contains(Location))
		{
			continue;
		}

		bUnsaved |= Actor.GetPackage()->IsDirty();
		FEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Name = Actor.GetName();
		Entry.Path = Actor.GetPathName();
		Entry.Distance = Range.Kind == FRange::EKind::Level ? 0.0 : FVector::Dist(Range.Center, Location);
		Entry.Line = FString::Printf(TEXT("%s %s at %s%s%s box %s%s%s"), *NameText(Label, Entry.Name), *Actor.GetClass()->GetName(),
			*VectorText(Location), *RotationText(Actor.GetActorRotation()), *ScaleText(Actor.GetActorScale3D()), *BoxText(Box), *MeshText(Actor),
			Actor.IsHiddenEd() ? TEXT(" hidden") : TEXT(""));
	}

	// World Partition: actors in unloaded regions are known from their saved descriptions; nothing is loaded.
	int32 UnloadedCount = 0;
	if (UWorldPartition* WorldPartition = World->GetWorldPartition())
	{
		auto AddUnloaded = [&](const FWorldPartitionActorDescInstance* Desc)
		{
			if (Desc->IsLoaded())
			{
				return true;
			}
			const FBox Box = Desc->GetEditorBounds();
			if (Box.IsValid ? !Range.Overlaps(Box) : Range.Kind != FRange::EKind::Level)
			{
				return true;
			}
			const UClass* NativeClass = Desc->GetActorNativeClass();
			const FTopLevelAssetPath BaseClass = Desc->GetBaseClass();
			const FString ClassName = BaseClass.IsValid() ? BaseClass.GetAssetName().ToString() : NativeClass ? NativeClass->GetName() : FString(TEXT("?"));
			if (!ClassFilters.IsEmpty() && !ClassMatches(NativeClass, ClassFilters)
				&& !ClassFilters.ContainsByPredicate([&ClassName](const FString& Filter) { return ClassNameMatches(ClassName, Filter); }))
			{
				return true;
			}
			const FString Name = Desc->GetActorNameString();
			const FString Label = Desc->GetActorLabelString().IsEmpty() ? Name : Desc->GetActorLabelString();
			if (!NameFilter.IsEmpty() && !Label.Contains(NameFilter) && !Name.Contains(NameFilter))
			{
				return true;
			}
			FEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Name = Name;
			Entry.Path = Desc->GetActorSoftPath().ToString();
			Entry.Distance = Range.Kind == FRange::EKind::Level || !Box.IsValid ? 0.0 : FVector::Dist(Range.Center, Box.GetCenter());
			Entry.Line = FString::Printf(TEXT("%s %s unloaded box %s"), *NameText(Label, Name), *ClassName, *BoxText(Box));
			++UnloadedCount;
			return true;
		};
		if (Range.Kind == FRange::EKind::Level)
		{
			FWorldPartitionHelpers::ForEachActorDescInstance(WorldPartition, AActor::StaticClass(), AddUnloaded);
		}
		else
		{
			FWorldPartitionHelpers::ForEachIntersectingActorDescInstance(WorldPartition, Range.QueryBox(), AActor::StaticClass(), AddUnloaded);
		}
	}

	// Fixed order, so the same level and arguments give the same text.
	Entries.Sort([](const FEntry& A, const FEntry& B)
	{
		if (A.Distance != B.Distance)
		{
			return A.Distance < B.Distance;
		}
		const int32 ByName = A.Name.Compare(B.Name, ESearchCase::CaseSensitive);
		return ByName != 0 ? ByName < 0 : A.Path.Compare(B.Path, ESearchCase::CaseSensitive) < 0;
	});

	TArray<FString> Notes;
	Notes.Add(FString::Printf(TEXT("World Partition: %s"), World->IsPartitionedWorld() ? TEXT("yes") : TEXT("no")));
	if (GEditor->IsPlaySessionInProgress())
	{
		Notes.Add(TEXT("a play session is running; this is the editor's level, not the game's"));
	}
	FString Filters;
	if (!ClassFilters.IsEmpty())
	{
		Filters += TEXT(", class ") + FString::Join(ClassFilters, TEXT(", "));
	}
	if (!NameFilter.IsEmpty())
	{
		Filters += FString::Printf(TEXT(", name \"%s\""), *NameFilter);
	}

	TArray<FString> Lines;
	Lines.Add(bUnsaved ? TEXT("source: editor (has unsaved changes)") : TEXT("source: editor"));
	Lines.Add(FString::Printf(TEXT("%s (%s), %s, bounds %s%s: %d actor(s)%s%s, %s"), *MapPackage->GetName(), *FString::Join(Notes, TEXT("; ")), *RangeText, ModeName, *Filters,
		Entries.Num(), UnloadedCount > 0 ? *FString::Printf(TEXT(" (%d unloaded)"), UnloadedCount) : TEXT(""),
		Entries.Num() > Limit ? *FString::Printf(TEXT(", first %d shown"), Limit) : TEXT(""),
		Range.Kind == FRange::EKind::Level ? TEXT("by object name") : TEXT("nearest first")));
	for (int32 Index = 0; Index < Entries.Num() && Index < Limit; ++Index)
	{
		Lines.Add(Entries[Index].Line);
	}
	if (Entries.Num() > Limit)
	{
		Lines.Add(FString::Printf(TEXT("... %d more. Narrow the range or the filters, or raise 'limit' (at most %d)."), Entries.Num() - Limit, MaxLimit));
	}
	if (Entries.IsEmpty() && !ClassFilters.IsEmpty())
	{
		Lines.Add(TEXT("No actor matched. 'class' is matched against each actor's class and its parents by name, e.g. StaticMeshActor, PointLight or BP_Door."));
	}
	return FLevelActorsAnswer{ FString::Join(Lines, TEXT("\n")), false };
}
