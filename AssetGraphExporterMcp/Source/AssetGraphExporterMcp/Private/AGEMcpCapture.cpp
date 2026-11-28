// Asset Graph Exporter

#include "AGEMcpCapture.h"
#include "AGEMcpToolLog.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IAssetViewport.h"
#include "IModelContextProtocolTool.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "LevelEditor.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ModelContextProtocolSession.h"
#include "ModelContextProtocolToolResults.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/SoftObjectPath.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"

namespace AGE::McpCaptureDetail
{
	using UE::ModelContextProtocol::MakeErrorResult;
	using UE::ModelContextProtocol::MakeTextResult;

	constexpr int32 MaxFrames = 60;
	/** A capture answers within this, well inside a client's tool timeout. */
	constexpr double MaxSeconds = 20.0;
	constexpr int32 DefaultLongSide = 1024;
	constexpr int32 MaxLongSide = 1568;
	/** A newly opened editor tab needs a few frames before it has drawn itself. */
	constexpr double SettleSeconds = 0.5;

	struct FFrame
	{
		TArray<FColor> Pixels;
		int32 Width = 0;
		int32 Height = 0;
		double Seconds = 0.0;
	};

	/** Fits Width x Height into LongSide, never larger than it is. */
	FIntPoint FitSize(int32 Width, int32 Height, int32 LongSide)
	{
		const int32 Long = FMath::Max(Width, Height);
		if (Long <= LongSide)
		{
			return FIntPoint(Width, Height);
		}
		return FIntPoint(FMath::Max(1, Width * LongSide / Long), FMath::Max(1, Height * LongSide / Long));
	}

	FFrame Resized(const FFrame& Frame, FIntPoint Size)
	{
		if (Size.X == Frame.Width && Size.Y == Frame.Height)
		{
			return Frame;
		}
		FFrame Out;
		Out.Width = Size.X;
		Out.Height = Size.Y;
		Out.Seconds = Frame.Seconds;
		Out.Pixels.SetNumUninitialized(Size.X * Size.Y);
		FImageCore::ResizeImage(FImageView(Frame.Pixels.GetData(), Frame.Width, Frame.Height), FImageView(Out.Pixels.GetData(), Out.Width, Out.Height));
		return Out;
	}

