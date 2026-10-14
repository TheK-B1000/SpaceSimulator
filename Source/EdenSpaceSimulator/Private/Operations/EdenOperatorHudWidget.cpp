// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/EdenOperatorHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/EdenLogCategories.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"

namespace EdenOperatorHudWidgetPrivate
{
const FLinearColor PanelColor(0.01f, 0.02f, 0.05f, 0.72f);
const FLinearColor WarningPanelColor(0.30f, 0.16f, 0.00f, 0.80f);
const FLinearColor CriticalPanelColor(0.35f, 0.02f, 0.02f, 0.85f);
const FLinearColor TitleColor(0.35f, 0.85f, 1.0f, 1.0f);
const FLinearColor BodyColor(0.86f, 0.94f, 1.0f, 1.0f);
const FLinearColor SuccessColor(0.25f, 0.95f, 0.45f, 1.0f);
const FLinearColor FailureColor(1.0f, 0.62f, 0.20f, 1.0f);
const TCHAR* Unavailable = TEXT("--");

FString EnumDisplayName(const UEnum* Enum, int64 Value)
{
	if (!Enum)
	{
		return TEXT("Unknown");
	}

	const FText Display = Enum->GetDisplayNameTextByValue(Value);
	return Display.IsEmpty() ? Enum->GetNameStringByValue(Value) : Display.ToString();
}

template <typename TEnum>
FString EnumToString(TEnum Value)
{
	return EnumDisplayName(StaticEnum<TEnum>(), static_cast<int64>(Value));
}

FString AssistText(const FEdenFlightDynamicsSnapshot& Flight)
{
	if (!Flight.bFlightAssistRequested)
	{
		return TEXT("OFF  (momentum conserved)");
	}
	return Flight.bFlightAssistActive ? TEXT("ON   (thrusters hold velocity/rate)") : TEXT("ON - UNAVAILABLE (load shed)");
}

FString ConnectionText(const FEdenOsConnectionSnapshot& Connection)
{
	if (!Connection.bEnabled)
	{
		return TEXT("OFFLINE  (transport disabled - console: EnableEdenOs)");
	}

	switch (Connection.ConnectionState)
	{
	case EEdenOsConnectionState::Connected:
		return TEXT("ONLINE");
	case EEdenOsConnectionState::Connecting:
		return TEXT("CONNECTING");
	case EEdenOsConnectionState::Degraded:
		return TEXT("DEGRADED");
	case EEdenOsConnectionState::Disconnected:
		return TEXT("DISCONNECTED");
	case EEdenOsConnectionState::Disabled:
	default:
		return TEXT("DISABLED");
	}
}

FSlateFontInfo Font(const TCHAR* Typeface, int32 Size)
{
	return FCoreStyle::GetDefaultFontStyle(Typeface, Size);
}
}

void UEdenOperatorHudWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshPanels();
	RefreshFeedback(LastNowSeconds);
}

TSharedRef<SWidget> UEdenOperatorHudWidget::RebuildWidget()
{
	// Build the UMG tree before UUserWidget::RebuildWidget turns WidgetTree->RootWidget into Slate.
	EnsureLayoutBuilt();
	return Super::RebuildWidget();
}

