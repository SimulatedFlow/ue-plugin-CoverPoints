// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "CoverPointsSettings.h"

UCoverPointsSettings::UCoverPointsSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("CoverPoints");

	// The defaults describe a rifle-carrying humanoid in a level built to human scale. They are here rather
	// than in the struct's own initialisers so that the one place a project looks to change them is also
	// the one place they are written down.
	DefaultQueryParams.ShieldingWeight = 3.0f;
	DefaultQueryParams.AgentDistanceWeight = 1.0f;
	DefaultQueryParams.ThreatDistanceWeight = 1.0f;
	DefaultQueryParams.HighCoverWeight = 0.75f;
	DefaultQueryParams.PeekWeight = 1.0f;
	DefaultQueryParams.WallProximityWeight = 0.5f;
	DefaultQueryParams.PreferredThreatDistance = 1200.0f;
	DefaultQueryParams.MinShielding = 0.3f;
	DefaultQueryParams.bRequireHighCover = false;
	DefaultQueryParams.bRequirePeek = false;
	DefaultQueryParams.bAllowClaimed = false;
}

FName UCoverPointsSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UCoverPointsSettings::GetSectionName() const
{
	return TEXT("CoverPoints");
}

const UCoverPointsSettings& UCoverPointsSettings::Get()
{
	const UCoverPointsSettings* Settings = GetDefault<UCoverPointsSettings>();
	check(Settings);
	return *Settings;
}
