// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "CoverPointsSubsystem.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "CoverPointsLog.h"
#include "CoverPointsSettings.h"
#include "CoverVolume.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GlobalRenderResources.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/StringBuilder.h"
#include "NavigationSystem.h"
#include "SceneTypes.h"

namespace CoverPointsPrivate
{
	/** Lines the counters box draws. Fixed - there is no per-something list on it. */
	static constexpr int32 StatsLines = 13;

	static constexpr float LineHeight = 15.0f;
	static constexpr float BoxPadding = 8.0f;

	static const FLinearColor PanelBackground(0.0f, 0.0f, 0.0f, 0.62f);
	static const FLinearColor HeadingColor(0.42f, 0.78f, 1.0f, 1.0f);
	static const FLinearColor BodyColor(0.9f, 0.9f, 0.9f, 1.0f);
	static const FLinearColor GoodColor(0.55f, 0.95f, 0.55f, 1.0f);
	static const FLinearColor WarnColor(0.98f, 0.78f, 0.35f, 1.0f);
	static const FLinearColor BadColor(0.98f, 0.42f, 0.38f, 1.0f);
	static const FLinearColor DimColor(0.62f, 0.62f, 0.62f, 1.0f);

	/** Most cells one query may walk before it gives up on the hash and scans the array instead. */
	static constexpr int32 MaxQueryCells = 4096;

	/** Most points the debug drawing will put on screen in one frame. */
	static constexpr int32 MaxDebugPoints = 6000;

	/** The eight directions a grid sample looks in, filled once on first use. */
	static const TArray<FVector>& SweepDirections()
	{
		static TArray<FVector> Directions = []()
		{
			TArray<FVector> Result;
			Result.Reserve(8);
			for (int32 Index = 0; Index < 8; ++Index)
			{
				const float Radians = FMath::DegreesToRadians(Index * 45.0f);
				Result.Add(FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.0f));
			}
			return Result;
		}();
		return Directions;
	}

	static void DrawFilledRect(UCanvas* Canvas, const FVector2D& Position, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Position, GWhiteTexture, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	static UCoverPointsSubsystem* GetSubsystem(UWorld* World)
	{
		return World ? World->GetSubsystem<UCoverPointsSubsystem>() : nullptr;
	}

	/** A twenty-character progress bar, so the box shows movement and not just a number. */
	static void AppendBar(TStringBuilder<192>& Builder, float Alpha)
	{
		constexpr int32 Segments = 20;
		const int32 Filled = FMath::Clamp(FMath::RoundToInt(Alpha * Segments), 0, Segments);
		Builder.AppendChar(TEXT('['));
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			Builder.AppendChar(Index < Filled ? TEXT('=') : TEXT('.'));
		}
		Builder.AppendChar(TEXT(']'));
	}

	/** Agent-relative right, for an agent standing at a point and facing its cover. */
	static FVector RightOfCover(const FVector& CoverNormal)
	{
		const FVector Forward = -CoverNormal;
		return FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	}
}

//~ Lifecycle ----------------------------------------------------------------------------------------------

void UCoverPointsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UCoverPointsSettings& Settings = UCoverPointsSettings::Get();

	CellSize = FMath::Max(Settings.QueryCellSize, 1.0f);
	bShowPoints = Settings.bShowPointsByDefault;
	bShowPeekSides = Settings.bShowPeekSidesByDefault;
	bAutoDrawStatsOnAnyHUD = Settings.bAutoDrawStatsOnAnyHUD;

	RebindHudDelegate();
}

void UCoverPointsSubsystem::Deinitialize()
{
	if (HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}

	Points.Reset();
	SpatialHash.Reset();
	OwnerToPoint.Reset();
	Volumes.Reset();
	Build = FBuildJob();

	Super::Deinitialize();
}

bool UCoverPointsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE. An editor viewport is somebody's workspace, and quietly filling it with generated
	// cover while they are still moving the walls around helps nobody.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UCoverPointsSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCoverPointsSubsystem, STATGROUP_Tickables);
}

UCoverPointsSubsystem* UCoverPointsSubsystem::Get(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UCoverPointsSubsystem>() : nullptr;
}

//~ Tick ---------------------------------------------------------------------------------------------------

void UCoverPointsSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const UCoverPointsSettings& Settings = UCoverPointsSettings::Get();

	// A followed threat is resolved here rather than at query time so that everything that reads the
	// current threat this frame - the colours, the audit, the counters box - reads the same position.
	if (AActor* Threat = ThreatActor.Get())
	{
		ThreatLocation = Threat->GetActorLocation();
		bHasThreat = true;
	}

	TickBuild();
	TickVerification();

	ClaimSweepAccumulator += DeltaTime;
	if (ClaimSweepAccumulator >= Settings.ClaimSweepInterval)
	{
		ClaimSweepAccumulator = 0.0f;
		SweepExpiredClaims();
	}

	// Queries per second and microseconds per query over a one-second window. A per-frame average would
	// swing wildly on a frame that happened to hold one query; a lifetime average would never show a
	// regression that started ten seconds ago.
	QueryWindowAccumulator += DeltaTime;
	if (QueryWindowAccumulator >= 1.0f)
	{
		Stats.QueriesPerSecond = QueriesInWindow / QueryWindowAccumulator;
		Stats.MicrosecondsPerQuery = QueriesInWindow > 0
			? static_cast<float>(QueryMicrosecondsInWindow / QueriesInWindow)
			: 0.0f;

		QueryWindowAccumulator = 0.0f;
		QueriesInWindow = 0;
		QueryMicrosecondsInWindow = 0.0;
	}

	Stats.bHasThreat = bHasThreat;
	Stats.ThreatLocation = ThreatLocation;
	Stats.RegisteredVolumes = Volumes.Num();
	Stats.GridCells = SpatialHash.Num();
	Stats.ClaimedPoints = OwnerToPoint.Num();

	if (bShowPoints)
	{
		DrawDebugPoints();
	}
}

//~ Volumes ------------------------------------------------------------------------------------------------

void UCoverPointsSubsystem::RegisterVolume(ACoverVolume* Volume)
{
	if (!Volume)
	{
		return;
	}

	Volumes.RemoveAll([](const TWeakObjectPtr<ACoverVolume>& Entry) { return !Entry.IsValid(); });
	Volumes.AddUnique(Volume);
	Stats.RegisteredVolumes = Volumes.Num();
}

void UCoverPointsSubsystem::UnregisterVolume(ACoverVolume* Volume)
{
	if (!Volume)
	{
		return;
	}

	Volumes.Remove(Volume);
	Stats.RegisteredVolumes = Volumes.Num();

	// The volume is going away, so its points have to go with it - a point in a streamed-out region is a
	// point queries would keep offering to agents who then walk to a place that is no longer loaded.
	const int32 Removed = Points.RemoveAll([Volume](const FCoverPoint& Point)
	{
		return Point.SourceVolume.Get() == Volume;
	});

	if (Removed > 0)
	{
		++BuildId;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			Points[Index].Handle = FCoverPointHandle(Index, BuildId);
		}

		OwnerToPoint.Reset();
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			if (Points[Index].bClaimed && Points[Index].ClaimedBy.IsValid())
			{
				OwnerToPoint.Add(Points[Index].ClaimedBy, Index);
			}
			else
			{
				ClearClaimAt(Index);
			}
		}

		RebuildSpatialHash();
	}
}

void UCoverPointsSubsystem::GetAllVolumes(TArray<ACoverVolume*>& OutVolumes) const
{
	OutVolumes.Reset();
	OutVolumes.Reserve(Volumes.Num());
	for (const TWeakObjectPtr<ACoverVolume>& Entry : Volumes)
	{
		if (ACoverVolume* Volume = Entry.Get())
		{
			OutVolumes.Add(Volume);
		}
	}
}

//~ Generation ---------------------------------------------------------------------------------------------