void UEdenOperatorHudWidget::EnsureLayoutBuilt()
{
	using namespace EdenOperatorHudWidgetPrivate;

	if (RootCanvas || !WidgetTree)
	{
		return;
	}

	RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!RootCanvas)
	{
		if (WidgetTree->RootWidget)
		{
			// Respect a designer-authored root instead of replacing it.
			UE_LOG(LogEdenOperations, Display, TEXT("%s keeps its designer root; EDEN HUD panels not added."), *GetNameSafe(this));
			return;
		}
		RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("EdenHudRoot"));
		WidgetTree->RootWidget = RootCanvas;
	}

	FlightPanel = MakePanel(RootCanvas, TEXT("Flight"), FText::FromString(TEXT("EDEN | FLIGHT")), FVector2D(0.0, 0.0), FVector2D(16.0, 16.0));
	MissionPanel = MakePanel(RootCanvas, TEXT("Mission"), FText::FromString(TEXT("MISSION")), FVector2D(0.5, 0.0), FVector2D(0.0, 16.0));
	SystemsPanel = MakePanel(RootCanvas, TEXT("Systems"), FText::FromString(TEXT("SYSTEMS")), FVector2D(1.0, 0.0), FVector2D(-16.0, 16.0));
	OperatorPanel = MakePanel(RootCanvas, TEXT("Operator"), FText::FromString(TEXT("OPERATOR CONTROLS")), FVector2D(0.0, 1.0), FVector2D(16.0, -16.0));
	EdenPanel = MakePanel(RootCanvas, TEXT("Eden"), FText::FromString(TEXT("EDEN STATUS")), FVector2D(1.0, 1.0), FVector2D(-16.0, -16.0));

	FeedbackBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("EdenHudFeedbackBorder"));
	FeedbackBorder->SetBrushColor(PanelColor);
	FeedbackBorder->SetPadding(FMargin(14.0f, 8.0f));
	FeedbackText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EdenHudFeedbackText"));
	FeedbackText->SetFont(Font(TEXT("Bold"), 14));
	FeedbackText->SetJustification(ETextJustify::Center);
	FeedbackBorder->SetContent(FeedbackText);
	if (UCanvasPanelSlot* FeedbackSlot = RootCanvas->AddChildToCanvas(FeedbackBorder))
	{
		FeedbackSlot->SetAnchors(FAnchors(0.5f, 0.78f));
		FeedbackSlot->SetAlignment(FVector2D(0.5, 0.5));
		FeedbackSlot->SetPosition(FVector2D::ZeroVector);
		FeedbackSlot->SetAutoSize(true);
		FeedbackSlot->SetZOrder(20);
	}
	FeedbackBorder->SetVisibility(ESlateVisibility::Collapsed);
	OwnedWidgets.Add(FeedbackBorder);
	OwnedWidgets.Add(FeedbackText);
}

UEdenOperatorHudWidget::FPanel UEdenOperatorHudWidget::MakePanel(
	UCanvasPanel* Canvas,
	const TCHAR* Name,
	const FText& TitleText,
	const FVector2D& Anchor,
	const FVector2D& Offset)
{
	using namespace EdenOperatorHudWidgetPrivate;

	FPanel Panel;
	Panel.Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), *FString::Printf(TEXT("EdenHud%sBorder"), Name));
	Panel.Border->SetBrushColor(PanelColor);
	Panel.Border->SetPadding(FMargin(12.0f, 8.0f));

	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), *FString::Printf(TEXT("EdenHud%sStack"), Name));
	Panel.Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("EdenHud%sTitle"), Name));
	Panel.Title->SetText(TitleText);
	Panel.Title->SetFont(Font(TEXT("Bold"), 11));
	Panel.Title->SetColorAndOpacity(FSlateColor(TitleColor));

	Panel.Body = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("EdenHud%sBody"), Name));
	Panel.Body->SetFont(Font(TEXT("Mono"), 10));
	Panel.Body->SetColorAndOpacity(FSlateColor(BodyColor));
	Panel.Body->SetText(FText::FromString(Unavailable));

	if (UVerticalBoxSlot* TitleSlot = Stack->AddChildToVerticalBox(Panel.Title))
	{
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	}
	Stack->AddChildToVerticalBox(Panel.Body);
	Panel.Border->SetContent(Stack);

	if (UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel.Border))
	{
		PanelSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
		PanelSlot->SetAlignment(Anchor);
		PanelSlot->SetPosition(Offset);
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetZOrder(10);
	}

	OwnedWidgets.Add(Panel.Border);
	OwnedWidgets.Add(Stack);
	OwnedWidgets.Add(Panel.Title);
	OwnedWidgets.Add(Panel.Body);
	return Panel;
}

