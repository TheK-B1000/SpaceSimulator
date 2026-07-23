import math

import unreal

EDITOR_ASSET_LIBRARY = unreal.EditorAssetLibrary
BLUEPRINT_EDITOR_LIBRARY = unreal.BlueprintEditorLibrary
SUBOBJECT_SUBSYSTEM = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
SUBOBJECT_LIBRARY = unreal.SubobjectDataBlueprintFunctionLibrary

FUEL_ASSET_PATH = "/Game/Eden/Data/Systems/DA_EdenFuelConfig"
POWER_ASSET_PATH = "/Game/Eden/Data/Systems/DA_EdenPowerConfig"
THERMAL_ASSET_PATH = "/Game/Eden/Data/Systems/DA_EdenThermalConfig"
PAWN_BLUEPRINT_PATH = "/Game/Eden/Blueprints/BP_EdenSpacecraftPawn"


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


def require_finite_nonnegative(value, field_name):
    require(math.isfinite(value), "{} must be finite".format(field_name))
    require(value >= 0.0, "{} must be nonnegative".format(field_name))


def require_fraction(value, field_name):
    require(math.isfinite(value), "{} must be finite".format(field_name))
    require(0.0 <= value <= 1.0, "{} must be within [0, 1]".format(field_name))


def require_positive(value, field_name):
    require(math.isfinite(value), "{} must be finite".format(field_name))
    require(value > 0.0, "{} must be positive".format(field_name))


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


def verify_fuel_asset(asset):
    require(asset.get_class().get_name() == "EdenFuelConfigDataAsset", "DA_EdenFuelConfig has wrong class")
    config = asset.get_editor_property("fuel_config")

    capacity = config.get_editor_property("capacity_kilograms")
    consumption_rate = config.get_editor_property("consumption_rate_kilograms_per_second")
    initial_fraction = config.get_editor_property("initial_fuel_fraction")
    warning = config.get_editor_property("warning_threshold_fraction")
    critical = config.get_editor_property("critical_threshold_fraction")

    require_positive(capacity, "Fuel CapacityKilograms")
    require_finite_nonnegative(consumption_rate, "Fuel ConsumptionRateKilogramsPerSecond")
    require_fraction(initial_fraction, "Fuel InitialFuelFraction")
    require_fraction(warning, "Fuel WarningThresholdFraction")
    require_fraction(critical, "Fuel CriticalThresholdFraction")
    require(critical < warning, "Fuel thresholds must satisfy CriticalThresholdFraction < WarningThresholdFraction")
    validate_with_unreal_data_validation(asset, FUEL_ASSET_PATH)


def verify_power_asset(asset):
    require(asset.get_class().get_name() == "EdenPowerConfigDataAsset", "DA_EdenPowerConfig has wrong class")
    config = asset.get_editor_property("power_config")

    capacity = config.get_editor_property("battery_capacity_kilowatt_hours")
    generation = config.get_editor_property("generation_kilowatts")
    baseline_demand = config.get_editor_property("baseline_demand_kilowatts")
    initial_fraction = config.get_editor_property("initial_charge_fraction")
    warning = config.get_editor_property("warning_threshold_fraction")
    critical = config.get_editor_property("critical_threshold_fraction")

    require_positive(capacity, "Power BatteryCapacityKilowattHours")
    require_finite_nonnegative(generation, "Power GenerationKilowatts")
    require_finite_nonnegative(baseline_demand, "Power BaselineDemandKilowatts")
    require_fraction(initial_fraction, "Power InitialChargeFraction")
    require_fraction(warning, "Power WarningThresholdFraction")
    require_fraction(critical, "Power CriticalThresholdFraction")
    require(critical < warning, "Power thresholds must satisfy CriticalThresholdFraction < WarningThresholdFraction")
    validate_with_unreal_data_validation(asset, POWER_ASSET_PATH)


def verify_thermal_asset(asset):
    require(asset.get_class().get_name() == "EdenThermalConfigDataAsset", "DA_EdenThermalConfig has wrong class")
    config = asset.get_editor_property("thermal_config")

    absolute_min = config.get_editor_property("absolute_min_temperature_celsius")
    ambient = config.get_editor_property("ambient_temperature_celsius")
    warning = config.get_editor_property("warning_temperature_celsius")
    critical = config.get_editor_property("critical_temperature_celsius")
    absolute_max = config.get_editor_property("absolute_max_temperature_celsius")
    initial = config.get_editor_property("initial_temperature_celsius")
    heat = config.get_editor_property("heat_generation_degrees_celsius_per_second")
    dissipation = config.get_editor_property("dissipation_degrees_celsius_per_second")

    for field_name, value in [
        ("Thermal AbsoluteMinTemperatureCelsius", absolute_min),
        ("Thermal AmbientTemperatureCelsius", ambient),
        ("Thermal WarningTemperatureCelsius", warning),
        ("Thermal CriticalTemperatureCelsius", critical),
        ("Thermal AbsoluteMaxTemperatureCelsius", absolute_max),
        ("Thermal InitialTemperatureCelsius", initial),
    ]:
        require(math.isfinite(value), "{} must be finite".format(field_name))

    require(absolute_min <= ambient < warning < critical <= absolute_max, "Thermal thresholds must satisfy AbsMin <= Ambient < Warning < Critical <= AbsMax")
    require(absolute_min <= initial <= absolute_max, "Thermal InitialTemperatureCelsius must be inside absolute bounds")
    require_finite_nonnegative(heat, "Thermal HeatGenerationDegreesCelsiusPerSecond")
    require_finite_nonnegative(dissipation, "Thermal DissipationDegreesCelsiusPerSecond")
    validate_with_unreal_data_validation(asset, THERMAL_ASSET_PATH)


