// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ue5_dev_cpp : ModuleRules
{
	public ue5_dev_cpp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine", 
			"InputCore", 
			"EnhancedInput",
			
		});
		
		// -------------------------------------------------------------------------------------
		// Pluginコードの依存.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				// ... add other public dependencies that you statically link with here ...
				"NagaExperiment",
			}
		);
		// -------------------------------------------------------------------------------------
	}
}
