// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "EdenFlightHUD.generated.h"

UCLASS()
class EDENSPACESIMULATOR_API AEdenFlightHUD : public AHUD
{
	GENERATED_BODY()

public:
	AEdenFlightHUD();

	virtual void DrawHUD() override;

private:
#if !UE_BUILD_SHIPPING
	void DrawEdenSystemsOverlay();
	void DrawEdenMissionOverlay();
#endif
};
