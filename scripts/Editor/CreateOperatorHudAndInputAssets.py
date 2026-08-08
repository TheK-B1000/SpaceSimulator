# Create operator Input Actions, bind them on IMC_Flight, create WBP_EdenOperatorHud,
# and assign references on BP_EdenFlightPlayerController.
# Run via UnrealEditor-Cmd -ExecutePythonScript=...

import unreal

EDITOR_ASSET_LIBRARY = unreal.EditorAssetLibrary
BLUEPRINT_EDITOR_LIBRARY = unreal.BlueprintEditorLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

INPUT_DIR = "/Game/Eden/Input"
UI_DIR = "/Game/Eden/UI"
IMC_PATH = "/Game/Eden/Input/IMC_Flight"
CONTROLLER_BP_PATH = "/Game/Eden/Blueprints/BP_EdenFlightPlayerController"
WBP_PATH = "/Game/Eden/UI/WBP_EdenOperatorHud"

OPERATOR_ACTIONS = [
    ("IA_ThermalMode", "T"),
    ("IA_LoadShed", "L"),
    ("IA_PropulsionPriority", "P"),
]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def ensure_dir(path):
    if not EDITOR_ASSET_LIBRARY.does_directory_exist(path):
        EDITOR_ASSET_LIBRARY.make_directory(path)


def load_or_create_boolean_action(name):
    path = "{}/{}".format(INPUT_DIR, name)
    if EDITOR_ASSET_LIBRARY.does_asset_exist(path):
        action = EDITOR_ASSET_LIBRARY.load_asset(path)
        unreal.log("Loaded existing {}".format(path))
    else:
        # InputActionFactory is not always exposed to Python; duplicate a known Boolean action.
        template_path = "/Game/Eden/Input/IA_FlightStabilize"
        require(EDITOR_ASSET_LIBRARY.does_asset_exist(template_path), "Missing template {}".format(template_path))
        duplicated = EDITOR_ASSET_LIBRARY.duplicate_asset(template_path, path)
        require(duplicated is not None, "Failed to duplicate {} -> {}".format(template_path, path))
        action = EDITOR_ASSET_LIBRARY.load_asset(path)
        require(action is not None, "Failed to load newly created {}".format(path))
        unreal.log("Created {} by duplicating IA_FlightStabilize".format(path))

    action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    EDITOR_ASSET_LIBRARY.save_asset(path, only_if_is_dirty=False)
    return action


def key_to_text(key_value):
    return key_value.export_text()


def get_imc_mappings(imc):
    mapping_data = imc.get_editor_property("default_key_mappings")
    return list(mapping_data.get_editor_property("mappings")), mapping_data


def find_mapping(mappings, action_name, key_name):
    for mapping in mappings:
        action = mapping.get_editor_property("action")
        if action and action.get_name() == action_name and key_to_text(mapping.get_editor_property("key")) == key_name:
            return mapping
    return None


def make_key(key_name):
    key = unreal.Key()
    key.import_text(key_name)
    require(key_to_text(key) == key_name, "Failed to construct key '{}' (got '{}')".format(key_name, key_to_text(key)))
    return key


def ensure_operator_mappings(imc, actions_by_name):
    mappings, _mapping_data = get_imc_mappings(imc)
    for action_name, key_name in OPERATOR_ACTIONS:
        action = actions_by_name[action_name]
        if find_mapping(mappings, action_name, key_name):
            unreal.log("IMC already maps {} -> {}".format(action_name, key_name))
            continue

        key = make_key(key_name)
        require(hasattr(imc, "map_key"), "InputMappingContext.map_key unavailable")
        imc.map_key(action, key)
        unreal.log("Mapped {} -> {} via map_key".format(action_name, key_name))

        mappings, _ = get_imc_mappings(imc)
        require(
            find_mapping(mappings, action_name, key_name) is not None,
            "Failed to persist IMC mapping {} -> {}".format(action_name, key_name),
        )


