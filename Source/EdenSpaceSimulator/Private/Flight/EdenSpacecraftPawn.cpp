// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenSpacecraftPawn.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/EdenLogCategories.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Engine/CollisionProfile.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenFlightTypes.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"

#if !UE_BUILD_SHIPPING
#include "DisplayDebugHelpers.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"
#include "UObject/UnrealType.h"
#endif

#if !UE_BUILD_SHIPPING
namespace EdenSpacecraftPawnDebug
{
const FName EdenSystemsDebugName(TEXT("EdenSystems"));

FString BoolText(bool bValue)
{
	return bValue ? TEXT("true") : TEXT("false");
}

template <typename TEnum>
FString EnumText(TEnum Value)
{
	return UEnum::GetValueAsString(Value);
}

void DrawLine(UCanvas* Canvas, const FString& Text, const FColor& Color)
{
	if (!Canvas)
	{
		return;
	}

	Canvas->DisplayDebugManager.SetDrawColor(Color);
	Canvas->DisplayDebugManager.DrawString(Text);
}

void DrawEdenSystems(UCanvas* Canvas, const FEdenSpacecraftSystemsDebugSnapshot& Snapshot)
{
	DrawLine(Canvas, TEXT("Eden Systems"), FColor::Green);
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Clock: Available=%s Elapsed=%.2fs FixedStep=%.3fs Paused=%s Subscribers=%d LastDroppedSteps=%d"),
			*BoolText(Snapshot.Clock.bClockAvailable),
			Snapshot.Clock.ElapsedSimulationTimeSeconds,
			Snapshot.Clock.FixedStepSeconds,
			*BoolText(Snapshot.Clock.bPaused),
			Snapshot.Clock.SubscriberCount,
			Snapshot.Clock.LastDroppedSteps),
		FColor::White);
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Fuel: Component=%s %.2f / %.2f kg (%.1f%%) Demand=%.2f State=%s ConfigValid=%s ClockRegistered=%s"),
			*BoolText(Snapshot.Fuel.bComponentAvailable),
			Snapshot.Fuel.FuelQuantityKilograms,
			Snapshot.Fuel.CapacityKilograms,
			Snapshot.Fuel.FuelPercent,
			Snapshot.Fuel.PropulsionDemandNormalized,
			*EnumText(Snapshot.Fuel.FuelState),
			*BoolText(Snapshot.Fuel.bConfigurationValid),
			*BoolText(Snapshot.Fuel.bRegisteredWithClock)),
		FColor::Cyan);
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Power: Component=%s %.3f / %.3f kWh (%.1f%%) Gen=%.2f kW Demand=%.2f kW Net=%.2f kW State=%s ConfigValid=%s ClockRegistered=%s"),
			*BoolText(Snapshot.Power.bComponentAvailable),
			Snapshot.Power.BatteryChargeKilowattHours,
			Snapshot.Power.BatteryCapacityKilowattHours,
			Snapshot.Power.ChargePercent,
			Snapshot.Power.GenerationKilowatts,
			Snapshot.Power.DemandKilowatts,
			Snapshot.Power.NetPowerKilowatts,
			*EnumText(Snapshot.Power.PowerState),
			*BoolText(Snapshot.Power.bConfigurationValid),
			*BoolText(Snapshot.Power.bRegisteredWithClock)),
		FColor::Yellow);
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Thermal: Component=%s %.2f C Ambient=%.2f C Heat=%.2f C/s Dissipation=%.2f C/s State=%s ConfigValid=%s ClockRegistered=%s"),
			*BoolText(Snapshot.Thermal.bComponentAvailable),
			Snapshot.Thermal.TemperatureCelsius,
			Snapshot.Thermal.AmbientTemperatureCelsius,
			Snapshot.Thermal.HeatGenerationDegreesCelsiusPerSecond,
			Snapshot.Thermal.DissipationDegreesCelsiusPerSecond,
			*EnumText(Snapshot.Thermal.ThermalState),
			*BoolText(Snapshot.Thermal.bConfigurationValid),
			*BoolText(Snapshot.Thermal.bRegisteredWithClock)),
		FColor::Orange);
}

