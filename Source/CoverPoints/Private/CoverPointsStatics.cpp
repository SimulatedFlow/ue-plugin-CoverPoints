// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "CoverPointsStatics.h"

#include "CoverPointsSubsystem.h"
#include "CoverVolume.h"
#include "GameFramework/Actor.h"

namespace CoverPointsStaticsPrivate
{
	static UCoverPointsSubsystem* Resolve(const UObject* WorldContextObject)
	{
		return UCoverPointsSubsystem::Get(WorldContextObject);
	}
}

//~ Queries ------------------------------------------------------------------------------------------------

FCoverQueryResult UCoverPointsStatics::FindBestCover(
	const UObject* WorldContextObject,
	AActor* AgentActor,
	const FVector& ThreatLocation,
	float SearchRadius,
	bool bClaimCover,
	const FCoverQueryParams& Params)
{
	UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	if (!Subsystem || !AgentActor)
	{
		// A default result, which reads as "no cover found". A Blueprint written against this plugin has
		// to keep working in a map that has none of it, and it does that by taking the branch it already
		// wrote for the case where the room has no cover in it.
		return FCoverQueryResult();
	}

	return Subsystem->FindBestCover(
		AgentActor,
		AgentActor->GetActorLocation(),
		ThreatLocation,
		SearchRadius,
		Params,
		bClaimCover);
}

FCoverQueryResult UCoverPointsStatics::FindBestCoverAtLocation(
	const UObject* WorldContextObject,
	const FVector& AgentLocation,
	const FVector& ThreatLocation,
	float SearchRadius,
	const FCoverQueryParams& Params)
{
	UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	if (!Subsystem)
	{
		return FCoverQueryResult();
	}

	// No agent, so nothing to claim for. This overload is for asking questions about a level, not for
	// sending anybody anywhere.
	return Subsystem->FindBestCover(nullptr, AgentLocation, ThreatLocation, SearchRadius, Params, false);
}

void UCoverPointsStatics::FindCoverNear(
	const UObject* WorldContextObject,
	const FVector& Location,
	float Radius,
	int32 MaxResults,
	TArray<FCoverPoint>& OutPoints)
{
	OutPoints.Reset();
	if (const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject))
	{
		Subsystem->FindCoverNear(Location, Radius, MaxResults, OutPoints);
	}
}

//~ Claims -------------------------------------------------------------------------------------------------

bool UCoverPointsStatics::ClaimCover(
	const UObject* WorldContextObject,
	const FCoverPointHandle& Handle,
	AActor* AgentActor,
	float LifetimeSeconds)
{
	UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem && Subsystem->ClaimCover(Handle, AgentActor, LifetimeSeconds);
}

bool UCoverPointsStatics::ReleaseCover(const UObject* WorldContextObject, const FCoverPointHandle& Handle, AActor* AgentActor)
{
	UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem && Subsystem->ReleaseCover(Handle, AgentActor);
}

int32 UCoverPointsStatics::ReleaseCoverForActor(const UObject* WorldContextObject, AActor* AgentActor)
{
	UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem ? Subsystem->ReleaseCoverForActor(AgentActor) : 0;
}

int32 UCoverPointsStatics::ReleaseAllCover(const UObject* WorldContextObject)
{
	UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem ? Subsystem->ReleaseAllClaims() : 0;
}

bool UCoverPointsStatics::IsCoverValid(const UObject* WorldContextObject, const FCoverPointHandle& Handle)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem && Subsystem->IsCoverValid(Handle);
}

//~ Generation ---------------------------------------------------------------------------------------------

bool UCoverPointsStatics::RebuildVolume(const UObject* WorldContextObject, ACoverVolume* Volume)
{
	UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem && Subsystem->RequestBuild(Volume);
}

void UCoverPointsStatics::FinishBuildImmediately(const UObject* WorldContextObject)
{
	if (UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject))
	{
		Subsystem->FinishBuildImmediately();
	}
}

void UCoverPointsStatics::ClearAllCover(const UObject* WorldContextObject)
{
	if (UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject))
	{
		Subsystem->ClearAllPoints();
	}
}

bool UCoverPointsStatics::IsBuildingCover(const UObject* WorldContextObject)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem && Subsystem->IsBuilding();
}

float UCoverPointsStatics::GetCoverBuildProgress(const UObject* WorldContextObject)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	// No subsystem means nothing is building, and "finished" is the honest answer to that.
	return Subsystem ? Subsystem->GetBuildProgress() : 1.0f;
}

//~ Threat -------------------------------------------------------------------------------------------------

