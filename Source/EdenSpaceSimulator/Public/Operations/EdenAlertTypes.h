// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdenAlertTypes.generated.h"

UENUM(BlueprintType)
enum class EEdenAlertSeverity : uint8
{
	Info UMETA(DisplayName = "Info"),
	Warning UMETA(DisplayName = "Warning"),
	Critical UMETA(DisplayName = "Critical"),
	Emergency UMETA(DisplayName = "Emergency")
};

USTRUCT(BlueprintType)
struct EDENSPACESIMULATOR_API FEdenAlert
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Alerts")
	FName AlertId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Alerts")
	EEdenAlertSeverity Severity = EEdenAlertSeverity::Info;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Alerts")
	FText DisplayText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Alerts")
	FName SourceSystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Alerts")
	float RaisedAtSimTimeSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Alerts")
	bool bAcknowledged = false;
};
