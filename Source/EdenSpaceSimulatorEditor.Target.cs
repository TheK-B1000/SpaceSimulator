// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class EdenSpaceSimulatorEditorTarget : TargetRules
{
	public EdenSpaceSimulatorEditorTarget( TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		// VS 2026's MSVC 14.50 ICE/C1001's on this project's unity files. Pin the
		// installed VS 2022 14.44 toolchain that UE 5.8 actually supports.
		WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2022;
		WindowsPlatform.CompilerVersion = "14.44.35207";
		ExtraModuleNames.Add("EdenSpaceSimulator");
	}
}
