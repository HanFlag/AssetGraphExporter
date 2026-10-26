// Asset Graph Exporter

#include "AGEMcpRevision.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/Linker.h"
#include "UObject/LinkerInstancingContext.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

// Named (not anonymous) so these helpers cannot clash with other files in a unity build.
namespace AGE::McpRevisionDetail
{
	const TCHAR* const MountRoot = TEXT("/AGEDiff/");

	struct FGitResult
	{
		int32 ExitCode = -1;
		TArray<uint8> Output;
		/** stderr, or why git could not run. */
		FString Error;
	};

	FString BytesToText(const TArray<uint8>& Bytes)
	{
		const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Text.Length(), Text.Get()).TrimStartAndEnd();
	}

	/** Runs git and keeps stdout as bytes (a .uasset is binary). */
	FGitResult RunGit(const FString& WorkingDirectory, const FString& Arguments)
	{
		FGitResult Result;
		void* OutRead = nullptr;
		void* OutWrite = nullptr;
		void* ErrRead = nullptr;
		void* ErrWrite = nullptr;
		if (!FPlatformProcess::CreatePipe(OutRead, OutWrite) || !FPlatformProcess::CreatePipe(ErrRead, ErrWrite))
		{
			FPlatformProcess::ClosePipe(OutRead, OutWrite);
			Result.Error = TEXT("could not create pipes for git");
			return Result;
		}

		FProcHandle Process = FPlatformProcess::CreateProc(TEXT("git"), *Arguments, false, true, true, nullptr, 0, *WorkingDirectory, OutWrite, nullptr, ErrWrite);
		if (!Process.IsValid())
		{
			Result.Error = TEXT("git could not be started; is it on PATH?");
		}
		else
		{
			TArray<uint8> Chunk;
			TArray<uint8> ErrorBytes;
			// Read while git runs so a large file cannot fill the pipe and stall it.
			auto ReadPipes = [&]()
			{
				bool bRead = false;
				if (FPlatformProcess::ReadPipeToArray(OutRead, Chunk) && Chunk.Num() > 0)
				{
					Result.Output.Append(Chunk);
					bRead = true;
				}
				if (FPlatformProcess::ReadPipeToArray(ErrRead, Chunk) && Chunk.Num() > 0)
				{
					ErrorBytes.Append(Chunk);
					bRead = true;
				}
				return bRead;
			};
			while (FPlatformProcess::IsProcRunning(Process))
			{
				if (!ReadPipes())
				{
					FPlatformProcess::Sleep(0.002f);
				}
			}
			while (ReadPipes())
			{
			}
			FPlatformProcess::GetProcReturnCode(Process, &Result.ExitCode);
			FPlatformProcess::CloseProc(Process);
			Result.Error = BytesToText(ErrorBytes);
		}
		FPlatformProcess::ClosePipe(OutRead, OutWrite);
		FPlatformProcess::ClosePipe(ErrRead, ErrWrite);
		return Result;
	}

	FString DiffDirectory()
	{
		FString Dir;
		if (!FParse::Value(FCommandLine::Get(), TEXT("AGEDiffDir="), Dir))
		{
			Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AssetGraphExporter"), TEXT("Diff"));
		}
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Dir);
	}

	/** /AGEDiff/ stays mounted for the rest of the process: loaded revisions live there until garbage collection. */
	void EnsureMounted(const FString& Directory)
	{
		static bool bMounted = false;
		if (!bMounted)
		{
			FPackageName::RegisterMountPoint(MountRoot, Directory + TEXT("/"));
			bMounted = true;
		}
	}

	/** Bytes of a Git LFS file from the local store, or false when the pointer is not one or the file is not on this computer. */
	bool ReadLfsObject(const FString& Root, const TArray<uint8>& Pointer, TArray<uint8>& OutBytes, FString& OutError)
	{
		const FString Text = BytesToText(Pointer);
		const FString OidMarker = TEXT("oid sha256:");
		const int32 OidStart = Text.Find(OidMarker);
		const FString Oid = OidStart == INDEX_NONE ? FString() : Text.Mid(OidStart + OidMarker.Len(), 64);
		if (Oid.Len() != 64)
		{
			OutError = TEXT("the Git LFS pointer has no sha256 oid");
			return false;
		}
		const FGitResult Common = RunGit(Root, TEXT("rev-parse --git-common-dir"));
		FString CommonDir = BytesToText(Common.Output);
		if (FPaths::IsRelative(CommonDir))
		{
			CommonDir = FPaths::Combine(Root, CommonDir);
		}
		const FString ObjectFile = FPaths::Combine(CommonDir, TEXT("lfs"), TEXT("objects"), Oid.Left(2), Oid.Mid(2, 2), Oid);
		if (!FFileHelper::LoadFileToArray(OutBytes, *ObjectFile))
		{
			OutError = TEXT("its Git LFS file is not on this computer (nothing is downloaded). Run git lfs fetch for that revision, then try again");
			return false;
		}
		return true;
	}
}

