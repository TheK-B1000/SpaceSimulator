// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Telemetry/EdenAfterActionModel.h"

#include "EdenAfterActionReviewWidget.generated.h"

class UTextBlock;
class UVerticalBox;

/**
 * Minimal after-action review surface. Formats FEdenAfterActionResult only.
 * Shown via ShowAfterAction; never auto-popups on mission end (0006 lock 2B).
 */
UCLASS(Blueprintable, BlueprintType)
class EDENSPACESIMULATOR_API UEdenAfterActionReviewWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Eden|AAR")
	void ApplyAfterActionResult(const FEdenAfterActionResult& InResult);

	UFUNCTION(BlueprintPure, Category = "Eden|AAR")
	FEdenAfterActionResult GetAfterActionResult() const;

	/** Pure formatter for the review text, exposed for tests. */
	static FText FormatResult(const FEdenAfterActionResult& Result);

protected:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Eden|AAR")
	void OnAfterActionResultUpdated(const FEdenAfterActionResult& InResult);

private:
	void EnsureDisplayCreated();
	void RefreshDisplayFromResult();

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Eden|AAR", meta = (AllowPrivateAccess = "true"))
	FEdenAfterActionResult CurrentResult;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> RootLayout = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ReviewTextBlock = nullptr;
};
