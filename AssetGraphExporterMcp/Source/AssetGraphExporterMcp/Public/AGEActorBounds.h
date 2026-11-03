// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"

class AActor;

/** How age_level_actors measures an actor, shared so other plugins' tools report the same box. */
namespace AGE::Mcp
{
	enum class EActorBoundsMode : uint8
	{
		/** Registered primitive components that exist in the game: editor-only helpers (sprites, arrows, the camera's frustum) are skipped. */
		Visible,
		/** As Visible, and only components with collision enabled. */
		Collision,
		/** Every registered primitive component, as the engine's GetActorBounds(false). */
		All,
	};

	/** World-space, world-axis box of the actor's components. Not valid when no component counts (a light, an empty actor). */
	ASSETGRAPHEXPORTERMCP_API FBox ActorBounds(const AActor& Actor, EActorBoundsMode Mode = EActorBoundsMode::Visible);

	/** "(x, y, z)-(x, y, z)" with one decimal, or "none" for a box that is not valid. */
	ASSETGRAPHEXPORTERMCP_API FString BoxText(const FBox& Box);
}