bool UCoverPointsSubsystem::RequestBuild(ACoverVolume* Volume)
{
	const UCoverPointsSettings& Settings = UCoverPointsSettings::Get();
	if (!Settings.bEnabled)
	{
		UE_LOG(LogCoverPoints, Warning, TEXT("RequestBuild ignored: CoverPoints is disabled in project settings."));
		return false;
	}

	// A build already in flight is dropped rather than queued behind. The newer request is by definition
	// the more recent description of the level; finishing the older one first would publish points that
	// the caller has already told us are wrong.
	Build = FBuildJob();

	if (Volume)
	{
		if (!Volume->bVolumeEnabled)
		{
			UE_LOG(LogCoverPoints, Verbose, TEXT("RequestBuild: %s is disabled, nothing to do."), *Volume->GetName());
			return true;
		}
		Build.Volumes.Add(Volume);
	}
	else
	{
		for (const TWeakObjectPtr<ACoverVolume>& Entry : Volumes)
		{
			ACoverVolume* Candidate = Entry.Get();
			if (Candidate && Candidate->bVolumeEnabled)
			{
				Build.Volumes.Add(Candidate);
			}
		}
	}

	Build.SamplesPerVolume.Reserve(Build.Volumes.Num());
	for (const TWeakObjectPtr<ACoverVolume>& Entry : Build.Volumes)
	{
		const ACoverVolume* Candidate = Entry.Get();
		const int32 Samples = Candidate ? Candidate->GetSampleCount() : 0;
		Build.SamplesPerVolume.Add(Samples);
		Build.SamplesTotal += Samples;
	}

	Build.StartTimeSeconds = FPlatformTime::Seconds();
	Build.bActive = Build.SamplesTotal > 0;

	Stats.bBuilding = Build.bActive;
	Stats.SamplesTotal = Build.SamplesTotal;
	Stats.SamplesProcessed = 0;
	Stats.BuildProgress = Build.bActive ? 0.0f : 1.0f;
	Stats.BuildMillisecondsPeak = 0.0f;

	if (!Build.bActive)
	{
		// Nothing to sample. Still a legitimate build: it publishes an empty set for the volumes asked
		// about, which is what a caller who just shrank a volume to nothing expects to see.
		FinalizeBuild();
	}

	return true;
}

void UCoverPointsSubsystem::FinishBuildImmediately()
{
	if (!Build.bActive)
	{
		return;
	}

	while (Build.bActive)
	{
		const int32 VolumeIndex = Build.VolumeIndex;
		if (!Build.Volumes.IsValidIndex(VolumeIndex))
		{
			FinalizeBuild();
			break;
		}

		if (const ACoverVolume* Volume = Build.Volumes[VolumeIndex].Get())
		{
			ProcessSample(*Volume, Build.SampleIndex, Build.Points);
		}

		++Build.SampleIndex;
		++Build.SamplesProcessed;

		if (Build.SampleIndex >= Build.SamplesPerVolume[VolumeIndex])
		{
			Build.SampleIndex = 0;
			++Build.VolumeIndex;
			if (Build.VolumeIndex >= Build.Volumes.Num())
			{
				FinalizeBuild();
			}
		}
	}
}

void UCoverPointsSubsystem::TickBuild()
{
	if (!Build.bActive)
	{
		Stats.BuildMillisecondsThisFrame = 0.0f;
		return;
	}

	const UCoverPointsSettings& Settings = UCoverPointsSettings::Get();

	const double SliceStart = FPlatformTime::Seconds();
	const double Deadline = SliceStart + (Settings.MaxBuildMillisecondsPerFrame / 1000.0);
	const int32 SampleCeiling = FMath::Max(Settings.MaxSamplesPerFrame, 1);

	int32 SamplesThisFrame = 0;

	while (Build.bActive && SamplesThisFrame < SampleCeiling)
	{
		const int32 VolumeIndex = Build.VolumeIndex;
		if (!Build.Volumes.IsValidIndex(VolumeIndex))
		{
			FinalizeBuild();
			break;
		}

		// A volume that was destroyed mid-build is skipped whole rather than aborting the run. Its samples
		// still count towards progress, so the percentage cannot stall at 63 for the rest of the level.
		if (const ACoverVolume* Volume = Build.Volumes[VolumeIndex].Get())
		{
			ProcessSample(*Volume, Build.SampleIndex, Build.Points);
		}

		++Build.SampleIndex;
		++Build.SamplesProcessed;
		++SamplesThisFrame;

		if (Build.SampleIndex >= Build.SamplesPerVolume[VolumeIndex])
		{
			Build.SampleIndex = 0;
			++Build.VolumeIndex;
			if (Build.VolumeIndex >= Build.Volumes.Num())
			{
				FinalizeBuild();
				break;
			}
		}

		// Checked every 16 samples rather than every one: FPlatformTime::Seconds is not free, and a check
		// that costs a measurable slice of the budget it is guarding is a check that has become the cost.
		if ((SamplesThisFrame & 15) == 0 && FPlatformTime::Seconds() >= Deadline)
		{
			break;
		}
	}

	const float SliceMilliseconds = static_cast<float>((FPlatformTime::Seconds() - SliceStart) * 1000.0);
	Stats.BuildMillisecondsThisFrame = SliceMilliseconds;
	Build.PeakSliceMilliseconds = FMath::Max(Build.PeakSliceMilliseconds, SliceMilliseconds);
	Stats.BuildMillisecondsPeak = Build.PeakSliceMilliseconds;

	Stats.SamplesProcessed = Build.SamplesProcessed;
	Stats.SamplesTotal = Build.SamplesTotal;
	Stats.bBuilding = Build.bActive;
	Stats.BuildProgress = Build.SamplesTotal > 0
		? FMath::Clamp(static_cast<float>(Build.SamplesProcessed) / static_cast<float>(Build.SamplesTotal), 0.0f, 1.0f)
		: 1.0f;
}

