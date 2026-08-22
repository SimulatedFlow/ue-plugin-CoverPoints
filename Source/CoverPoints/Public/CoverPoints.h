// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime module for CoverPoints. Loads at PreDefault so the world subsystem, the volume class and the
 * Cover.* console commands all exist before the first game world is created - a volume in a map that is
 * loaded on startup must be able to start generating on the very first frame.
 */
class FCoverPointsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
