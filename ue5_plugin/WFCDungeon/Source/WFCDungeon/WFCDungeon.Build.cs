// SPDX-License-Identifier: MIT
//
// Build configuration for the WFCDungeon runtime module.
//
// The module needs:
//   - Core / CoreUObject / Engine for the usual UObject machinery
//   - Json + JsonUtilities for FJsonObjectConverter (parses dungeon.json)
//
// Everything else is pulled transitively. We use IWYU-style includes
// (Public/Private split) which is the recommended layout since UE 5.2.

using UnrealBuildTool;

public class WFCDungeon : ModuleRules
{
	public WFCDungeon(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicIncludePaths.AddRange(new string[] { });
		PrivateIncludePaths.AddRange(new string[] { });

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Json",
			"JsonUtilities",
		});
	}
}
