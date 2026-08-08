// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightTypes.h"
#include "GameFramework/PlayerController.h"

#include "EdenFlightPlayerController.generated.h"

struct FInputActionValue;
class UInputAction;
class UInputMappingContext;

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
	void LogMissingInputAssetState();

	bool bLoggedMissingInputAssetState = false;
	bool bLoggedUnexpectedPawnState = false;
};
