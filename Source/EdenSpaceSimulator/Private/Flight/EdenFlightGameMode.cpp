// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightGameMode.h"

#include "Core/EdenLogCategories.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Flight/EdenFlightHUD.h"
#include "Flight/EdenFlightPlayerController.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "GameFramework/PlayerStart.h"

AEdenFlightGameMode::AEdenFlightGameMode()
{
	DefaultPawnClass = AEdenSpacecraftPawn::StaticClass();
	PlayerControllerClass = AEdenFlightPlayerController::StaticClass();
	HUDClass = AEdenFlightHUD::StaticClass();
}

void AEdenFlightGameMode::BeginPlay()
{
	Super::BeginPlay();
	EnsureFlightSandboxStation();
}

void AEdenFlightGameMode::EnsureFlightSandboxStation()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	static const TCHAR* StationMeshPath =
		TEXT("/Game/Eden/Art/Station/SM_SpaceStationModules.SM_SpaceStationModules");
	UStaticMesh* StationMesh = LoadObject<UStaticMesh>(nullptr, StationMeshPath);
	if (!StationMesh)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s could not load station mesh (%s)."), *GetNameSafe(this), StationMeshPath);
		return;
	}

	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* Actor = *It;
		if (!Actor || !Actor->GetStaticMeshComponent())
		{
			continue;
		}

		UStaticMeshComponent* MeshComponent = Actor->GetStaticMeshComponent();
		if (MeshComponent->GetStaticMesh() == StationMesh)
		{
			return;
		}

		// Editor world may still hold the placed actor with a cleared mesh after the deferred-content load.
		if (!MeshComponent->GetStaticMesh()
			&& (Actor->GetName().Contains(TEXT("SpaceStation")) || Actor->GetActorLabel().Contains(TEXT("Station"))))
		{
			MeshComponent->SetStaticMesh(StationMesh);
			UE_LOG(LogEdenFlight, Log, TEXT("%s restored station mesh on '%s'."), *GetNameSafe(this), *GetNameSafe(Actor));
			return;
		}
	}

	FVector SpawnLocation = FVector(8000.0f, 0.0f, 0.0f);
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (*It)
		{
			SpawnLocation = It->GetActorLocation() + FVector(8000.0f, 0.0f, 0.0f);
			break;
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Name = TEXT("EdenSandboxStation");

	AStaticMeshActor* Station = World->SpawnActor<AStaticMeshActor>(SpawnLocation, FRotator::ZeroRotator, Params);
	if (!Station || !Station->GetStaticMeshComponent())
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s failed to spawn sandbox station."), *GetNameSafe(this));
		return;
	}

	UStaticMeshComponent* MeshComponent = Station->GetStaticMeshComponent();
	MeshComponent->SetMobility(EComponentMobility::Static);
	MeshComponent->SetStaticMesh(StationMesh);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	const float Extent = StationMesh->GetBounds().BoxExtent.GetMax();
	constexpr float TargetRadiusCentimeters = 2500.0f;
	const float Scale = Extent > KINDA_SMALL_NUMBER ? TargetRadiusCentimeters / Extent : 1.0f;
	Station->SetActorScale3D(FVector(Scale));

	UE_LOG(
		LogEdenFlight,
		Log,
		TEXT("%s spawned sandbox station at %s (scale %.3f)."),
		*GetNameSafe(this),
		*SpawnLocation.ToCompactString(),
		Scale);
}
