// Asset Graph Exporter

#include "AGEMcpLiveCoding.h"
#include "AGELiveCodingState.h"
#include "AGEMcpState.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/ReloadUtilities.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Class.h"
#include "UObject/Reload.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace AGE::McpLiveCodingDetail
{
	FDelegateHandle ReloadCompleteHandle;
	FDateTime StartUtc;
	FDateTime LastPatchUtc;
	int32 Patches = 0;
	bool bReinstancingOn = false;
	/** A patch changed UCLASS/USTRUCT layouts while re-instancing was off: the engine kept the old layouts. Stays set until the editor restarts. */
	bool bLayoutIgnored = false;
	TSet<FString> Reinstanced;
	/** Project headers written since the editor started, relative to the project folder. */
	TSet<FString> ChangedHeaders;

	FString JoinSorted(const TSet<FString>& Names)
	{
		TArray<FString> Sorted = Names.Array();
		Sorted.Sort();
		constexpr int32 MaxShown = 30;
		const int32 Total = Sorted.Num();
		if (Total > MaxShown)
		{
			Sorted.SetNum(MaxShown);
			Sorted.Add(FString::Printf(TEXT("+%d more"), Total - MaxShown));
		}
		return FString::Join(Sorted, TEXT(", "));
	}

	/** .h files under the project's Source folders (game and project plugins) written after Since. */
	TSet<FString> FindHeadersWrittenAfter(const FDateTime& Since)
	{
		TSet<FString> Result;
		const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		auto Visit = [&Result, &Since, &ProjectDir](const TCHAR* Path, const FFileStatData& Stat)
		{
			const FString File(Path);
			if (!Stat.bIsDirectory && Stat.ModificationTime > Since && File.EndsWith(TEXT(".h")) && File.Contains(TEXT("/Source/")) && !File.Contains(TEXT("/Intermediate/")))
			{
				FString Relative = File;
				FPaths::MakePathRelativeTo(Relative, *ProjectDir);
				Result.Add(Relative);
			}
			return true;
		};
		for (const FString& Dir : { FPaths::GameSourceDir(), FPaths::ProjectPluginsDir() })
		{
			const FString Full = FPaths::ConvertRelativePathToFull(Dir);
			if (IFileManager::Get().DirectoryExists(*Full))
			{
				IFileManager::Get().IterateDirectoryStatRecursively(*Full, Visit);
			}
		}
		return Result;
	}

	void Notify(const FString& Text, SNotificationItem::ECompletionState State, float Seconds, bool bOfferRestart = false)
	{
		FNotificationInfo Info(FText::FromString(Text));
		Info.ExpireDuration = Seconds;
		Info.WidthOverride = 560.0f;
		if (bOfferRestart && IConsoleManager::Get().FindConsoleObject(AGE::Mcp::RebuildAndRestartCommand))
		{
			Info.ButtonDetails.Add(FNotificationButtonInfo(FText::FromString(TEXT("Rebuild and restart")),
				FText::FromString(TEXT("Close the editor, build the C++ code and reopen the editor")),
				FSimpleDelegate::CreateLambda([]()
				{
					if (IConsoleObject* Command = IConsoleManager::Get().FindConsoleObject(AGE::Mcp::RebuildAndRestartCommand))
					{
						Command->AsCommand()->Execute({}, nullptr, *GLog);
					}
				}),
				State));
		}
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(State);
		}
	}

	void HandleReloadComplete(EReloadCompleteReason Reason)
	{
#if WITH_LIVE_CODING
		IReload* Reload = GetActiveReloadInterface();
		if (!Reload || Reload->GetType() != EActiveReloadType::LiveCoding)
		{
			return;
		}
		// In the editor, Live Coding's reload object is an FReload (LiveCodingModule.cpp, FLiveCodingModule::BeginReload).
		const FReload& EditorReload = static_cast<const FReload&>(*Reload);
		// The engine compares each UCLASS/USTRUCT's size and declaration hash with the loaded one; a change sets this flag
		// whether or not re-instancing is on (FReload::GetEnableReinstancing, FReload::NotifyChange).
		const bool bLayoutChanged = EditorReload.HasReinstancingOccurred();
		bReinstancingOn = Reload->GetEnableReinstancing(false);
		LastPatchUtc = FDateTime::UtcNow();
		++Patches;

		TSet<FString> PatchReinstanced;
		for (const TPair<UClass*, UClass*>& Pair : Reload->GetReinstancedClasses())
		{
			if (Pair.Value) // null: unchanged
			{
				PatchReinstanced.Add(Pair.Value->GetName());
			}
		}
		Reinstanced.Append(PatchReinstanced);

		const TSet<FString> Headers = FindHeadersWrittenAfter(StartUtc);
		const TSet<FString> NewHeaders = Headers.Difference(ChangedHeaders);
		ChangedHeaders = Headers;

		if (bLayoutChanged && !bReinstancingOn)
		{
			bLayoutIgnored = true;
			const FString Text = TEXT("Live Coding: C++ class or struct layouts changed (a header edit), but re-instancing is off, so the editor keeps the old layouts. "
				"Objects of those types may hold wrong values or crash. Do not save; close the editor, build and reopen.");
			UE_LOG(LogAssetGraphExporterMcp, Warning, TEXT("%s Headers changed since the editor started: %s"), *Text, Headers.IsEmpty() ? TEXT("none found") : *JoinSorted(Headers));
			Notify(Text, SNotificationItem::CS_Fail, 30.0f, true);
		}
		else if (bLayoutChanged)
		{
			const FString Text = FString::Printf(TEXT("Live Coding re-instanced C++ types whose layout changed%s%s. Check objects and Blueprints of those types before saving; restart the editor if anything looks wrong."),
				PatchReinstanced.IsEmpty() ? TEXT("") : TEXT(": "), *JoinSorted(PatchReinstanced));
			UE_LOG(LogAssetGraphExporterMcp, Warning, TEXT("%s"), *Text);
			Notify(Text, SNotificationItem::CS_None, 20.0f, true);
		}
		else if (!NewHeaders.IsEmpty())
		{
			const FString Text = FString::Printf(TEXT("Live Coding: headers changed (%s). No UCLASS/USTRUCT layout changed. The engine does not check plain C++ classes and structs: "
				"if one of those gained or lost members or virtual functions, restart the editor."), *JoinSorted(NewHeaders));
			UE_LOG(LogAssetGraphExporterMcp, Display, TEXT("%s"), *Text);
			Notify(Text, SNotificationItem::CS_None, 12.0f);
		}
#endif
	}
}

