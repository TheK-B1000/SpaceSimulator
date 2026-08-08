import math
import unreal

EDITOR_ASSET_LIBRARY = unreal.EditorAssetLibrary

MISSION_ASSET_PATH = "/Game/Eden/Data/Missions/DA_SolarEventEmergency"
EXPECTED_MISSION_ID = "SolarCrisis"

EXPECTED_EVENT_TIMES = {
    "WarningPhaseBegin": 5.0,
    "ImpactPhaseBegin": 10.0,
    "SolarFlareHeating": 10.0,
    "AuxiliaryLoadDemand": 10.0,
    "RecoveryPhaseBegin": 30.0,
    "ClearSolarFlare": 30.0,
    "ClearAuxiliaryDemand": 30.0,
}

EXPECTED_OBJECTIVE_IDS = {
    "SurviveSolarEvent",
    "PreventOverheating",
    "RestoreBatteryCharge",
    "ConservePropellant",
}

UNSUPPORTED_COMMANDS = {
    "SET_POWER_GENERATION",
    "EdenMissionCommandType.SET_POWER_GENERATION",
    "SetPowerGeneration",
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def enum_name(value):
    text = str(value)
    if "." in text:
        return text.split(".")[-1]
    return text


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


def verify_solar_event_emergency_asset():
    require(
        EDITOR_ASSET_LIBRARY.does_asset_exist(MISSION_ASSET_PATH),
        "Missing mission asset {}".format(MISSION_ASSET_PATH),
    )

    asset = EDITOR_ASSET_LIBRARY.load_asset(MISSION_ASSET_PATH)
    require(asset is not None, "Failed to load {}".format(MISSION_ASSET_PATH))
    require(
        asset.get_class().get_name() == "EdenMissionDefinitionDataAsset",
        "{} has wrong class {}".format(MISSION_ASSET_PATH, asset.get_class().get_name()),
    )

    validate_with_unreal_data_validation(asset, MISSION_ASSET_PATH)

    definition = asset.get_editor_property("mission_definition")
    mission_id = str(definition.get_editor_property("mission_id"))
    require(mission_id == EXPECTED_MISSION_ID, "MissionId expected '{}', got '{}'".format(EXPECTED_MISSION_ID, mission_id))

    objectives = list(definition.get_editor_property("objectives"))
    require(len(objectives) > 0, "Solar Event must define objectives")
    objective_ids = set()
    for obj in objectives:
        obj_id = str(obj.get_editor_property("objective_id"))
        require(bool(obj_id), "objective_id is empty")
        require(obj_id not in objective_ids, "duplicate objective_id '{}'".format(obj_id))
        objective_ids.add(obj_id)
        target = float(obj.get_editor_property("target_value"))
        require(math.isfinite(target), "objective '{}' target_value is not finite".format(obj_id))

        obj_type = enum_name(obj.get_editor_property("objective_type"))
        if obj_type in ("RESTORE_POWER_ABOVE", "MAINTAIN_FUEL_ABOVE"):
            require(0.0 <= target <= 1.0, "objective '{}' fraction target out of [0,1]".format(obj_id))

    require(
        EXPECTED_OBJECTIVE_IDS.issubset(objective_ids),
        "Missing expected objectives. Have={} expected={}".format(sorted(objective_ids), sorted(EXPECTED_OBJECTIVE_IDS)),
    )

    events = list(definition.get_editor_property("events"))
    event_ids = set()
    for evt in events:
        evt_id = str(evt.get_editor_property("event_id"))
        require(bool(evt_id), "event_id is empty")
        require(evt_id not in event_ids, "duplicate event_id '{}'".format(evt_id))
        event_ids.add(evt_id)

        trigger = float(evt.get_editor_property("trigger_time_seconds"))
        require(math.isfinite(trigger) and trigger >= 0.0, "event '{}' has invalid trigger time".format(evt_id))

        if evt_id in EXPECTED_EVENT_TIMES:
            require(
                abs(trigger - EXPECTED_EVENT_TIMES[evt_id]) < 0.0001,
                "event '{}' expected t={}, got {}".format(evt_id, EXPECTED_EVENT_TIMES[evt_id], trigger),
            )

        command = enum_name(evt.get_editor_property("command_type"))
        require(command not in UNSUPPORTED_COMMANDS, "event '{}' uses unsupported command {}".format(evt_id, command))
        require("SET_POWER_GENERATION" not in command.upper(), "event '{}' must not use SetPowerGeneration".format(evt_id))

        float_param = float(evt.get_editor_property("float_parameter"))
        require(math.isfinite(float_param), "event '{}' float_parameter is not finite".format(evt_id))

        if command in ("SET_EXTERNAL_HEATING_RATE", "SET_EXTERNAL_POWER_DEMAND"):
            require(float_param >= 0.0, "event '{}' external modifier must be nonnegative".format(evt_id))

        if command == "SET_MISSION_PHASE":
            phase = enum_name(evt.get_editor_property("phase_parameter"))
            require(bool(phase), "event '{}' missing typed phase_parameter".format(evt_id))

    for expected_id in EXPECTED_EVENT_TIMES:
        require(expected_id in event_ids, "missing expected event '{}'".format(expected_id))

    print(
        "Verified actual mission asset '{}' ({} objectives, {} events)".format(
            MISSION_ASSET_PATH,
            len(objectives),
            len(events),
        )
    )


def main():
    print("Beginning mission asset verification...")
    verify_solar_event_emergency_asset()
    print("Mission asset verification completed successfully.")


if __name__ == "__main__":
    main()
