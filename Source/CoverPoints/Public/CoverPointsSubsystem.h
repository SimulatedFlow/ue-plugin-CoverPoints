// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CoverPointsTypes.h"
#include "CoverPointsSubsystem.generated.h"

class ACoverVolume;
class AHUD;
class UCanvas;

/** Fired when a build finishes. The int is how many points the world now holds. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCoverBuildCompleted, int32, TotalPoints);

/** Fired when the spot-check verifier strikes a point out. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCoverPointInvalidated, FCoverPointHandle, Handle);

/**
 * The whole plugin: generation in slices, a spatial index, scoring, exclusive reservation and the numbers.
 *
 * Its job in one sentence: turn level geometry into a list of places worth standing, then answer "which
 * of them is best for this agent against that threat" often enough and cheaply enough that a fight can
 * ask on every decision tick.
 *
 * **Generation costs traces. Queries do not.** That is the trade the whole design is built on, and it is
 * what separates this from the version everybody writes by hand. The hand-written one shoots a line trace
 * per candidate per query, so it gets slower every time an artist adds a crate and slower again every
 * time a designer adds an agent. Here, the dozen or so sweeps that decide what a point *is* - one down for the floor, eight to find
 * the wall, one at standing height and up to five for the ways out of it - are paid once, in slices, with a
 * millisecond ceiling per frame. Afterwards a query is a hash lookup over a few cells and a handful of
 * dot products, and it costs the same whether the level holds two hundred points or twenty thousand.
 *
 * **A point belongs to at most one agent.** Not a preference, a guarantee. Claim takes a point or fails,
 * and every claim carries an expiry, because the agent that cannot release its point is precisely the one
 * that died holding it.
 *
 * **Nothing is trusted forever.** A level changes after it has been sampled - a door opens, a wall comes
 * down - so a small fixed budget of line traces per frame audits points that claim to be shielded, and
 * strikes out the ones that turn out not to be. It is a background sweep, not a per-query check; the
 * counter for it is on the box next to everything else.
 *
 * Game and PIE only. Generating cover into an editor viewport while somebody is still building the level
 * is the sort of help nobody asked for.
 */
