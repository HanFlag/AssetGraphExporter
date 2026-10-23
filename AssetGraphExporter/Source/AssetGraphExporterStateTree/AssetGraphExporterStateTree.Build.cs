// Asset Graph Exporter

using UnrealBuildTool;

public class AssetGraphExporterStateTree : ModuleRules
{
	public AssetGraphExporterStateTree(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"AssetGraphExporter",
			"StateTreeModule",
			"StateTreeEditorModule",
			"PropertyBindingUtils",
		});
	}
}
