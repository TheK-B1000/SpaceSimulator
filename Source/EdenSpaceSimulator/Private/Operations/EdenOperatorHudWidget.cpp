// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace EdenOperatorHudWidgetPrivate
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
}

void UEdenOperatorHudWidget::NativeConstruct()
{
	Super::NativeConstruct();
	EnsureDisplayCreated();
	RefreshDisplayFromSnapshot();
}

void UEdenOperatorHudWidget::ApplyHudSnapshot(const FEdenOperatorHudSnapshot& InSnapshot)
{
	CurrentSnapshot = InSnapshot;
	EnsureDisplayCreated();
	RefreshDisplayFromSnapshot();
	OnHudSnapshotUpdated(InSnapshot);
}

FEdenOperatorHudSnapshot UEdenOperatorHudWidget::GetHudSnapshot() const
{
	return CurrentSnapshot;
}

void UEdenOperatorHudWidget::EnsureDisplayCreated()
{
	if (HudTextBlock || !WidgetTree)
	{
		return;
	}

	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!RootCanvas)
	{
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("OperatorHudRoot"));
		WidgetTree->RootWidget = RootCanvas;
	}

	RootLayout = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OperatorHudLayout"));
	HudTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("OperatorHudText"));
	HudTextBlock->SetAutoWrapText(true);
	HudTextBlock->SetJustification(ETextJustify::Left);
	HudTextBlock->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13));
	HudTextBlock->SetColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.95f, 1.0f, 1.0f)));

	if (UVerticalBoxSlot* TextSlot = RootLayout->AddChildToVerticalBox(HudTextBlock))
	{
		TextSlot->SetPadding(FMargin(12.0f));
		TextSlot->SetHorizontalAlignment(HAlign_Left);
		TextSlot->SetVerticalAlignment(VAlign_Top);
	}

	if (UCanvasPanelSlot* LayoutSlot = RootCanvas->AddChildToCanvas(RootLayout))
	{
		LayoutSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.42f, 1.0f));
		LayoutSlot->SetOffsets(FMargin(16.0f, 16.0f, 16.0f, 16.0f));
	}
}

void UEdenOperatorHudWidget::RefreshDisplayFromSnapshot()
{
	if (HudTextBlock)
	{
		HudTextBlock->SetText(FormatSnapshot(CurrentSnapshot));
	}
}

FText UEdenOperatorHudWidget::FormatSnapshot(const FEdenOperatorHudSnapshot& Snapshot)
{
	using namespace EdenOperatorHudWidgetPrivate;

	FString ObjectivesText;
	for (const FEdenMissionObjectiveRuntime& Objective : Snapshot.Objectives)
	{
		ObjectivesText += FString::Printf(
			TEXT("\n  - %s [%s]"),
			*Objective.ObjectiveId.ToString(),
			*EnumToString(Objective.State));
	}
	if (ObjectivesText.IsEmpty())
	{
		ObjectivesText = TEXT("\n  (none)");
	}

	FString AlertsText;
	for (const FEdenAlert& Alert : Snapshot.ActiveAlerts)
	{
		AlertsText += FString::Printf(
			TEXT("\n  - [%s] %s"),
			*EnumToString(Alert.Severity),
			*Alert.DisplayText.ToString());
	}
	if (AlertsText.IsEmpty())
	{
		AlertsText = TEXT("\n  (none)");
	}

	const FString Body = FString::Printf(
		TEXT(
			"MISSION\n"
			"  %s / %s / %s\n"
			"  Elapsed: %.1fs\n"
			"  Objectives:%s\n"
			"\n"
			"RESOURCES\n"
			"  Fuel: %.0f%%\n"
			"  Battery: %.0f%%\n"
			"  Generation: %.2f kW\n"
			"  Demand: %.2f kW\n"
			"  Temperature: %.1f C\n"
			"\n"
			"OPERATOR\n"
			"  Thermal: %s\n"
			"  Load-shed: %s\n"
			"  Propulsion: %s\n"
			"\n"
			"ALERTS%s"),
		*Snapshot.MissionId.ToString(),
		*EnumToString(Snapshot.MissionState),
		*EnumToString(Snapshot.MissionPhase),
		Snapshot.MissionElapsedTimeSeconds,
		*ObjectivesText,
		Snapshot.Fuel.FuelFraction * 100.0f,
		Snapshot.Power.ChargeFraction * 100.0f,
		Snapshot.Power.GenerationKilowatts,
		Snapshot.Power.TotalDemandKilowatts,
		Snapshot.Thermal.TemperatureCelsius,
		*EnumToString(Snapshot.Operator.ThermalMode),
		*EnumToString(Snapshot.Operator.LoadShedMode),
		*EnumToString(Snapshot.Operator.PropulsionPriority),
		*AlertsText);

	return FText::FromString(Body);
}
