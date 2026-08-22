// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "CoverPointsTypes.generated.h"

class AActor;

/**
 * How much of an agent a point hides.
 *
 * Measured, not authored: a second sweep is fired at standing height in the same direction as the one
 * that found the cover. If it also hits, the shielding geometry reaches above the agent's head and the
 * point is High. If it passes through, the geometry is a crate or a low wall and the point is Low - the
 * agent has to crouch to be hidden, and can shoot over the top without leaning out.
 */
UENUM(BlueprintType)
enum class ECoverHeight : uint8
{
	/** Chest-high or lower. Crouching hides the agent; standing does not. */
	Low		UMETA(DisplayName = "Low"),

	/** Taller than the agent. Standing behind it hides the agent completely. */
	High	UMETA(DisplayName = "High"),
};

/**
 * The way out of cover a query picked for this agent against this threat.
 *
 * A point can allow more than one; this is the one that faces the threat, which is the one the animation
 * or the aiming code actually wants to be told about.
 */
UENUM(BlueprintType)
enum class ECoverPeekSide : uint8
{
	/** Nowhere to lean. The point hides, but shooting from it means stepping out of it. */
	None	UMETA(DisplayName = "None"),

	/** Lean out around the left-hand edge. */
	Left	UMETA(DisplayName = "Left"),

	/** Lean out around the right-hand edge. */
	Right	UMETA(DisplayName = "Right"),

	/** Rise up and shoot over the top. Only ever offered by Low cover. */
	Over	UMETA(DisplayName = "Over"),
};

/**
 * A stable reference to one cover point.
 *
 * The index alone would not do. Points live in one flat array that is thrown away and rebuilt whenever a
 * volume is regenerated, so an index handed out before a rebuild would silently address a different
 * point afterwards - the worst kind of bug, because it looks like it works. The build id is bumped on
 * every rebuild, so a handle from a previous generation fails IsCoverValid instead of lying.
 */
USTRUCT(BlueprintType)
struct COVERPOINTS_API FCoverPointHandle
{
	GENERATED_BODY()

	/** Index into the subsystem's point array, or INDEX_NONE. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 Index = INDEX_NONE;

	/** Which generation of the point set this handle was cut from. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 BuildId = 0;

	FCoverPointHandle() = default;

	FCoverPointHandle(int32 InIndex, int32 InBuildId)
		: Index(InIndex)
		, BuildId(InBuildId)
	{
	}

	/** True when this handle points at something. Says nothing about whether that something still exists. */
	bool IsSet() const
	{
		return Index != INDEX_NONE;
	}

	bool operator==(const FCoverPointHandle& Other) const
	{
		return Index == Other.Index && BuildId == Other.BuildId;
	}

	bool operator!=(const FCoverPointHandle& Other) const
	{
		return !(*this == Other);
	}

	friend uint32 GetTypeHash(const FCoverPointHandle& Handle)
	{
		return HashCombine(::GetTypeHash(Handle.Index), ::GetTypeHash(Handle.BuildId));
	}
};

/**
 * One piece of cover: a place to stand and the direction of the thing that shields it.
 *
 * CoverNormal is the surface normal of the geometry that was hit, so it points *away* from the wall and
 * out into the open - the direction an exposed threat would have to be standing in for this point to be
 * useless. That sign convention is the whole scoring function in one line: a threat is blocked when the
 * direction from the point to the threat is opposite to CoverNormal.
 *
 * Everything in here was measured once during generation. Nothing in here is recomputed per query, which
 * is why a query costs no traces.
 */
USTRUCT(BlueprintType)
struct COVERPOINTS_API FCoverPoint
{
	GENERATED_BODY()

