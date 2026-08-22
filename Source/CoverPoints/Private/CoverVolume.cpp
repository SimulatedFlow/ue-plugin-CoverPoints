// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "CoverVolume.h"

#include "Components/BoxComponent.h"
#include "CoverPointsLog.h"
#include "CoverPointsSubsystem.h"
#include "Engine/World.h"

ACoverVolume::ACoverVolume()
{
	// Nothing here ticks. Generation is the subsystem's job and it runs on one budgeted tick for the whole
	// world - a hundred volumes must not cost a hundred ticks.
	PrimaryActorTick.bCanEverTick = false;

	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Bounds->SetBoxExtent(FVector(1000.0f, 1000.0f, 300.0f));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetGenerateOverlapEvents(false);

	// The box is a description of an area, not a thing in the world. If it affected navigation it would
	// carve a hole in the navmesh under itself and then reject every sample it laid down for failing to
	// project onto the navmesh it just removed.
	Bounds->SetCanEverAffectNavigation(false);
	Bounds->bDrawOnlyIfSelected = false;
	Bounds->ShapeColor = FColor(60, 180, 255);

	RootComponent = Bounds;

	bReplicates = false;
	SetCanBeDamaged(false);
}

void ACoverVolume::BeginPlay()
{
	Super::BeginPlay();

	if (UCoverPointsSubsystem* Subsystem = UCoverPointsSubsystem::Get(this))
	{
		Subsystem->RegisterVolume(this);

		if (bBuildOnBeginPlay && bVolumeEnabled)
		{
			Subsystem->RequestBuild(this);
		}
	}
}

void ACoverVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UCoverPointsSubsystem* Subsystem = UCoverPointsSubsystem::Get(this))
	{
		Subsystem->UnregisterVolume(this);
	}

	Super::EndPlay(EndPlayReason);
}

bool ACoverVolume::Rebuild()
{
	UCoverPointsSubsystem* Subsystem = UCoverPointsSubsystem::Get(this);
	if (!Subsystem)
	{
		UE_LOG(LogCoverPoints, Warning, TEXT("%s: Rebuild with no CoverPoints subsystem in this world."), *GetName());
		return false;
	}

	return Subsystem->RequestBuild(this);
}

FVector ACoverVolume::GetScaledExtent() const
{
	if (!Bounds)
	{
		return FVector::ZeroVector;
	}

	return Bounds->GetScaledBoxExtent();
}

FIntPoint ACoverVolume::GetGridDimensions() const
{
	const FVector Extent = GetScaledExtent();
	const float Spacing = FMath::Max(GridSpacing, 1.0f);

	// Inclusive on both edges: a 1000 cm half-extent at 200 cm spacing gives 11 samples across, not 10, so
	// the walls at the very edge of the volume are sampled rather than missed by half a cell.
	const int32 CountX = FMath::Max(1, FMath::FloorToInt((Extent.X * 2.0f) / Spacing) + 1);
	const int32 CountY = FMath::Max(1, FMath::FloorToInt((Extent.Y * 2.0f) / Spacing) + 1);

	return FIntPoint(CountX, CountY);
}

int32 ACoverVolume::GetSampleCount() const
{
	const FIntPoint Dims = GetGridDimensions();
	return Dims.X * Dims.Y;
}

FVector ACoverVolume::GetSampleStartLocal(int32 SampleIndex) const
{
	const FIntPoint Dims = GetGridDimensions();
	const FVector Extent = GetScaledExtent();
	const float Spacing = FMath::Max(GridSpacing, 1.0f);

	const int32 Count = Dims.X * Dims.Y;
	if (Count <= 0)
	{
		return FVector::ZeroVector;
	}

	const int32 Clamped = FMath::Clamp(SampleIndex, 0, Count - 1);
	const int32 IndexX = Clamped % Dims.X;
	const int32 IndexY = Clamped / Dims.X;

	// The grid is centred: whatever is left over after fitting whole cells across the box is split evenly
	// between the two edges, so a volume that is nudged 30 cm sideways does not resample the whole room in
	// a different place.
	const float SpanX = (Dims.X - 1) * Spacing;
	const float SpanY = (Dims.Y - 1) * Spacing;

	const float LocalX = -SpanX * 0.5f + IndexX * Spacing;
	const float LocalY = -SpanY * 0.5f + IndexY * Spacing;

	// Start at the ceiling of the box. The floor is found by tracing down from here, so a volume dropped
	// over a staircase samples each tread rather than one flat plane through all of them.
	return FVector(LocalX, LocalY, Extent.Z);
}