void UEdenOperatorHudWidget::ApplyHudSnapshot(const FEdenOperatorHudSnapshot& InSnapshot)
{
	CurrentSnapshot = InSnapshot;
	EnsureLayoutBuilt();
	RefreshPanels();
	const UWorld* World = GetWorld();
	RefreshFeedback(World ? World->GetRealTimeSeconds() : LastNowSeconds);
	OnHudSnapshotUpdated(InSnapshot);
}

FEdenOperatorHudSnapshot UEdenOperatorHudWidget::GetHudSnapshot() const
{
	return CurrentSnapshot;
}

void UEdenOperatorHudWidget::ShowControlFeedback(const FText& Message, bool bSucceeded, double NowSeconds)
{
	using namespace EdenOperatorHudWidgetPrivate;

	EnsureLayoutBuilt();
	FeedbackShownAtSeconds = NowSeconds;
	if (FeedbackText)
	{
		FeedbackText->SetText(Message);
		FeedbackText->SetColorAndOpacity(FSlateColor(bSucceeded ? SuccessColor : FailureColor));
	}
	RefreshFeedback(NowSeconds);
}

FText UEdenOperatorHudWidget::GetControlFeedbackText() const
{
	return FeedbackText && FeedbackBorder && FeedbackBorder->GetVisibility() != ESlateVisibility::Collapsed
		? FeedbackText->GetText()
		: FText::GetEmpty();
}