def create_or_load_operator_hud_widget():
    ensure_dir(UI_DIR)
    parent_class = unreal.EdenOperatorHudWidget
    require(parent_class is not None, "EdenOperatorHudWidget class missing; build editor target first")

    if EDITOR_ASSET_LIBRARY.does_asset_exist(WBP_PATH):
        widget_bp = EDITOR_ASSET_LIBRARY.load_asset(WBP_PATH)
        unreal.log("Loaded existing {}".format(WBP_PATH))
    else:
        factory = unreal.WidgetBlueprintFactory()
        factory.set_editor_property("ParentClass", parent_class)
        widget_bp = ASSET_TOOLS.create_asset(
            "WBP_EdenOperatorHud",
            UI_DIR,
            unreal.WidgetBlueprint,
            factory,
        )
        require(widget_bp is not None, "Failed to create {}".format(WBP_PATH))
        unreal.log("Created {}".format(WBP_PATH))

    generated = BLUEPRINT_EDITOR_LIBRARY.generated_class(widget_bp)
    require(generated is not None, "WBP_EdenOperatorHud missing generated class")
    parent_ok = False
    try:
        parent_ok = unreal.MathLibrary.class_is_child_of(generated, parent_class)
    except Exception:
        parent_ok = "EdenOperatorHudWidget" in str(generated.get_super_class()) if hasattr(generated, "get_super_class") else False
        if not parent_ok:
            # Fallback: generated Blueprint class name is expected.
            parent_ok = generated.get_name().startswith("WBP_EdenOperatorHud_C")
    require(parent_ok, "WBP_EdenOperatorHud parent must be EdenOperatorHudWidget")
    EDITOR_ASSET_LIBRARY.save_asset(WBP_PATH, only_if_is_dirty=False)
    return widget_bp, generated


def assign_controller_references(actions_by_name, widget_class):
    controller_bp = EDITOR_ASSET_LIBRARY.load_asset(CONTROLLER_BP_PATH)
    require(controller_bp is not None, "Missing {}".format(CONTROLLER_BP_PATH))
    cdo = unreal.get_default_object(BLUEPRINT_EDITOR_LIBRARY.generated_class(controller_bp))

    property_aliases = {
        "ThermalModeAction": ("ThermalModeAction", "thermal_mode_action"),
        "LoadShedAction": ("LoadShedAction", "load_shed_action"),
        "PropulsionPriorityAction": ("PropulsionPriorityAction", "propulsion_priority_action"),
        "OperatorHudWidgetClass": ("OperatorHudWidgetClass", "operator_hud_widget_class"),
    }
    values = {
        "ThermalModeAction": actions_by_name["IA_ThermalMode"],
        "LoadShedAction": actions_by_name["IA_LoadShed"],
        "PropulsionPriorityAction": actions_by_name["IA_PropulsionPriority"],
        "OperatorHudWidgetClass": widget_class,
    }

    for logical_name, value in values.items():
        set_ok = False
        for candidate in property_aliases[logical_name]:
            try:
                cdo.set_editor_property(candidate, value)
                set_ok = True
                break
            except Exception:
                continue
        require(set_ok, "Failed to set {} on BP_EdenFlightPlayerController".format(logical_name))

        read_value = None
        for candidate in property_aliases[logical_name]:
            try:
                read_value = cdo.get_editor_property(candidate)
                break
            except Exception:
                continue
        require(read_value == value, "Failed to assign {} on BP_EdenFlightPlayerController".format(logical_name))

    EDITOR_ASSET_LIBRARY.save_asset(CONTROLLER_BP_PATH, only_if_is_dirty=False)
    unreal.log("Assigned operator input actions and HUD class on BP_EdenFlightPlayerController")


def main():
    ensure_dir(INPUT_DIR)
    actions_by_name = {}
    for action_name, _key in OPERATOR_ACTIONS:
        actions_by_name[action_name] = load_or_create_boolean_action(action_name)

    imc = EDITOR_ASSET_LIBRARY.load_asset(IMC_PATH)
    require(imc is not None, "Missing {}".format(IMC_PATH))
    ensure_operator_mappings(imc, actions_by_name)
    EDITOR_ASSET_LIBRARY.save_asset(IMC_PATH, only_if_is_dirty=False)

    _widget_bp, widget_class = create_or_load_operator_hud_widget()
    assign_controller_references(actions_by_name, widget_class)
    unreal.log("Operator HUD and input content wiring completed successfully.")


if __name__ == "__main__":
    main()