void DrawFlightDynamics(UCanvas* Canvas, const FEdenFlightDynamicsSnapshot& Flight)
{
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Flight: Speed=%.2f m/s Vel=(%.2f, %.2f, %.2f) m/s Accel=%.2f m/s2 Force=(%.1f, %.1f, %.1f) kN Mass=%.0f kg"),
			Flight.SpeedMetersPerSecond,
			Flight.VelocityWorldMetersPerSecond.X,
			Flight.VelocityWorldMetersPerSecond.Y,
			Flight.VelocityWorldMetersPerSecond.Z,
			Flight.AccelerationWorldMetersPerSecondSquared.Size(),
			Flight.AppliedBodyForceNewtons.X / 1000.0,
			Flight.AppliedBodyForceNewtons.Y / 1000.0,
			Flight.AppliedBodyForceNewtons.Z / 1000.0,
			Flight.MassKilograms),
		FColor::Magenta);
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Flight: Assist Requested=%s Active=%s Authority=%.2f Thrusters=%.1f kN Available+=(%.0f, %.0f, %.0f) kN SimTime=%.2fs Steps=%lld Dropped=%lld"),
			*BoolText(Flight.bFlightAssistRequested),
			*BoolText(Flight.bFlightAssistActive),
			Flight.EffectiveThrustAuthority,
			Flight.DeliveredThrusterOutputNewtons / 1000.0,
			Flight.AvailablePositiveThrustNewtons.X / 1000.0,
			Flight.AvailablePositiveThrustNewtons.Y / 1000.0,
			Flight.AvailablePositiveThrustNewtons.Z / 1000.0,
			Flight.FlightSimulationTimeSeconds,
			Flight.FlightStepCount,
			Flight.DroppedFlightStepCount),
		FColor::Magenta);
	const FRotator Orientation = Flight.OrientationWorld.Rotator();
	const FVector RateDegrees(
		FMath::RadiansToDegrees(Flight.AngularVelocityBodyRadiansPerSecond.X),
		FMath::RadiansToDegrees(Flight.AngularVelocityBodyRadiansPerSecond.Y),
		FMath::RadiansToDegrees(Flight.AngularVelocityBodyRadiansPerSecond.Z));
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Attitude: P=%.1f Y=%.1f R=%.1f deg  Rate(body X,Y,Z)=(%.2f, %.2f, %.2f) deg/s  Torque=(%.1f, %.1f, %.1f) kN*m of +/-(%.0f, %.0f, %.0f)  |L|=%.0f kg*m^2/s"),
			Orientation.Pitch,
			Orientation.Yaw,
			Orientation.Roll,
			RateDegrees.X,
			RateDegrees.Y,
			RateDegrees.Z,
			Flight.AppliedBodyTorqueNewtonMeters.X / 1000.0,
			Flight.AppliedBodyTorqueNewtonMeters.Y / 1000.0,
			Flight.AppliedBodyTorqueNewtonMeters.Z / 1000.0,
			Flight.AvailableTorqueNewtonMeters.X / 1000.0,
			Flight.AvailableTorqueNewtonMeters.Y / 1000.0,
			Flight.AvailableTorqueNewtonMeters.Z / 1000.0,
			Flight.AngularMomentumWorld.Size()),
		FColor::Magenta);
	DrawLine(
		Canvas,
		FString::Printf(
			TEXT("Propulsion: Requested=%.1f kN Delivered=%.1f kN Power=%.2f Burn=%.2f Flow=%.4f kg/s Propellant=%.3f kg Mass=%.1f kg Metered=%s Limited(power=%s propellant=%s)"),
			Flight.RequestedThrusterOutputNewtons / 1000.0,
			Flight.DeliveredThrusterOutputNewtons / 1000.0,
			Flight.PowerThrustFraction,
			Flight.PropellantBurnFraction,
			Flight.PropellantFlowKilogramsPerSecond,
			Flight.PropellantMassKilograms,
			Flight.MassKilograms,
			*BoolText(Flight.bPropellantMetered),
			*BoolText(Flight.bPowerLimited),
			*BoolText(Flight.bPropellantLimited)),
		FColor::Magenta);
}
}
#endif