void UCoverPointsSubsystem::ProcessSample(const ACoverVolume& Volume, int32 SampleIndex, TArray<FCoverPoint>& OutPoints) const
{
	UWorld* World = GetWorld();
	if (!World || !Volume.Bounds)
	{
		return;
	}

	const UCoverPointsSettings& Settings = UCoverPointsSettings::Get();
	const ECollisionChannel Channel = Settings.GenerationChannel;

	FCollisionQueryParams QueryParams(FName(TEXT("CoverPointsBuild")), Settings.bTraceComplex);
	QueryParams.AddIgnoredActor(&Volume);
	for (const TObjectPtr<AActor>& Ignored : Volume.IgnoredActors)
	{
		if (Ignored)
		{
			QueryParams.AddIgnoredActor(Ignored);
		}
	}

	const FTransform& VolumeTransform = Volume.Bounds->GetComponentTransform();
	const FVector Extent = Volume.GetScaledExtent();

	//~ 1. Find the floor. -----------------------------------------------------------------------------
	//
	// Traced from the ceiling of the volume straight down its own local Z, so a rotated volume laid along
	// a ramp samples the ramp and a volume dropped over a staircase samples each tread rather than one
	// flat plane cutting through all of them.
	const FVector LocalStart = Volume.GetSampleStartLocal(SampleIndex);
	const FVector WorldStart = VolumeTransform.TransformPosition(LocalStart);
	const FVector DownAxis = -VolumeTransform.GetUnitAxis(EAxis::Z);
	const FVector WorldEnd = WorldStart + DownAxis * (Extent.Z * 2.0f);

	FHitResult FloorHit;
	if (!World->LineTraceSingleByChannel(FloorHit, WorldStart, WorldEnd, Channel, QueryParams))
	{
		return;
	}

	if (FloorHit.ImpactNormal.Z < Settings.MinFloorNormalZ)
	{
		return;
	}

	FVector Floor = FloorHit.ImpactPoint;

	//~ 2. Make sure an agent could stand there. --------------------------------------------------------
	if (Settings.bProjectToNavMesh)
	{
		if (const UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(World))
		{
			FNavLocation Projected;
			const FVector ProjectionExtent(Settings.NavProjectionExtent);
			if (!NavSystem->ProjectPointToNavigation(Floor, Projected, ProjectionExtent))
			{
				return;
			}
			Floor = Projected.Location;
		}
		// No navigation system in this world: fall through on the floor trace. A test map without a
		// navmesh should still generate cover rather than silently generate nothing.
	}

	const FVector LowOrigin = Floor + FVector::UpVector * Volume.CrouchHeight;
	const FVector HighOrigin = Floor + FVector::UpVector * Volume.StandHeight;
	const FCollisionShape AgentSphere = FCollisionShape::MakeSphere(FMath::Max(Volume.AgentRadius, 1.0f));

	//~ 3. Look around for something to hide behind. ----------------------------------------------------
	struct FCandidate
	{
		FVector Normal = FVector::ForwardVector;
		FVector Direction = FVector::ForwardVector;
		float Distance = 0.0f;
	};

	TArray<FCandidate, TInlineAllocator<8>> Candidates;

	for (const FVector& Direction : CoverPointsPrivate::SweepDirections())
	{
		FHitResult Hit;
		const FVector SweepEnd = LowOrigin + Direction * Volume.CoverDistance;
		if (!World->SweepSingleByChannel(Hit, LowOrigin, SweepEnd, FQuat::Identity, Channel, AgentSphere, QueryParams))
		{
			continue;
		}

		// The sample is buried inside geometry. There is no cover here because there is no standing here,
		// and every other sweep from this origin would be nonsense too.
		if (Hit.bStartPenetrating)
		{
			return;
		}

		// Cover is a horizontal idea: the normal that matters is the one an agent would put its back to.
		// A hit whose normal is essentially vertical is a floor or a ceiling caught by a sweep that grazed
		// it, and it shields nothing.
		const FVector FlatNormal = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.0f).GetSafeNormal();
		if (FlatNormal.IsNearlyZero())
		{
			continue;
		}

		// One flat wall is hit by three of the eight sweeps. A corner is genuinely two pieces of cover
		// facing two ways. The dot product between normals is what tells those two cases apart.
		bool bDuplicate = false;
		for (const FCandidate& Existing : Candidates)
		{
			if (FVector::DotProduct(Existing.Normal, FlatNormal) > Settings.DistinctNormalThreshold)
			{
				bDuplicate = true;
				break;
			}
		}

		if (bDuplicate)
		{
			continue;
		}

		FCandidate Candidate;
		Candidate.Normal = FlatNormal;
		Candidate.Direction = Direction;
		Candidate.Distance = Hit.Distance;
		Candidates.Add(Candidate);

		if (Candidates.Num() >= Settings.MaxPointsPerSample)
		{
			break;
		}
	}

	if (Candidates.Num() == 0)
	{
		return;
	}

	//~ 4. Turn each candidate into a point. ------------------------------------------------------------
	for (const FCandidate& Candidate : Candidates)
	{
		if (Settings.MinPointSeparation > 0.0f)
		{
			const float SeparationSq = FMath::Square(Settings.MinPointSeparation);
			bool bTooClose = false;
			for (const FCoverPoint& Existing : OutPoints)
			{
				if (FVector::DistSquared(Existing.Location, Floor) < SeparationSq
					&& FVector::DotProduct(Existing.CoverNormal, Candidate.Normal) > Settings.DistinctNormalThreshold)
				{
					bTooClose = true;
					break;
				}
			}

			if (bTooClose)
			{
				continue;
			}
		}

		FCoverPoint Point;
		Point.Location = Floor;
		Point.CoverNormal = Candidate.Normal;
		Point.WallDistance = Candidate.Distance;
		Point.CoverDistanceRef = Volume.CoverDistance;
		// const_cast because generation reads the volume and only records which one it was. The weak
		// pointer is a label on the point, not a handle anything is going to write through.
		Point.SourceVolume = const_cast<ACoverVolume*>(&Volume);

		// High or low, measured. The same sweep at standing height: still blocked means the geometry is
		// taller than the agent, and the agent is hidden without crouching.
		FHitResult HighHit;
		const FVector HighEnd = HighOrigin + Candidate.Direction * Volume.CoverDistance;
		const bool bHighBlocked = World->SweepSingleByChannel(
			HighHit, HighOrigin, HighEnd, FQuat::Identity, Channel, AgentSphere, QueryParams);
		Point.Height = bHighBlocked ? ECoverHeight::High : ECoverHeight::Low;

		// Which ways out of it there are. Two sweeps per side: one to check the agent can get to the
		// leaning position at all, one to check that from there it can see past the cover. Checking only
		// the second would report a peek through a wall the agent cannot lean into.
		const FVector Forward = -Candidate.Normal;
		const FVector RightDir = CoverPointsPrivate::RightOfCover(Candidate.Normal);

		if (!RightDir.IsNearlyZero())
		{
			auto TestLateralPeek = [&](float Sign) -> bool
			{
				const FVector LeanOrigin = LowOrigin + RightDir * (Sign * Settings.PeekLateralOffset);

				FHitResult LeanHit;
				if (World->SweepSingleByChannel(
					LeanHit, LowOrigin, LeanOrigin, FQuat::Identity, Channel, AgentSphere, QueryParams))
				{
					return false;
				}

				FHitResult LookHit;
				const FVector LookEnd = LeanOrigin + Forward * Settings.PeekProbeDistance;
				return !World->SweepSingleByChannel(
					LookHit, LeanOrigin, LookEnd, FQuat::Identity, Channel, AgentSphere, QueryParams);
			};

			Point.bCanPeekRight = TestLateralPeek(1.0f);
			Point.bCanPeekLeft = TestLateralPeek(-1.0f);
		}

		// Over the top, from standing height. High cover fails this by construction - the sweep that made
		// it High is the same line - so no special case is needed and none is written.
		{
			FHitResult OverHit;
			const FVector OverEnd = HighOrigin + Forward * Settings.PeekProbeDistance;
			Point.bCanPeekOver = !World->SweepSingleByChannel(
				OverHit, HighOrigin, OverEnd, FQuat::Identity, Channel, AgentSphere, QueryParams);
		}

		OutPoints.Add(Point);
	}
}

void UCoverPointsSubsystem::FinalizeBuild()
{
	// Points from volumes this build did not touch survive it. Rebuilding the volume around a door that
	// just blew open must not throw away the cover in the rest of the level.
	TSet<const AActor*> RebuiltVolumes;
	RebuiltVolumes.Reserve(Build.Volumes.Num());
	for (const TWeakObjectPtr<ACoverVolume>& Entry : Build.Volumes)
	{
		if (const ACoverVolume* Volume = Entry.Get())
		{
			RebuiltVolumes.Add(Volume);
		}
	}

	TArray<FCoverPoint> Merged;
	Merged.Reserve(Points.Num() + Build.Points.Num());

	for (const FCoverPoint& Existing : Points)
	{
		const AActor* Source = Existing.SourceVolume.Get();
		if (Source && !RebuiltVolumes.Contains(Source))
		{
			Merged.Add(Existing);
		}
	}

	Merged.Append(MoveTemp(Build.Points));

	Points = MoveTemp(Merged);

	// Every handle cut before this moment now addresses a different point, so the generation counter goes
	// up and those handles start failing IsCoverValid instead of quietly meaning something else.
	++BuildId;

	OwnerToPoint.Reset();
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		FCoverPoint& Point = Points[Index];
		Point.Handle = FCoverPointHandle(Index, BuildId);

		// Claims on surviving points are kept. An agent standing behind a wall in the untouched half of
		// the level does not lose its cover because a different room was regenerated.
		if (Point.bClaimed && Point.ClaimedBy.IsValid())
		{
			OwnerToPoint.Add(Point.ClaimedBy, Index);
		}
		else
		{
			Point.bClaimed = false;
			Point.ClaimedBy.Reset();
			Point.ClaimExpiryTime = 0.0;
		}
	}

	RebuildSpatialHash();

	int32 HighCount = 0;
	int32 PeekCount = 0;
	for (const FCoverPoint& Point : Points)
	{
		if (Point.Height == ECoverHeight::High)
		{
			++HighCount;
		}
		if (Point.bCanPeekLeft || Point.bCanPeekRight || Point.bCanPeekOver)
		{
			++PeekCount;
		}
	}

	Stats.TotalPoints = Points.Num();
	Stats.HighPoints = HighCount;
	Stats.LowPoints = Points.Num() - HighCount;
	Stats.PeekablePoints = PeekCount;
	Stats.ClaimedPoints = OwnerToPoint.Num();
	Stats.GridCells = SpatialHash.Num();
	Stats.BuildProgress = 1.0f;
	Stats.bBuilding = false;
	Stats.LastBuildTotalMilliseconds = static_cast<float>((FPlatformTime::Seconds() - Build.StartTimeSeconds) * 1000.0);

	const int32 Total = Points.Num();
	VerificationCursor = 0;
	Build = FBuildJob();

	UE_LOG(LogCoverPoints, Log, TEXT("Build finished: %d points (%d high, %d low) in %.1f ms wall clock."),
		Total, HighCount, Total - HighCount, Stats.LastBuildTotalMilliseconds);

	OnBuildCompleted.Broadcast(Total);
}