void AGE::Mcp::LiveCoding::Startup()
{
	using namespace McpLiveCodingDetail;
	StartUtc = FDateTime::UtcNow();
	ReloadCompleteHandle = FCoreUObjectDelegates::ReloadCompleteDelegate.AddStatic(&HandleReloadComplete);
}

void AGE::Mcp::LiveCoding::Shutdown()
{
	FCoreUObjectDelegates::ReloadCompleteDelegate.Remove(McpLiveCodingDetail::ReloadCompleteHandle);
}

bool AGE::Mcp::HasIgnoredLiveCodingLayoutChange()
{
	return McpLiveCodingDetail::bLayoutIgnored;
}

void AGE::Mcp::LiveCoding::AppendStatus(TArray<FString>& Lines)
{
	using namespace McpLiveCodingDetail;
	if (Patches == 0)
	{
		return;
	}
	Lines.Add(FString::Printf(TEXT("live coding: %d patch(es) since the editor started, last at %s UTC; re-instancing %s"),
		Patches, *LastPatchUtc.ToString(TEXT("%H:%M:%S")), bReinstancingOn ? TEXT("on") : TEXT("off")));
	if (bLayoutIgnored)
	{
		Lines.Add(TEXT("live coding: a patch changed UCLASS/USTRUCT layouts while re-instancing was off; the editor still uses the old layouts, "
			"so objects of those types may hold wrong values. Do not save; close the editor, build and reopen."));
	}
	if (!Reinstanced.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("live coding: re-instanced: %s"), *JoinSorted(Reinstanced)));
	}
	if (!ChangedHeaders.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("live coding: project headers changed since the editor started: %s (plain C++ types are not checked by the engine)"), *JoinSorted(ChangedHeaders)));
	}
}
