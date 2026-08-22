// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/DeveloperSettings.h"
#include "CoverPointsTypes.h"
#include "CoverPointsSettings.generated.h"

/**
 * Project-wide defaults for CoverPoints, under Project Settings -> Plugins -> CoverPoints.
 *
 * The split is deliberate. What a *place* looks like - grid spacing, agent radius, how tall the agent is
 * standing and crouching - belongs on the volume, because it changes from the warehouse to the trench.
 * What the *project* decided once - which collision channel a sweep uses, how many milliseconds a build
 * may take out of a frame, how many spot-check traces a frame may afford - is here.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "CoverPoints"))
class COVERPOINTS_API UCoverPointsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCoverPointsSettings();

	//~ UDeveloperSettings interface
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	/** The settings object, never null. */
	static const UCoverPointsSettings& Get();

	//~ General ------------------------------------------------------------------------------------------

	/**
	 * Let volumes generate at all.
	 *
	 * Off leaves the subsystem running - volumes still register, the counters box still draws, queries
	 * still answer "no cover here" - but no grid is ever laid down. That is what a project wants while it
	 * is working out whether a frame cost is coming from cover generation or from something else, and it
	 * is a far smaller change than pulling the volumes out of a map.
	 */
	UPROPERTY(config, EditAnywhere, Category = "General")
	bool bEnabled = true;

	//~ Generation ---------------------------------------------------------------------------------------

	/**
	 * Collision channel the generation sweeps are traced on.
	 *
	 * Visibility is the right default because it is the channel that already answers "is there something
	 * solid here" in every project. A project with a dedicated cover channel should say so here once.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Generation")
	TEnumAsByte<ECollisionChannel> GenerationChannel = ECC_Visibility;

	/** Trace against complex collision while generating. Off - the simple shapes - is what cover wants. */
	UPROPERTY(config, EditAnywhere, Category = "Generation")
	bool bTraceComplex = false;

	/**
	 * Grid samples a single frame may process.
	 *
	 * The other half of the budget. Whichever ceiling is reached first ends the slice, so a frame full of
	 * cheap misses is still bounded and a frame full of expensive hits is still bounded.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Generation", meta = (ClampMin = "1", UIMax = "4096"))
	int32 MaxSamplesPerFrame = 192;

	/**
	 * Milliseconds a single frame may spend generating.
	 *
	 * This is the promise the plugin makes, and the counters box prints the number actually spent next to
	 * it so nobody has to take it on trust. Two milliseconds of a sixteen millisecond frame is a level
	 * that builds its cover in the background while the player walks in.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (ClampMin = "0.05", UIMax = "16.0", ForceUnits = "ms"))
	float MaxBuildMillisecondsPerFrame = 2.0f;

	/**
	 * Project every grid sample onto the navmesh before it may become cover.
	 *
	 * A point an agent cannot reach is not cover, it is a decoration that queries will keep offering. When
	 * there is no navmesh in the world this silently falls back to the downward floor trace, so a test map
	 * without navigation still generates something rather than nothing.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Generation")
	bool bProjectToNavMesh = true;

	/** How far a sample may be moved to land on the navmesh, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (ClampMin = "1.0", UIMax = "500.0", ForceUnits = "cm", EditCondition = "bProjectToNavMesh"))
	float NavProjectionExtent = 120.0f;

	/**
	 * Steepest floor a cover point may sit on, as the Z of its surface normal.
	 *
	 * 0.7 is roughly a 45 degree slope. Below that an agent slides, and a cover point on a slide is a cover
	 * point the agent will be pushed out of a second after it arrives.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Generation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinFloorNormalZ = 0.7f;

	/**
	 * How different two cover normals must be for one sample to yield two points, as a dot product.
	 *
	 * A sample tucked into a corner genuinely has two pieces of cover facing two ways and should produce
	 * two points. A sample in front of a flat wall hits it with three of its eight sweeps and must produce
	 * one. 0.9 - about 25 degrees - separates those two cases.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Generation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DistinctNormalThreshold = 0.9f;

	/** Most cover points one grid sample may produce, however many distinct walls it can see. */
	UPROPERTY(config, EditAnywhere, Category = "Generation", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxPointsPerSample = 2;

	/**
	 * Discard a new point when an existing one within this many centimetres already faces the same way.
	 *
	 * Zero - the default - turns thinning off, so halving the grid spacing really does give four times the
	 * points and the spatial index is measured against the load it was built for. Raise it when a level
	 * has been sampled finer than its geometry deserves.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Generation",
		meta = (ClampMin = "0.0", UIMax = "200.0", ForceUnits = "cm"))
	float MinPointSeparation = 0.0f;

	//~ Peeking ------------------------------------------------------------------------------------------

	/**
	 * How far sideways an agent is assumed to lean, in centimetres.
	 *
	 * This is a body width, not a step. Too small and every point claims it can peek because the probe
	 * never left the wall's shadow; too large and the probe tests a place the agent is not going to stand.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Peeking",
		meta = (ClampMin = "10.0", UIMax = "200.0", ForceUnits = "cm"))
	float PeekLateralOffset = 85.0f;

	/**
	 * How far past the cover the peek probe looks, in centimetres.
	 *
	 * Measured from the point, so it has to be comfortably longer than the volume's cover distance or the
	 * probe stops inside the wall and every point reports that it cannot peek.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Peeking",
		meta = (ClampMin = "50.0", UIMax = "1500.0", ForceUnits = "cm"))
	float PeekProbeDistance = 600.0f;

	//~ Queries ------------------------------------------------------------------------------------------

	/**
	 * Edge length of one spatial hash cell, in centimetres.
	 *
	 * The one number that decides what a query costs. Too small and a query walks hundreds of nearly empty
	 * cells; too large and each cell hands the scorer points from the next room. A cell roughly the size of
	 * a typical search radius is the shape that keeps both halves small.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Queries",
		meta = (ClampMin = "50.0", UIMax = "5000.0", ForceUnits = "cm"))
	float QueryCellSize = 500.0f;

	/**
	 * Most points one query may score, however many the cells contain.
	 *
	 * A ceiling, not a target. It exists so that a query with an absurd radius over a finely sampled level
	 * degrades into a slightly worse answer rather than into a frame spike, and the counters box prints the
	 * number actually scored so the truncation is never silent.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Queries", meta = (ClampMin = "8", UIMax = "4096"))
	int32 MaxCandidatesPerQuery = 512;

	/** The scoring defaults every query starts from. */
	UPROPERTY(config, EditAnywhere, Category = "Queries")
	FCoverQueryParams DefaultQueryParams;

	//~ Claims -------------------------------------------------------------------------------------------

	/**
	 * Seconds an unrefreshed claim survives.
	 *
	 * The reason it exists: an agent that is destroyed while holding a point cannot release it, and cover
	 * that is permanently held by a corpse is a level that runs out of cover over an afternoon. Claiming
	 * the same point again refreshes the clock, so an agent that is alive and still there never loses it.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Claims",
		meta = (ClampMin = "0.0", UIMax = "600.0", ForceUnits = "s"))
	float ClaimLifetimeSeconds = 30.0f;

	/**
	 * Seconds between sweeps for expired and orphaned claims.
	 *
	 * The sweep is O(claims), not O(points), and the claims are the small number. Running it every frame
	 * would be affordable and still pointless - nothing downstream can tell the difference between a claim
	 * collected now and one collected a quarter of a second later.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Claims",
		meta = (ClampMin = "0.05", UIMax = "5.0", ForceUnits = "s"))
	float ClaimSweepInterval = 0.25f;

	//~ Verification -------------------------------------------------------------------------------------

	/**
	 * Spot-check a few points per frame against the current threat.
	 *
	 * Generation happens once; levels do not. A door opens, a wall is blown out, a container is driven
	 * away, and a point that was measured as safe is now a lie. This catches that without re-running
	 * generation and without a trace per query.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Verification")
	bool bVerifyCover = true;

	/**
	 * Line traces a single frame may spend verifying.
	 *
	 * Small on purpose. This is a background audit sweeping the whole point set over seconds, not a
	 * per-query check - the moment it costs enough to be worth optimising, it has stopped being free.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Verification", meta = (ClampMin = "0", UIMax = "64"))
	int32 MaxVerificationTracesPerFrame = 4;

	/**
	 * Only spot-check points scoring at least this much shielding against the threat.
	 *
	 * A point that never claimed to block cannot be caught lying about blocking. Auditing those would spend
	 * the trace budget confirming things nobody asserted.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Verification",
		meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float VerificationMinShielding = 0.3f;

	/** Height above the point the verification trace starts from, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Verification",
		meta = (ClampMin = "0.0", UIMax = "300.0", ForceUnits = "cm"))
	float VerificationEyeHeight = 60.0f;

	//~ Presentation -------------------------------------------------------------------------------------

	/** Start with the counters box on. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bShowStatsByDefault = true;

	/**
	 * Draw the counters box through AHUD::OnHUDPostRender as well, so a project keeps its own HUD class.
	 *
	 * ACoverPointsHUD exists for projects that have no HUD of their own. This is for the far more common
	 * case where a project already has one and is not about to reparent it to see a cover system's numbers.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bAutoDrawStatsOnAnyHUD = false;

	/** Start with the point drawing on. Equivalent to Cover.Show 1 at startup. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bShowPointsByDefault = false;

	/** Start with the peek markers on. Equivalent to Cover.Show 1 plus the peek toggle. */
	UPROPERTY(config, EditAnywhere, Category = "Presentation")
	bool bShowPeekSidesByDefault = false;
};