void UCoverPointsSubsystem::ClearAllPoints()
{
	Build = FBuildJob();
	Points.Reset();
	SpatialHash.Reset();
	OwnerToPoint.Reset();
	VerificationCursor = 0;
	++BuildId;

	Stats.TotalPoints = 0;
	Stats.HighPoints = 0;
	Stats.LowPoints = 0;
	Stats.PeekablePoints = 0;
	Stats.ClaimedPoints = 0;
	Stats.GridCells = 0;
	Stats.SamplesProcessed = 0;
	Stats.SamplesTotal = 0;
	Stats.bBuilding = false;
	Stats.BuildProgress = 1.0f;
	Stats.BuildMillisecondsThisFrame = 0.0f;
	Stats.BuildMillisecondsPeak = 0.0f;
}

bool UCoverPointsSubsystem::IsBuilding() const
{
	return Build.bActive;
}

float UCoverPointsSubsystem::GetBuildProgress() const
{
	return Stats.BuildProgress;
}

//~ Spatial index ------------------------------------------------------------------------------------------

FIntVector UCoverPointsSubsystem::CellOf(const FVector& Location) const
{
	const float Size = FMath::Max(CellSize, 1.0f);
	return FIntVector(
		FMath::FloorToInt(Location.X / Size),
		FMath::FloorToInt(Location.Y / Size),
		FMath::FloorToInt(Location.Z / Size));
}

void UCoverPointsSubsystem::RebuildSpatialHash()
{
	CellSize = FMath::Max(UCoverPointsSettings::Get().QueryCellSize, 1.0f);

	SpatialHash.Reset();
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		SpatialHash.FindOrAdd(CellOf(Points[Index].Location)).Add(Index);
	}

	Stats.GridCells = SpatialHash.Num();
}

//~ Points -------------------------------------------------------------------------------------------------

int32 UCoverPointsSubsystem::GetPointCount() const
{
	return Points.Num();
}

bool UCoverPointsSubsystem::GetPoint(const FCoverPointHandle& Handle, FCoverPoint& OutPoint) const
{
	if (Handle.BuildId != BuildId || !Points.IsValidIndex(Handle.Index))
	{
		return false;
	}

	OutPoint = Points[Handle.Index];
	return true;
}

bool UCoverPointsSubsystem::IsCoverValid(const FCoverPointHandle& Handle) const
{
	return Handle.BuildId == BuildId
		&& Points.IsValidIndex(Handle.Index)
		&& !Points[Handle.Index].bInvalidated;
}

void UCoverPointsSubsystem::FindCoverNear(const FVector& Location, float Radius, int32 MaxResults, TArray<FCoverPoint>& OutPoints) const
{
	OutPoints.Reset();

	if (Points.Num() == 0 || Radius <= 0.0f)
	{
		return;
	}

	const float RadiusSquared = Radius * Radius;

	TArray<TPair<float, int32>> Found;

	const FIntVector MinCell = CellOf(Location - FVector(Radius));
	const FIntVector MaxCell = CellOf(Location + FVector(Radius));

	for (int32 CellZ = MinCell.Z; CellZ <= MaxCell.Z; ++CellZ)
	{
		for (int32 CellY = MinCell.Y; CellY <= MaxCell.Y; ++CellY)
		{
			for (int32 CellX = MinCell.X; CellX <= MaxCell.X; ++CellX)
			{
				const TArray<int32>* Cell = SpatialHash.Find(FIntVector(CellX, CellY, CellZ));
				if (!Cell)
				{
					continue;
				}

				for (int32 PointIndex : *Cell)
				{
					const FCoverPoint& Point = Points[PointIndex];
					if (Point.bInvalidated)
					{
						continue;
					}

					const float DistanceSquared = FVector::DistSquared(Point.Location, Location);
					if (DistanceSquared <= RadiusSquared)
					{
						Found.Emplace(DistanceSquared, PointIndex);
					}
				}
			}
		}
	}

	Found.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });

	const int32 Limit = MaxResults > 0 ? FMath::Min(MaxResults, Found.Num()) : Found.Num();
	OutPoints.Reserve(Limit);
	for (int32 Index = 0; Index < Limit; ++Index)
	{
		OutPoints.Add(Points[Found[Index].Value]);
	}
}

//~ Scoring ------------------------------------------------------------------------------------------------

float UCoverPointsSubsystem::GetShielding(const FCoverPoint& Point, const FVector& InThreatLocation)
{
	const FVector ToThreat = (InThreatLocation - Point.Location).GetSafeNormal();
	if (ToThreat.IsNearlyZero())
	{
		// Standing on top of the threat. Not shielded from it by anything, and saying so is more useful
		// than dividing by zero and calling the result perfect cover.
		return -1.0f;
	}

	// CoverNormal points out of the wall towards the point, so a threat on the far side of that wall lies
	// in the opposite direction. One dot product, and that is the entire definition of cover here.
	return -FVector::DotProduct(Point.CoverNormal, ToThreat);
}

ECoverPeekSide UCoverPointsSubsystem::GetPeekSideTowards(const FCoverPoint& Point, const FVector& InThreatLocation)
{
	const FVector ToThreat = (InThreatLocation - Point.Location).GetSafeNormal();
	if (ToThreat.IsNearlyZero())
	{
		return ECoverPeekSide::None;
	}

	const FVector RightDir = CoverPointsPrivate::RightOfCover(Point.CoverNormal);
	const float Lateral = FVector::DotProduct(ToThreat, RightDir);

	// A threat that is nearly straight ahead does not favour either edge, so the top is tried first - it
	// is the shot that needs no sidestep. The dead band keeps the choice from flickering left/right as a
	// moving threat crosses the centre line.
	constexpr float LateralDeadBand = 0.15f;

	if (Lateral > LateralDeadBand && Point.bCanPeekRight)
	{
		return ECoverPeekSide::Right;
	}
	if (Lateral < -LateralDeadBand && Point.bCanPeekLeft)
	{
		return ECoverPeekSide::Left;
	}
	if (Point.bCanPeekOver)
	{
		return ECoverPeekSide::Over;
	}
	if (Lateral > 0.0f && Point.bCanPeekRight)
	{
		return ECoverPeekSide::Right;
	}
	if (Lateral <= 0.0f && Point.bCanPeekLeft)
	{
		return ECoverPeekSide::Left;
	}

	return ECoverPeekSide::None;
}

