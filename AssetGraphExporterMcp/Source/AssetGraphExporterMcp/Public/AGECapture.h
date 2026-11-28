// Asset Graph Exporter

#pragma once

#include "CoreMinimal.h"

/** Pictures of the editor, for tools that other plugins add to the same MCP server. */
namespace AGE::Mcp
{
	struct FCaptureFrame
	{
		/** Rows from the top, opaque. */
		TArray<FColor> Pixels;
		int32 Width = 0;
		int32 Height = 0;
		/** When it was taken, on the clock of whoever took it. */
		double Seconds = 0.0;
	};

	/** A picture of the level viewport as it is now (the game, while Play In Editor runs in it), made to fit into LongSide. Game thread. */
	ASSETGRAPHEXPORTERMCP_API bool CaptureLevelViewport(int32 LongSide, FCaptureFrame& OutFrame, FString& OutError);

	/**
	 * Writes frames of one size into a new folder under the project's Saved/AGE/Captures. One frame: capture.png. Several: sheet.jpg (all of
	 * them in one picture), capture.gif, capture.mp4 (each frame shown for FrameSeconds) and frame_NNN.png. OutLines gets what was written;
	 * OutJpeg, when given, the picture to look at (the frame or the sheet, fitted into LongSide).
	 */
	ASSETGRAPHEXPORTERMCP_API void WriteCaptureFrames(const TArray<FCaptureFrame>& Frames, double FrameSeconds, int32 LongSide, TArray<FString>& OutLines, TArray64<uint8>* OutJpeg);
}
