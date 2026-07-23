// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightGameMode.h"

#include "Flight/EdenFlightHUD.h"
#include "Flight/EdenFlightPlayerController.h"
#include "Flight/EdenSpacecraftPawn.h"

AEdenFlightGameMode::AEdenFlightGameMode()
{
	DefaultPawnClass = AEdenSpacecraftPawn::StaticClass();
	PlayerControllerClass = AEdenFlightPlayerController::StaticClass();
	HUDClass = AEdenFlightHUD::StaticClass();
}
