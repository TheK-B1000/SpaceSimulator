// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "EdenFlightGameMode.generated.h"

UCLASS(BlueprintType, Blueprintable)
class EDENSPACESIMULATOR_API AEdenFlightGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEdenFlightGameMode();

	virtual void BeginPlay() override;

private:
	/** Reloads or spawns the sandbox station if the editor world lost the mesh during a content deferral. */
	void EnsureFlightSandboxStation();
};
