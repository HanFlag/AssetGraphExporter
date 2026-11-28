// Asset Graph Exporter

#include "AGEMcpVideo.h"
#include "HAL/FileManager.h"

#if PLATFORM_WINDOWS

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"

THIRD_PARTY_INCLUDES_START

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformTypes.h"
#include "Microsoft/COMPointer.h"

namespace AGE::McpVideoDetail
{
	/** Media Foundation times are in units of 100 nanoseconds. */
	constexpr double TicksPerSecond = 10'000'000.0;

	/** Ends what Begin started, in the same thread. */
	struct FMediaFoundationScope
	{
		bool bCom = false;
		bool bStarted = false;

		bool Begin(FString& OutError)
		{
			// S_FALSE: already initialized on this thread, still to be balanced. RPC_E_CHANGED_MODE: initialized another way by someone else, usable as it is.
			const HRESULT Com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
			bCom = SUCCEEDED(Com);
			if (!bCom && Com != RPC_E_CHANGED_MODE)
			{
				OutError = FString::Printf(TEXT("COM could not be started (HRESULT 0x%08X)"), static_cast<uint32>(Com));
				return false;
			}
			const HRESULT Startup = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
			bStarted = SUCCEEDED(Startup);
			if (!bStarted)
			{
				OutError = FString::Printf(TEXT("Windows Media Foundation is not available (HRESULT 0x%08X; Windows N editions need the Media Feature Pack)"), static_cast<uint32>(Startup));
			}
			return bStarted;
		}

		~FMediaFoundationScope()
		{
			if (bStarted)
			{
				::MFShutdown();
			}
			if (bCom)
			{
				::CoUninitialize();
			}
		}
	};
}