	/** Where the agent stands. Already projected onto walkable floor when navmesh projection is on. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	FVector Location = FVector::ZeroVector;

	/** Surface normal of the shielding geometry, pointing from the wall towards this point. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	FVector CoverNormal = FVector::ForwardVector;

	/** High cover hides a standing agent; low cover only hides a crouching one. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	ECoverHeight Height = ECoverHeight::Low;

	/** Centimetres from this point to the geometry that shields it. Closer is better cover. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float WallDistance = 0.0f;

	/** There is a clear line past the left-hand edge of the cover. Measured with two sweeps. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	bool bCanPeekLeft = false;

	/** There is a clear line past the right-hand edge of the cover. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	bool bCanPeekRight = false;

	/** There is a clear line over the top of the cover from standing height. Low cover only. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	bool bCanPeekOver = false;

	/** Score this point was last given by a query. Purely informational; a query never reads it. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float Score = 0.0f;

	/** Some agent owns this point right now. No second agent will be given it. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	bool bClaimed = false;

	/** The spot-check verifier caught this point failing to block. It is skipped by every query. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	bool bInvalidated = false;

	/** Stable reference to this point, safe to keep across frames. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	FCoverPointHandle Handle;

	/**
	 * The agent holding the claim, when there is one.
	 *
	 * Weak on purpose. An agent that is destroyed mid-firefight must not be kept alive by the cover system
	 * it happened to be standing in, and the claim it left behind has to be reclaimable - which is exactly
	 * what a stale weak pointer tells the sweep that collects expired claims.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	TWeakObjectPtr<AActor> ClaimedBy;

	/** Seconds on the world clock at which an unrefreshed claim is collected. Not reflected. */
	double ClaimExpiryTime = 0.0;

	/**
	 * The cover distance of the volume this point came from. Not reflected.
	 *
	 * Kept per point rather than looked up per query so that WallDistance can be scored as a fraction -
	 * 40 cm from a wall is excellent in a volume that allows 140 and unremarkable in one that allows 50,
	 * and a query that had to find the volume again to know which would not be constant time.
	 */
	float CoverDistanceRef = 140.0f;

	/** Which volume generated this point, so rebuilding one volume leaves the others alone. Not reflected. */
	TWeakObjectPtr<AActor> SourceVolume;
};

/**
 * The knobs one query may turn without touching project settings.
 *
 * Defaults come from UCoverPointsSettings, so a Blueprint that passes an untouched struct behaves the way
 * the project decided. Weights are relative to each other, not absolute: doubling all five changes
 * nothing, which is the property that makes them safe to tune by feel.
 */
USTRUCT(BlueprintType)
struct COVERPOINTS_API FCoverQueryParams
{
	GENERATED_BODY()

	/**
	 * How much the angle between cover normal and threat direction counts.
	 *
	 * This is the term that means "cover". Every other weight is a tie-breaker between points that all
	 * already block, so this one is normally the largest by a wide margin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weights", meta = (ClampMin = "0.0"))
	float ShieldingWeight = 3.0f;

	/** How much being close to the agent counts. Raise it and agents stop crossing the map for a better wall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weights", meta = (ClampMin = "0.0"))
	float AgentDistanceWeight = 1.0f;

	/**
	 * How much standing at the preferred distance from the threat counts.
	 *
	 * Not "far from the threat" - a point that is too far is as wrong as one that is too close, because an
	 * agent that retreats out of its own weapon range stops being a threat and starts being scenery.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weights", meta = (ClampMin = "0.0"))
	float ThreatDistanceWeight = 1.0f;

	/** How much high cover is preferred over low. Zero makes crates and walls equally attractive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weights", meta = (ClampMin = "0.0"))
	float HighCoverWeight = 0.75f;

	/** How much a usable way to lean out towards the threat counts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weights", meta = (ClampMin = "0.0"))
	float PeekWeight = 1.0f;

	/** How much hugging the shielding geometry counts, measured against the volume's cover distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weights", meta = (ClampMin = "0.0"))
	float WallProximityWeight = 0.5f;

	/**
	 * Distance from the threat a point is scored as ideal, in centimetres.
	 *
	 * Set this to the effective range of whatever the agent is carrying. It is the single number that
	 * turns the same cover field into a shotgun layout or a rifle layout.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring",
		meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float PreferredThreatDistance = 1200.0f;

	/**
	 * Minimum dot product between the threat direction and the reversed cover normal, -1..1.
	 *
	 * Points below it are not scored badly, they are not returned at all. 0 means "the wall is at least
	 * side-on to the threat"; 0.3 is a sensible floor for something you would call cover in a review.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring",
		meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MinShielding = 0.3f;

	/** Only return high cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bRequireHighCover = false;

	/** Only return points that offer some way to lean out towards the threat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bRequirePeek = false;

	/**
	 * Return points another agent already holds.
	 *
	 * Off is the whole point of the plugin and should stay off for anything that walks. On is for the
	 * cases where you are asking a question rather than sending somebody: a designer tool, a heat map, a
	 * "how much cover does this room have" count.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bAllowClaimed = false;
};

/**
 * What a query answered.
 *
 * The point is copied out rather than referenced. A caller that holds a reference into an array that a
 * rebuild is about to replace is a caller waiting for a crash; a copy plus a handle costs nothing at this
 * size and cannot dangle.
 */
USTRUCT(BlueprintType)
struct COVERPOINTS_API FCoverQueryResult
{
	GENERATED_BODY()