def get_subobject_object(data):
    get_associated_object = getattr(SUBOBJECT_LIBRARY, "get_associated_object", None)
    if get_associated_object:
        obj = get_associated_object(data)
        if obj:
            return obj
    return SUBOBJECT_LIBRARY.get_object(data)


def gather_subobjects(blueprint):
    subobjects = []
    for handle in SUBOBJECT_SUBSYSTEM.k2_gather_subobject_data_for_blueprint(blueprint):
        data = SUBOBJECT_LIBRARY.get_data(handle)
        obj = get_subobject_object(data)
        variable_name = str(SUBOBJECT_LIBRARY.get_variable_name(data))
        display_name = str(SUBOBJECT_LIBRARY.get_display_name(data))
        subobjects.append((obj, variable_name, display_name))
    return subobjects


def count_components_by_class(blueprint):
    counts = {}
    names = set()
    for obj, variable_name, display_name in gather_subobjects(blueprint):
        if obj:
            class_name = obj.get_class().get_name()
            counts[class_name] = counts.get(class_name, 0) + 1
            names.add(obj.get_name())
        names.add(variable_name)
        names.add(display_name)
    return counts, names


def require_component_assignment(component, property_name, expected_asset, component_label):
    assigned_asset = component.get_editor_property(property_name)
    require(
        assigned_asset == expected_asset,
        "{} expected {} but found {}".format(component_label, expected_asset.get_path_name(), assigned_asset),
    )


def verify_blueprint_composition(pawn_blueprint, fuel_asset, power_asset, thermal_asset):
    cdo = get_cdo(pawn_blueprint)

    required_root = cdo.get_required_collision_root()
    flight_component = cdo.get_flight_movement_component()
    fuel_component = cdo.get_fuel_system_component()
    power_component = cdo.get_power_system_component()
    thermal_component = cdo.get_thermal_system_component()

    require(required_root is not None, "BP_EdenSpacecraftPawn missing RequiredCollisionRoot accessor result")
    require(required_root.get_class().get_name() == "SphereComponent", "RequiredCollisionRoot must remain a SphereComponent")
    require(cdo.get_editor_property("root_component") == required_root, "RequiredCollisionRoot must remain the Blueprint CDO root component")
    require(flight_component is not None, "BP_EdenSpacecraftPawn missing inherited FlightMovementComponent")
    require(fuel_component is not None, "BP_EdenSpacecraftPawn missing inherited FuelSystem")
    require(power_component is not None, "BP_EdenSpacecraftPawn missing inherited PowerSystem")
    require(thermal_component is not None, "BP_EdenSpacecraftPawn missing inherited ThermalSystem")

    counts, names = count_components_by_class(pawn_blueprint)
    expected_counts = {
        "EdenFlightMovementComponent": 1,
        "EdenFuelSystemComponent": 1,
        "EdenPowerSystemComponent": 1,
        "EdenThermalSystemComponent": 1,
    }

    for class_name, expected_count in expected_counts.items():
        require(
            counts.get(class_name, 0) == expected_count,
            "BP_EdenSpacecraftPawn expected {} {} but found {}".format(expected_count, class_name, counts.get(class_name, 0)),
        )

    for expected_name in ["RequiredCollisionRoot", "DebugPlaceholderMesh", "DebugFlightCamera", "FuelSystem", "PowerSystem", "ThermalSystem", "FlightMovementComponent"]:
        require(expected_name in names, "BP_EdenSpacecraftPawn missing {}".format(expected_name))

    require_component_assignment(fuel_component, "fuel_config_data_asset", fuel_asset, "FuelSystem")
    require_component_assignment(power_component, "power_config_data_asset", power_asset, "PowerSystem")
    require_component_assignment(thermal_component, "thermal_config_data_asset", thermal_asset, "ThermalSystem")


fuel_asset = load_asset(FUEL_ASSET_PATH)
power_asset = load_asset(POWER_ASSET_PATH)
thermal_asset = load_asset(THERMAL_ASSET_PATH)
pawn_blueprint = load_asset(PAWN_BLUEPRINT_PATH)

verify_fuel_asset(fuel_asset)
verify_power_asset(power_asset)
verify_thermal_asset(thermal_asset)
verify_blueprint_composition(pawn_blueprint, fuel_asset, power_asset, thermal_asset)

unreal.log("Resource asset verification passed.")
