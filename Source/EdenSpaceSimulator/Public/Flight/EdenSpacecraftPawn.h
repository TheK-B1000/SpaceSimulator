// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightPresentationTypes.h"
#include "Flight/EdenFlightTypes.h"
#include "GameFramework/Pawn.h"
#include "Systems/EdenResourceDebugTypes.h"

#include "EdenSpacecraftPawn.generated.h"

class UEdenFlightMovementComponent;
class UEdenFuelSystemComponent;
class UEdenOperatorControlComponent;
class UEdenPowerSystemComponent;
class UEdenThermalSystemComponent;
class UCameraComponent;
class UCanvas;
class UNiagaraComponent;
class USphereComponent;
class USpringArmComponent;

UCLASS(BlueprintType, Blueprintable)
class EDENSPACESIMULATOR_API AEdenSpacecraftPawn : public APawn
{
	GENERATED_BODY()

public:
	AEdenSpacecraftPawn();

	virtual UPawnMovementComponent* GetMovementComponent() const override;
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
#if !UE_BUILD_SHIPPING
	virtual void DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos) override;
#endif

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

	UFUNCTION(BlueprintPure, Category = "Eden|Operations")
	UEdenOperatorControlComponent* GetOperatorControlComponent() const;

	FEdenSpacecraftSystemsDebugSnapshot GetEdenSystemsDebugSnapshot() const;

	/** Main-engine visual level last shown (delivered forward force / forward rating). Presentation only. */
	UFUNCTION(BlueprintPure, Category = "Eden|Presentation")
	float GetMainEngineVisualLevel() const { return MainEngineVisualLevel; }

	UFUNCTION(BlueprintPure, Category = "Eden|Presentation")
	USpringArmComponent* GetChaseCameraArm() const { return ChaseCameraArm; }

	/** Bounding radius of the presentation hull (cm) used for camera framing; 0 until the hull is applied. */
	UFUNCTION(BlueprintPure, Category = "Eden|Presentation")
	float GetPresentationHullRadiusCentimeters() const { return PresentationHullRadiusCentimeters; }

	/** Rebuilds the chase camera from CameraSettings (e.g. after editing them at runtime). */
	UFUNCTION(BlueprintCallable, Category = "Eden|Presentation")
	void ApplyChaseCamera();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Presentation")
	FEdenFlightCameraSettings CameraSettings;

	/** Sky dome star brightness (M_Star_Nebula scalar parameters). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Presentation", meta = (ClampMin = "0.0"))
	float NearStarsIntensity = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Presentation", meta = (ClampMin = "0.0"))
	float FarStarsIntensity = 1.8f;

private:
	void RestoreRequiredCollisionRoot();
	void ApplyPresentationHull();
	void ApplySpaceBackdrop();
	void ApplyThrusterPresentation();
	void UpdateThrusterPresentation();

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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Operations", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UEdenOperatorControlComponent> OperatorControlComponent;

	/** Spawns the nebula sky dome and planet (light M_Star_Nebula path, not the 4GB MI cubemap). */
	UPROPERTY(EditAnywhere, Category = "Eden|Presentation")
	bool bEnableSpaceBackdrop = true;

	/** Spawns Arghanion NS_Thruster. Keep off until Niagara + thruster load is validated in PIE. */
	UPROPERTY(EditAnywhere, Category = "Eden|Presentation")
	bool bEnableThrusterEffects = false;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> MainThrusterEffect;

	UPROPERTY(Transient)
	TObjectPtr<UCameraComponent> PresentationCamera;

	UPROPERTY(Transient)
	TObjectPtr<USpringArmComponent> ChaseCameraArm;

	float PresentationBaseFieldOfView = 90.0f;
	float PresentationHullRadiusCentimeters = 0.0f;
	float MainEngineVisualLevel = 0.0f;
};