FCoverQueryResult UCoverPointsSubsystem::FindBestCover(
	AActor* AgentActor,
	const FVector& AgentLocation,
	const FVector& InThreatLocation,
	float SearchRadius,
	const FCoverQueryParams& Params,
	bool bClaimAgent)
{
	const uint64 StartCycles = FPlatformTime::Cycles64();

	FCoverQueryResult Result;

	const UCoverPointsSettings& Settings = UCoverPointsSettings::Get();
	const int32 CandidateCeiling = FMath::Max(Settings.MaxCandidatesPerQuery, 1);

	// Weights are relative, so the sum is divided out. That makes a score a number between 0 and 1 which
	// means the same thing in two different levels, instead of a number whose scale is an accident of how
	// the weights happened to be tuned.
	const float TotalWeight = FMath::Max(
		Params.ShieldingWeight + Params.AgentDistanceWeight + Params.ThreatDistanceWeight
		+ Params.HighCoverWeight + Params.PeekWeight + Params.WallProximityWeight,
		KINDA_SMALL_NUMBER);

	const float RadiusSquared = FMath::Square(FMath::Max(SearchRadius, 0.0f));
	const float SafeRadius = FMath::Max(SearchRadius, 1.0f);
	const float PreferredThreatDistance = FMath::Max(Params.PreferredThreatDistance, 1.0f);

	int32 BestIndex = INDEX_NONE;
	float BestScore = -FLT_MAX;
	float BestShielding = 0.0f;
	float BestDistance = 0.0f;
	ECoverPeekSide BestPeek = ECoverPeekSide::None;
	int32 Examined = 0;

	auto Consider = [&](int32 PointIndex)
	{
		const FCoverPoint& Point = Points[PointIndex];

		if (Point.bInvalidated)
		{
			return;
		}

		// A point this agent already holds is fair game - re-querying must be able to return the cover the
		// agent is already standing in, or an agent that asks twice would walk away from its own wall.
		if (!Params.bAllowClaimed && Point.bClaimed && Point.ClaimedBy.Get() != AgentActor)
		{
			return;
		}

		const float DistanceSquared = FVector::DistSquared(Point.Location, AgentLocation);
		if (DistanceSquared > RadiusSquared)
		{
			return;
		}

		if (Params.bRequireHighCover && Point.Height != ECoverHeight::High)
		{
			return;
		}

		++Examined;

		const float Shielding = GetShielding(Point, InThreatLocation);
		if (Shielding < Params.MinShielding)
		{
			return;
		}

		const ECoverPeekSide PeekSide = GetPeekSideTowards(Point, InThreatLocation);
		if (Params.bRequirePeek && PeekSide == ECoverPeekSide::None)
		{
			return;
		}

		const float AgentDistance = FMath::Sqrt(DistanceSquared);
		const float ThreatDistance = FVector::Dist(Point.Location, InThreatLocation);

		const float ShieldTerm = (Shielding + 1.0f) * 0.5f;
		const float AgentTerm = 1.0f - FMath::Clamp(AgentDistance / SafeRadius, 0.0f, 1.0f);

		// Not "as far from the threat as possible". A point too far away is as wrong as one too close,
		// because an agent that retreats out of its own weapon range stops being a threat and starts
		// being scenery.
		const float ThreatTerm = 1.0f - FMath::Clamp(
			FMath::Abs(ThreatDistance - PreferredThreatDistance) / PreferredThreatDistance, 0.0f, 1.0f);

		const float HighTerm = Point.Height == ECoverHeight::High ? 1.0f : 0.0f;
		const float PeekTerm = PeekSide != ECoverPeekSide::None ? 1.0f : 0.0f;
		const float WallTerm = 1.0f - FMath::Clamp(
			Point.WallDistance / FMath::Max(Point.CoverDistanceRef, 1.0f), 0.0f, 1.0f);

		const float Score = (
			Params.ShieldingWeight * ShieldTerm +
			Params.AgentDistanceWeight * AgentTerm +
			Params.ThreatDistanceWeight * ThreatTerm +
			Params.HighCoverWeight * HighTerm +
			Params.PeekWeight * PeekTerm +
			Params.WallProximityWeight * WallTerm) / TotalWeight;

		if (Score > BestScore)
		{
			BestScore = Score;
			BestIndex = PointIndex;
			BestShielding = Shielding;
			BestDistance = AgentDistance;
			BestPeek = PeekSide;
		}
	};

	if (Points.Num() > 0 && SearchRadius > 0.0f)
	{
		const FIntVector MinCell = CellOf(AgentLocation - FVector(SearchRadius));
		const FIntVector MaxCell = CellOf(AgentLocation + FVector(SearchRadius));

		const int64 CellCount =
			static_cast<int64>(MaxCell.X - MinCell.X + 1) *
			static_cast<int64>(MaxCell.Y - MinCell.Y + 1) *
			static_cast<int64>(MaxCell.Z - MinCell.Z + 1);

		if (CellCount <= CoverPointsPrivate::MaxQueryCells)
		{
			for (int32 CellZ = MinCell.Z; CellZ <= MaxCell.Z && Examined < CandidateCeiling; ++CellZ)
			{
				for (int32 CellY = MinCell.Y; CellY <= MaxCell.Y && Examined < CandidateCeiling; ++CellY)
				{
					for (int32 CellX = MinCell.X; CellX <= MaxCell.X && Examined < CandidateCeiling; ++CellX)
					{
						const TArray<int32>* Cell = SpatialHash.Find(FIntVector(CellX, CellY, CellZ));
						if (!Cell)
						{
							continue;
						}

						for (int32 PointIndex : *Cell)
						{
							if (Examined >= CandidateCeiling)
							{
								break;
							}
							Consider(PointIndex);
						}
					}
				}
			}
		}
		else
		{
			// A search radius so large that walking its cells costs more than walking the points. Rare,
			// and it degrades to a linear scan rather than to a stall - and the candidate counter on the
			// box makes it visible that it happened.
			for (int32 PointIndex = 0; PointIndex < Points.Num() && Examined < CandidateCeiling; ++PointIndex)
			{
				Consider(PointIndex);
			}
		}
	}

	if (BestIndex != INDEX_NONE)
	{
		Points[BestIndex].Score = BestScore;

		Result.bFound = true;
		Result.Handle = Points[BestIndex].Handle;
		Result.Score = BestScore;
		Result.Shielding = BestShielding;
		Result.DistanceToAgent = BestDistance;
		Result.PeekSide = BestPeek;

		// Claimed here rather than by the caller a frame later, so two agents querying on the same frame
		// cannot both be handed the same point. This is the whole reason the flag exists.
		if (bClaimAgent && AgentActor)
		{
			Result.bClaimed = ClaimCover(Points[BestIndex].Handle, AgentActor);
		}

		Result.Point = Points[BestIndex];
	}

	Result.CandidatesExamined = Examined;

	const double Microseconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartCycles) * 1000000.0;
	Stats.LastQueryMicroseconds = static_cast<float>(Microseconds);
	Stats.LastQueryCandidates = Examined;
	++Stats.TotalQueries;
	++QueriesInWindow;
	QueryMicrosecondsInWindow += Microseconds;

	return Result;
}

//~ Claims -------------------------------------------------------------------------------------------------

void UCoverPointsSubsystem::ClearClaimAt(int32 PointIndex)
{
	if (!Points.IsValidIndex(PointIndex))
	{
		return;
	}

	FCoverPoint& Point = Points[PointIndex];
	Point.bClaimed = false;
	Point.ClaimedBy.Reset();
	Point.ClaimExpiryTime = 0.0;
}

bool UCoverPointsSubsystem::ClaimCover(const FCoverPointHandle& Handle, AActor* AgentActor, float LifetimeSeconds)
{
	if (!AgentActor || !IsCoverValid(Handle))
	{
		return false;
	}

	FCoverPoint& Point = Points[Handle.Index];

	AActor* CurrentOwner = Point.ClaimedBy.Get();
	if (Point.bClaimed && CurrentOwner && CurrentOwner != AgentActor)
	{
		return false;
	}

	// One agent, one point. Taking a new one gives the old one back in the same call, because an agent
	// that moves from cover to cover and forgets to release would otherwise hoard the whole level.
	if (const int32* Previous = OwnerToPoint.Find(AgentActor))
	{
		if (*Previous != Handle.Index)
		{
			ClearClaimAt(*Previous);
		}
	}

	const UCoverPointsSettings& Settings = UCoverPointsSettings::Get();
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const float Lifetime = LifetimeSeconds >= 0.0f ? LifetimeSeconds : Settings.ClaimLifetimeSeconds;

	Point.bClaimed = true;
	Point.ClaimedBy = AgentActor;
	Point.ClaimExpiryTime = Lifetime > 0.0f ? Now + Lifetime : TNumericLimits<double>::Max();

	OwnerToPoint.Add(AgentActor, Handle.Index);
	Stats.ClaimedPoints = OwnerToPoint.Num();

	return true;
}

