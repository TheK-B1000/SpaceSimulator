# Create WBP_EdenAfterActionReview and assign on BP_EdenFlightPlayerController.
# Run via UnrealEditor-Cmd -ExecutePythonScript=...

import unreal

EDITOR_ASSET_LIBRARY = unreal.EditorAssetLibrary
BLUEPRINT_EDITOR_LIBRARY = unreal.BlueprintEditorLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

UI_DIR = "/Game/Eden/UI"
WBP_PATH = "/Game/Eden/UI/WBP_EdenAfterActionReview"
CONTROLLER_BP_PATH = "/Game/Eden/Blueprints/BP_EdenFlightPlayerController"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def ensure_dir(path):
    if not EDITOR_ASSET_LIBRARY.does_directory_exist(path):
        EDITOR_ASSET_LIBRARY.make_directory(path)


def create_or_load_widget():
    ensure_dir(UI_DIR)
    parent_class = unreal.EdenAfterActionReviewWidget
    require(parent_class is not None, "EdenAfterActionReviewWidget missing; build editor first")

    if EDITOR_ASSET_LIBRARY.does_asset_exist(WBP_PATH):
        widget_bp = EDITOR_ASSET_LIBRARY.load_asset(WBP_PATH)
        unreal.log("Loaded existing {}".format(WBP_PATH))
    else:
        factory = unreal.WidgetBlueprintFactory()
        factory.set_editor_property("ParentClass", parent_class)
        widget_bp = ASSET_TOOLS.create_asset(
            "WBP_EdenAfterActionReview",
            UI_DIR,
            unreal.WidgetBlueprint,
            factory,
        )
        require(widget_bp is not None, "Failed to create {}".format(WBP_PATH))
        unreal.log("Created {}".format(WBP_PATH))

    generated = BLUEPRINT_EDITOR_LIBRARY.generated_class(widget_bp)
    require(generated is not None, "Missing generated class for AAR widget")
    EDITOR_ASSET_LIBRARY.save_asset(WBP_PATH, only_if_is_dirty=False)
    return generated


def assign_controller(widget_class):
    controller_bp = EDITOR_ASSET_LIBRARY.load_asset(CONTROLLER_BP_PATH)
    require(controller_bp is not None, "Missing controller BP")
    cdo = unreal.get_default_object(BLUEPRINT_EDITOR_LIBRARY.generated_class(controller_bp))

    set_ok = False
    for candidate in ("AfterActionReviewWidgetClass", "after_action_review_widget_class"):
        try:
            cdo.set_editor_property(candidate, widget_class)
            set_ok = True
            break
        except Exception:
            continue
    require(set_ok, "Failed to set AfterActionReviewWidgetClass")

    read_value = None
    for candidate in ("AfterActionReviewWidgetClass", "after_action_review_widget_class"):
        try:
            read_value = cdo.get_editor_property(candidate)
            break
        except Exception:
            continue
    require(read_value == widget_class, "Controller AAR widget class assignment failed")
    EDITOR_ASSET_LIBRARY.save_asset(CONTROLLER_BP_PATH, only_if_is_dirty=False)
    unreal.log("Assigned WBP_EdenAfterActionReview on BP_EdenFlightPlayerController")


def main():
    widget_class = create_or_load_widget()
    assign_controller(widget_class)
    unreal.log("After-action review content wiring completed successfully.")


if __name__ == "__main__":
    main()
