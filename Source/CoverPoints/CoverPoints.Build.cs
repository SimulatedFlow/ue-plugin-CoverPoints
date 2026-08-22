// Copyright 2026 Silvan Teufel. All Rights Reserved.

using UnrealBuildTool;

public class CoverPoints : ModuleRules
{
	public CoverPoints(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// One runtime module and nothing else.
		//
		// NavigationSystem is here for exactly one job: projecting a raw grid sample onto walkable floor
		// before it is allowed to become a cover point. A point an agent cannot stand on is not cover, and
		// the navmesh already knows where an agent can stand - re-deriving that from traces would be a
		// second, worse answer to a question the project has already answered.
		//
		// Deliberately NOT here:
		//   UMG      - the product is a query service. The counters box is drawn on UCanvas from AHUD so it
		//              survives a cooked Shipping build. The demo map's buttons are UMG assets in Content
		//              that call the Blueprint library, exactly as a project would.
		//   Niagara  - nothing here is a particle.
		//   Chaos    - the only physics touched is world sweeps and line traces, which are plain world
		//              queries and need no physics module of their own.
		//   UnrealEd - everything here ships. There is no editor module, so nothing can go missing between
		//              what a designer places in the editor and what the packaged game generates.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"NavigationSystem",
		});

		// RenderCore gives us GWhiteTexture, the one-pixel texture the counters box is tiled from.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
		});
	}
}
