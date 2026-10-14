// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightPresentationTypes.h"
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

	/** Raw mouse virtual-stick deflection (X = pitch, Y = yaw), before the dead zone. */
	UFUNCTION(BlueprintPure, Category = "Eden|Controls")
	FVector2D GetMouseStickDeflection() const { return MouseStickPitchYaw; }

	/** Re-centres the mouse virtual stick (also console). */
	UFUNCTION(BlueprintCallable, Exec, Category = "Eden|Controls")
	void EdenCenterMouseStick();

	/** Mouse rotation input arrives as per-frame deltas; feeds them to the virtual stick. Exposed for tests. */
	void AddRotationInputSample(const FVector& RotationAxes);

	/** Releases the rotation action: roll keys stop, the mouse stick keeps its deflection and auto-centres. */
	void ReleaseRotationInput();

	/** Advances stick auto-centring by DeltaSeconds. Called from PlayerTick after the command is applied. */
	void AdvanceMouseStick(float DeltaSeconds);

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

	/** X: toggle Flight Assist. Shows the confirmed resulting state (or why assist is unavailable) on the HUD. */
	UFUNCTION(BlueprintCallable, Exec, Category = "Eden|Controls")
	void EdenToggleFlightAssist();

	/** T: cycle thermal mode through operator control; HUD shows the confirmed mode or the rejection reason. */
	UFUNCTION(BlueprintCallable, Exec, Category = "Eden|Controls")
	void EdenCycleThermalMode();

	/** L: toggle load shedding through operator control; HUD shows the confirmed state or the rejection reason. */
	UFUNCTION(BlueprintCallable, Exec, Category = "Eden|Controls")
	void EdenToggleLoadShed();

	/** P: toggle propulsion priority through operator control; HUD shows the confirmed authority or the rejection reason. */
	UFUNCTION(BlueprintCallable, Exec, Category = "Eden|Controls")
	void EdenTogglePropulsionPriority();

	/** Re-reads authoritative state into the HUD now (the HUD otherwise refreshes at OperatorHudRefreshHz). Read-only. */
	UFUNCTION(BlueprintCallable, Category = "Eden|HUD")
	void RefreshOperatorHudNow();

	UFUNCTION(BlueprintPure, Category = "Eden|HUD")
	UEdenOperatorHudWidget* GetOperatorHudWidget() const;

	/** Last control-feedback message shown for X/T/L/P (confirmed state or rejection reason). */
	UFUNCTION(BlueprintPure, Category = "Eden|HUD")
	FString GetLastControlFeedback() const;

	UFUNCTION(BlueprintPure, Category = "Eden|HUD")
	bool WasLastControlFeedbackSuccessful() const;

	/** PIE/dev: log the operator HUD's widget tree, viewport attachment, visibility and geometry. */
	UFUNCTION(Exec, Category = "Eden|Debug")
	void EdenHudDiagnostics();

	/**
	 * PIE/dev: capture a screenshot (with HUD) after DelaySeconds of real time, to Saved/Screenshots.
	 * QuitAfter != 0 quits once the capture has been written (for scripted framing checks).
	 */
	UFUNCTION(Exec, Category = "Eden|Debug")
	void EdenCaptureScreenshot(float DelaySeconds, int32 QuitAfter);

	/** PIE/dev: set stored propellant (kg) through the fuel system, its only owner. For empty-tank / depletion checks. */
	UFUNCTION(Exec, Category = "Eden|Debug")
	void EdenSetFuelKilograms(float Kilograms);

	/** PIE/dev: set battery charge as a fraction of capacity through the power system. For power-tier checks. */
	UFUNCTION(Exec, Category = "Eden|Debug")
	void EdenSetBatteryFraction(float Fraction);

	/**
	 * PIE/dev: enable EDEN OS transport.
	 * BaseUrl optional; if empty, uses EDEN_OS_LIVE_E2E_BASE_URL then UEdenOsConnectionSettings::BaseUrl.
	 */
	UFUNCTION(Exec, Category = "Eden|OS")
	void EnableEdenOs(const FString& BaseUrl);

	/**
	 * PIE/dev: load Bearer JWT from an environment variable (value never logged).
	 * VariableName optional; defaults to EDEN_OS_LIVE_E2E_BEARER_JWT.
	 */
	UFUNCTION(Exec, Category = "Eden|OS")
	void SetEdenOsBearerFromEnv(const FString& VariableName);

	/** Default mission definition loaded by StartMission/RestartMission. Soft reference; no hard package dependency. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|Mission")
	TSoftObjectPtr<UEdenMissionDefinitionDataAsset> DefaultMissionDefinitionAsset;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|HUD")
	TSubclassOf<UEdenOperatorHudWidget> OperatorHudWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|AAR")
	TSubclassOf<UEdenAfterActionReviewWidget> AfterActionReviewWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eden|HUD", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float OperatorHudRefreshHz = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|Controls")
	FEdenFlightMouseSettings MouseStickSettings;

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

	UPROPERTY(Transient)
	FString LastControlFeedback;

	UPROPERTY(Transient)
	bool bLastControlFeedbackSucceeded = false;

private:
	void EnsureFlightInputAssetsLoaded();
	/** Shows confirmed control feedback on the HUD and refreshes it immediately (outside the 10 Hz cadence). */
	void ReportControlFeedback(const FString& Message, bool bSucceeded);
	FString DescribeOperatorRejection(const class UEdenOperatorControlComponent* Operator) const;
	void ApplyFlightInputMode();
	void AddFlightInputMappingContext();
	void BindFlightInputActions();
	void SetTranslationIntent(FVector NewTranslationInput);
	void SetRotationIntent(FVector NewRotationInput);
	void ApplyComposedRotationIntent();
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

	FTimerHandle ScreenshotTimerHandle;

	/** Mouse virtual stick (X = pitch, Y = yaw) in [-1, 1]; persists between frames. */
	FVector2D MouseStickPitchYaw = FVector2D::ZeroVector;

	/** Roll keys (E/Q) are direct: held = deflected, released = centred. */
	double RollKeyInput = 0.0;
};
