// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CoverVolume.generated.h"

class UBoxComponent;

/**
 * The area cover is generated in, and the body measurements it is generated for.
 *
 * A box component rather than an AVolume with a brush, on purpose. A brush is an editor-authored asset:
 * it cannot be spawned at runtime with a sensible size, it cannot be scaled from a Blueprint, and a
 * procedurally generated level therefore cannot lay one down over the room it just built. A box can do
 * all three, and everything this class needs from its shape is an oriented extent.
 *
 * Everything here is a property of the *place*, not of the project: how fine the floor is sampled, how
 * wide the agent is, how tall it stands and how low it crouches, how far from a wall it may stand and
 * still be behind it. A warehouse of shipping containers and a trench line want different numbers, and
 * they are different volumes.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Cover Volume"))
class COVERPOINTS_API ACoverVolume : public AActor
{
	GENERATED_BODY()

public:
	ACoverVolume();

	//~ AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The area sampled. Scale it, rotate it, move it - the grid is laid out in its local space. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CoverVolume")
	TObjectPtr<UBoxComponent> Bounds;

	//~ Grid ---------------------------------------------------------------------------------------------

	/**
	 * Centimetres between grid samples on the floor.
	 *
	 * Cost is quadratic in this number and so is point count: halving it gives four times the samples and
	 * roughly four times the points. 200 is a corridor-scale sweep, 100 finds cover behind individual
	 * crates, 50 is a debugging exercise.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverVolume|Grid",
		meta = (ClampMin = "10.0", UIMax = "1000.0", ForceUnits = "cm"))
	float GridSpacing = 200.0f;

	/**
	 * Radius of the agent this cover is generated for, in centimetres.
	 *
	 * The generation sweeps use a sphere of this radius, so a gap narrower than the agent is never reported
	 * as a way past a wall and a wall found at this distance is a wall the agent can actually reach.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverVolume|Grid",
		meta = (ClampMin = "1.0", UIMax = "200.0", ForceUnits = "cm"))
	float AgentRadius = 34.0f;

	/**
	 * How far from geometry a sample may stand and still count as cover, in centimetres.
	 *
	 * Not a search radius - a standoff. A point 20 cm from a wall is pressed against it; one at the far end
	 * of this distance is loosely behind it and scores worse for it. Long values generate more points that
	 * are worth less.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverVolume|Grid",
		meta = (ClampMin = "20.0", UIMax = "1000.0", ForceUnits = "cm"))
	float CoverDistance = 140.0f;

	//~ Agent --------------------------------------------------------------------------------------------

	/**
	 * Height above the floor the low sweep is fired from, in centimetres.
	 *
	 * This is the crouching chest. It is the sweep that decides whether a piece of cover exists at all, so
	 * it should sit at the height the agent is narrowest and lowest - anything found here shields something.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverVolume|Agent",
		meta = (ClampMin = "5.0", UIMax = "200.0", ForceUnits = "cm"))
	float CrouchHeight = 60.0f;

	/**
	 * Height above the floor the high sweep is fired from, in centimetres.
	 *
	 * The standing chest. Cover that also blocks at this height is High; cover that does not is Low, and
	 * the same miss is what makes shooting over the top possible.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverVolume|Agent",
		meta = (ClampMin = "20.0", UIMax = "400.0", ForceUnits = "cm"))
	float StandHeight = 140.0f;

	//~ Behaviour ----------------------------------------------------------------------------------------

	/**
	 * Start generating as soon as the level starts.
	 *
	 * On is right for a level whose cover never changes. Off is right when a level builds itself first and
	 * something else calls RebuildVolume when the geometry has stopped moving - generating against a level
	 * that is still being assembled measures the scaffolding.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverVolume|Behaviour")
	bool bBuildOnBeginPlay = true;

	/**
	 * Include this volume when everything is rebuilt.
	 *
	 * Off keeps the volume registered and its points gone. Useful for a region that is sealed off until an
	 * act of the game opens it, and cheaper than moving the actor out of the level.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverVolume|Behaviour")
	bool bVolumeEnabled = true;

	/**
	 * Actors the generation sweeps ignore.
	 *
	 * The volume itself is always ignored. This is for the things that are solid but are not cover - a
	 * clip-brush cage around the playable area, a physics prop that will be gone by the time anyone fights
	 * here, the agents themselves if they happen to be standing in the volume while it builds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverVolume|Behaviour")
	TArray<TObjectPtr<AActor>> IgnoredActors;

	//~ API ----------------------------------------------------------------------------------------------

	/** Throw this volume's points away and generate them again. Returns false when generation is off. */
	UFUNCTION(BlueprintCallable, Category = "CoverVolume")
	bool Rebuild();

	/** How many grid samples this volume will process, without processing any of them. */
	UFUNCTION(BlueprintPure, Category = "CoverVolume")
	int32 GetSampleCount() const;

	/** Half-extent of the sampled box in local space, scale applied. */
	FVector GetScaledExtent() const;

	/** Samples along local X and local Y. Z is unused: the grid is laid on the floor, not through the air. */
	FIntPoint GetGridDimensions() const;

	/** Local-space position of one grid sample, at the top of the box, ready for a downward trace. */
	FVector GetSampleStartLocal(int32 SampleIndex) const;
};
