// Copyright 2026 Simulated Flow. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CoverPointsHUD.generated.h"

class UCanvas;

/**
 * The counters box for CoverPoints, drawn on UCanvas from AHUD.
 *
 * Canvas rather than UMG, and there are deliberately no buttons on it. Two reasons, pulling the same way:
 *
 *   - This box has to survive a cooked Shipping build. DrawDebug is compiled out there and a debug widget
 *     is usually stripped; a Canvas overlay is not. It is a real overlay, not a development one.
 *
 *   - Anything that has to be *clicked* belongs in UMG instead. An AHUD hit box is tested against
 *     UGameViewportClient::GetMousePosition(), which reports nothing at all on a machine with no mouse
 *     attached - a capture rig, a build agent, a headless test - so the click never lands. Widgets are
 *     not affected. The numbers live here, where they cost nothing and always draw; controls live in a
 *     widget, where they always receive the click.
 *
 * A project that already has a HUD class does not have to reparent it: turn on bAutoDrawStatsOnAnyHUD in
 * Project Settings and the same box is drawn through AHUD::OnHUDPostRender instead. The two paths know
 * about each other and cannot stack.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Cover Points HUD"))
class COVERPOINTS_API ACoverPointsHUD : public AHUD
{
	GENERATED_BODY()

public:
	ACoverPointsHUD();

	//~ AActor interface
	virtual void BeginPlay() override;

	//~ AHUD interface
	virtual void DrawHUD() override;

	/** Draw the counters box. Toggled from Blueprint, or from a widget button. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverPoints|HUD")
	bool bShowStats = true;

	/** Top-left corner of the counters box, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverPoints|HUD")
	FVector2D StatsBoxOrigin = FVector2D(28.0f, 90.0f);

	/** Width of the counters box, in pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CoverPoints|HUD", meta = (ClampMin = "160.0"))
	float StatsBoxWidth = 430.0f;

	/** Flip the counters box on and off. This is what a STATS button in a widget calls. */
	UFUNCTION(BlueprintCallable, Category = "CoverPoints|HUD")
	void ToggleStats();
};