AGE::Mcp::Revision::FLoaded AGE::Mcp::Revision::Load(const FAssetData& Asset, const FString& Revision)
{
	using namespace AGE::McpRevisionDetail;
	FLoaded Loaded;
	const FString PackageName = Asset.PackageName.ToString();
	FString Filename;
	if (!FPackageName::DoesPackageExist(PackageName, &Filename))
	{
		Loaded.Error = FString::Printf(TEXT("%s has no file on disk."), *PackageName);
		return Loaded;
	}
	Filename = FPaths::ConvertRelativePathToFull(Filename);

	const FGitResult TopLevel = RunGit(FPaths::GetPath(Filename), TEXT("rev-parse --show-toplevel"));
	if (TopLevel.ExitCode != 0)
	{
		Loaded.Error = FString::Printf(TEXT("%s is not in a git repository (%s)."), *PackageName, *TopLevel.Error);
		return Loaded;
	}
	const FString Root = BytesToText(TopLevel.Output);
	FString InRepository = Filename;
	FPaths::MakePathRelativeTo(InRepository, *(Root + TEXT("/")));

	// %x09 is a tab: hash, short hash, date, subject.
	const FGitResult Commit = RunGit(Root, FString::Printf(TEXT("log -1 --format=%%H%%x09%%h%%x09%%cs%%x09%%s \"%s\" --"), *Revision));
	TArray<FString> Fields;
	BytesToText(Commit.Output).ParseIntoArray(Fields, TEXT("\t"), false);
	if (Commit.ExitCode != 0 || Fields.Num() < 4)
	{
		Loaded.Error = FString::Printf(TEXT("Unknown git revision '%s' (%s)."), *Revision, *Commit.Error);
		return Loaded;
	}
	const FString& Hash = Fields[0];
	Loaded.Label = FString::Printf(TEXT("git %s (%s \"%s\")"), *Fields[1], *Fields[2], *Fields[3]);

	FGitResult Blob = RunGit(Root, FString::Printf(TEXT("cat-file -p \"%s:%s\""), *Hash, *InRepository));
	if (Blob.ExitCode != 0)
	{
		Loaded.Error = FString::Printf(TEXT("%s is not in git at %s (%s)."), *InRepository, *Fields[1], *Blob.Error);
		return Loaded;
	}
	TArray<uint8> Bytes = MoveTemp(Blob.Output);
	const FString LfsPrefix = TEXT("version https://git-lfs.github.com/spec/v1");
	if (Bytes.Num() < 1024 && BytesToText(Bytes).StartsWith(LfsPrefix))
	{
		TArray<uint8> LfsBytes;
		FString LfsError;
		if (!ReadLfsObject(Root, Bytes, LfsBytes, LfsError))
		{
			Loaded.Error = FString::Printf(TEXT("%s at %s: %s."), *InRepository, *Fields[1], *LfsError);
			return Loaded;
		}
		Bytes = MoveTemp(LfsBytes);
	}

	// /AGEDiff/<commit>/Game/Folder/Asset, so revisions and assets never share a package.
	const FString Directory = DiffDirectory();
	EnsureMounted(Directory);
	FString PackagePath = PackageName;
	PackagePath.RemoveFromStart(TEXT("/"));
	const FString CommitDirectory = Hash.Left(12);
	Loaded.TempPackageName = FString(MountRoot) + CommitDirectory + TEXT("/") + PackagePath;

	// The same commit gives the same bytes, so a package still in memory from an earlier call is reused.
	UPackage* Package = FindPackage(nullptr, *Loaded.TempPackageName);
	if (!Package)
	{
		const FString TempFile = FPaths::Combine(Directory, CommitDirectory, PackagePath) + FPaths::GetExtension(Filename, true);
		if (!FFileHelper::SaveArrayToFile(Bytes, *TempFile))
		{
			Loaded.Error = FString::Printf(TEXT("Could not write %s."), *TempFile);
			return Loaded;
		}
		FLinkerInstancingContext Context;
		Context.AddPackageMapping(Asset.PackageName, FName(*Loaded.TempPackageName));
		// Not LOAD_ForDiff: the engine allows it only for /Temp/ packages (they live in the project's Saved folder) and clears it otherwise.
		Package = LoadPackage(nullptr, *Loaded.TempPackageName, LOAD_DisableCompileOnLoad | LOAD_DisableEngineVersionChecks, nullptr, &Context);
	}
	Loaded.Asset = Package ? FindObject<UObject>(Package, *Asset.AssetName.ToString()) : nullptr;
	if (!Loaded.Asset)
	{
		Loaded.Error = FString::Printf(TEXT("Could not load %s as it was at %s."), *PackageName, *Fields[1]);
	}
	return Loaded;
}

void AGE::Mcp::Revision::Release(const FLoaded& Loaded)
{
	if (UPackage* Package = Loaded.TempPackageName.IsEmpty() ? nullptr : FindPackage(nullptr, *Loaded.TempPackageName))
	{
		ResetLoaders(Package);
	}
}
