// Asset Graph Exporter

#pragma once

#include "AGECapture.h"
#include "CoreMinimal.h"

namespace AGE::Mcp
{
	/**
	 * Writes frames of one size as an H.264 video in an mp4 file, each shown for FrameSeconds, with the encoder that comes with Windows
	 * (Media Foundation), so nothing has to be installed. False with the reason on other platforms or when Windows has no encoder.
	 */
	bool WriteMp4(const FString& Path, const TArray<FCaptureFrame>& Frames, double FrameSeconds, FString& OutError);
}
