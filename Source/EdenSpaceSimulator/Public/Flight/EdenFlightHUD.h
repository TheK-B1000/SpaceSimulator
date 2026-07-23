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

	virtual void BeginPlay() override;
	virtual void DrawHUD() override;
	virtual void ShowDebug(FName DebugType = NAME_None) override;

private:
#if !UE_BUILD_SHIPPING
	void EnableEdenSystemsDebugDisplay();
	void DrawEdenSystemsOverlay();

	bool bRequestedDefaultEdenSystemsDebug = false;
#endif
};
