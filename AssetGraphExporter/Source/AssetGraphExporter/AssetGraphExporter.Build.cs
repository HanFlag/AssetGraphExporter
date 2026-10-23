// Asset Graph Exporter

using UnrealBuildTool;

public class AssetGraphExporter : ModuleRules
{
	public AssetGraphExporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AssetRegistry",
			"UnrealEd",
			"BlueprintGraph",
			"AnimGraph",
		});
	}
}