	/** All frames in one picture, left to right then top to bottom, so one look shows the motion. */
	FFrame ContactSheet(const TArray<FFrame>& Frames, int32 LongSide)
	{
		const int32 Columns = FMath::CeilToInt32(FMath::Sqrt(static_cast<float>(Frames.Num())));
		const int32 Rows = FMath::DivideAndRoundUp(Frames.Num(), Columns);
		const FIntPoint Cell = FitSize(Frames[0].Width, Frames[0].Height, FMath::Max(1, LongSide / FMath::Max(Columns, Rows)));
		FFrame Sheet;
		Sheet.Width = Cell.X * Columns;
		Sheet.Height = Cell.Y * Rows;
		Sheet.Pixels.Init(FColor::Black, Sheet.Width * Sheet.Height);
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			const FFrame Small = Resized(Frames[Index], Cell);
			const int32 Left = (Index % Columns) * Cell.X;
			const int32 Top = (Index / Columns) * Cell.Y;
			for (int32 Y = 0; Y < Cell.Y; ++Y)
			{
				FMemory::Memcpy(&Sheet.Pixels[(Top + Y) * Sheet.Width + Left], &Small.Pixels[Y * Cell.X], Cell.X * sizeof(FColor));
			}
		}
		return Sheet;
	}

	/** GIF89a bit stream of LZW codes, in sub-blocks of at most 255 bytes. */
	class FGifBits
	{
	public:
		explicit FGifBits(TArray<uint8>& InOut) : Out(InOut) {}

		void Write(uint32 Code, int32 Bits)
		{
			Accumulator |= Code << Count;
			Count += Bits;
			while (Count >= 8)
			{
				Block.Add(static_cast<uint8>(Accumulator & 0xFF));
				Accumulator >>= 8;
				Count -= 8;
				if (Block.Num() == 255)
				{
					FlushBlock();
				}
			}
		}

		void Finish()
		{
			if (Count > 0)
			{
				Block.Add(static_cast<uint8>(Accumulator & 0xFF));
			}
			FlushBlock();
			Out.Add(0);
		}

	private:
		void FlushBlock()
		{
			if (!Block.IsEmpty())
			{
				Out.Add(static_cast<uint8>(Block.Num()));
				Out.Append(Block);
				Block.Reset();
			}
		}

		TArray<uint8>& Out;
		TArray<uint8> Block;
		uint32 Accumulator = 0;
		int32 Count = 0;
	};

	/**
	 * An animated GIF with one fixed palette (6 x 7 x 6 levels of red, green, blue): plain to write and enough to show motion.
	 * The engine's image wrappers have no GIF writer.
	 */
	TArray<uint8> MakeGif(const TArray<FFrame>& Frames, double FrameSeconds)
	{
		const int32 Width = Frames[0].Width;
		const int32 Height = Frames[0].Height;
		TArray<uint8> Out;
		const auto Add16 = [&Out](int32 Value)
		{
			Out.Add(static_cast<uint8>(Value & 0xFF));
			Out.Add(static_cast<uint8>((Value >> 8) & 0xFF));
		};
		Out.Append(reinterpret_cast<const uint8*>("GIF89a"), 6);
		Add16(Width);
		Add16(Height);
		Out.Append({ 0xF7, 0, 0 });
		for (int32 Index = 0; Index < 256; ++Index)
		{
			const int32 Red = Index / 42;
			const int32 Green = (Index / 6) % 7;
			const int32 Blue = Index % 6;
			Out.Append(Index < 252 ? TArray<uint8>{ static_cast<uint8>(Red * 51), static_cast<uint8>(Green * 255 / 6), static_cast<uint8>(Blue * 51) } : TArray<uint8>{ 0, 0, 0 });
		}
		// Loop forever.
		Out.Append({ 0x21, 0xFF, 0x0B });
		Out.Append(reinterpret_cast<const uint8*>("NETSCAPE2.0"), 11);
		Out.Append({ 0x03, 0x01, 0x00, 0x00, 0x00 });

		const int32 Delay = FMath::Clamp(FMath::RoundToInt32(FrameSeconds * 100.0), 2, 65535);
		for (const FFrame& Frame : Frames)
		{
			Out.Append({ 0x21, 0xF9, 0x04, 0x00 });
			Add16(Delay);
			Out.Append({ 0x00, 0x00, 0x2C });
			Add16(0);
			Add16(0);
			Add16(Width);
			Add16(Height);
			Out.Append({ 0x00, 0x08 });

			const auto IndexOf = [](const FColor& Color)
			{
				return static_cast<uint32>(((Color.R * 5 + 127) / 255 * 7 + (Color.G * 6 + 127) / 255) * 6 + (Color.B * 5 + 127) / 255);
			};
			constexpr uint32 ClearCode = 256;
			constexpr uint32 EndCode = 257;
			FGifBits Bits(Out);
			TMap<uint32, uint32> Codes;
			int32 CodeSize = 9;
			uint32 LastCode = EndCode;
			Bits.Write(ClearCode, CodeSize);
			uint32 Current = IndexOf(Frame.Pixels[0]);
			for (int32 Pixel = 1; Pixel < Frame.Pixels.Num(); ++Pixel)
			{
				const uint32 Next = IndexOf(Frame.Pixels[Pixel]);
				const uint32 Key = (Current << 8) | Next;
				if (const uint32* Known = Codes.Find(Key))
				{
					Current = *Known;
					continue;
				}
				Bits.Write(Current, CodeSize);
				Codes.Add(Key, ++LastCode);
				if (LastCode >= (1u << CodeSize))
				{
					++CodeSize;
				}
				if (LastCode == 4095)
				{
					Bits.Write(ClearCode, CodeSize);
					Codes.Reset();
					CodeSize = 9;
					LastCode = EndCode;
				}
				Current = Next;
			}
			Bits.Write(Current, CodeSize);
			Bits.Write(EndCode, CodeSize);
			Bits.Finish();
		}
		Out.Add(0x3B);
		return Out;
	}

	FString WindowTitles(const TArray<TSharedRef<SWindow>>& Windows)
	{
		TArray<FString> Titles;
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			Titles.Add(TEXT("'") + Window->GetTitle().ToString() + TEXT("'"));
		}
		return FString::Join(Titles, TEXT(", "));
	}

	/** The widget to take pictures of. OutOpened: an asset's editor was opened or brought forward for it, so it needs time to draw. */
	TSharedPtr<SWidget> FindTarget(const FString& Target, const FString& Title, FString& OutName, bool& bOutOpened, FString& OutError)
	{
		bOutOpened = false;
		if (!GEditor || IsRunningCommandlet() || !FSlateApplication::IsInitialized())
		{
			OutError = TEXT("age_capture takes pictures of the editor's windows, so it needs the project open in the Unreal editor (the background server has no windows).");
			return nullptr;
		}
		if (Target.IsEmpty() || Target == TEXT("viewport"))
		{
			FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
			const TSharedPtr<IAssetViewport> Viewport = LevelEditor ? LevelEditor->GetFirstActiveViewport() : nullptr;
			if (!Viewport.IsValid())
			{
				OutError = TEXT("The level editor has no active viewport.");
				return nullptr;
			}
			OutName = GEditor->PlayWorld ? TEXT("the level viewport (Play In Editor is running)") : TEXT("the level viewport");
			return Viewport->AsWidget();
		}
		if (Target == TEXT("window"))
		{
			TArray<TSharedRef<SWindow>> Ordered;
			FSlateApplication::Get().GetAllVisibleWindowsOrdered(Ordered);
			TArray<TSharedRef<SWindow>> Windows;
			for (const TSharedRef<SWindow>& Window : Ordered)
			{
				if (Window->IsRegularWindow() && !Window->IsWindowMinimized() && !Window->GetTitle().IsEmpty())
				{
					Windows.Add(Window);
				}
			}
			TSharedPtr<SWindow> Found;
			if (Title.IsEmpty())
			{
				Found = FSlateApplication::Get().GetActiveTopLevelRegularWindow();
				if (!Found.IsValid() && !Windows.IsEmpty())
				{
					Found = Windows.Last();
				}
			}
			else
			{
				// The list is ordered back to front: the frontmost match wins.
				for (const TSharedRef<SWindow>& Window : Windows)
				{
					if (Window->GetTitle().ToString().Contains(Title, ESearchCase::IgnoreCase))
					{
						Found = Window;
					}
				}
			}
			if (!Found.IsValid())
			{
				OutError = FString::Printf(TEXT("No open editor window%s. Windows: %s."), Title.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" has '%s' in its title"), *Title), *WindowTitles(Windows));
				return nullptr;
			}
			OutName = FString::Printf(TEXT("the window '%s'"), *Found->GetTitle().ToString());
			return Found;
		}

		// An asset: its editor, opened or brought forward. Nothing in the asset changes.
		FString PackageName = Target;
		int32 Dot = INDEX_NONE;
		if (PackageName.FindChar(TEXT('.'), Dot))
		{
			PackageName.LeftInline(Dot);
		}
		if (!Target.StartsWith(TEXT("/")) || !FPackageName::DoesPackageExist(PackageName))
		{
			OutError = FString::Printf(TEXT("'target' must be \"viewport\", \"window\" or an asset path such as /Game/Characters/BP_Hero; '%s' is none of them."), *Target);
			return nullptr;
		}
		UObject* Asset = FSoftObjectPath(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName)).TryLoad();
		UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!Asset || !Editors)
		{
			OutError = FString::Printf(TEXT("%s could not be loaded."), *Target);
			return nullptr;
		}
		IAssetEditorInstance* Instance = Editors->FindEditorForAsset(Asset, /*bFocusIfOpen=*/true);
		if (!Instance)
		{
			Editors->OpenEditorForAsset(Asset);
			Instance = Editors->FindEditorForAsset(Asset, /*bFocusIfOpen=*/true);
		}
		const TSharedPtr<FTabManager> Tabs = Instance ? Instance->GetAssociatedTabManager() : nullptr;
		const TSharedPtr<SDockTab> Tab = Tabs.IsValid() ? Tabs->GetOwnerTab() : nullptr;
		const TSharedPtr<SWindow> Window = Tab.IsValid() ? Tab->GetParentWindow() : nullptr;
		if (!Window.IsValid())
		{
			OutError = FString::Printf(TEXT("%s has no editor window to take a picture of (a %s)."), *Target, *Asset->GetClass()->GetName());
			return nullptr;
		}
		bOutOpened = true;
		OutName = FString::Printf(TEXT("the editor of %s (window '%s')"), *PackageName, *Window->GetTitle().ToString());
		return Window;
	}

	class FCapture : public TSharedFromThis<FCapture>
	{
	public:
		FCapture(TSharedRef<SWidget> InWidget, FString InName, int32 InFrames, double InInterval, int32 InLongSide, bool bInImage, double InFirstDelay,
			FJsonObject InArguments, IModelContextProtocolTool::FResultCallback InOnComplete)
			: Widget(MoveTemp(InWidget)), Name(MoveTemp(InName)), FrameCount(InFrames), Interval(InInterval), LongSide(InLongSide), bImage(bInImage), FirstDelay(InFirstDelay)
			, Arguments(MoveTemp(InArguments)), OnComplete(MoveTemp(InOnComplete))
		{
		}

		void Begin()
		{
			Start = FPlatformTime::Seconds();
			// An editor in the background is throttled to a few frames a second, which would spoil the timing.
			UEditorEngine::FShouldDisableCPUThrottling NoThrottle = UEditorEngine::FShouldDisableCPUThrottling::CreateLambda([]() { return true; });
			ThrottleHandle = NoThrottle.GetHandle();
			GEditor->ShouldDisableCPUThrottlingDelegates.Add(MoveTemp(NoThrottle));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(this, &FCapture::Tick));
		}

		void Cancel()
		{
			bCancelled = true;
		}

	private:
		bool Tick(float)
		{
			const double Elapsed = FPlatformTime::Seconds() - Start - FirstDelay;
			if (bCancelled)
			{
				return End(MakeErrorResult(TEXT("The capture was cancelled. No files were written.")));
			}
			if (Elapsed < Frames.Num() * Interval)
			{
				return true;
			}
			FFrame Frame;
			FIntVector Size;
			if (!FSlateApplication::Get().TakeScreenshot(Widget, Frame.Pixels, Size) || Size.X <= 0 || Size.Y <= 0 || Frame.Pixels.Num() != Size.X * Size.Y)
			{
				return End(MakeErrorResult(FString::Printf(TEXT("The editor could not take a picture of %s (is its window minimized or hidden?). No files were written."), *Name)));
			}
			Frame.Width = Size.X;
			Frame.Height = Size.Y;
			Frame.Seconds = Elapsed;
			for (FColor& Pixel : Frame.Pixels)
			{
				Pixel.A = 255;
			}
			// Several frames are kept small from the start; one frame is saved at full size.
			Frames.Add(FrameCount > 1 ? Resized(Frame, FitSize(Frame.Width, Frame.Height, LongSide)) : MoveTemp(Frame));
			if (Frames.Num() < FrameCount)
			{
				// A window resized in between would give frames of different sizes.
				if (Frames.Last().Width != Frames[0].Width || Frames.Last().Height != Frames[0].Height)
				{
					return End(MakeErrorResult(TEXT("The window changed size during the capture. No files were written; try again.")));
				}
				return true;
			}
			return End(Write());
		}

		FModelContextProtocolToolResult Write()
		{
			const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AGE") / TEXT("Captures") / FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_%s")));
			IFileManager::Get().MakeDirectory(*Folder, /*Tree=*/true);
			TArray<FString> Lines;
			const auto Save = [&Folder, &Lines](const TCHAR* File, const FFrame& Picture, const TCHAR* What)
			{
				const FString Path = Folder / File;
				const bool bSaved = FImageUtils::SaveImageByExtension(*Path, FImageView(Picture.Pixels.GetData(), Picture.Width, Picture.Height));
				Lines.Add(FString::Printf(TEXT("%s: %s (%d x %d)%s"), What, *Path, Picture.Width, Picture.Height, bSaved ? TEXT("") : TEXT(" -- COULD NOT BE WRITTEN")));
			};

			FFrame Shown;
			if (FrameCount == 1)
			{
				Lines.Add(FString::Printf(TEXT("Picture of %s."), *Name));
				Save(TEXT("capture.png"), Frames[0], TEXT("file"));
				Shown = Resized(Frames[0], FitSize(Frames[0].Width, Frames[0].Height, LongSide));
			}
			else
			{
				Lines.Add(FString::Printf(TEXT("%d pictures of %s over %.2f s (%.2f s apart; real times: %s)."), Frames.Num(), *Name, Frames.Last().Seconds - Frames[0].Seconds, Interval,
					*FString::JoinBy(Frames, TEXT(", "), [this](const FFrame& Frame) { return FString::Printf(TEXT("%.2f"), Frame.Seconds - Frames[0].Seconds); })));
				Shown = ContactSheet(Frames, MaxLongSide);
				Save(TEXT("sheet.jpg"), Shown, TEXT("all frames in one picture, left to right then top to bottom"));
				const FString GifPath = Folder / TEXT("capture.gif");
				const bool bGif = FFileHelper::SaveArrayToFile(MakeGif(Frames, Interval), *GifPath);
				Lines.Add(FString::Printf(TEXT("animation for the user to open (256 colors): %s%s"), *GifPath, bGif ? TEXT("") : TEXT(" -- COULD NOT BE WRITTEN")));
				for (int32 Index = 0; Index < Frames.Num(); ++Index)
				{
					FImageUtils::SaveImageByExtension(*(Folder / FString::Printf(TEXT("frame_%03d.png"), Index)), FImageView(Frames[Index].Pixels.GetData(), Frames[Index].Width, Frames[Index].Height));
				}
				Lines.Add(FString::Printf(TEXT("single frames: %s (%d x %d)"), *(Folder / TEXT("frame_000.png ...")), Frames[0].Width, Frames[0].Height));
			}

			FModelContextProtocolToolResult Result = MakeTextResult(FString::Join(Lines, TEXT("\n")));
			TArray64<uint8> Jpeg;
			if (bImage && FImageUtils::CompressImage(Jpeg, TEXT("jpg"), FImageView(Shown.Pixels.GetData(), Shown.Width, Shown.Height), 85))
			{
				TArray<TSharedPtr<FJsonValue>> Content = Result.JsonObject->GetArrayField(TEXT("content"));
				Content.Add(MakeShared<FJsonValueObject>(UE::ModelContextProtocol::MakeImageContentObject(TEXT("image/jpeg"), FBase64::Encode(Jpeg.GetData(), static_cast<uint32>(Jpeg.Num())))));
				Result.JsonObject->SetArrayField(TEXT("content"), Content);
			}
			return Result;
		}

		/** Always returns false: the ticker is done. The tool's reference to this object goes with it. */
		bool End(const FModelContextProtocolToolResult& Result);

		TSharedRef<SWidget> Widget;
		const FString Name;
		const int32 FrameCount;
		const double Interval;
		const int32 LongSide;
		const bool bImage;
		const double FirstDelay;
		const FJsonObject Arguments;
		IModelContextProtocolTool::FResultCallback OnComplete;
		double Start = 0.0;
		bool bCancelled = false;
		TArray<FFrame> Frames;
		FDelegateHandle ThrottleHandle;
	};

	/** One capture at a time; also keeps it alive while its ticker runs. */
	TSharedPtr<FCapture>& ActiveCapture()
	{
		static TSharedPtr<FCapture> Capture;
		return Capture;
	}

	bool FCapture::End(const FModelContextProtocolToolResult& Result)
	{
		GEditor->ShouldDisableCPUThrottlingDelegates.RemoveAll([this](const UEditorEngine::FShouldDisableCPUThrottling& Delegate)
		{
			return Delegate.GetHandle() == ThrottleHandle;
		});
		AGE::Mcp::RecordExternalToolCall(TEXT("age_capture"), Arguments, Result, (FPlatformTime::Seconds() - Start) * 1000.0);
		const IModelContextProtocolTool::FResultCallback Callback = MoveTemp(OnComplete);
		// May be the last reference to this object.
		ActiveCapture().Reset();
		Callback(Result);
		return false;
	}

	class FCaptureTool : public IModelContextProtocolTool
	{
	public:
		virtual FString GetName() const override { return TEXT("age_capture"); }

		virtual FString GetDescription() const override
		{
			return TEXT("Take a picture of the editor and get it back as an image you can look at, to check a result with your own eyes or to show the user. Changes no asset. "
				"target \"viewport\" (default): the level viewport, which shows the game while Play In Editor runs in it. "
				"target \"window\": an editor window as the user sees it (with 'title', the window whose title contains that text; otherwise the active one). "
				"target an asset path: that asset's editor, opened or brought forward first (a Blueprint or other graph, a mesh, an animation preview). "
				"frames above 1 takes several pictures 'interval' seconds apart, to see motion (a playing game, an animation preview): "
				"you get them as one picture, left to right then top to bottom, and the user gets an animated GIF. "
				"Files go under the project's Saved/AGE/Captures; give the user the paths. Videos are not made: you cannot watch one, and the frames show the same.");
		}

		virtual TSharedPtr<FJsonObject> GetInputJsonSchema() const override
		{
			const auto Property = [](const TCHAR* Type, const TCHAR* Text)
			{
				const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
				Object->SetStringField(TEXT("type"), Type);
				Object->SetStringField(TEXT("description"), Text);
				return MakeShared<FJsonValueObject>(Object);
			};
			const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
			Properties->SetField(TEXT("target"), Property(TEXT("string"), TEXT("\"viewport\" (default), \"window\", or an asset path whose editor to show.")));
			Properties->SetField(TEXT("title"), Property(TEXT("string"), TEXT("With target \"window\": part of the window's title.")));
			Properties->SetField(TEXT("frames"), Property(TEXT("integer"), TEXT("How many pictures, default 1, at most 60.")));
			Properties->SetField(TEXT("interval"), Property(TEXT("number"), TEXT("Seconds between pictures, default 0.2. frames x interval is at most 20 s.")));
			Properties->SetField(TEXT("size"), Property(TEXT("integer"), TEXT("Longest side in pixels of the image returned to you and of the frames, default 1024, at most 1568. A single picture's file keeps its full size.")));
			Properties->SetField(TEXT("image"), Property(TEXT("boolean"), TEXT("false: only write the files and return their paths, without the image. Default true.")));
			const TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			Schema->SetObjectField(TEXT("properties"), Properties);
			return Schema;
		}

		virtual void RunAsync(const FModelContextProtocolToolRequestId& RequestId, const TSharedPtr<FJsonObject>& Params, const FResultCallback& OnComplete) override
		{
			AGE::Mcp::NoteExternalToolStart();
			const TSharedRef<FJsonObject> NoArguments = MakeShared<FJsonObject>();
			const FJsonObject& Arguments = Params.IsValid() ? *Params : *NoArguments;
			const auto Refuse = [&Arguments, &OnComplete](const FString& Text)
			{
				const FModelContextProtocolToolResult Result = MakeErrorResult(Text);
				AGE::Mcp::RecordExternalToolCall(TEXT("age_capture"), Arguments, Result, 0.0);
				OnComplete(Result);
			};
			if (ActiveCapture().IsValid())
			{
				Refuse(TEXT("Another age_capture is still taking pictures. Nothing new was started."));
				return;
			}
			FString Target;
			Arguments.TryGetStringField(TEXT("target"), Target);
			FString Title;
			Arguments.TryGetStringField(TEXT("title"), Title);
			int32 Frames = 1;
			Arguments.TryGetNumberField(TEXT("frames"), Frames);
			double Interval = 0.2;
			Arguments.TryGetNumberField(TEXT("interval"), Interval);
			int32 LongSide = DefaultLongSide;
			Arguments.TryGetNumberField(TEXT("size"), LongSide);
			bool bImage = true;
			Arguments.TryGetBoolField(TEXT("image"), bImage);
			if (Frames < 1 || Frames > MaxFrames || Interval < 0.0 || (Frames - 1) * Interval > MaxSeconds)
			{
				Refuse(FString::Printf(TEXT("'frames' must be 1 to %d and the pictures must fit in %.0f s (frames x interval). Nothing was captured."), MaxFrames, MaxSeconds));
				return;
			}

			FString Name;
			FString Error;
			bool bOpened = false;
			const TSharedPtr<SWidget> Widget = FindTarget(Target.TrimStartAndEnd(), Title.TrimStartAndEnd(), Name, bOpened, Error);
			if (!Widget.IsValid())
			{
				Refuse(Error + TEXT(" Nothing was captured."));
				return;
			}
			ActiveCapture() = MakeShared<FCapture>(Widget.ToSharedRef(), MoveTemp(Name), Frames, Interval, FMath::Clamp(LongSide, 64, MaxLongSide), bImage,
				bOpened ? SettleSeconds : 0.0, FJsonObject(Arguments), OnComplete);
			ActiveCapture()->Begin();
		}

		virtual void CancelAsync(const FModelContextProtocolToolRequestId& RequestId) override
		{
			if (ActiveCapture().IsValid())
			{
				ActiveCapture()->Cancel();
			}
		}
	};
}

TSharedRef<IModelContextProtocolTool> AGE::Mcp::CreateCaptureTool()
{
	return MakeShared<AGE::McpCaptureDetail::FCaptureTool>();
}