	/** False means no point in the radius passed the filters. Every other field is then meaningless. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	bool bFound = false;

	/** Reference to the winning point, valid until the next rebuild. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	FCoverPointHandle Handle;

	/** A copy of the winning point as it stood when the query ran. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	FCoverPoint Point;

	/** The winning point's score. Comparable between points of one query, not between queries. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float Score = 0.0f;

	/** How far the agent has to walk to reach it, straight-line. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float DistanceToAgent = 0.0f;

	/** How exposed the point is to the threat: 1 fully shielded, -1 facing it head on. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float Shielding = 0.0f;

	/** The way out of cover that faces the threat, if the point offers one. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	ECoverPeekSide PeekSide = ECoverPeekSide::None;

	/** The claim went through. Only ever true when the query was asked to claim. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	bool bClaimed = false;

	/** Points the spatial hash actually handed to the scorer. The number the grid index exists to keep small. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 CandidatesExamined = 0;
};

/**
 * Everything the counters box draws, and everything a test can assert on.
 *
 * All of it is measured. Nothing here is a setting read back at you - the build budget appears as the
 * milliseconds actually spent, not as the ceiling that was configured, because the second number proves
 * nothing.
 */
USTRUCT(BlueprintType)
struct COVERPOINTS_API FCoverPointsStats
{
	GENERATED_BODY()

	/** Cover points in the world right now. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 TotalPoints = 0;

	/** Of those, how many hide a standing agent. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 HighPoints = 0;

	/** Of those, how many only hide a crouching one. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 LowPoints = 0;

	/** Points offering at least one way to lean out. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 PeekablePoints = 0;

	/** Generation is running. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	bool bBuilding = false;

	/** Generation progress, 0..1. Sits at 1 when nothing is building. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float BuildProgress = 1.0f;

	/** Grid samples processed so far by the build in flight. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 SamplesProcessed = 0;

	/** Grid samples the build in flight will process in total. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 SamplesTotal = 0;

	/** Milliseconds the last build slice cost. This is the number that must stay under the budget. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float BuildMillisecondsThisFrame = 0.0f;

	/** Worst single build slice of the build in flight. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float BuildMillisecondsPeak = 0.0f;

	/** Wall-clock milliseconds the whole last build took, slices and the gaps between them included. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float LastBuildTotalMilliseconds = 0.0f;

	/** Queries answered in the last second. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float QueriesPerSecond = 0.0f;

	/** Average microseconds per query over the last second. The headline number of the plugin. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float MicrosecondsPerQuery = 0.0f;

	/** Microseconds the most recent query cost. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	float LastQueryMicroseconds = 0.0f;

	/** Queries answered since the level started. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 TotalQueries = 0;

	/** Points the last query's cells handed to the scorer, against TotalPoints a linear search would touch. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 LastQueryCandidates = 0;

	/** Occupied cells in the spatial hash. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 GridCells = 0;

	/** Points held by an agent right now. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 ClaimedPoints = 0;

	/** Claims collected because the holder died or stopped refreshing. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 ExpiredClaims = 0;

	/** Spot-check line traces fired since the level started. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 VerificationTraces = 0;

	/** Points a spot-check caught failing to block, and struck out. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 InvalidatedPoints = 0;

	/** A threat position is set, so scoring and verification have something to work against. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	bool bHasThreat = false;

	/** Where that threat is. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	FVector ThreatLocation = FVector::ZeroVector;

	/** Cover volumes registered in this world. */
	UPROPERTY(BlueprintReadOnly, Category = "CoverPoints")
	int32 RegisteredVolumes = 0;
};
