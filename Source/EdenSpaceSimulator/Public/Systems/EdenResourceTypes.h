// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdenResourceTypes.generated.h"

UENUM(BlueprintType)
enum class EEdenFuelState : uint8
{
	Normal,
	Warning,
	Critical,
	Depleted
};