bool UCoverPointsSubsystem::ReleaseCover(const FCoverPointHandle& Handle, AActor* AgentActor)
{
	if (Handle.BuildId != BuildId || !Points.IsValidIndex(Handle.Index))
	{
		return false;
	}

	FCoverPoint& Point = Points[Handle.Index];
	if (!Point.bClaimed || Point.ClaimedBy.Get() != AgentActor)
	{
		return false;
	}

	ClearClaimAt(Handle.Index);
	OwnerToPoint.Remove(AgentActor);
	Stats.ClaimedPoints = OwnerToPoint.Num();

	return true;
}

int32 UCoverPointsSubsystem::ReleaseCoverForActor(AActor* AgentActor)
{
	if (!AgentActor)
	{
		return 0;
	}

	int32 Released = 0;
	if (const int32* PointIndex = OwnerToPoint.Find(AgentActor))
	{
		ClearClaimAt(*PointIndex);
		++Released;
	}

	OwnerToPoint.Remove(AgentActor);
	Stats.ClaimedPoints = OwnerToPoint.Num();

	return Released;
}

int32 UCoverPointsSubsystem::ReleaseAllClaims()
{
	const int32 Released = OwnerToPoint.Num();

	for (const TPair<TWeakObjectPtr<AActor>, int32>& Entry : OwnerToPoint)
	{
		ClearClaimAt(Entry.Value);
	}

	OwnerToPoint.Reset();
	Stats.ClaimedPoints = 0;

	return Released;
}

AActor* UCoverPointsSubsystem::GetCoverOwner(const FCoverPointHandle& Handle) const
{
	if (Handle.BuildId != BuildId || !Points.IsValidIndex(Handle.Index))
	{
		return nullptr;
	}

	return Points[Handle.Index].ClaimedBy.Get();
}

FCoverPointHandle UCoverPointsSubsystem::GetClaimedCoverForActor(const AActor* AgentActor) const
{
	if (!AgentActor)
	{
		return FCoverPointHandle();
	}

	// The map is keyed by weak pointer to a non-const actor; the lookup does not modify anything.
	if (const int32* PointIndex = OwnerToPoint.Find(const_cast<AActor*>(AgentActor)))
	{
		if (Points.IsValidIndex(*PointIndex))
		{
			return Points[*PointIndex].Handle;
		}
	}

	return FCoverPointHandle();
}

void UCoverPointsSubsystem::SweepExpiredClaims()
{
	if (OwnerToPoint.Num() == 0)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	TArray<TWeakObjectPtr<AActor>, TInlineAllocator<16>> Dead;

	for (const TPair<TWeakObjectPtr<AActor>, int32>& Entry : OwnerToPoint)
	{
		// Either the agent is gone - the case this whole mechanism exists for - or it is alive and has
		// stopped refreshing, which for cover means it has moved on and forgotten to say so.
		const bool bOwnerGone = !Entry.Key.IsValid();
		const bool bExpired = Points.IsValidIndex(Entry.Value) && Now > Points[Entry.Value].ClaimExpiryTime;

		if (bOwnerGone || bExpired)
		{
			ClearClaimAt(Entry.Value);
			Dead.Add(Entry.Key);
		}
	}

	for (const TWeakObjectPtr<AActor>& Entry : Dead)
	{
		OwnerToPoint.Remove(Entry);
	}

	Stats.ExpiredClaims += Dead.Num();
	Stats.ClaimedPoints = OwnerToPoint.Num();
}

//~ Threat -------------------------------------------------------------------------------------------------

void UCoverPointsSubsystem::SetThreatLocation(const FVector& Location)
{
	ThreatActor.Reset();
	ThreatLocation = Location;
	bHasThreat = true;
}

void UCoverPointsSubsystem::SetThreatActor(AActor* Actor)
{
	ThreatActor = Actor;

	if (Actor)
	{
		ThreatLocation = Actor->GetActorLocation();
		bHasThreat = true;
	}
}

void UCoverPointsSubsystem::ClearThreat()
{
	ThreatActor.Reset();
	bHasThreat = false;
}

FVector UCoverPointsSubsystem::GetThreatLocation() const
{
	return ThreatLocation;
}

bool UCoverPointsSubsystem::HasThreat() const
{
	return bHasThreat;
}

//~ Verification -------------------------------------------------------------------------------------------

