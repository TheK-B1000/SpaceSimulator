// Copyright Epic Games, Inc. All Rights Reserved.

#include "Telemetry/EdenAfterActionReviewWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace EdenAfterActionReviewWidgetPrivate
{
	FString EnumDisplayName(const UEnum* Enum, int64 Value)
	{
		if (!Enum)
		{
			return TEXT("Unknown");
		}
		const FText Display = Enum->GetDisplayNameTextByValue(Value);
		if (!Display.IsEmpty())
		{
			return Display.ToString();
		}
		return Enum->GetNameStringByValue(Value);
	}

	template <typename TEnum>
	FString EnumToString(TEnum Value)
	{
		return EnumDisplayName(StaticEnum<TEnum>(), static_cast<int64>(Value));
	}

	FString OutcomeLabel(EEdenMissionState State)
	{
		switch (State)
		{
		case EEdenMissionState::Succeeded:
			return TEXT("SUCCESS");
		case EEdenMissionState::Failed:
			return TEXT("FAILURE");
		case EEdenMissionState::Running:
			return TEXT("RUNNING");
		case EEdenMissionState::Ready:
			return TEXT("READY");
		default:
			return TEXT("INACTIVE");
		}
	}

	FString ObjectiveMark(EEdenObjectiveState State)
	{
		switch (State)
		{
		case EEdenObjectiveState::Completed:
			return TEXT("[OK]");
		case EEdenObjectiveState::Failed:
			return TEXT("[FAIL]");
		case EEdenObjectiveState::Active:
			return TEXT("[ACTIVE]");
		default:
			return TEXT("[PENDING]");
		}
	}
}

void UEdenAfterActionReviewWidget::NativeConstruct()
{
	Super::NativeConstruct();
	EnsureDisplayCreated();
	RefreshDisplayFromResult();
}

void UEdenAfterActionReviewWidget::ApplyAfterActionResult(const FEdenAfterActionResult& InResult)
{
	CurrentResult = InResult;
	EnsureDisplayCreated();
	RefreshDisplayFromResult();
	OnAfterActionResultUpdated(InResult);
}

FEdenAfterActionResult UEdenAfterActionReviewWidget::GetAfterActionResult() const
{
	return CurrentResult;
}

void UEdenAfterActionReviewWidget::EnsureDisplayCreated()
{
	if (ReviewTextBlock || !WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!RootCanvas)
	{
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("AfterActionRoot"));
		WidgetTree->RootWidget = RootCanvas;
	}

	RootLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AfterActionLayout"));
	ReviewTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("AfterActionText"));
	ReviewTextBlock->SetAutoWrapText(true);
	ReviewTextBlock->SetJustification(ETextJustify::Left);
	ReviewTextBlock->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
	ReviewTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.95f, 0.85f, 1.0f)));

	if (UVerticalBoxSlot* TextSlot = RootLayout->AddChildToVerticalBox(ReviewTextBlock))
	{
		TextSlot->SetPadding(FMargin(16.0f));
		TextSlot->SetHorizontalAlignment(HAlign_Left);
		TextSlot->SetVerticalAlignment(VAlign_Top);
	}

	if (UCanvasPanelSlot* LayoutSlot = RootCanvas->AddChildToCanvas(RootLayout))
	{
		LayoutSlot->SetAnchors(FAnchors(0.25f, 0.15f, 0.75f, 0.85f));
		LayoutSlot->SetOffsets(FMargin(0.0f));
	}
}

void UEdenAfterActionReviewWidget::RefreshDisplayFromResult()
{
	if (ReviewTextBlock)
	{
		ReviewTextBlock->SetText(FormatResult(CurrentResult));
	}
}

FText UEdenAfterActionReviewWidget::FormatResult(const FEdenAfterActionResult& Result)
{
	using namespace EdenAfterActionReviewWidgetPrivate;

	FString ObjectivesText;
	for (const FEdenAfterActionObjectiveLine& Objective : Result.Objectives)
	{
		ObjectivesText += FString::Printf(
			TEXT("\n  %s %s"),
			*ObjectiveMark(Objective.State),
			*Objective.ObjectiveId.ToString());
		if ((Objective.State == EEdenObjectiveState::Completed || Objective.State == EEdenObjectiveState::Failed) && Objective.ResolvedAtSeconds >= 0.0f)
		{
			ObjectivesText += FString::Printf(TEXT("  (T=%.1f s)"), Objective.ResolvedAtSeconds);
		}
		if (!Objective.FailureReason.IsEmpty())
		{
			ObjectivesText += FString::Printf(TEXT("\n      %s"), *Objective.FailureReason);
		}
	}
	if (ObjectivesText.IsEmpty())
	{
		ObjectivesText = TEXT("\n  (none)");
	}

	const FString TruncationNote = Result.bHistoryTruncated ? TEXT("\n\n[History truncated]") : TEXT("");
	FString PowerBusText;
	if (Result.bPowerBusLimited)
	{
		// Curtailed auxiliary systems were not fully powered; say so explicitly.
		PowerBusText = FString::Printf(
			TEXT("\n\nPower Bus\n  Lowest cooling power  %.0f%%\n  Auxiliary peak        %.1f kW requested, up to %.1f kW curtailed\n  Time curtailed        %.1f s%s"),
			Result.LowestCoolingAuthority * 100.0f,
			Result.PeakAuxiliaryRequestedKilowatts,
			Result.PeakAuxiliaryCurtailedKilowatts,
			Result.SecondsAuxiliaryCurtailed,
			Result.PeakAuxiliaryCurtailedKilowatts > 1.0e-3f ? TEXT("  (auxiliary systems were NOT fully powered)") : TEXT(""));
	}
	const FString ReasonText = Result.TerminalReason.IsEmpty() ? FString() : FString::Printf(TEXT("\n%s"), *Result.TerminalReason);
	const FString Body = FString::Printf(
		TEXT(
			"%s\n"
			"%s\n"
			"\n"
			"Duration              %.1f s\n"
			"Peak Temperature      %.1f C\n"
			"Lowest Battery        %.0f%%\n"
			"Fuel Remaining        %.0f%%\n"
			"\n"
			"Operator Actions      %d\n"
			"Critical Alerts       %d\n"
			"\n"
			"Objectives%s%s%s"),
		*Result.MissionId.ToString(),
		*(OutcomeLabel(Result.FinalMissionState) + ReasonText),
		Result.DurationSeconds,
		Result.PeakRecordedSimulationTemperatureCelsius,
		Result.LowestRecordedBatteryChargeFraction * 100.0f,
		Result.FinalFuelFraction * 100.0f,
		Result.OperatorCommandCount,
		Result.CriticalAlertCount,
		*ObjectivesText,
		*PowerBusText,
		*TruncationNote);

	return FText::FromString(Body);
}
