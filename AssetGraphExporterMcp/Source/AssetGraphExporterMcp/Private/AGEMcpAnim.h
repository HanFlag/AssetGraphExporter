// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace AGE::Mcp
{
	struct FAnimOverviewAnswer
	{
		FString Text;
		bool bError = false;
	};

	/** age_anim_overview: length, frames, bone tracks, chosen bones' component-space positions over time and peak speeds; a second animation to compare, or a folder. */
	FAnimOverviewAnswer AnimOverview(const FJsonObject& Arguments);
}
