# Create operator control Data Asset and operator HUD placeholder documentation.
# Run via UnrealEditor-Cmd -ExecutePythonScript=...

import unreal

ASSET_PATH = "/Game/Eden/Data/Operations/DA_EdenOperatorControlConfig"
PACKAGE_PATH = "/Game/Eden/Data/Operations"
ASSET_NAME = "DA_EdenOperatorControlConfig"


def ensure_dir(path: str) -> None:
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def main() -> None:
    ensure_dir(PACKAGE_PATH)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("DataAssetClass", unreal.EdenOperatorControlConfigDataAsset)

    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
        unreal.log(f"Loaded existing {ASSET_PATH}")
    else:
        asset = asset_tools.create_asset(
            ASSET_NAME,
            PACKAGE_PATH,
            unreal.EdenOperatorControlConfigDataAsset,
            factory,
        )
        unreal.log(f"Created {ASSET_PATH}")

    if not asset:
        raise RuntimeError("Failed to create/load operator control config asset")

    config = asset.get_editor_property("OperatorControlConfig")
    config.set_editor_property("BoostDissipationDegreesCelsiusPerSecond", 1.0)
    config.set_editor_property("EmergencyDissipationDegreesCelsiusPerSecond", 2.0)
    config.set_editor_property("BoostCoolingDemandKilowatts", 1.5)
    config.set_editor_property("EmergencyCoolingDemandKilowatts", 4.5)
    config.set_editor_property("LoadShedDemandReductionKilowatts", 2.0)
    config.set_editor_property("LoadShedDissipationReductionDegreesCelsiusPerSecond", 0.4)
    config.set_editor_property("ReducedThrustAuthority", 0.5)
    asset.set_editor_property("OperatorControlConfig", config)

    unreal.EditorAssetLibrary.save_asset(ASSET_PATH)
    unreal.log("Operator control config asset verified successfully.")


if __name__ == "__main__":
    main()
