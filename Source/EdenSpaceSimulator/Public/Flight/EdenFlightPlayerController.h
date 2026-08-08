// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightTypes.h"
#include "GameFramework/PlayerController.h"
#include "Operations/EdenOperatorHudTypes.h"

#include "EdenFlightPlayerController.generated.h"

struct FInputActionValue;
class UInputAction;
class UInputMappingContext;
class UEdenMissionDefinitionDataAsset;
class UEdenOperatorHudWidget;
class UEdenAfterActionReviewWidget;

UCLASS(BlueprintType, Blueprintable)
class EDENSPACESIMULATOR_API AEdenFlightPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AEdenFlightPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void Reset() override;

	UFUNCTION(BlueprintCallable, Category = "Eden|Flight")
	void ClearFlightInputIntent();

	UFUNCTION(BlueprintPure, Category = "Eden|Flight")
	FEdenFlightInputCommand GetCurrentFlightInputCommand() const;

	UFUNCTION(BlueprintPure, Category = "Eden|HUD")
	FEdenOperatorHudSnapshot GetOperatorHudSnapshot() const;

	UFUNCTION(Exec, Category = "Eden|Mission")
	void StartMission();

	UFUNCTION(Exec, Category = "Eden|Mission")
	void RestartMission();

	UFUNCTION(Exec, Category = "Eden|Mission")
	void AbortMission();

	UFUNCTION(Exec, Category = "Eden|Telemetry")
	void ExportTelemetry();

	UFUNCTION(Exec, Category = "Eden|Telemetry")
	void ShowAfterAction();

	/** Default mission definition loaded by StartMission/RestartMission. Soft reference; no hard package dependency. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|Mission")
	TSoftObjectPtr<UEdenMissionDefinitionDataAsset> DefaultMissionDefinitionAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|HUD")
	TSubclassOf<UEdenOperatorHudWidget> OperatorHudWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|AAR")
	TSubclassOf<UEdenAfterActionReviewWidget> AfterActionReviewWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|HUD", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float OperatorHudRefreshHz = 10.0f;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|Flight|Input")
	TObjectPtr<UInputMappingContext> FlightInputMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|Flight|Input")
	TObjectPtr<UInputAction> FlightTranslateAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|Flight|Input")
	TObjectPtr<UInputAction> FlightRotateAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|Flight|Input")
	TObjectPtr<UInputAction> FlightStabilizeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|Flight|Input")
	TObjectPtr<UInputAction> ThermalModeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|Flight|Input")
	TObjectPtr<UInputAction> LoadShedAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|Flight|Input")
	TObjectPtr<UInputAction> PropulsionPriorityAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|Flight|Input")
	int32 FlightInputMappingPriority = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eden|Flight|Input")
	FEdenFlightInputIntent FlightInputIntent;

private:
	void AddFlightInputMappingContext();
	void BindFlightInputActions();
	void SetTranslationIntent(FVector NewTranslationInput);
	void SetRotationIntent(FVector NewRotationInput);
	void HandleTranslateInput(const FInputActionValue& Value);
	void HandleTranslateReleased(const FInputActionValue& Value);
	void HandleRotateInput(const FInputActionValue& Value);
	void HandleRotateReleased(const FInputActionValue& Value);
	void HandleStabilizeStarted(const FInputActionValue& Value);
	void HandleThermalModeStarted(const FInputActionValue& Value);
	void HandleLoadShedStarted(const FInputActionValue& Value);
	void HandlePropulsionPriorityStarted(const FInputActionValue& Value);
	void LogMissingInputAssetState();
	void EnsureOperatorHudCreated();
	void RefreshOperatorHudSnapshot();
	FEdenOperatorHudSnapshot AssembleOperatorHudSnapshot() const;
	void EnsureAfterActionReviewCreated();

	bool bLoggedMissingInputAssetState = false;
	bool bLoggedUnexpectedPawnState = false;

	UPROPERTY(Transient)
	TObjectPtr<UEdenOperatorHudWidget> OperatorHudWidget;

	UPROPERTY(Transient)
	TObjectPtr<UEdenAfterActionReviewWidget> AfterActionReviewWidget;

	UPROPERTY(Transient)
	FEdenOperatorHudSnapshot CachedOperatorHudSnapshot;

	float OperatorHudRefreshAccumulatorSeconds = 0.0f;
};
