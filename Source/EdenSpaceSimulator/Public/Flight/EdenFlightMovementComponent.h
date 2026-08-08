// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenPropulsionDemandSource.h"
#include "Flight/EdenFlightTypes.h"
#include "GameFramework/PawnMovementComponent.h"

#include "EdenFlightMovementComponent.generated.h"

UCLASS(ClassGroup = (Movement), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class EDENSPACESIMULATOR_API UEdenFlightMovementComponent : public UPawnMovementComponent, public IEdenPropulsionDemandSource
{
	GENERATED_BODY()

public:
	UEdenFlightMovementComponent();

	virtual void InitializeComponent() override;
	virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;
	virtual void StopMovementImmediately() override;
	virtual float GetPropulsionDemandNormalized() const override;

	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	void MoveWithCommand(const FEdenFlightInputCommand& Command, float DeltaTimeSeconds);

	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	void ResetFlightMovement();

	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	bool SetThrustAuthority(float NewThrustAuthority);

	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	void ResetThrustAuthority();

	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	float GetThrustAuthority() const;

	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	void SetStabilizationAssistAvailable(bool bAvailable);

	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	bool IsStabilizationAssistAvailable() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	FVector GetAngularVelocityLocalDegreesPerSecond() const;

	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	void SetAngularVelocityLocalDegreesPerSecond(FVector NewAngularVelocityLocalDegreesPerSecond);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight")
	FEdenFlightMovementSettings MovementSettings;

private:
	bool ValidateUpdatedComponentForFlight() const;
	void ApplyRotation(float DeltaTimeSeconds);
	void LogInvalidInputState(bool bInputWasSanitized);
	void LogBlockingHitState(const FHitResult& Hit);

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	FVector AngularVelocityLocalDegreesPerSecond = FVector::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	float PropulsionDemandNormalized = 0.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	float ThrustAuthority = 1.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	bool bStabilizationAssistAvailable = true;

	bool bLoggedInvalidInputState = false;
	bool bWasBlockedLastMove = false;
};