void UCoverPointsStatics::SetThreatLocation(const UObject* WorldContextObject, const FVector& Location)
{
	if (UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject))
	{
		Subsystem->SetThreatLocation(Location);
	}
}

void UCoverPointsStatics::SetThreatActor(const UObject* WorldContextObject, AActor* Actor)
{
	if (UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject))
	{
		Subsystem->SetThreatActor(Actor);
	}
}

void UCoverPointsStatics::ClearThreat(const UObject* WorldContextObject)
{
	if (UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject))
	{
		Subsystem->ClearThreat();
	}
}

FVector UCoverPointsStatics::GetThreatLocation(const UObject* WorldContextObject)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem ? Subsystem->GetThreatLocation() : FVector::ZeroVector;
}

//~ Inspection ---------------------------------------------------------------------------------------------

bool UCoverPointsStatics::GetCoverPoint(const UObject* WorldContextObject, const FCoverPointHandle& Handle, FCoverPoint& OutPoint)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem && Subsystem->GetPoint(Handle, OutPoint);
}

AActor* UCoverPointsStatics::GetCoverOwner(const UObject* WorldContextObject, const FCoverPointHandle& Handle)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem ? Subsystem->GetCoverOwner(Handle) : nullptr;
}

FCoverPointHandle UCoverPointsStatics::GetClaimedCoverForActor(const UObject* WorldContextObject, AActor* AgentActor)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem ? Subsystem->GetClaimedCoverForActor(AgentActor) : FCoverPointHandle();
}

int32 UCoverPointsStatics::GetCoverPointCount(const UObject* WorldContextObject)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem ? Subsystem->GetPointCount() : 0;
}

FCoverPointsStats UCoverPointsStatics::GetCoverStats(const UObject* WorldContextObject)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem ? Subsystem->GetStats() : FCoverPointsStats();
}

void UCoverPointsStatics::GetAllCoverVolumes(const UObject* WorldContextObject, TArray<ACoverVolume*>& OutVolumes)
{
	OutVolumes.Reset();
	if (const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject))
	{
		Subsystem->GetAllVolumes(OutVolumes);
	}
}

//~ Scoring helpers ----------------------------------------------------------------------------------------

float UCoverPointsStatics::GetCoverShielding(const FCoverPoint& Point, const FVector& ThreatLocation)
{
	return UCoverPointsSubsystem::GetShielding(Point, ThreatLocation);
}

ECoverPeekSide UCoverPointsStatics::GetPeekSideTowards(const FCoverPoint& Point, const FVector& ThreatLocation)
{
	return UCoverPointsSubsystem::GetPeekSideTowards(Point, ThreatLocation);
}

FRotator UCoverPointsStatics::GetCoverFacingRotation(const FCoverPoint& Point, const FVector& ThreatLocation)
{
	// Out into the open, not into the wall. Prefer the threat when there is one, because an agent in cover
	// looks at what it is hiding from; fall back to the cover normal, which points the same way when the
	// threat is directly behind the wall and is at least never into the geometry.
	FVector Facing = (ThreatLocation - Point.Location).GetSafeNormal2D();
	if (Facing.IsNearlyZero())
	{
		Facing = Point.CoverNormal;
	}

	return Facing.Rotation();
}

FString UCoverPointsStatics::PeekSideToString(ECoverPeekSide Side)
{
	switch (Side)
	{
	case ECoverPeekSide::Left:  return TEXT("Left");
	case ECoverPeekSide::Right: return TEXT("Right");
	case ECoverPeekSide::Over:  return TEXT("Over");
	default:                    return TEXT("None");
	}
}

//~ Debug --------------------------------------------------------------------------------------------------

void UCoverPointsStatics::SetShowCoverPoints(const UObject* WorldContextObject, bool bShow)
{
	if (UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject))
	{
		Subsystem->SetShowPoints(bShow);
	}
}

bool UCoverPointsStatics::IsShowingCoverPoints(const UObject* WorldContextObject)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem && Subsystem->IsShowingPoints();
}

void UCoverPointsStatics::SetShowPeekSides(const UObject* WorldContextObject, bool bShow)
{
	if (UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject))
	{
		Subsystem->SetShowPeekSides(bShow);
	}
}

bool UCoverPointsStatics::IsShowingPeekSides(const UObject* WorldContextObject)
{
	const UCoverPointsSubsystem* Subsystem = CoverPointsStaticsPrivate::Resolve(WorldContextObject);
	return Subsystem && Subsystem->IsShowingPeekSides();
}
