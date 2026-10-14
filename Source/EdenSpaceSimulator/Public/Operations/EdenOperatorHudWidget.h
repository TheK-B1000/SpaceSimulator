// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Operations/EdenOperatorHudTypes.h"

#include "EdenOperatorHudWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UTextBlock;

/**
 * EDEN flight HUD (UMG). Formats and displays FEdenOperatorHudSnapshot only; it never owns or
 * mutates simulation state and has no path back into flight physics.
 *
 * The widget tree is built in RebuildWidget(), before Slate builds the widget hierarchy, so it does
 * not depend on WBP_EdenOperatorHud having a designer tree (it has none). Building it later, in
 * NativeConstruct(), left the Slate hierarchy without the panels: the widget was created and in the
 * viewport but rendered nothing.
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

	/**
	 * Shows a control-feedback banner (confirmed state or rejection reason) immediately.
	 * NowSeconds is real time; the banner hides after FeedbackDisplaySeconds.
	 */
	UFUNCTION(BlueprintCallable, Category = "Eden|HUD")
	void ShowControlFeedback(const FText& Message, bool bSucceeded, double NowSeconds);

	UFUNCTION(BlueprintPure, Category = "Eden|HUD")
	FText GetControlFeedbackText() const;

	/** Root, viewport attachment, visibility, Slate children, geometry and Z-order, for EdenHudDiagnostics. */
	FString DescribeLayoutForDiagnostics() const;

	/** Number of panels whose UMG widgets exist and have a built Slate widget. */
	int32 GetBuiltPanelCount() const;

	// Pure formatters, exposed for tests. Unavailable sources render as "--", never as zero.
	static FString FormatFlightPanel(const FEdenOperatorHudSnapshot& Snapshot);
	static FString FormatSystemsPanel(const FEdenOperatorHudSnapshot& Snapshot);
	static FString FormatOperatorPanel(const FEdenOperatorHudSnapshot& Snapshot);
	static FString FormatMissionPanel(const FEdenOperatorHudSnapshot& Snapshot);
	static FString FormatEdenPanel(const FEdenOperatorHudSnapshot& Snapshot);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|HUD", meta = (ClampMin = "0.5", Units = "s"))
	float FeedbackDisplaySeconds = 3.0f;

	/** Text and padding scale for every panel (1 = original size). Panels keep their screen anchors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eden|HUD", meta = (ClampMin = "0.75", ClampMax = "2.5"))
	float HudScale = 1.35f;

	/** Changes HudScale at runtime and re-applies fonts and padding (also console: EdenHudScale). */
	UFUNCTION(BlueprintCallable, Category = "Eden|HUD")
	void SetHudScale(float NewScale);

	UFUNCTION(BlueprintPure, Category = "Eden|HUD")
	float GetHudScale() const { return HudScale; }

	/** Scaled font size: never below 6 pt, rounded to the nearest point. Exposed for tests. */
	static int32 ScaledFontSize(int32 BaseSize, float Scale);

	/** Point size currently applied to the panel bodies (0 before the layout is built). For tests and diagnostics. */
	int32 GetAppliedBodyFontSize() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Eden|HUD")
	void OnHudSnapshotUpdated(const FEdenOperatorHudSnapshot& InSnapshot);

private:
	struct FPanel
	{
		TObjectPtr<UBorder> Border = nullptr;
		TObjectPtr<UTextBlock> Title = nullptr;
		TObjectPtr<UTextBlock> Body = nullptr;
	};

	void EnsureLayoutBuilt();
	FPanel MakePanel(UCanvasPanel* Canvas, const TCHAR* Name, const FText& TitleText, const FVector2D& Anchor, const FVector2D& Offset);
	void RefreshPanels();
	void RefreshFeedback(double NowSeconds);
	void ApplyHudScale();

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Eden|HUD", meta = (AllowPrivateAccess = "true"))
	FEdenOperatorHudSnapshot CurrentSnapshot;

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> RootCanvas = nullptr;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> OwnedWidgets;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> FeedbackBorder = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FeedbackText = nullptr;

	FPanel FlightPanel;
	FPanel SystemsPanel;
	FPanel OperatorPanel;
	FPanel MissionPanel;
	FPanel EdenPanel;

	double FeedbackShownAtSeconds = -1.0;
	double LastNowSeconds = 0.0;
};
