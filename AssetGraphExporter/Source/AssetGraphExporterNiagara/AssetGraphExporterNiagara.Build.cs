// Asset Graph Exporter

using UnrealBuildTool;

public class AssetGraphExporterNiagara : ModuleRules
{
	public AssetGraphExporterNiagara(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"AssetGraphExporter",
			"Niagara",
			"NiagaraCore",
			"NiagaraEditor",
			// Needed only because the NiagaraEditor node headers include them.
			"UnrealEd",
			"EditorFramework",
			"GraphEditor",
			"Slate",
			"SlateCore",
			"InputCore",
		});
	}
}
