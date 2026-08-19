// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class EdenSpaceSimulatorTarget : TargetRules
{
	public EdenSpaceSimulatorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2022;
		WindowsPlatform.CompilerVersion = "14.44.35207";
		ExtraModuleNames.Add("EdenSpaceSimulator");
	}
}
