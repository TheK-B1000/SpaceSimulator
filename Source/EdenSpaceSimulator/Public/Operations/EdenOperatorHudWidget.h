// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Operations/EdenOperatorHudTypes.h"

#include "EdenOperatorHudWidget.generated.h"

class UTextBlock;
class UVerticalBox;

/**
 * Production operator HUD. Formats and displays FEdenOperatorHudSnapshot only.
 * Does not own or mutate authoritative simulation state.
 */
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
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Eden|HUD")
	void OnHudSnapshotUpdated(const FEdenOperatorHudSnapshot& InSnapshot);

private:
	void EnsureDisplayCreated();
	void RefreshDisplayFromSnapshot();
	static FText FormatSnapshot(const FEdenOperatorHudSnapshot& Snapshot);

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Eden|HUD", meta = (AllowPrivateAccess = "true"))
	FEdenOperatorHudSnapshot CurrentSnapshot;

	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> RootLayout = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> HudTextBlock = nullptr;
};