AEdenSpacecraftPawn::AEdenSpacecraftPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	RequiredCollisionRoot = CreateDefaultSubobject<USphereComponent>(TEXT("RequiredCollisionRoot"));
	SetRootComponent(RequiredCollisionRoot);
	RequiredCollisionRoot->InitSphereRadius(100.0f);
	RequiredCollisionRoot->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	RequiredCollisionRoot->SetMobility(EComponentMobility::Movable);
	RequiredCollisionRoot->SetGenerateOverlapEvents(false);

	FlightMovementComponent = CreateDefaultSubobject<UEdenFlightMovementComponent>(TEXT("FlightMovementComponent"));
	FlightMovementComponent->SetUpdatedComponent(RequiredCollisionRoot);

	FuelSystemComponent = CreateDefaultSubobject<UEdenFuelSystemComponent>(TEXT("FuelSystem"));
	PowerSystemComponent = CreateDefaultSubobject<UEdenPowerSystemComponent>(TEXT("PowerSystem"));
	ThermalSystemComponent = CreateDefaultSubobject<UEdenThermalSystemComponent>(TEXT("ThermalSystem"));
	OperatorControlComponent = CreateDefaultSubobject<UEdenOperatorControlComponent>(TEXT("OperatorControl"));
}

UPawnMovementComponent* AEdenSpacecraftPawn::GetMovementComponent() const
{
	return FlightMovementComponent;
}

void AEdenSpacecraftPawn::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	RestoreRequiredCollisionRoot();

	if (FlightMovementComponent)
	{
		FlightMovementComponent->SetUpdatedComponent(RequiredCollisionRoot);
		FlightMovementComponent->ResetFlightMovement();
	}
}

#if !UE_BUILD_SHIPPING
void AEdenSpacecraftPawn::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

	if (!Canvas || !DebugDisplay.IsDisplayOn(EdenSpacecraftPawnDebug::EdenSystemsDebugName))
	{
		return;
	}

	EdenSpacecraftPawnDebug::DrawEdenSystems(Canvas, GetEdenSystemsDebugSnapshot());
	if (FlightMovementComponent)
	{
		EdenSpacecraftPawnDebug::DrawFlightDynamics(Canvas, FlightMovementComponent->GetFlightDynamicsSnapshot());
	}
	YL = Canvas->DisplayDebugManager.GetMaxCharHeight();
	YPos = Canvas->DisplayDebugManager.GetYPos();
}
#endif

void AEdenSpacecraftPawn::BeginPlay()
{
	Super::BeginPlay();

	// Every component has begun play now: apply operator modifiers deferred by startup ordering
	// (possession can reset operator control before power/thermal initialize).
	if (OperatorControlComponent)
	{
		OperatorControlComponent->ApplyPendingIntent();
	}

	ApplyPresentationHull();
	// BP instances may have serialized bEnableSpaceBackdrop=false from the crash-hardening pass.
	// Always apply the light sky path so PIE is not a black void.
	ApplySpaceBackdrop();
	if (bEnableThrusterEffects)
	{
		ApplyThrusterPresentation();
	}
}

void AEdenSpacecraftPawn::ApplyPresentationHull()
{
	// Presentation only. The sphere root remains the movement body.
	static const TCHAR* HullPath = TEXT("/Game/StarSparrow/Meshes/Examples/SM_StarSparrow01.SM_StarSparrow01");
	UStaticMesh* Hull = LoadObject<UStaticMesh>(nullptr, HullPath);
	if (!Hull)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s could not load Star Sparrow hull (%s)."), *GetNameSafe(this), HullPath);
		return;
	}

	UStaticMeshComponent* HullMesh = nullptr;
	TArray<UStaticMeshComponent*> Meshes;
	GetComponents<UStaticMeshComponent>(Meshes);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh && Mesh->GetName().Contains(TEXT("Placeholder")))
		{
			HullMesh = Mesh;
			break;
		}
	}

	if (!HullMesh)
	{
		HullMesh = NewObject<UStaticMeshComponent>(this, TEXT("StarSparrowHull"));
		HullMesh->SetupAttachment(RequiredCollisionRoot);
		HullMesh->RegisterComponent();
	}

	const FBoxSphereBounds LocalBounds = Hull->GetBounds();
	const float LongestAxis = LocalBounds.BoxExtent.GetMax() * 2.0f;
	constexpr float TargetLengthCentimeters = 1200.0f;
	const float Scale = LongestAxis > KINDA_SMALL_NUMBER ? TargetLengthCentimeters / LongestAxis : 1.0f;

	HullMesh->SetStaticMesh(Hull);
	HullMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HullMesh->SetGenerateOverlapEvents(false);
	HullMesh->SetRelativeScale3D(FVector(Scale));
	HullMesh->SetRelativeLocation(-LocalBounds.Origin * Scale);
	HullMesh->SetRelativeRotation(FRotator::ZeroRotator);

	UE_LOG(
		LogEdenFlight,
		Log,
		TEXT("%s presentation hull SM_StarSparrow01 length %.0f cm scaled to %.0f cm (scale %.3f). Collision root unchanged."),
		*GetNameSafe(this),
		LongestAxis,
		TargetLengthCentimeters,
		Scale);
}