void UEdenOperatorHudWidget::RefreshFeedback(double NowSeconds)
{
	LastNowSeconds = NowSeconds;
	if (!FeedbackBorder)
	{
		return;
	}

	const bool bVisible = FeedbackShownAtSeconds >= 0.0 && NowSeconds - FeedbackShownAtSeconds < FeedbackDisplaySeconds;
	FeedbackBorder->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UEdenOperatorHudWidget::RefreshPanels()
{
	using namespace EdenOperatorHudWidgetPrivate;

	auto SetBody = [](const FPanel& Panel, const FString& Text)
	{
		if (Panel.Body)
		{
			Panel.Body->SetText(FText::FromString(Text));
		}
	};

	SetBody(FlightPanel, FormatFlightPanel(CurrentSnapshot));
	SetBody(SystemsPanel, FormatSystemsPanel(CurrentSnapshot));
	SetBody(OperatorPanel, FormatOperatorPanel(CurrentSnapshot));
	SetBody(MissionPanel, FormatMissionPanel(CurrentSnapshot));
	SetBody(EdenPanel, FormatEdenPanel(CurrentSnapshot));

	// Systems panel tint follows the worst authoritative resource state.
	if (SystemsPanel.Border)
	{
		const FEdenOperatorHudSnapshot& S = CurrentSnapshot;
		const bool bCritical = (S.bHasPower && (S.Power.PowerState == EEdenPowerState::Critical || S.Power.PowerState == EEdenPowerState::Depleted))
			|| (S.bHasFuel && (S.Fuel.FuelState == EEdenFuelState::Critical || S.Fuel.FuelState == EEdenFuelState::Depleted))
			|| (S.bHasThermal && (S.Thermal.ThermalState == EEdenThermalState::Critical || S.Thermal.ThermalState == EEdenThermalState::Overheated));
		const bool bWarning = (S.bHasPower && S.Power.PowerState == EEdenPowerState::Warning)
			|| (S.bHasFuel && S.Fuel.FuelState == EEdenFuelState::Warning)
			|| (S.bHasThermal && S.Thermal.ThermalState == EEdenThermalState::Warning);
		SystemsPanel.Border->SetBrushColor(bCritical ? CriticalPanelColor : (bWarning ? WarningPanelColor : PanelColor));
	}
}

FString UEdenOperatorHudWidget::FormatFlightPanel(const FEdenOperatorHudSnapshot& Snapshot)
{
	using namespace EdenOperatorHudWidgetPrivate;

	if (!Snapshot.bHasFlight)
	{
		return TEXT("--  no flight data (no spacecraft possessed)");
	}

	const FEdenFlightDynamicsSnapshot& Flight = Snapshot.Flight;
	const FQuat Orientation = Flight.OrientationWorld.GetNormalized();
	const FVector Velocity = Flight.VelocityWorldMetersPerSecond;
	const double Speed = Velocity.Size();
	const FVector VelocityBody = Orientation.UnrotateVector(Velocity);
	const FVector Acceleration = Flight.AccelerationWorldMetersPerSecondSquared;
	const double AlongTravel = Speed > 1.0e-3 ? FVector::DotProduct(Acceleration, Velocity / Speed) : Acceleration.Size();

	FString Travel = TEXT("at rest");
	if (Speed > 0.05)
	{
		const double Cosine = FMath::Clamp(FVector::DotProduct(Orientation.GetForwardVector(), Velocity / Speed), -1.0, 1.0);
		Travel = FString::Printf(TEXT("%.1f deg off the nose"), FMath::RadiansToDegrees(FMath::Acos(Cosine)));
	}

	// Pilot convention, matching the stick: body w is (X roll, Y pitch, Z yaw); FRotator pitch/roll are about -Y/-X.
	const FVector Rates = Flight.AngularVelocityBodyRadiansPerSecond;
	const FRotator Attitude = Orientation.Rotator();
	return FString::Printf(
		TEXT("SPEED     %9.2f m/s\n")
		TEXT("ACCEL     %+9.2f m/s2 (along travel)\n")
		TEXT("VELOCITY  fwd %+8.2f  right %+8.2f  up %+8.2f m/s\n")
		TEXT("TRAVEL    %s\n")
		TEXT("RATES     pitch %+6.1f  yaw %+6.1f  roll %+6.1f deg/s\n")
		TEXT("ATTITUDE  P %+6.1f  Y %+6.1f  R %+6.1f deg\n")
		TEXT("STICK     pitch %+5.2f  yaw %+5.2f  roll %+5.2f\n")
		TEXT("ASSIST    %s   [X]"),
		Speed,
		AlongTravel,
		VelocityBody.X,
		VelocityBody.Y,
		VelocityBody.Z,
		*Travel,
		FMath::RadiansToDegrees(-Rates.Y),
		FMath::RadiansToDegrees(Rates.Z),
		FMath::RadiansToDegrees(-Rates.X),
		Attitude.Pitch,
		Attitude.Yaw,
		Attitude.Roll,
		Snapshot.PilotRotationCommand.X,
		Snapshot.PilotRotationCommand.Y,
		Snapshot.PilotRotationCommand.Z,
		*AssistText(Flight));
}

FString UEdenOperatorHudWidget::FormatSystemsPanel(const FEdenOperatorHudSnapshot& Snapshot)
{
	using namespace EdenOperatorHudWidgetPrivate;

	const FEdenFlightDynamicsSnapshot& Flight = Snapshot.Flight;
	FString Propellant;
	if (!Snapshot.bHasFuel)
	{
		Propellant = TEXT("--  (fuel system not simulating)");
	}
	else
	{
		Propellant = FString::Printf(
			TEXT("%.2f / %.0f kg (%.0f%%)  %s"),
			Snapshot.Fuel.FuelQuantityKilograms,
			Snapshot.FuelCapacityKilograms,
			Snapshot.Fuel.FuelFraction * 100.0f,
			*EnumToString(Snapshot.Fuel.FuelState).ToUpper());
	}

	const FString Flow = !Snapshot.bHasFlight
		? FString(Unavailable)
		: (Flight.bPropellantMetered ? FString::Printf(TEXT("%.3f kg/s"), Flight.PropellantFlowKilogramsPerSecond) : FString(TEXT("unmetered")));
	const FString Mass = !Snapshot.bHasFlight
		? FString(Unavailable)
		: FString::Printf(TEXT("%.0f kg (dry %.0f + propellant %.1f)"), Flight.MassKilograms, Flight.DryMassKilograms, Flight.PropellantMassKilograms);
	const FString Battery = !Snapshot.bHasPower
		? FString(TEXT("--  (power system not simulating)"))
		: FString::Printf(
			  TEXT("%.0f%%  %s  net %+.2f kW (gen %.2f / load %.2f)"),
			  Snapshot.Power.ChargeFraction * 100.0f,
			  *EnumToString(Snapshot.Power.PowerState).ToUpper(),
			  Snapshot.Power.NetPowerKilowatts,
			  Snapshot.Power.GenerationKilowatts,
			  Snapshot.Power.TotalDemandKilowatts);
	const FString Thermal = !Snapshot.bHasThermal
		? FString(TEXT("--  (thermal system not simulating)"))
		: FString::Printf(TEXT("%.1f C  %s"), Snapshot.Thermal.TemperatureCelsius, *EnumToString(Snapshot.Thermal.ThermalState).ToUpper());

	FString Propulsion = Unavailable;
	FString Limits = Unavailable;
	if (Snapshot.bHasFlight)
	{
		Propulsion = FString::Printf(
			TEXT("delivered %.1f kN / requested %.1f kN  (power %.0f%%)"),
			Flight.DeliveredThrusterOutputNewtons / 1000.0f,
			Flight.RequestedThrusterOutputNewtons / 1000.0f,
			Flight.PowerThrustFraction * 100.0f);
		Limits.Reset();
		if (Flight.bPowerLimited)
		{
			Limits += TEXT("POWER-LIMITED  ");
		}
		if (Flight.bPropellantLimited)
		{
			Limits += Flight.PropellantMassKilograms <= 0.0f ? TEXT("NO PROPELLANT  ") : TEXT("PROPELLANT-LIMITED  ");
		}
		if (Limits.IsEmpty())
		{
			Limits = TEXT("none");
		}
	}

	return FString::Printf(
		TEXT("PROPELLANT  %s\n")
		TEXT("FLOW        %s\n")
		TEXT("MASS        %s\n")
		TEXT("BATTERY     %s\n")
		TEXT("THERMAL     %s\n")
		TEXT("THRUST      %s\n")
		TEXT("LIMITS      %s"),
		*Propellant,
		*Flow,
		*Mass,
		*Battery,
		*Thermal,
		*Propulsion,
		*Limits);
}

FString UEdenOperatorHudWidget::FormatOperatorPanel(const FEdenOperatorHudSnapshot& Snapshot)
{
	using namespace EdenOperatorHudWidgetPrivate;

	const FString Assist = Snapshot.bHasFlight ? AssistText(Snapshot.Flight) : FString(Unavailable);
	if (!Snapshot.bHasOperator)
	{
		return FString::Printf(
			TEXT("[X] FLIGHT ASSIST  %s\n")
			TEXT("[T] [L] [P]  --  operator control disabled (missing DA_EdenOperatorControlConfig)"),
			*Assist);
	}

	const FEdenOperatorStateSnapshot& Operator = Snapshot.Operator;
	const FString Deliverable = Snapshot.bHasFlight
		? FString::Printf(TEXT("%.0f%%"), Snapshot.Flight.EffectiveThrustAuthority * 100.0f)
		: FString(Unavailable);
	return FString::Printf(
		TEXT("[X] FLIGHT ASSIST  %s\n")
		TEXT("[T] THERMAL MODE   %s  (cooling %+.2f C/s)\n")
		TEXT("[L] LOAD SHED      %s  (power %+.2f kW)\n")
		TEXT("[P] PROPULSION     %s  (operator %.0f%%, deliverable %s)"),
		*Assist,
		*EnumToString(Operator.ThermalMode),
		Operator.OperatorDissipationDegreesCelsiusPerSecond,
		Operator.LoadShedMode == EEdenLoadShedMode::Shed ? TEXT("ON ") : TEXT("OFF"),
		Operator.OperatorDemandKilowatts,
		*EnumToString(Operator.PropulsionPriority),
		Operator.ThrustAuthority * 100.0f,
		*Deliverable);
}

FString UEdenOperatorHudWidget::FormatMissionPanel(const FEdenOperatorHudSnapshot& Snapshot)
{
	using namespace EdenOperatorHudWidgetPrivate;

	if (!Snapshot.bHasMission)
	{
		return TEXT("--  mission system unavailable");
	}
	if (Snapshot.MissionState == EEdenMissionState::Inactive)
	{
		return TEXT("NO ACTIVE MISSION  (console: StartMission)");
	}

	FString Text = FString::Printf(
		TEXT("%s  %s  phase %s  t=%.1f s"),
		*Snapshot.MissionId.ToString(),
		*EnumToString(Snapshot.MissionState).ToUpper(),
		*EnumToString(Snapshot.MissionPhase).ToUpper(),
		Snapshot.MissionElapsedTimeSeconds);
	for (const FEdenMissionObjectiveRuntime& Objective : Snapshot.Objectives)
	{
		Text += FString::Printf(TEXT("\n  %-26s %s"), *Objective.ObjectiveId.ToString(), *EnumToString(Objective.State));
	}
	for (const FEdenAlert& Alert : Snapshot.ActiveAlerts)
	{
		Text += FString::Printf(TEXT("\n  ALERT [%s] %s"), *EnumToString(Alert.Severity), *Alert.DisplayText.ToString());
	}
	return Text;
}

FString UEdenOperatorHudWidget::FormatEdenPanel(const FEdenOperatorHudSnapshot& Snapshot)
{
	using namespace EdenOperatorHudWidgetPrivate;

	if (!Snapshot.bHasEdenAdapter)
	{
		return TEXT("--  EDEN OS adapter unavailable");
	}

	const FEdenOsConnectionSnapshot& Connection = Snapshot.EdenConnection;
	FString Text = FString::Printf(
		TEXT("LINK       %s\n")
		TEXT("AUTHORITY  %s  -  command execution %s"),
		*ConnectionText(Connection),
		*EnumToString(Connection.AuthorityMode),
		Connection.bExternalCommandExecutionEnabled ? TEXT("ENABLED") : TEXT("OFF"));
	if (Snapshot.bHasAdvisory)
	{
		Text += FString::Printf(
			TEXT("\nADVISORY   %s\n           %s  (t=%.1f s)"),
			*Snapshot.AdvisoryRecommendation,
			*Snapshot.AdvisoryRationale,
			Snapshot.AdvisoryIssuedSimulationTimeSeconds);
	}
	else
	{
		Text += TEXT("\nADVISORY   none received");
	}
	return Text;
}

int32 UEdenOperatorHudWidget::GetBuiltPanelCount() const
{
	int32 Count = 0;
	for (const FPanel* Panel : {&FlightPanel, &SystemsPanel, &OperatorPanel, &MissionPanel, &EdenPanel})
	{
		Count += Panel->Border && Panel->Border->GetCachedWidget().IsValid() ? 1 : 0;
	}
	return Count;
}

FString UEdenOperatorHudWidget::DescribeLayoutForDiagnostics() const
{
	const TSharedPtr<SWidget> Cached = GetCachedWidget();
	const FVector2D Size = GetCachedGeometry().GetLocalSize();
	const FVector2D FlightSize = FlightPanel.Border ? FlightPanel.Border->GetDesiredSize() : FVector2D::ZeroVector;
	return FString::Printf(
		TEXT("root=%s rootIsHudCanvas=%s slateBuilt=%s slateParent=%s inViewport=%s visibility=%s panelsBuilt=%d/5 hudSize=%.0fx%.0f flightPanelDesired=%.0fx%.0f feedback=\"%s\""),
		WidgetTree && WidgetTree->RootWidget ? *WidgetTree->RootWidget->GetName() : TEXT("none"),
		WidgetTree && RootCanvas && WidgetTree->RootWidget == RootCanvas ? TEXT("true") : TEXT("false"),
		Cached.IsValid() ? TEXT("true") : TEXT("false"),
		Cached.IsValid() && Cached->GetParentWidget().IsValid() ? TEXT("true") : TEXT("false"),
		IsInViewport() ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(GetVisibility()),
		GetBuiltPanelCount(),
		Size.X,
		Size.Y,
		FlightSize.X,
		FlightSize.Y,
		*GetControlFeedbackText().ToString());
}
