// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CoverPointsTypes.h"
#include "CoverPointsStatics.generated.h"

class AActor;
class ACoverVolume;

/**
 * The whole plugin from Blueprint, without a reference to the subsystem in sight.
 *
 * Every call is world-context-aware and safe in a world that has no CoverPoints subsystem at all: queries
 * answer "no cover", setters do nothing, and nobody crashes. That matters more than it sounds - it means
 * an AI Blueprint written against CoverPoints still runs in a test map where no volume was ever placed,
 * and it takes the fallback branch a designer already wrote instead of taking down the level.
 */
UCLASS(meta = (DisplayName = "CoverPoints"))
class COVERPOINTS_API UCoverPointsStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	//~ The five calls an AI actually makes ---------------------------------------------------------------

	/**
	 * The best cover for this agent against this threat, and optionally reserve it in the same breath.
	 *
	 * No traces are fired. The cells around the agent are pulled out of the spatial hash and each point in
	 * them is scored on six numbers that were all measured during generation.
	 *
	 * Leave bClaimCover on unless you are asking a question rather than sending somebody. Claiming inside
	 * the query is what makes it impossible for two agents that ask on the same frame to be handed the
	 * same point; claiming a frame later, from the Blueprint, is not.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints",
		meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Params",
			AutoCreateRefTerm = "Params", DisplayName = "Find Best Cover"))
	static FCoverQueryResult FindBestCover(
		const UObject* WorldContextObject,
		AActor* AgentActor,
		const FVector& ThreatLocation,
		float SearchRadius = 2000.0f,
		bool bClaimCover = true,
		const FCoverQueryParams& Params = FCoverQueryParams());

	/**
	 * The same query, for an agent that is not an actor yet - a spawn picker, a designer tool, a test.
	 *
	 * Takes the agent's position directly instead of reading it off an actor, and therefore cannot claim.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints",
		meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "Params",
			AutoCreateRefTerm = "Params", DisplayName = "Find Best Cover At Location"))
	static FCoverQueryResult FindBestCoverAtLocation(
		const UObject* WorldContextObject,
		const FVector& AgentLocation,
		const FVector& ThreatLocation,
		float SearchRadius = 2000.0f,
		const FCoverQueryParams& Params = FCoverQueryParams());

	/** Every point inside a sphere, nearest first, ignoring the threat entirely. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static void FindCoverNear(
		const UObject* WorldContextObject,
		const FVector& Location,
		float Radius,
		int32 MaxResults,
		TArray<FCoverPoint>& OutPoints);

	/**
	 * Reserve a point for an agent, exclusively.
	 *
	 * False means somebody else has it. Claiming a point the same agent already holds succeeds and pushes
	 * the expiry out, so an agent that sits in cover keeps it by asking again rather than by the caller
	 * having to know that expiries exist at all.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static bool ClaimCover(
		const UObject* WorldContextObject,
		const FCoverPointHandle& Handle,
		AActor* AgentActor,
		float LifetimeSeconds = -1.0f);

	/** Give the point back. False when this agent did not hold it. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static bool ReleaseCover(const UObject* WorldContextObject, const FCoverPointHandle& Handle, AActor* AgentActor);

	/** Give back whatever this agent holds, without having to have kept the handle. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static int32 ReleaseCoverForActor(const UObject* WorldContextObject, AActor* AgentActor);

	/** Free every claim in the world. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static int32 ReleaseAllCover(const UObject* WorldContextObject);

	/**
	 * True while this handle still addresses a live point of the current generation.
	 *
	 * Call it before walking to a point that was chosen a few seconds ago. It goes false when the level
	 * was regenerated and when the spot-check verifier caught that point failing to block - which is the
	 * case worth checking for, because it means the wall the agent is walking towards is not there.
	 */
	UFUNCTION(BlueprintPure, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static bool IsCoverValid(const UObject* WorldContextObject, const FCoverPointHandle& Handle);

	//~ Generation ---------------------------------------------------------------------------------------

	/**
	 * Generate cover for one volume, or for every enabled volume when Volume is null.
	 *
	 * Runs in slices across the following frames under the millisecond budget in project settings. Points
	 * belonging to volumes this call did not name are left alone, so rebuilding the room a wall just came
	 * down in does not throw away the cover in the rest of the level.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static bool RebuildVolume(const UObject* WorldContextObject, ACoverVolume* Volume);

	/** Run the queued build to completion on this frame. For loading screens, not for gameplay. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static void FinishBuildImmediately(const UObject* WorldContextObject);

	/** Throw away every point, every claim and any build in flight. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static void ClearAllCover(const UObject* WorldContextObject);

	/** A build is running. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static bool IsBuildingCover(const UObject* WorldContextObject);

	/** Generation progress, 0..1. Sits at 1 when nothing is building. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static float GetCoverBuildProgress(const UObject* WorldContextObject);

	//~ Threat -------------------------------------------------------------------------------------------

	/** Set what the debug colours and the spot-check verifier are measured against. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static void SetThreatLocation(const UObject* WorldContextObject, const FVector& Location);

	/** Follow an actor as the threat, updated every tick. Null goes back to the fixed position. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static void SetThreatActor(const UObject* WorldContextObject, AActor* Actor);

	/** Forget the threat. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static void ClearThreat(const UObject* WorldContextObject);

	/** Where the current threat is. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static FVector GetThreatLocation(const UObject* WorldContextObject);

	//~ Inspection ---------------------------------------------------------------------------------------

	/** Copy out one point. False when the handle is stale or from a previous generation. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static bool GetCoverPoint(const UObject* WorldContextObject, const FCoverPointHandle& Handle, FCoverPoint& OutPoint);

	/** The agent holding a point, or null. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static AActor* GetCoverOwner(const UObject* WorldContextObject, const FCoverPointHandle& Handle);

	/** The point this agent holds. The handle is unset when it holds none. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static FCoverPointHandle GetClaimedCoverForActor(const UObject* WorldContextObject, AActor* AgentActor);

	/** How many cover points exist in this world. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static int32 GetCoverPointCount(const UObject* WorldContextObject);

	/** Everything the counters box draws. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static FCoverPointsStats GetCoverStats(const UObject* WorldContextObject);

	/** Every cover volume in this world. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints", meta = (WorldContext = "WorldContextObject"))
	static void GetAllCoverVolumes(const UObject* WorldContextObject, TArray<ACoverVolume*>& OutVolumes);

	//~ Scoring helpers ----------------------------------------------------------------------------------

	/** How well a point shields against a threat: 1 fully behind the cover, -1 fully exposed. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	static float GetCoverShielding(const FCoverPoint& Point, const FVector& ThreatLocation);

	/** Which way a point should lean to face a threat, given what it can actually do. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	static ECoverPeekSide GetPeekSideTowards(const FCoverPoint& Point, const FVector& ThreatLocation);

	/**
	 * The rotation an agent should stand at when it takes this point.
	 *
	 * Facing away from the cover, out into the open - which is where the threat is. Turning to face the
	 * wall is a mistake every hand-rolled cover system makes once, because the cover normal is the obvious
	 * thing to point at.
	 */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	static FRotator GetCoverFacingRotation(const FCoverPoint& Point, const FVector& ThreatLocation);

	/** "Left", "Right", "Over" or "None", for a label on a screen. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	static FString PeekSideToString(ECoverPeekSide Side);

	//~ Debug --------------------------------------------------------------------------------------------

	/** Draw the cover points in the world, coloured against the current threat. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints|Debug", meta = (WorldContext = "WorldContextObject"))
	static void SetShowCoverPoints(const UObject* WorldContextObject, bool bShow);

	/** Points are being drawn. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints|Debug", meta = (WorldContext = "WorldContextObject"))
	static bool IsShowingCoverPoints(const UObject* WorldContextObject);

	/** Draw a marker at each point for every direction it can lean out in. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints|Debug", meta = (WorldContext = "WorldContextObject"))
	static void SetShowPeekSides(const UObject* WorldContextObject, bool bShow);

	/** Peek markers are being drawn. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints|Debug", meta = (WorldContext = "WorldContextObject"))
	static bool IsShowingPeekSides(const UObject* WorldContextObject);
};