void UCoverPointsSubsystem::TickVerification()
{
	const UCoverPointsSettings& Settings = UCoverPointsSettings::Get();

	if (!Settings.bVerifyCover || !bHasThreat || Points.Num() == 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 TraceBudget = Settings.MaxVerificationTracesPerFrame;
	if (TraceBudget <= 0)
	{
		return;
	}

	FCollisionQueryParams QueryParams(FName(TEXT("CoverPointsVerify")), false);
	if (const AActor* Threat = ThreatActor.Get())
	{
		QueryParams.AddIgnoredActor(Threat);
	}

	const FVector EyeOffset = FVector::UpVector * Settings.VerificationEyeHeight;

	// Bounded twice over: by the trace budget, and by never walking the array more than once in a frame.
	// Without the second bound, a level where nothing passes the shielding filter would spin the cursor
	// through every point every frame looking for work it is never going to find.
	int32 Scanned = 0;
	while (TraceBudget > 0 && Scanned < Points.Num())
	{
		const int32 Index = VerificationCursor;
		VerificationCursor = (VerificationCursor + 1) % Points.Num();
		++Scanned;

		FCoverPoint& Point = Points[Index];
		if (Point.bInvalidated)
		{
			continue;
		}

		// A point that never claimed to block cannot be caught lying about blocking. Auditing those would
		// spend the budget confirming things nobody asserted.
		if (GetShielding(Point, ThreatLocation) < Settings.VerificationMinShielding)
		{
			continue;
		}

		--TraceBudget;
		++Stats.VerificationTraces;

		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(
			Hit,
			Point.Location + EyeOffset,
			ThreatLocation + EyeOffset,
			Settings.GenerationChannel,
			QueryParams);

		if (!bBlocked)
		{
			// The geometry that made this a cover point is gone. The point said it was safe and it is not,
			// so it stops being offered - immediately, and visibly on the counter.
			Point.bInvalidated = true;
			++Stats.InvalidatedPoints;

			if (Point.bClaimed)
			{
				AActor* Owner = Point.ClaimedBy.Get();
				ClearClaimAt(Index);
				if (Owner)
				{
					OwnerToPoint.Remove(Owner);
				}
				Stats.ClaimedPoints = OwnerToPoint.Num();
			}

			OnPointInvalidated.Broadcast(Point.Handle);
		}
	}
}

//~ Debug --------------------------------------------------------------------------------------------------

void UCoverPointsSubsystem::SetShowPoints(bool bShow)
{
	bShowPoints = bShow;
}

void UCoverPointsSubsystem::SetShowPeekSides(bool bShow)
{
	bShowPeekSides = bShow;

	// Asking for the peek markers and not seeing the points they belong to would be a puzzle, not a
	// debug view.
	if (bShow)
	{
		bShowPoints = true;
	}
}

void UCoverPointsSubsystem::DrawDebugPoints() const
{
#if ENABLE_DRAW_DEBUG
	const UWorld* World = GetWorld();
	if (!World || Points.Num() == 0)
	{
		return;
	}

	// Drawn 10 cm up. At the floor exactly, half of every marker is inside the ground plane and the
	// colours read as much darker than they are.
	const FVector Lift = FVector::UpVector * 10.0f;
	const int32 Limit = FMath::Min(Points.Num(), CoverPointsPrivate::MaxDebugPoints);

	for (int32 Index = 0; Index < Limit; ++Index)
	{
		const FCoverPoint& Point = Points[Index];
		const FVector Origin = Point.Location + Lift;

		FColor Color;
		if (Point.bInvalidated)
		{
			Color = FColor(90, 90, 90);
		}
		else if (bHasThreat)
		{
			// The picture the product sells: green where the cover faces the threat, red where it turns
			// its back on it, recomputed every frame from one dot product per point.
			const float Shielding = GetShielding(Point, ThreatLocation);
			const float Alpha = FMath::Clamp((Shielding + 1.0f) * 0.5f, 0.0f, 1.0f);
			Color = FLinearColor(1.0f - Alpha, Alpha, 0.12f).ToFColor(false);
		}
		else
		{
			Color = Point.Height == ECoverHeight::High ? FColor(80, 170, 255) : FColor(140, 200, 255);
		}

		DrawDebugPoint(World, Origin, Point.Height == ECoverHeight::High ? 14.0f : 9.0f, Color, false, -1.0f, SDPG_World);

		// Which way the cover faces. Without it a field of coloured dots says where cover is but not what
		// it is behind, and the two are not the same picture.
		DrawDebugLine(World, Origin, Origin + Point.CoverNormal * 45.0f, Color, false, -1.0f, SDPG_World, 1.5f);

		if (Point.bClaimed)
		{
			DrawDebugSphere(World, Origin, 32.0f, 8, FColor(255, 220, 90), false, -1.0f, SDPG_World, 1.5f);
		}

		if (bShowPeekSides)
		{
			const FVector RightDir = CoverPointsPrivate::RightOfCover(Point.CoverNormal);
			const FVector Forward = -Point.CoverNormal;

			if (Point.bCanPeekRight)
			{
				DrawDebugDirectionalArrow(World, Origin + RightDir * 25.0f, Origin + RightDir * 55.0f + Forward * 30.0f,
					18.0f, FColor(120, 220, 255), false, -1.0f, SDPG_World, 2.0f);
			}
			if (Point.bCanPeekLeft)
			{
				DrawDebugDirectionalArrow(World, Origin - RightDir * 25.0f, Origin - RightDir * 55.0f + Forward * 30.0f,
					18.0f, FColor(120, 220, 255), false, -1.0f, SDPG_World, 2.0f);
			}
			if (Point.bCanPeekOver)
			{
				DrawDebugDirectionalArrow(World, Origin, Origin + FVector::UpVector * 60.0f + Forward * 20.0f,
					18.0f, FColor(255, 160, 255), false, -1.0f, SDPG_World, 2.0f);
			}
		}
	}
#endif
}

//~ Counters box -------------------------------------------------------------------------------------------

int32 UCoverPointsSubsystem::GetStatsLineCount() const
{
	return CoverPointsPrivate::StatsLines;
}

void UCoverPointsSubsystem::DrawStatsBox(UCanvas* Canvas, const FVector2D& Origin, float Width) const
{
	using namespace CoverPointsPrivate;

	if (!Canvas)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}

	LastStatsDrawFrame = GFrameCounter;

	const UCoverPointsSettings& Settings = UCoverPointsSettings::Get();

	const float BoxHeight = GetStatsLineCount() * LineHeight + BoxPadding * 2.0f;
	DrawFilledRect(Canvas,
		FVector2D(Origin.X - BoxPadding, Origin.Y - BoxPadding),
		FVector2D(Width, BoxHeight),
		PanelBackground);

	float LineY = static_cast<float>(Origin.Y);
	auto DrawLine = [&](FStringView Line, const FLinearColor& Color)
	{
		FCanvasTextStringViewItem Item(FVector2D(Origin.X, LineY), Line, Font, Color);
		Canvas->DrawItem(Item);
		LineY += LineHeight;
	};

	TStringBuilder<192> Line;

	Line.Reset();
	Line.Append(TEXT("CoverPoints"));
	DrawLine(Line.ToView(), HeadingColor);

	Line.Reset();
	Line.Appendf(TEXT("Points        %d   high %d / low %d"), Stats.TotalPoints, Stats.HighPoints, Stats.LowPoints);
	DrawLine(Line.ToView(), Stats.TotalPoints > 0 ? BodyColor : DimColor);

	Line.Reset();
	Line.Appendf(TEXT("Peekable      %d"), Stats.PeekablePoints);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Append(TEXT("Build         "));
	AppendBar(Line, Stats.BuildProgress);
	Line.Appendf(TEXT(" %3.0f%%"), Stats.BuildProgress * 100.0f);
	DrawLine(Line.ToView(), Stats.bBuilding ? WarnColor : GoodColor);

	Line.Reset();
	Line.Appendf(TEXT("Samples       %d / %d"), Stats.SamplesProcessed, Stats.SamplesTotal);
	DrawLine(Line.ToView(), Stats.bBuilding ? WarnColor : DimColor);

	// The promise and the measurement on one line. A budget printed without the cost next to it proves
	// nothing at all.
	Line.Reset();
	Line.Appendf(TEXT("Build ms/f    %.3f   peak %.3f   budget %.2f"),
		Stats.BuildMillisecondsThisFrame, Stats.BuildMillisecondsPeak, Settings.MaxBuildMillisecondsPerFrame);
	DrawLine(Line.ToView(),
		Stats.BuildMillisecondsThisFrame > Settings.MaxBuildMillisecondsPerFrame * 1.5f ? BadColor : BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Last build    %.1f ms wall clock"), Stats.LastBuildTotalMilliseconds);
	DrawLine(Line.ToView(), DimColor);

	Line.Reset();
	Line.Appendf(TEXT("Queries       %.1f /s   %d total"), Stats.QueriesPerSecond, Stats.TotalQueries);
	DrawLine(Line.ToView(), BodyColor);

	// The headline number. Microseconds, not milliseconds, and it is the average over the last second
	// rather than a best case anybody has to take on trust.
	Line.Reset();
	Line.Appendf(TEXT("Per query     %.1f us avg   %.1f us last"),
		Stats.MicrosecondsPerQuery, Stats.LastQueryMicroseconds);
	DrawLine(Line.ToView(), Stats.MicrosecondsPerQuery > 100.0f ? WarnColor : GoodColor);

	// The evidence for the line above: how many points the hash handed the scorer, against how many a
	// linear search would have touched.
	Line.Reset();
	Line.Appendf(TEXT("Candidates    %d of %d   %d cells"),
		Stats.LastQueryCandidates, Stats.TotalPoints, Stats.GridCells);
	DrawLine(Line.ToView(), BodyColor);

	Line.Reset();
	Line.Appendf(TEXT("Claimed       %d   expired %d"), Stats.ClaimedPoints, Stats.ExpiredClaims);
	DrawLine(Line.ToView(), Stats.ClaimedPoints > 0 ? GoodColor : DimColor);

	Line.Reset();
	Line.Appendf(TEXT("Verify        %d traces   %d invalidated"),
		Stats.VerificationTraces, Stats.InvalidatedPoints);
	DrawLine(Line.ToView(), Stats.InvalidatedPoints > 0 ? WarnColor : BodyColor);

	Line.Reset();
	if (Stats.bHasThreat)
	{
		Line.Appendf(TEXT("Threat        %.0f, %.0f, %.0f   volumes %d"),
			Stats.ThreatLocation.X, Stats.ThreatLocation.Y, Stats.ThreatLocation.Z, Stats.RegisteredVolumes);
	}
	else
	{
		Line.Appendf(TEXT("Threat        none   volumes %d"), Stats.RegisteredVolumes);
	}
	DrawLine(Line.ToView(), Stats.bHasThreat ? BodyColor : DimColor);
}

void UCoverPointsSubsystem::RebindHudDelegate()
{
	if (bAutoDrawStatsOnAnyHUD && !HudPostRenderHandle.IsValid())
	{
		HudPostRenderHandle = AHUD::OnHUDPostRender.AddUObject(this, &UCoverPointsSubsystem::OnAnyHUDPostRender);
	}
	else if (!bAutoDrawStatsOnAnyHUD && HudPostRenderHandle.IsValid())
	{
		AHUD::OnHUDPostRender.Remove(HudPostRenderHandle);
		HudPostRenderHandle.Reset();
	}
}

void UCoverPointsSubsystem::OnAnyHUDPostRender(AHUD* HUD, UCanvas* Canvas)
{
	if (!bAutoDrawStatsOnAnyHUD || !HUD || !Canvas)
	{
		return;
	}

	if (HUD->GetWorld() != GetWorld())
	{
		return;
	}

	// ACoverPointsHUD already drew this frame - do not stack a second box on top of it.
	if (LastStatsDrawFrame == GFrameCounter)
	{
		return;
	}

	DrawStatsBox(Canvas, FVector2D(28.0f, 90.0f), 400.0f);
}

//~ Log ----------------------------------------------------------------------------------------------------

void UCoverPointsSubsystem::LogStats() const
{
	UE_LOG(LogCoverPoints, Display, TEXT("CoverPoints:"));
	UE_LOG(LogCoverPoints, Display, TEXT("  Points           %d (high %d, low %d, peekable %d)"),
		Stats.TotalPoints, Stats.HighPoints, Stats.LowPoints, Stats.PeekablePoints);
	UE_LOG(LogCoverPoints, Display, TEXT("  Build            %s  %.0f%%  (%d / %d samples)"),
		Stats.bBuilding ? TEXT("running") : TEXT("idle"),
		Stats.BuildProgress * 100.0f, Stats.SamplesProcessed, Stats.SamplesTotal);
	UE_LOG(LogCoverPoints, Display, TEXT("  Build ms/frame   %.3f (peak %.3f), last build %.1f ms wall clock"),
		Stats.BuildMillisecondsThisFrame, Stats.BuildMillisecondsPeak, Stats.LastBuildTotalMilliseconds);
	UE_LOG(LogCoverPoints, Display, TEXT("  Queries          %.1f/s, %d total"),
		Stats.QueriesPerSecond, Stats.TotalQueries);
	UE_LOG(LogCoverPoints, Display, TEXT("  Per query        %.1f us avg, %.1f us last, %d candidates of %d"),
		Stats.MicrosecondsPerQuery, Stats.LastQueryMicroseconds, Stats.LastQueryCandidates, Stats.TotalPoints);
	UE_LOG(LogCoverPoints, Display, TEXT("  Grid             %d cells at %.0f cm"), Stats.GridCells, CellSize);
	UE_LOG(LogCoverPoints, Display, TEXT("  Claims           %d held, %d expired"),
		Stats.ClaimedPoints, Stats.ExpiredClaims);
	UE_LOG(LogCoverPoints, Display, TEXT("  Verification     %d traces, %d points invalidated"),
		Stats.VerificationTraces, Stats.InvalidatedPoints);
	UE_LOG(LogCoverPoints, Display, TEXT("  Threat           %s"),
		Stats.bHasThreat ? *Stats.ThreatLocation.ToCompactString() : TEXT("none"));
	UE_LOG(LogCoverPoints, Display, TEXT("  Volumes          %d"), Stats.RegisteredVolumes);
}

//~ Console commands ---------------------------------------------------------------------------------------

namespace CoverPointsPrivate
{
	static FAutoConsoleCommandWithWorldAndArgs CmdBuild(
		TEXT("Cover.Build"),
		TEXT("Cover.Build [now] - generate cover for every enabled volume. 'now' skips the per-frame budget."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UCoverPointsSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogCoverPoints, Warning, TEXT("Cover.Build: no CoverPoints subsystem in this world."));
				return;
			}

			if (!Subsystem->RequestBuild(nullptr))
			{
				return;
			}

			if (Args.Num() > 0 && Args[0].Equals(TEXT("now"), ESearchCase::IgnoreCase))
			{
				Subsystem->FinishBuildImmediately();
				UE_LOG(LogCoverPoints, Display, TEXT("Cover.Build: finished on this frame, %d points."),
					Subsystem->GetPointCount());
			}
			else
			{
				UE_LOG(LogCoverPoints, Display, TEXT("Cover.Build: started, %d samples queued."),
					Subsystem->GetStats().SamplesTotal);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStats(
		TEXT("Cover.Stats"),
		TEXT("Cover.Stats - print the measured cover counters to the log."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const UCoverPointsSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogCoverPoints, Warning, TEXT("Cover.Stats: no CoverPoints subsystem in this world."));
				return;
			}
			Subsystem->LogStats();
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdShow(
		TEXT("Cover.Show"),
		TEXT("Cover.Show [0|1] [peek] - draw the cover points, coloured against the current threat."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UCoverPointsSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogCoverPoints, Warning, TEXT("Cover.Show: no CoverPoints subsystem in this world."));
				return;
			}

			const bool bPeek = Args.ContainsByPredicate([](const FString& Arg)
			{
				return Arg.Equals(TEXT("peek"), ESearchCase::IgnoreCase);
			});

			bool bEnable = !Subsystem->IsShowingPoints();
			for (const FString& Arg : Args)
			{
				if (Arg.IsNumeric())
				{
					bEnable = FCString::Atoi(*Arg) != 0;
					break;
				}
			}

			Subsystem->SetShowPoints(bEnable);
			Subsystem->SetShowPeekSides(bEnable && bPeek);

			UE_LOG(LogCoverPoints, Display, TEXT("Cover.Show: points %s, peek markers %s."),
				bEnable ? TEXT("on") : TEXT("off"),
				(bEnable && bPeek) ? TEXT("on") : TEXT("off"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdThreat(
		TEXT("Cover.Threat"),
		TEXT("Cover.Threat [X Y Z | player | off] - set what the colours and the spot-checks are measured against."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UCoverPointsSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogCoverPoints, Warning, TEXT("Cover.Threat: no CoverPoints subsystem in this world."));
				return;
			}

			if (Args.Num() == 0)
			{
				UE_LOG(LogCoverPoints, Display, TEXT("Cover.Threat: %s"),
					Subsystem->HasThreat() ? *Subsystem->GetThreatLocation().ToCompactString() : TEXT("none"));
				return;
			}

			if (Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase) || Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
			{
				Subsystem->ClearThreat();
				UE_LOG(LogCoverPoints, Display, TEXT("Cover.Threat: cleared."));
				return;
			}

			if (Args[0].Equals(TEXT("player"), ESearchCase::IgnoreCase))
			{
				APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
				APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
				if (!Pawn)
				{
					UE_LOG(LogCoverPoints, Warning, TEXT("Cover.Threat: no player pawn to follow."));
					return;
				}

				Subsystem->SetThreatActor(Pawn);
				UE_LOG(LogCoverPoints, Display, TEXT("Cover.Threat: following %s."), *Pawn->GetName());
				return;
			}

			if (Args.Num() < 3)
			{
				UE_LOG(LogCoverPoints, Warning, TEXT("Cover.Threat: expected three numbers, 'player' or 'off'."));
				return;
			}

			const FVector Location(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
			Subsystem->SetThreatLocation(Location);
			UE_LOG(LogCoverPoints, Display, TEXT("Cover.Threat: %s"), *Location.ToCompactString());
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdClear(
		TEXT("Cover.Clear"),
		TEXT("Cover.Clear - throw away every cover point, every claim and any build in flight."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			UCoverPointsSubsystem* Subsystem = GetSubsystem(World);
			if (!Subsystem)
			{
				UE_LOG(LogCoverPoints, Warning, TEXT("Cover.Clear: no CoverPoints subsystem in this world."));
				return;
			}

			Subsystem->ClearAllPoints();
			UE_LOG(LogCoverPoints, Display, TEXT("Cover.Clear: cleared."));
		}));
}