bool AGE::Mcp::WriteMp4(const FString& Path, const TArray<FCaptureFrame>& Frames, double FrameSeconds, FString& OutError)
{
	using namespace AGE::McpVideoDetail;
	// H.264 takes even sizes only; an odd last column or row is left out.
	const int32 Width = Frames.IsEmpty() ? 0 : Frames[0].Width & ~1;
	const int32 Height = Frames.IsEmpty() ? 0 : Frames[0].Height & ~1;
	if (Width < 2 || Height < 2 || FrameSeconds <= 0.0)
	{
		OutError = TEXT("no frames, or frames too small for a video");
		return false;
	}
	FMediaFoundationScope Scope;
	if (!Scope.Begin(OutError))
	{
		return false;
	}

	// The step each call failed at, so a refusal says where.
	const TCHAR* Step = TEXT("");
	HRESULT Hr = S_OK;
	const auto Try = [&Hr, &Step](const TCHAR* Name, HRESULT Result)
	{
		if (SUCCEEDED(Hr))
		{
			Step = Name;
			Hr = Result;
		}
	};
	const LONGLONG Duration = static_cast<LONGLONG>(FrameSeconds * TicksPerSecond);
	// Frames a second as a ratio, exact enough for any interval.
	const UINT32 RateNumerator = static_cast<UINT32>(FMath::Clamp(FMath::RoundToInt32(1000.0 / FrameSeconds), 1, 1000000));
	// About 0.15 bits a pixel and frame, at least 1 Mbit/s: editor pictures have sharp text and lines.
	const UINT32 Bitrate = static_cast<UINT32>(FMath::Clamp(static_cast<double>(Width) * Height * (1.0 / FrameSeconds) * 0.15, 1000000.0, 40000000.0));
	{
		TComPtr<IMFSinkWriter> Writer;
		TComPtr<IMFMediaType> OutType;
		TComPtr<IMFMediaType> InType;
		DWORD Stream = 0;
		Try(TEXT("making the mp4 writer"), ::MFCreateSinkWriterFromURL(*Path, nullptr, nullptr, &Writer));
		if (SUCCEEDED(Hr))
		{
			Try(TEXT("making the output type"), ::MFCreateMediaType(&OutType));
		}
		if (SUCCEEDED(Hr))
		{
			Try(TEXT("setting the output type"), OutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
			Try(TEXT("setting the output type"), OutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
			Try(TEXT("setting the output type"), OutType->SetUINT32(MF_MT_AVG_BITRATE, Bitrate));
			Try(TEXT("setting the output type"), OutType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
			Try(TEXT("setting the output type"), ::MFSetAttributeSize(OutType, MF_MT_FRAME_SIZE, Width, Height));
			Try(TEXT("setting the output type"), ::MFSetAttributeRatio(OutType, MF_MT_FRAME_RATE, RateNumerator, 1000));
			Try(TEXT("setting the output type"), ::MFSetAttributeRatio(OutType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
			Try(TEXT("adding the H.264 stream (is the Windows H.264 encoder there?)"), Writer->AddStream(OutType, &Stream));
			Try(TEXT("making the input type"), ::MFCreateMediaType(&InType));
		}
		if (SUCCEEDED(Hr))
		{
			// FColor is B, G, R, A in memory: Media Foundation's RGB32. A positive stride says the rows come from the top.
			Try(TEXT("setting the input type"), InType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
			Try(TEXT("setting the input type"), InType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
			Try(TEXT("setting the input type"), InType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(Width * 4)));
			Try(TEXT("setting the input type"), InType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
			Try(TEXT("setting the input type"), ::MFSetAttributeSize(InType, MF_MT_FRAME_SIZE, Width, Height));
			Try(TEXT("setting the input type"), ::MFSetAttributeRatio(InType, MF_MT_FRAME_RATE, RateNumerator, 1000));
			Try(TEXT("setting the input type"), ::MFSetAttributeRatio(InType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
			Try(TEXT("giving the encoder the picture format"), Writer->SetInputMediaType(Stream, InType, nullptr));
			Try(TEXT("starting to write"), Writer->BeginWriting());
		}
		for (int32 Index = 0; Index < Frames.Num() && SUCCEEDED(Hr); ++Index)
		{
			const FCaptureFrame& Frame = Frames[Index];
			if ((Frame.Width & ~1) != Width || (Frame.Height & ~1) != Height)
			{
				OutError = TEXT("the frames are not all of one size");
				return false;
			}
			const DWORD Bytes = static_cast<DWORD>(Width * Height * 4);
			TComPtr<IMFMediaBuffer> Buffer;
			TComPtr<IMFSample> Sample;
			BYTE* Data = nullptr;
			Try(TEXT("making a frame buffer"), ::MFCreateMemoryBuffer(Bytes, &Buffer));
			if (SUCCEEDED(Hr))
			{
				Try(TEXT("locking a frame buffer"), Buffer->Lock(&Data, nullptr, nullptr));
			}
			if (SUCCEEDED(Hr))
			{
				for (int32 Y = 0; Y < Height; ++Y)
				{
					FMemory::Memcpy(Data + static_cast<SIZE_T>(Y) * Width * 4, &Frame.Pixels[Y * Frame.Width], static_cast<SIZE_T>(Width) * 4);
				}
				Buffer->Unlock();
				Try(TEXT("writing a frame"), Buffer->SetCurrentLength(Bytes));
				Try(TEXT("writing a frame"), ::MFCreateSample(&Sample));
			}
			if (SUCCEEDED(Hr))
			{
				Try(TEXT("writing a frame"), Sample->AddBuffer(Buffer));
				Try(TEXT("writing a frame"), Sample->SetSampleTime(Duration * Index));
				Try(TEXT("writing a frame"), Sample->SetSampleDuration(Duration));
				Try(TEXT("writing a frame"), Writer->WriteSample(Stream, Sample));
			}
		}
		if (SUCCEEDED(Hr))
		{
			Try(TEXT("finishing the file"), Writer->Finalize());
		}
		// The writer lets go of the file here, before a failed one is deleted and before Media Foundation shuts down.
	}
	if (FAILED(Hr))
	{
		IFileManager::Get().Delete(*Path, /*RequireExists=*/false, /*EvenReadOnly=*/false, /*Quiet=*/true);
		OutError = FString::Printf(TEXT("%s failed (HRESULT 0x%08X)"), Step, static_cast<uint32>(Hr));
		return false;
	}
	return true;
}

#else

bool AGE::Mcp::WriteMp4(const FString& Path, const TArray<FCaptureFrame>& Frames, double FrameSeconds, FString& OutError)
{
	OutError = TEXT("videos are written with the encoder that comes with Windows, so only on Windows");
	return false;
}

#endif
