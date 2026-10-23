// Asset Graph Exporter

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
		});
	}
}
