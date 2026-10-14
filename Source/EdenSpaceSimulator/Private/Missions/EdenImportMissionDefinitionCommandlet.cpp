// Copyright Epic Games, Inc. All Rights Reserved.

#include "Missions/EdenImportMissionDefinitionCommandlet.h"

#include "Core/EdenLogCategories.h"
#include "Missions/EdenMissionDefinitionDataAsset.h"
#include "Missions/EdenMissionModel.h"

#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

UEdenImportMissionDefinitionCommandlet::UEdenImportMissionDefinitionCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UEdenImportMissionDefinitionCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	FString JsonPath;
	FString AssetPath;
	if (!FParse::Value(*Params, TEXT("Json="), JsonPath) || !FParse::Value(*Params, TEXT("Asset="), AssetPath))
	{
		UE_LOG(LogEdenMission, Error, TEXT("Usage: -run=EdenImportMissionDefinition -Json=<file.json> -Asset=/Game/Path/DA_Name"));
		return 1;
	}
	if (FPaths::IsRelative(JsonPath))
	{
		JsonPath = FPaths::Combine(FPaths::ProjectDir(), JsonPath);
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
	{
		UE_LOG(LogEdenMission, Error, TEXT("Cannot read mission JSON '%s'."), *JsonPath);
		return 2;
	}

	FEdenMissionDefinitionConfig Definition;
	FText FailReason;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(JsonText, &Definition, 0, 0, /*bStrictMode*/ false, &FailReason))
	{
		UE_LOG(LogEdenMission, Error, TEXT("Mission JSON '%s' does not match FEdenMissionDefinitionConfig: %s"), *JsonPath, *FailReason.ToString());
		return 3;
	}

	TArray<FString> Errors;
	if (!FEdenMissionModel::ValidateDefinition(Definition, &Errors))
	{
		for (const FString& Error : Errors)
		{
			UE_LOG(LogEdenMission, Error, TEXT("Mission '%s' is invalid: %s"), *Definition.MissionId.ToString(), *Error);
		}
		return 4;
	}

	if (!FPackageName::IsValidLongPackageName(AssetPath))
	{
		UE_LOG(LogEdenMission, Error, TEXT("'%s' is not a valid long package name (expected /Game/...)."), *AssetPath);
		return 5;
	}
	const FString AssetName = FPackageName::GetShortName(AssetPath);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);

	// Overwrite in place when the asset exists, so references to it stay valid.
	UEdenMissionDefinitionDataAsset* Asset = LoadObject<UEdenMissionDefinitionDataAsset>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	UPackage* Package = Asset ? Asset->GetOutermost() : CreatePackage(*AssetPath);
	if (!Asset)
	{
		Asset = NewObject<UEdenMissionDefinitionDataAsset>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	}
	Asset->MissionDefinition = Definition;
	Package->MarkPackageDirty();

	const FString Filename = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	if (!UPackage::SavePackage(Package, Asset, *Filename, SaveArgs))
	{
		UE_LOG(LogEdenMission, Error, TEXT("Failed to save '%s'."), *Filename);
		return 6;
	}

	UE_LOG(
		LogEdenMission,
		Display,
		TEXT("Imported mission '%s' (%d events, %d objectives) from %s into %s."),
		*Definition.MissionId.ToString(),
		Definition.Events.Num(),
		Definition.Objectives.Num(),
		*JsonPath,
		*Filename);
	return 0;
#else
	UE_LOG(LogEdenMission, Error, TEXT("EdenImportMissionDefinition requires an editor build."));
	return 1;
#endif
}
