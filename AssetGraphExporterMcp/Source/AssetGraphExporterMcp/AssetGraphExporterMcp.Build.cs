// Asset Graph Exporter

using System.Diagnostics;
using UnrealBuildTool;

public class AssetGraphExporterMcp : ModuleRules
{
	public AssetGraphExporterMcp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities",
			"AssetRegistry",
			"UnrealEd",
			"AssetGraphExporter",
			// Unreal MCP (engine plugin, Experimental): the server our tools are added to.
			"ModelContextProtocol",
			"ModelContextProtocolEngine",
			// Activity page served next to the MCP endpoint, and the plugin folder it is read from.
			"HTTPServer",
			"Projects",
			// age_anim_overview: the engine's pose evaluation (UAnimPoseExtensions).
			"AnimationBlueprintLibrary",
		});

		// age_status names the source a build came from, so two builds of the same version can be told apart.
		PrivateDefinitions.Add("AGE_BUILD_COMMIT=\"" + GitCommit() + "\"");
	}

	/** The short commit hash, with "+changes" when tracked files differ from it; "unknown" outside a git checkout. */
	private string GitCommit()
	{
		string Head = RunGit("rev-parse --short HEAD");
		if (string.IsNullOrEmpty(Head))
		{
			return "unknown";
		}
		return string.IsNullOrEmpty(RunGit("status --porcelain --untracked-files=no")) ? Head : Head + "+changes";
	}

	private string RunGit(string Arguments)
	{
		try
		{
			ProcessStartInfo Info = new ProcessStartInfo("git", "-C \"" + ModuleDirectory + "\" " + Arguments)
			{
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				UseShellExecute = false,
				CreateNoWindow = true,
			};
			using (Process Git = Process.Start(Info))
			{
				string Output = Git.StandardOutput.ReadToEnd().Trim();
				Git.WaitForExit();
				return Git.ExitCode == 0 ? Output : "";
			}
		}
		catch (System.Exception)
		{
			return "";
		}
	}
}
