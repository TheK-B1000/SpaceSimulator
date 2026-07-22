// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightTypes.h"
#include "GameFramework/Pawn.h"

#include "EdenSpacecraftPawn.generated.h"

class UEdenFlightMovementComponent;
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

private:
	void RestoreRequiredCollisionRoot();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> RequiredCollisionRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UEdenFlightMovementComponent> FlightMovementComponent;
};
