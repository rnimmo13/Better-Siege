// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Seige : ModuleRules
{
	public Seige(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"NavigationSystem",
			"GameplayTasks"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Seige",
			"Seige/Variant_Horror",
			"Seige/Variant_Horror/UI",
			"Seige/Variant_Shooter",
			"Seige/Variant_Shooter/AI",
			"Seige/Variant_Shooter/UI",
			"Seige/Variant_Shooter/Weapons",
			"Seige/Siege"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
