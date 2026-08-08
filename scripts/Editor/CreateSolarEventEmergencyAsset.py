import unreal

EDITOR_ASSET_LIBRARY = unreal.EditorAssetLibrary

POWER_PATH = "/Game/Eden/Data/Systems/DA_EdenPowerConfig"
THERMAL_PATH = "/Game/Eden/Data/Systems/DA_EdenThermalConfig"
FUEL_PATH = "/Game/Eden/Data/Systems/DA_EdenFuelConfig"
MISSION_DIR = "/Game/Eden/Data/Missions"
MISSION_PATH = "/Game/Eden/Data/Missions/DA_SolarEventEmergency"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def dump_resource_configs():
    power = EDITOR_ASSET_LIBRARY.load_asset(POWER_PATH)
    thermal = EDITOR_ASSET_LIBRARY.load_asset(THERMAL_PATH)
    fuel = EDITOR_ASSET_LIBRARY.load_asset(FUEL_PATH)
    require(power and thermal and fuel, "Missing sandbox resource Data Assets")

    power_cfg = power.get_editor_property("power_config")
    thermal_cfg = thermal.get_editor_property("thermal_config")
    fuel_cfg = fuel.get_editor_property("fuel_config")

    print("POWER generation_kw={}".format(power_cfg.get_editor_property("generation_kilowatts")))
    print("POWER baseline_demand_kw={}".format(power_cfg.get_editor_property("baseline_demand_kilowatts")))
    print("POWER capacity_kwh={}".format(power_cfg.get_editor_property("battery_capacity_kilowatt_hours")))
    print("POWER initial_charge_fraction={}".format(power_cfg.get_editor_property("initial_charge_fraction")))

    print("THERMAL ambient_c={}".format(thermal_cfg.get_editor_property("ambient_temperature_celsius")))
    print("THERMAL initial_c={}".format(thermal_cfg.get_editor_property("initial_temperature_celsius")))
    print("THERMAL heat_c_per_s={}".format(thermal_cfg.get_editor_property("heat_generation_degrees_celsius_per_second")))
    print("THERMAL dissipation_c_per_s={}".format(thermal_cfg.get_editor_property("dissipation_degrees_celsius_per_second")))
    print("THERMAL warning_c={}".format(thermal_cfg.get_editor_property("warning_temperature_celsius")))
    print("THERMAL critical_c={}".format(thermal_cfg.get_editor_property("critical_temperature_celsius")))
    print("THERMAL absolute_max_c={}".format(thermal_cfg.get_editor_property("absolute_max_temperature_celsius")))

    print("FUEL capacity_kg={}".format(fuel_cfg.get_editor_property("capacity_kilograms")))
    print("FUEL consumption_kg_per_s={}".format(fuel_cfg.get_editor_property("consumption_rate_kilograms_per_second")))
    print("FUEL initial_fraction={}".format(fuel_cfg.get_editor_property("initial_fuel_fraction")))

    return power_cfg, thermal_cfg, fuel_cfg


def make_objective(objective_id, display_name, objective_type, target_value):
    obj = unreal.EdenMissionObjectiveConfig()
    obj.set_editor_property("objective_id", unreal.Name(objective_id))
    obj.set_editor_property("display_name", unreal.Text(display_name))
    obj.set_editor_property("objective_type", objective_type)
    obj.set_editor_property("target_value", float(target_value))
    obj.set_editor_property("required", True)
    obj.set_editor_property("activate_on_start", True)
    return obj


def make_phase_event(event_id, time_seconds, phase):
    evt = unreal.EdenMissionEventConfig()
    evt.set_editor_property("event_id", unreal.Name(event_id))
    evt.set_editor_property("trigger_time_seconds", float(time_seconds))
    evt.set_editor_property("command_type", unreal.EdenMissionCommandType.SET_MISSION_PHASE)
    evt.set_editor_property("phase_parameter", phase)
    evt.set_editor_property("float_parameter", 0.0)
    return evt


def make_float_event(event_id, time_seconds, command_type, float_parameter):
    evt = unreal.EdenMissionEventConfig()
    evt.set_editor_property("event_id", unreal.Name(event_id))
    evt.set_editor_property("trigger_time_seconds", float(time_seconds))
    evt.set_editor_property("command_type", command_type)
    evt.set_editor_property("float_parameter", float(float_parameter))
    return evt