void AEdenSpacecraftPawn::ApplySpaceBackdrop()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UStaticMesh* DomeMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Animated_Nebulas_Starter_Pack/Planet_System/Static_Mesh/SM_Dome.SM_Dome"));
	// Parent material defaults to tiny black/white cubes. MI_Star_Nebula_01 swaps in a
	// 15359x8640 RGBA16F skybox (~4 GB decode) that OOMs D3D12 PSO creation on this machine.
	UMaterialInterface* SkyBaseMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Animated_Nebulas_Starter_Pack/Planet_System/Materials/Star_Dome/M_Star_Nebula.M_Star_Nebula"));
	UTexture* FarStars = LoadObject<UTexture>(
		nullptr,
		TEXT("/Game/Animated_Nebulas_Starter_Pack/Planet_System/Textures/T_HDRIs/TC_Stars_Far_Blue.TC_Stars_Far_Blue"));
	UTexture* NearStars = LoadObject<UTexture>(
		nullptr,
		TEXT("/Game/Animated_Nebulas_Starter_Pack/Planet_System/Textures/T_HDRIs/TC_Stars_Near_Small.TC_Stars_Near_Small"));
	UTexture* NebulaColor = LoadObject<UTexture>(
		nullptr, TEXT("/Game/Animated_Nebulas_Starter_Pack/Planet_System/Textures/T_HDRIs/TC_Nebula.TC_Nebula"));
	UTexture* NebulaAlpha = LoadObject<UTexture>(
		nullptr,
		TEXT("/Game/Animated_Nebulas_Starter_Pack/Planet_System/Textures/T_HDRIs/TC_Nebula_Alpha.TC_Nebula_Alpha"));
	UStaticMesh* PlanetMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Game/Animated_Nebulas_Starter_Pack/Planet_System/Static_Mesh/SM_Planet_Face.SM_Planet_Face"));
	UMaterialInterface* PlanetMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Animated_Nebulas_Starter_Pack/Planet_System/Materials/Planet_Face/MI_Planet_Face.MI_Planet_Face"));

	if (!DomeMesh || !SkyBaseMaterial)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s could not load the nebula sky dome."), *GetNameSafe(this));
		return;
	}

	UMaterialInstanceDynamic* SkyMaterial = UMaterialInstanceDynamic::Create(SkyBaseMaterial, this);
	if (FarStars)
	{
		SkyMaterial->SetTextureParameterValue(TEXT("T_Stars_Far"), FarStars);
	}
	if (NearStars)
	{
		SkyMaterial->SetTextureParameterValue(TEXT("T_Stars_Near"), NearStars);
	}
	if (NebulaColor)
	{
		SkyMaterial->SetTextureParameterValue(TEXT("T_Texture"), NebulaColor);
	}
	if (NebulaAlpha)
	{
		SkyMaterial->SetTextureParameterValue(TEXT("T_Alpha_Extra_Cubemap"), NebulaAlpha);
	}
	SkyMaterial->SetScalarParameterValue(TEXT("Near_Stars_intensity"), 2.5f);
	SkyMaterial->SetScalarParameterValue(TEXT("Far_Stars_intensity"), 1.8f);

	const FVector ShipLocation = GetActorLocation();

	// Sky follows the ship in translation only. Absolute rotation keeps the starfield
	// from spinning with the hull. A StaticMeshActor cannot attach to a movable root.
	UStaticMeshComponent* SkyMesh = NewObject<UStaticMeshComponent>(this, TEXT("NebulaSkyDome"));
	SkyMesh->SetMobility(EComponentMobility::Movable);
	SkyMesh->SetupAttachment(RequiredCollisionRoot);
	SkyMesh->SetAbsolute(/*bNewAbsoluteLocation=*/false, /*bNewAbsoluteRotation=*/true, /*bNewAbsoluteScale=*/true);
	SkyMesh->SetStaticMesh(DomeMesh);
	SkyMesh->SetMaterial(0, SkyMaterial);
	SkyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkyMesh->SetGenerateOverlapEvents(false);
	SkyMesh->SetCastShadow(false);
	SkyMesh->SetRelativeLocation(FVector::ZeroVector);
	SkyMesh->SetWorldRotation(FRotator::ZeroRotator);
	{
		const float Extent = DomeMesh->GetBounds().BoxExtent.GetMax();
		constexpr float SkyRadiusCentimeters = 2000000.0f;
		const float SkyScale = Extent > KINDA_SMALL_NUMBER ? SkyRadiusCentimeters / Extent : 1.0f;
		SkyMesh->SetWorldScale3D(FVector(SkyScale));
	}
	SkyMesh->RegisterComponent();

	if (PlanetMesh && PlanetMaterial)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;

		const FVector PlanetLocation = ShipLocation + FVector(800000.0f, 0.0f, 0.0f);
		AStaticMeshActor* Planet = World->SpawnActor<AStaticMeshActor>(
			PlanetLocation, (ShipLocation - PlanetLocation).Rotation(), Params);
		if (Planet && Planet->GetStaticMeshComponent())
		{
			UStaticMeshComponent* PlanetComponent = Planet->GetStaticMeshComponent();
			PlanetComponent->SetMobility(EComponentMobility::Movable);
			PlanetComponent->SetStaticMesh(PlanetMesh);
			PlanetComponent->SetMaterial(0, PlanetMaterial);
			PlanetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PlanetComponent->SetGenerateOverlapEvents(false);

			const float Extent = PlanetMesh->GetBounds().BoxExtent.GetMax();
			constexpr float PlanetRadiusCentimeters = 120000.0f;
			const float PlanetScale = Extent > KINDA_SMALL_NUMBER ? PlanetRadiusCentimeters / Extent : 1.0f;
			Planet->SetActorScale3D(FVector(PlanetScale));
			Planet->SetActorEnableCollision(false);
		}
	}
}

