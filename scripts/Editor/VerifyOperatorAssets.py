# Verify operator HUD and input content wiring for ExecPlan 0005.
# Run via UnrealEditor-Cmd -ExecutePythonScript=...

import unreal

EDITOR_ASSET_LIBRARY = unreal.EditorAssetLibrary
BLUEPRINT_EDITOR_LIBRARY = unreal.BlueprintEditorLibrary
SUBOBJECT_SUBSYSTEM = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
SUBOBJECT_LIBRARY = unreal.SubobjectDataBlueprintFunctionLibrary

IA_THERMAL_PATH = "/Game/Eden/Input/IA_ThermalMode"
IA_LOAD_SHED_PATH = "/Game/Eden/Input/IA_LoadShed"
IA_PROPULSION_PATH = "/Game/Eden/Input/IA_PropulsionPriority"
IMC_PATH = "/Game/Eden/Input/IMC_Flight"
WBP_PATH = "/Game/Eden/UI/WBP_EdenOperatorHud"
CONTROLLER_BP_PATH = "/Game/Eden/Blueprints/BP_EdenFlightPlayerController"
PAWN_BP_PATH = "/Game/Eden/Blueprints/BP_EdenSpacecraftPawn"
OPERATOR_CONFIG_PATH = "/Game/Eden/Data/Operations/DA_EdenOperatorControlConfig"

