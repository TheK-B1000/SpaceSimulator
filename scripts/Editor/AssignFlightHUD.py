import unreal

GAME_MODE_PATH = "/Game/Eden/Blueprints/BP_EdenFlightGameMode"
HUD_CLASS_PATH = "/Script/EdenSpaceSimulator.EdenFlightHUD"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


game_mode = unreal.EditorAssetLibrary.load_asset(GAME_MODE_PATH)
require(game_mode is not None, "Missing BP_EdenFlightGameMode")

hud_class = unreal.load_class(None, HUD_CLASS_PATH)
require(hud_class is not None, "Missing EdenFlightHUD class. Build the editor target before running this script.")

cdo = unreal.get_default_object(unreal.BlueprintEditorLibrary.generated_class(game_mode))
cdo.set_editor_property("hud_class", hud_class)

require(
    cdo.get_editor_property("hud_class") == hud_class,
    "Failed to assign EdenFlightHUD on BP_EdenFlightGameMode",
)

unreal.EditorAssetLibrary.save_asset(GAME_MODE_PATH, only_if_is_dirty=False)
unreal.log("Assigned EdenFlightHUD to BP_EdenFlightGameMode.")