void AEdenSpacecraftPawn::ApplyThrusterPresentation()
{
	UNiagaraSystem* ThrusterSystem = LoadObject<UNiagaraSystem>(
		nullptr,
		TEXT("/Game/Arghanion_Flight_System/VFX/Thruster/NiagaraSystems/NS_Thruster.NS_Thruster"));
	if (!ThrusterSystem)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s could not load NS_Thruster."), *GetNameSafe(this));
		return;
	}

	MainThrusterEffect = NewObject<UNiagaraComponent>(this, TEXT("MainThrusterEffect"));
	MainThrusterEffect->SetAsset(ThrusterSystem);
	MainThrusterEffect->SetupAttachment(RequiredCollisionRoot);
	MainThrusterEffect->SetRelativeLocation(FVector(-750.0f, 0.0f, 0.0f));
	MainThrusterEffect->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));
	MainThrusterEffect->SetRelativeScale3D(FVector(3.0f));
	MainThrusterEffect->SetAutoActivate(false);
	MainThrusterEffect->RegisterComponent();

	PresentationCamera = FindComponentByClass<UCameraComponent>();
	if (PresentationCamera)
	{
		PresentationBaseFieldOfView = PresentationCamera->FieldOfView;
	}
}

void AEdenSpacecraftPawn::UpdateThrusterPresentation()
{
	const float Demand = FlightMovementComponent ? FlightMovementComponent->GetPropulsionDemandNormalized() : 0.0f;
	const bool bFiring = Demand > 0.02f;

	if (MainThrusterEffect)
	{
		MainThrusterEffect->SetVariableFloat(TEXT("User.Thrust"), Demand);
		MainThrusterEffect->SetVariableFloat(TEXT("User.Thruster_Power"), Demand);
		MainThrusterEffect->SetVariableFloat(TEXT("User.Thruster_Heat"), Demand);
		MainThrusterEffect->SetVariableFloat(TEXT("User.Thruster_Emissive"), Demand);
		MainThrusterEffect->SetVariableFloat(TEXT("User.Thruster_Spawn_Rate"), bFiring ? Demand * 80.0f : 0.0f);
		MainThrusterEffect->SetVariableFloat(TEXT("User.GroundProximity"), 0.0f);

		if (bFiring)
		{
			if (!MainThrusterEffect->IsActive())
			{
				MainThrusterEffect->Activate(true);
			}
		}
		else if (MainThrusterEffect->IsActive())
		{
			MainThrusterEffect->DeactivateImmediate();
		}
	}

	if (PresentationCamera)
	{
		PresentationCamera->SetFieldOfView(PresentationBaseFieldOfView + Demand * 8.0f);
	}
}