UCLASS(meta = (DisplayName = "Cover Points Subsystem"))
class COVERPOINTS_API UCoverPointsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//~ UWorldSubsystem interface
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	//~ FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The subsystem for whatever world the context object belongs to, or null. */
	static UCoverPointsSubsystem* Get(const UObject* WorldContextObject);

	//~ Volumes ------------------------------------------------------------------------------------------

	/** Called by ACoverVolume on BeginPlay. Harmless to call twice. */
	void RegisterVolume(ACoverVolume* Volume);

	/** Called by ACoverVolume on EndPlay. Drops the volume's points with it. */
	void UnregisterVolume(ACoverVolume* Volume);

	/** Every cover volume in this world, in registration order. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	void GetAllVolumes(TArray<ACoverVolume*>& OutVolumes) const;

	//~ Generation ---------------------------------------------------------------------------------------

	/**
	 * Queue one volume, or every enabled volume when Volume is null, for generation.
	 *
	 * Returns false only when generation is switched off in project settings. Calling it while a build is
	 * already running replaces that build rather than queueing behind it: the second request is always the
	 * more recent description of the level, and finishing a stale one first would put points into the
	 * world that the caller already knows are wrong.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	bool RequestBuild(ACoverVolume* Volume = nullptr);

	/** Run the queued build to completion on this frame, ignoring the per-frame budget. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	void FinishBuildImmediately();

	/** Throw away every point, every claim and any build in flight. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	void ClearAllPoints();

	/** A build is running right now. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	bool IsBuilding() const;

	/** Generation progress, 0..1. Sits at 1 when nothing is building. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	float GetBuildProgress() const;

	//~ Points -------------------------------------------------------------------------------------------

	/** How many cover points exist. Invalidated ones are included; they are still points, just unusable. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	int32 GetPointCount() const;

	/** Copy out one point. False when the handle is stale, out of range or from a previous build. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	bool GetPoint(const FCoverPointHandle& Handle, FCoverPoint& OutPoint) const;

	/** True when the handle still addresses a live, non-invalidated point of the current build. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	bool IsCoverValid(const FCoverPointHandle& Handle) const;

	/** Every point in a sphere, nearest first, ignoring threat direction entirely. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	void FindCoverNear(const FVector& Location, float Radius, int32 MaxResults, TArray<FCoverPoint>& OutPoints) const;

	//~ Queries ------------------------------------------------------------------------------------------

	/**
	 * The best point for this agent against this threat, inside this radius.
	 *
	 * No traces. The cells overlapping the search sphere are gathered from the spatial hash and each point
	 * in them is scored with six dot products and a few divisions. What that costs is on the counters box
	 * in microseconds, next to how many points it had to look at - because "constant time" is a claim, and
	 * the candidate count is the evidence for it.
	 *
	 * With bClaimAgent set, the winning point is claimed for AgentActor before it is returned, which closes
	 * the gap between deciding and reserving. Two agents querying on the same frame therefore cannot both
	 * be given the same point, which they can if the caller claims a frame later.
	 */
	FCoverQueryResult FindBestCover(
		AActor* AgentActor,
		const FVector& AgentLocation,
		const FVector& ThreatLocation,
		float SearchRadius,
		const FCoverQueryParams& Params,
		bool bClaimAgent);

	//~ Claims -------------------------------------------------------------------------------------------

	/**
	 * Give this point to this agent, exclusively.
	 *
	 * False when somebody else has it. Claiming a point the same agent already holds succeeds and refreshes
	 * the expiry - which is how an agent that sits in cover for two minutes keeps it without the caller
	 * having to know that expiries exist.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	bool ClaimCover(const FCoverPointHandle& Handle, AActor* AgentActor, float LifetimeSeconds = -1.0f);

	/** Give the point back. False when this agent did not hold it. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	bool ReleaseCover(const FCoverPointHandle& Handle, AActor* AgentActor);

	/** Give back whatever this agent holds. Returns how many points were freed. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	int32 ReleaseCoverForActor(AActor* AgentActor);

	/** Free every claim in the world. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	int32 ReleaseAllClaims();

	/** The agent holding this point, or null. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	AActor* GetCoverOwner(const FCoverPointHandle& Handle) const;

	/** The point this agent holds. Handle is unset when it holds none. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	FCoverPointHandle GetClaimedCoverForActor(const AActor* AgentActor) const;

	//~ Threat -------------------------------------------------------------------------------------------

	/**
	 * Where the danger is, for the debug drawing and for the spot-check verifier.
	 *
	 * Queries take their own threat position as an argument and never read this - one fight can have four
	 * threats and each agent scores against its own. This is the world's *current* threat, the one the
	 * colours on screen and the background audit are about.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	void SetThreatLocation(const FVector& Location);

	/** Follow this actor as the threat, updated every tick. Null goes back to the fixed position. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	void SetThreatActor(AActor* Actor);

	/** Forget the threat. Colours go neutral and the verifier idles. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints")
	void ClearThreat();

	/** Where the current threat is. Meaningless when HasThreat is false. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	FVector GetThreatLocation() const;

	/** A threat position is set. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	bool HasThreat() const;

	//~ Scoring helpers ----------------------------------------------------------------------------------

	/**
	 * How well a point shields against a threat: 1 fully behind the cover, -1 fully exposed.
	 *
	 * Public because the demo colours points with it and because it is the one number worth exposing from
	 * the scorer - anybody building their own selection can rank on this and skip the weights entirely.
	 */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	static float GetShielding(const FCoverPoint& Point, const FVector& ThreatLocation);

	/** Which way a point should lean to face a threat, given what it can actually do. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	static ECoverPeekSide GetPeekSideTowards(const FCoverPoint& Point, const FVector& ThreatLocation);

	//~ Debug --------------------------------------------------------------------------------------------

	/** Draw the points in the world. Compiled out of Shipping, like every DrawDebug call. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints|Debug")
	void SetShowPoints(bool bShow);

	/** Points are being drawn. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints|Debug")
	bool IsShowingPoints() const { return bShowPoints; }

	/** Draw a marker at each point for every direction it can lean out in. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints|Debug")
	void SetShowPeekSides(bool bShow);

	/** Peek markers are being drawn. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints|Debug")
	bool IsShowingPeekSides() const { return bShowPeekSides; }

	//~ Stats --------------------------------------------------------------------------------------------

	/** Everything the counters box draws. */
	UFUNCTION(BlueprintPure, Category = "CoverPoints")
	const FCoverPointsStats& GetStats() const { return Stats; }

	/** Draw the counters box at this corner and width. Called by ACoverPointsHUD. */
	void DrawStatsBox(UCanvas* Canvas, const FVector2D& Origin, float Width) const;

	/** How many lines the counters box will draw, so a caller can size a panel around it. */
	int32 GetStatsLineCount() const;

	/** Print the measured counters to the log. What Cover.Stats calls. */
	void LogStats() const;

	//~ Events -------------------------------------------------------------------------------------------

	/** A build finished and the points are usable. */
	UPROPERTY(BlueprintAssignable, Category = "CoverPoints")
	FCoverBuildCompleted OnBuildCompleted;

	/** The verifier caught a point failing to block and struck it out. */
	UPROPERTY(BlueprintAssignable, Category = "CoverPoints")
	FCoverPointInvalidated OnPointInvalidated;

