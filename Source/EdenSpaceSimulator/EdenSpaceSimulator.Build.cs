// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EdenSpaceSimulator : ModuleRules
{
	public EdenSpaceSimulator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"HTTP",
			"UMG",
			"Slate",
			"SlateCore"
		});

		// Json is used to parse EDEN OS advisory responses. Responses come from an external service,
		// so hand-scanned string extraction is not safe: escaped quotes inside rationale text would
		// silently truncate or corrupt parsed values.
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
	}
}
