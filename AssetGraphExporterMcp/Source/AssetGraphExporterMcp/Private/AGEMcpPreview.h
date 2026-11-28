// Asset Graph Exporter

#pragma once

#include "AGECapture.h"
#include "CoreMinimal.h"

namespace AGE::Mcp
{
	/** Pictures of assets in a scene of their own, as an asset editor's preview shows them. The open map is not touched. */
	struct FPreviewRequest
	{
		/** Static meshes and skeletal meshes, put side by side. Empty with an animation: the animation's preview mesh. */
		TArray<FString> Assets;
		/** An animation sequence or montage that poses the skeletal meshes it fits; empty for the reference pose. */
		FString Animation;
		/** The animation time of the first picture, and the animation time between pictures. */
		double Time = 0.0;
		double Interval = 0.0;
		int32 Frames = 1;
		/** Where the camera stands, in degrees: around the assets (0 looks at the side that faces +Y, a character's front) and above the horizon. */
		double Yaw = 30.0;
		double Pitch = 15.0;
		/** The pictures are squares of this many pixels. */
		int32 Size = 1024;
	};

	/** Game thread, with the editor open. False with OutError when an asset is missing or of another kind, or the picture could not be read back. */
	bool RenderPreview(const FPreviewRequest& Request, TArray<FCaptureFrame>& OutFrames, FString& OutName, FString& OutError);
}