private:
	//~ Generation ---------------------------------------------------------------------------------------

	/** One generation run, carried across as many frames as its budget needs. */
	struct FBuildJob
	{
		/** Volumes still to process, plus the ones already done - the whole list, walked by VolumeIndex. */
		TArray<TWeakObjectPtr<ACoverVolume>> Volumes;

		/** Samples per volume, worked out up front so progress is a real fraction and not a guess. */
		TArray<int32> SamplesPerVolume;

		int32 VolumeIndex = 0;
		int32 SampleIndex = 0;
		int32 SamplesProcessed = 0;
		int32 SamplesTotal = 0;

		/** Points accumulated so far. They are not published until the build finishes. */
		TArray<FCoverPoint> Points;

		double StartTimeSeconds = 0.0;
		float PeakSliceMilliseconds = 0.0f;
		bool bActive = false;
	};

	FBuildJob Build;

	/** Process at most one slice of the build in flight. */
	void TickBuild();

	/** Everything one grid sample does: find the floor, find the walls, measure height and peek sides. */
	void ProcessSample(const ACoverVolume& Volume, int32 SampleIndex, TArray<FCoverPoint>& OutPoints) const;

	/** Swap the accumulated points in, rebuild the spatial hash, bump the build id, fire the event. */
	void FinalizeBuild();

	//~ Spatial index -----------------------------------------------------------------------------------

	/** Which cell a world position falls in. */
	FIntVector CellOf(const FVector& Location) const;

	/** Throw the hash away and refill it from Points. O(points), run once per build. */
	void RebuildSpatialHash();

	//~ Verification -------------------------------------------------------------------------------------

	/** Spend at most the per-frame trace budget auditing points against the current threat. */
	void TickVerification();

	//~ Claims -------------------------------------------------------------------------------------------

	/** Collect claims whose holder died or stopped refreshing. */
	void SweepExpiredClaims();

	/** Free a claim without touching the owner map. Callers that iterate it need this. */
	void ClearClaimAt(int32 PointIndex);

	//~ Debug --------------------------------------------------------------------------------------------

	void DrawDebugPoints() const;

	//~ HUD ----------------------------------------------------------------------------------------------

	void RebindHudDelegate();
	void OnAnyHUDPostRender(AHUD* HUD, UCanvas* Canvas);

	//~ State --------------------------------------------------------------------------------------------

	/**
	 * Every point in the world, flat.
	 *
	 * Not a UPROPERTY, and it does not need to be: FCoverPoint holds a weak pointer and plain numbers, so
	 * there is nothing in here for the garbage collector to keep alive. Making it a UPROPERTY would add a
	 * reflected array of thousands of structs to every GC pass in exchange for nothing.
	 */
	TArray<FCoverPoint> Points;

	/** Cell -> indices into Points. The reason a query costs no traces and no linear scan. */
	TMap<FIntVector, TArray<int32>> SpatialHash;

	/** Agent -> the point it holds. Kept alongside the flag on the point so release-by-actor is O(1). */
	TMap<TWeakObjectPtr<AActor>, int32> OwnerToPoint;

	/** Volumes registered in this world. Weak: a streamed-out volume must not be held alive by this list. */
	TArray<TWeakObjectPtr<ACoverVolume>> Volumes;

	/** Bumped on every finished build so handles from earlier generations fail rather than mislead. */
	int32 BuildId = 1;

	/** Cached from settings on Initialize so the hot paths do not go through the CDO. */
	float CellSize = 500.0f;

	//~ Threat -------------------------------------------------------------------------------------------

	FVector ThreatLocation = FVector::ZeroVector;
	bool bHasThreat = false;
	TWeakObjectPtr<AActor> ThreatActor;

	//~ Verification cursor ------------------------------------------------------------------------------

	/** Where the background audit got to. It walks the whole array and wraps, over seconds. */
	int32 VerificationCursor = 0;

	//~ Timers -------------------------------------------------------------------------------------------

	float ClaimSweepAccumulator = 0.0f;
	float QueryWindowAccumulator = 0.0f;
	int32 QueriesInWindow = 0;
	double QueryMicrosecondsInWindow = 0.0;

	//~ Toggles ------------------------------------------------------------------------------------------

	bool bShowPoints = false;
	bool bShowPeekSides = false;
	bool bAutoDrawStatsOnAnyHUD = false;

	FDelegateHandle HudPostRenderHandle;

	/** Frame the counters box was last drawn on, so the two draw paths cannot stack. */
	mutable uint64 LastStatsDrawFrame = 0;

	/** Mutable because a query is logically const and still has to record what it cost. */
	mutable FCoverPointsStats Stats;
};