void AEdenSpacecraftPawn::ApplyFlightInputCommand(const FEdenFlightInputCommand& Command, float DeltaTimeSeconds)
{
	if (!FlightMovementComponent)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s cannot apply flight command without a movement component."), *GetNameSafe(this));
		return;
	}

	RestoreRequiredCollisionRoot();
	FlightMovementComponent->MoveWithCommand(Command, DeltaTimeSeconds);
	UpdateThrusterPresentation();
}

void AEdenSpacecraftPawn::ResetFlightState()
{
	if (FlightMovementComponent)
	{
		FlightMovementComponent->ResetFlightMovement();
	}

	if (OperatorControlComponent)
	{
		OperatorControlComponent->ResetOperatorControl();
	}
}

USphereComponent* AEdenSpacecraftPawn::GetRequiredCollisionRoot() const
{
	return RequiredCollisionRoot;
}

UEdenFlightMovementComponent* AEdenSpacecraftPawn::GetFlightMovementComponent() const
{
	return FlightMovementComponent;
}

UEdenFuelSystemComponent* AEdenSpacecraftPawn::GetFuelSystemComponent() const
{
	return FuelSystemComponent;
}

UEdenPowerSystemComponent* AEdenSpacecraftPawn::GetPowerSystemComponent() const
{
	return PowerSystemComponent;
}

UEdenThermalSystemComponent* AEdenSpacecraftPawn::GetThermalSystemComponent() const
{
	return ThermalSystemComponent;
}

UEdenOperatorControlComponent* AEdenSpacecraftPawn::GetOperatorControlComponent() const
{
	return OperatorControlComponent;
}

FEdenSpacecraftSystemsDebugSnapshot AEdenSpacecraftPawn::GetEdenSystemsDebugSnapshot() const
{
	FEdenSpacecraftSystemsDebugSnapshot Snapshot;

	if (const UWorld* World = GetWorld())
	{
		if (const UEdenSimulationClockSubsystem* SimulationClock = World->GetSubsystem<UEdenSimulationClockSubsystem>())
		{
			Snapshot.Clock = SimulationClock->GetSimulationClockDebugSnapshot();
		}
	}

	if (FuelSystemComponent)
	{
		Snapshot.Fuel = FuelSystemComponent->GetFuelDebugSnapshot();
	}

	if (PowerSystemComponent)
	{
		Snapshot.Power = PowerSystemComponent->GetPowerDebugSnapshot();
	}

	if (ThermalSystemComponent)
	{
		Snapshot.Thermal = ThermalSystemComponent->GetThermalDebugSnapshot();
	}

	return Snapshot;
}

void AEdenSpacecraftPawn::RestoreRequiredCollisionRoot()
{
	if (!RequiredCollisionRoot)
	{
		UE_LOG(LogEdenFlight, Error, TEXT("%s is missing the required C++ sphere collision root."), *GetNameSafe(this));
		return;
	}

	if (GetRootComponent() != RequiredCollisionRoot)
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s restored the required C++ sphere collision root. Blueprint composition may tune it but may not replace it."),
			*GetNameSafe(this));
		SetRootComponent(RequiredCollisionRoot);
	}
}
