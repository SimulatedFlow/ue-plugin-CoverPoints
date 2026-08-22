// Copyright 2026 Simulated Flow. All Rights Reserved.

#include "CoverPoints.h"
#include "CoverPointsLog.h"

DEFINE_LOG_CATEGORY(LogCoverPoints);

#define LOCTEXT_NAMESPACE "FCoverPointsModule"

void FCoverPointsModule::StartupModule()
{
	UE_LOG(LogCoverPoints, Log, TEXT("CoverPoints started."));
}

void FCoverPointsModule::ShutdownModule()
{
	UE_LOG(LogCoverPoints, Log, TEXT("CoverPoints shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCoverPointsModule, CoverPoints)
