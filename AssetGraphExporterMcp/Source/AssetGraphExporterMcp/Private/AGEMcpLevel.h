// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace AGE::Mcp
{
	struct FLevelActorsAnswer
	{
		FString Text;
		bool bError = false;
	};

	/** age_level_actors: the actors of the level open in the editor within a box or sphere, one line each. */
	FLevelActorsAnswer LevelActors(const FJsonObject& Arguments);
}