EXPECTED_OPERATOR_MAPPINGS = {
    ("IA_ThermalMode", "T"),
    ("IA_LoadShed", "L"),
    ("IA_PropulsionPriority", "P"),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_asset(path):
    asset = EDITOR_ASSET_LIBRARY.load_asset(path)
    require(asset is not None, "Missing asset {}".format(path))
    return asset


def generated_class(blueprint):
    result = BLUEPRINT_EDITOR_LIBRARY.generated_class(blueprint)
    require(result is not None, "Missing generated class for {}".format(blueprint.get_path_name()))
    return result


def get_cdo(blueprint):
    return unreal.get_default_object(generated_class(blueprint))


def get_editor_property(obj, *candidates):
    last_error = None
    for name in candidates:
        try:
            return obj.get_editor_property(name)
        except Exception as exc:
            last_error = exc
    raise RuntimeError("None of {} found on {}. Last error: {}".format(candidates, obj, last_error))


def key_to_text(key_value):
    return key_value.export_text()


def get_imc_mappings(imc):
    mapping_data = imc.get_editor_property("default_key_mappings")
    return list(mapping_data.get_editor_property("mappings"))


def verify_input_actions(ia_thermal, ia_load_shed, ia_propulsion):
    for action, path in (
        (ia_thermal, IA_THERMAL_PATH),
        (ia_load_shed, IA_LOAD_SHED_PATH),
        (ia_propulsion, IA_PROPULSION_PATH),
    ):
        require(
            action.get_editor_property("value_type") == unreal.InputActionValueType.BOOLEAN,
            "{} must be Boolean/Digital".format(path),
        )


def verify_imc_mappings(imc, ia_thermal, ia_load_shed, ia_propulsion):
    mappings = get_imc_mappings(imc)
    seen = set()
    for mapping in mappings:
        action = mapping.get_editor_property("action")
        if action is None:
            continue
        seen.add((action.get_name(), key_to_text(mapping.get_editor_property("key"))))

    for expected in EXPECTED_OPERATOR_MAPPINGS:
        require(expected in seen, "IMC_Flight missing operator mapping {}".format(expected))

    mapped_actions = {pair[0] for pair in seen}
    require("IA_ThermalMode" in mapped_actions, "IA_ThermalMode not mapped")
    require("IA_LoadShed" in mapped_actions, "IA_LoadShed not mapped")
    require("IA_PropulsionPriority" in mapped_actions, "IA_PropulsionPriority not mapped")
    require(ia_thermal.get_name() == "IA_ThermalMode", "Thermal action name mismatch")
    require(ia_load_shed.get_name() == "IA_LoadShed", "Load-shed action name mismatch")
    require(ia_propulsion.get_name() == "IA_PropulsionPriority", "Propulsion action name mismatch")


def verify_widget(wbp):
    parent = unreal.EdenOperatorHudWidget.static_class()
    require(parent is not None, "EdenOperatorHudWidget C++ class missing")
    generated = generated_class(wbp)
    parent_ok = False
    try:
        parent_ok = unreal.MathLibrary.class_is_child_of(generated, parent)
    except Exception:
        parent_ok = generated.get_name().startswith("WBP_EdenOperatorHud_C")
    require(parent_ok, "WBP_EdenOperatorHud must derive from EdenOperatorHudWidget (got {})".format(generated.get_name()))


def soft_path_text(soft_value):
    if soft_value is None:
        return ""
    if hasattr(soft_value, "to_soft_object_path"):
        return soft_value.to_soft_object_path().export_text()
    if hasattr(soft_value, "export_text"):
        return soft_value.export_text()
    return str(soft_value)


def verify_controller(controller_bp, ia_thermal, ia_load_shed, ia_propulsion, wbp):
    cdo = get_cdo(controller_bp)
    require(
        get_editor_property(cdo, "ThermalModeAction", "thermal_mode_action") == ia_thermal,
        "BP_EdenFlightPlayerController ThermalModeAction mismatch",
    )
    require(
        get_editor_property(cdo, "LoadShedAction", "load_shed_action") == ia_load_shed,
        "BP_EdenFlightPlayerController LoadShedAction mismatch",
    )
    require(
        get_editor_property(cdo, "PropulsionPriorityAction", "propulsion_priority_action") == ia_propulsion,
        "BP_EdenFlightPlayerController PropulsionPriorityAction mismatch",
    )

    hud_class = get_editor_property(cdo, "OperatorHudWidgetClass", "operator_hud_widget_class")
    require(hud_class is not None, "BP_EdenFlightPlayerController OperatorHudWidgetClass is unset")
    require(
        hud_class == generated_class(wbp),
        "BP_EdenFlightPlayerController OperatorHudWidgetClass must be WBP_EdenOperatorHud_C",
    )


def get_subobject_object(data):
    get_associated_object = getattr(SUBOBJECT_LIBRARY, "get_associated_object", None)
    if get_associated_object:
        obj = get_associated_object(data)
        if obj:
            return obj
    return SUBOBJECT_LIBRARY.get_object(data)


def count_components_by_class(blueprint):
    counts = {}
    for handle in SUBOBJECT_SUBSYSTEM.k2_gather_subobject_data_for_blueprint(blueprint):
        data = SUBOBJECT_LIBRARY.get_data(handle)
        obj = get_subobject_object(data)
        if obj:
            class_name = obj.get_class().get_name()
            counts[class_name] = counts.get(class_name, 0) + 1
    return counts


def verify_operator_config_and_no_duplicate_authority(pawn_bp, operator_config):
    cdo = get_cdo(pawn_bp)
    operator = cdo.get_operator_control_component()
    require(operator is not None, "BP_EdenSpacecraftPawn missing OperatorControl component")

    soft = get_editor_property(operator, "OperatorControlConfigDataAsset", "operator_control_config_data_asset")
    soft_text = soft_path_text(soft)
    require(
        "DA_EdenOperatorControlConfig" in soft_text,
        "OperatorControlConfigDataAsset soft path incorrect: {}".format(soft_text),
    )

    # Soft path may be unset on CDO if only C++ ctor default applies; also accept loaded asset equality.
    loaded = None
    if hasattr(soft, "load_synchronous"):
        loaded = soft.load_synchronous()
    if loaded is not None:
        require(loaded == operator_config, "Operator config soft reference resolves to wrong asset")

    counts = count_components_by_class(pawn_bp)
    require(
        counts.get("EdenOperatorControlComponent", 0) == 1,
        "Expected exactly one EdenOperatorControlComponent, found {}".format(counts.get("EdenOperatorControlComponent", 0)),
    )
    for class_name in (
        "EdenFuelSystemComponent",
        "EdenPowerSystemComponent",
        "EdenThermalSystemComponent",
        "EdenFlightMovementComponent",
    ):
        require(
            counts.get(class_name, 0) == 1,
            "Blueprint must not duplicate authoritative {} (found {})".format(class_name, counts.get(class_name, 0)),
        )

    validate_with_unreal_data_validation(operator_config, OPERATOR_CONFIG_PATH)


def validate_with_unreal_data_validation(asset, asset_path):
    validator = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    require(validator is not None, "EditorValidatorSubsystem unavailable for {}".format(asset_path))
    validation_usecase = unreal.DataValidationUsecase.COMMANDLET
    try:
        result = validator.is_asset_valid(asset, validation_usecase)
    except TypeError:
        asset_data = EDITOR_ASSET_LIBRARY.find_asset_data(asset_path)
        result = validator.is_asset_valid(asset_data, validation_usecase)

    if isinstance(result, bool):
        require(result, "{} failed Unreal Data Validation".format(asset_path))
        return

    result_text = str(result)
    require(
        "VALID" in result_text and "INVALID" not in result_text,
        "{} failed Unreal Data Validation: {}".format(asset_path, result_text),
    )


def main():
    print("Beginning operator asset verification...")
    ia_thermal = load_asset(IA_THERMAL_PATH)
    ia_load_shed = load_asset(IA_LOAD_SHED_PATH)
    ia_propulsion = load_asset(IA_PROPULSION_PATH)
    imc = load_asset(IMC_PATH)
    wbp = load_asset(WBP_PATH)
    controller_bp = load_asset(CONTROLLER_BP_PATH)
    pawn_bp = load_asset(PAWN_BP_PATH)
    operator_config = load_asset(OPERATOR_CONFIG_PATH)

    verify_input_actions(ia_thermal, ia_load_shed, ia_propulsion)
    verify_imc_mappings(imc, ia_thermal, ia_load_shed, ia_propulsion)
    verify_widget(wbp)
    verify_controller(controller_bp, ia_thermal, ia_load_shed, ia_propulsion, wbp)
    verify_operator_config_and_no_duplicate_authority(pawn_bp, operator_config)
    print("Operator asset verification completed successfully.")


if __name__ == "__main__":
    main()