def create_or_update_solar_event_asset(external_heating_c_per_s, external_demand_kw):
    if not EDITOR_ASSET_LIBRARY.does_directory_exist(MISSION_DIR):
        EDITOR_ASSET_LIBRARY.make_directory(MISSION_DIR)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    asset = None
    if EDITOR_ASSET_LIBRARY.does_asset_exist(MISSION_PATH):
        asset = EDITOR_ASSET_LIBRARY.load_asset(MISSION_PATH)
        print("Updating existing {}".format(MISSION_PATH))
    else:
        factory = unreal.DataAssetFactory()
        # DataAssetFactory needs class set via create_asset with class
        asset = asset_tools.create_asset(
            "DA_SolarEventEmergency",
            MISSION_DIR,
            unreal.EdenMissionDefinitionDataAsset,
            factory,
        )
        require(asset is not None, "Failed to create {}".format(MISSION_PATH))
        print("Created {}".format(MISSION_PATH))

    definition = unreal.EdenMissionDefinitionConfig()
    definition.set_editor_property("mission_id", unreal.Name("SolarCrisis"))
    definition.set_editor_property("display_name", unreal.Text("Solar Event Emergency"))

    objectives = [
        make_objective(
            "SurviveSolarEvent",
            "Survive solar flare until resolution (50s)",
            unreal.EdenObjectiveType.SURVIVE_UNTIL_TIME,
            50.0,
        ),
        make_objective(
            "PreventOverheating",
            "Keep core temperature below critical threshold",
            unreal.EdenObjectiveType.KEEP_TEMPERATURE_BELOW,
            100.0,
        ),
        make_objective(
            "RestoreBatteryCharge",
            "Maintain battery charge fraction above 0.10",
            unreal.EdenObjectiveType.RESTORE_POWER_ABOVE,
            0.10,
        ),
        make_objective(
            "ConservePropellant",
            "Preserve propellant fraction above 0.20",
            unreal.EdenObjectiveType.MAINTAIN_FUEL_ABOVE,
            0.20,
        ),
    ]

    events = [
        make_phase_event("WarningPhaseBegin", 5.0, unreal.EdenMissionPhase.WARNING),
        make_phase_event("ImpactPhaseBegin", 10.0, unreal.EdenMissionPhase.IMPACT),
        make_float_event(
            "SolarFlareHeating",
            10.0,
            unreal.EdenMissionCommandType.SET_EXTERNAL_HEATING_RATE,
            external_heating_c_per_s,
        ),
        make_float_event(
            "AuxiliaryLoadDemand",
            10.0,
            unreal.EdenMissionCommandType.SET_EXTERNAL_POWER_DEMAND,
            external_demand_kw,
        ),
        make_phase_event("RecoveryPhaseBegin", 30.0, unreal.EdenMissionPhase.RECOVERY),
        make_float_event(
            "ClearSolarFlare",
            30.0,
            unreal.EdenMissionCommandType.CLEAR_EXTERNAL_HEATING_RATE,
            0.0,
        ),
        make_float_event(
            "ClearAuxiliaryDemand",
            30.0,
            unreal.EdenMissionCommandType.CLEAR_EXTERNAL_POWER_DEMAND,
            0.0,
        ),
    ]

    definition.set_editor_property("objectives", objectives)
    definition.set_editor_property("events", events)
    asset.set_editor_property("mission_definition", definition)

    EDITOR_ASSET_LIBRARY.save_asset(MISSION_PATH)
    print(
        "Saved Solar Event Emergency with heating={} C/s demand={} kW".format(
            external_heating_c_per_s,
            external_demand_kw,
        )
    )


def choose_disturbances(power_cfg, thermal_cfg):
    generation = float(power_cfg.get_editor_property("generation_kilowatts"))
    baseline = float(power_cfg.get_editor_property("baseline_demand_kilowatts"))
    capacity = float(power_cfg.get_editor_property("battery_capacity_kilowatt_hours"))
    dissipation = float(thermal_cfg.get_editor_property("dissipation_degrees_celsius_per_second"))
    initial_temp = float(thermal_cfg.get_editor_property("initial_temperature_celsius"))
    critical = float(thermal_cfg.get_editor_property("critical_temperature_celsius"))

    # 20s impact window. Keep peak temp under 100C with margin if dissipation helps after recovery.
    # External heating chosen so temp rise over 20s is observable but not instant-fail:
    # delta ~= (heating - dissipation) * 20; target rise ~40C from typical sandbox initial.
    heating = max(2.0, dissipation + 2.0)
    headroom = critical - initial_temp
    if (heating - dissipation) * 20.0 >= headroom:
        heating = dissipation + max(0.5, (headroom * 0.5) / 20.0)

    # Demand should create battery drain but not empty a full battery in 20s.
    # Drain kWh ~= max(0, baseline + demand - generation) * (20/3600)
    demand = 5.0
    net_kw = baseline + demand - generation
    if net_kw < 1.0:
        demand = max(5.0, generation - baseline + 3.0)
        net_kw = baseline + demand - generation
    max_drain_kwh = capacity * 0.5
    drain_kwh = max(0.0, net_kw) * (20.0 / 3600.0)
    if drain_kwh > max_drain_kwh and net_kw > 0.0:
        demand = max(1.0, (max_drain_kwh * 3600.0 / 20.0) + generation - baseline)

    print("SELECTED external_heating_c_per_s={}".format(heating))
    print("SELECTED external_demand_kw={}".format(demand))
    return heating, demand


def main():
    power_cfg, thermal_cfg, fuel_cfg = dump_resource_configs()
    heating, demand = choose_disturbances(power_cfg, thermal_cfg)
    create_or_update_solar_event_asset(heating, demand)


if __name__ == "__main__":
    main()
