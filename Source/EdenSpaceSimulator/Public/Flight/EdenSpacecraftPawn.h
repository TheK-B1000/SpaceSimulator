// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightTypes.h"
#include "GameFramework/Pawn.h"

#include "EdenSpacecraftPawn.generated.h"

class UEdenFlightMovementComponent;
class UEdenFuelSystemComponent;
class UEdenPowerSystemComponent;
class UEdenThermalSystemComponent;
class USphereComponent;

UCLASS(BlueprintType, Blueprintable)
class EDENSPACESIMULATOR_API AEdenSpacecraftPawn : public APawn
{
	GENERATED_BODY()

public:
	AEdenSpacecraftPawn();

	virtual UPawnMovementComponent* GetMovementComponent() const override;
	virtual void PostInitializeComponents() override;

	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	void ApplyFlightInputCommand(const FEdenFlightInputCommand& Command, float DeltaTimeSeconds);

	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	void ResetFlightState();

	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	USphereComponent* GetRequiredCollisionRoot() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	UEdenFlightMovementComponent* GetFlightMovementComponent() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Systems")
	UEdenFuelSystemComponent* GetFuelSystemComponent() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Systems")
	UEdenPowerSystemComponent* GetPowerSystemComponent() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Systems")
	UEdenThermalSystemComponent* GetThermalSystemComponent() const;

private:
	void RestoreRequiredCollisionRoot();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> RequiredCollisionRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UEdenFlightMovementComponent> FlightMovementComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Systems", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UEdenFuelSystemComponent> FuelSystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Systems", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UEdenPowerSystemComponent> PowerSystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Systems", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UEdenThermalSystemComponent> ThermalSystemComponent;
};
