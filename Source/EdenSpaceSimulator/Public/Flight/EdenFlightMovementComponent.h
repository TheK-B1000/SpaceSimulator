// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightMovementModel.h"
#include "Flight/EdenFlightTypes.h"
#include "Flight/EdenPropellantSupply.h"
#include "Flight/EdenPropulsionDemandSource.h"
#include "Flight/EdenPropulsionPowerSource.h"
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

	/** True while a propellant supply is simulated: this component then draws propellant itself, once per flight step. */
	virtual bool MetersPropellantDirectly() const override;

	/** Finds the owner's IEdenPropellantSupply and IEdenPropulsionPowerSource. Runs at BeginPlay and lazily. */
	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	bool RefreshResourceSources();

	/**
	 * Advances flight by DeltaTimeSeconds of world time in fixed steps (StepSettings).
	 * Command applies to every step this update. Time does not advance while the
	 * world or the Eden simulation clock is paused. Returns the steps executed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	int32 MoveWithCommand(const FEdenFlightInputCommand& Command, float DeltaTimeSeconds);

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

	/** Body axes X (roll), Y (pitch), Z (yaw), rad/s. */
	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	FVector GetAngularVelocityBodyRadiansPerSecond() const;

	/** Sets body angular velocity (rad/s), e.g. for scenario setup. Not used by controllers, which act through torque. */
	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	void SetAngularVelocityBodyRadiansPerSecond(FVector NewAngularVelocityBodyRadiansPerSecond);

	/** Operator authority x power fraction. Propellant is a separate, per-step limit. */
	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	float GetEffectiveThrustAuthority() const;

	/** Fraction of thrust and torque the power system allows right now (1 when power is not simulated). */
	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	float GetPowerThrustFraction() const;

	/** Stored propellant as reported by the supply (0 when unmetered). Read-only: the fuel system owns it. */
	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	float GetPropellantMassKilograms() const;

	/** Total mass used by the dynamics: dry + stored propellant. */
	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	float GetMassKilograms() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	float GetSpeedMetersPerSecond() const;

	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	FEdenFlightDynamicsSnapshot GetFlightDynamicsSnapshot() const;

	/** Rigid-body rotation parameters for this spacecraft: inertia, control torque, assist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight")
	FEdenFlightAttitudeSettings AttitudeSettings;

	/** Newtonian translation parameters for this spacecraft. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight")
	FEdenFlightPropulsionSettings PropulsionSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Flight")
	FEdenFlightStepSettings StepSettings;

protected:
	virtual void BeginPlay() override;

private:
	bool ValidateUpdatedComponentForFlight() const;
	IEdenPropellantSupply* GetPropellantSupply() const;
	const IEdenPropulsionPowerSource* GetPowerSource() const;
	bool IsPropellantMetered() const;
	bool IsFlightTimePaused() const;
	FEdenFlightPropulsionSettings GetActivePropulsionSettings() const;
	FEdenFlightPropulsionSettings ResolvePropulsionSettings(double FixedStepSeconds);
	FEdenFlightAttitudeSettings GetActiveAttitudeSettings() const;
	FEdenFlightAttitudeSettings ResolveAttitudeSettings(double FixedStepSeconds);
	FEdenFlightStepSettings ResolveStepSettings();
	void RunFlightStep(
		const FEdenFlightInputCommand& Command,
		const FEdenFlightPropulsionSettings& ActivePropulsion,
		const FEdenFlightAttitudeSettings& ActiveAttitude,
		double FixedStepSeconds);
	void ResetFlightSimulationState();
	void LogInvalidInputState(bool bInputWasSanitized);
	void LogBlockingHitState(const FHitResult& Hit);
	void LogDroppedSteps(int32 DroppedSteps, double FixedStepSeconds);

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	FVector AngularVelocityBodyRadiansPerSecond = FVector::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	float PropulsionDemandNormalized = 0.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	float ThrustAuthority = 1.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	bool bStabilizationAssistAvailable = true;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	bool bFlightAssistRequested = true;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	bool bFlightAssistActive = true;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	double FlightSimulationTimeSeconds = 0.0;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	int64 FlightStepCount = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "Eden|Flight")
	int64 DroppedFlightStepCount = 0;

	double FlightAccumulatorSeconds = 0.0;

	/** Requested / delivered / propellant results of the most recent flight step. */
	FEdenFlightStepOutput LastStep;

	UPROPERTY(Transient)
	TWeakObjectPtr<UActorComponent> PropellantSupplyComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<UActorComponent> PowerSourceComponent;

	bool bResourceSourcesDiscovered = false;

	bool bLoggedInvalidInputState = false;
	bool bLoggedInvalidPropulsionSettings = false;
	bool bLoggedInvalidAttitudeSettings = false;
	bool bLoggedInvalidStepSettings = false;
	bool bWasBlockedLastMove = false;
};
