// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Operations/EdenOperatorHudTypes.h"

#include "EdenOperatorHudWidget.generated.h"

UCLASS(Blueprintable, BlueprintType)
class EDENSPACESIMULATOR_API UEdenOperatorHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Eden|HUD")
	void ApplyHudSnapshot(const FEdenOperatorHudSnapshot& InSnapshot);

	UFUNCTION(BlueprintPure, Category = "Eden|HUD")
	FEdenOperatorHudSnapshot GetHudSnapshot() const;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Eden|HUD")
	void OnHudSnapshotUpdated(const FEdenOperatorHudSnapshot& InSnapshot);

private:
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Eden|HUD", meta = (AllowPrivateAccess = "true"))
	FEdenOperatorHudSnapshot CurrentSnapshot;
};
