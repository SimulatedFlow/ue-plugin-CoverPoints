// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "CoverPointsHUD.h"

#include "CoverPointsSettings.h"
#include "CoverPointsSubsystem.h"
#include "Engine/Canvas.h"

ACoverPointsHUD::ACoverPointsHUD()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ACoverPointsHUD::BeginPlay()
{
	Super::BeginPlay();

	bShowStats = UCoverPointsSettings::Get().bShowStatsByDefault;
}

void ACoverPointsHUD::ToggleStats()
{
	bShowStats = !bShowStats;
}

void ACoverPointsHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas || !bShowStats)
	{
		return;
	}

	// Everything drawn is read from the subsystem on the frame it is drawn. Nothing is cached here, so the
	// box cannot claim one thing while the queries do another.
	if (const UCoverPointsSubsystem* Subsystem = UCoverPointsSubsystem::Get(this))
	{
		Subsystem->DrawStatsBox(Canvas, StatsBoxOrigin, StatsBoxWidth);
	}
}
