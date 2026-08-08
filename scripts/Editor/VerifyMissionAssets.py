import math
import unreal

EDITOR_ASSET_LIBRARY = unreal.EditorAssetLibrary


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def validate_mission_definition(config, label):
    require(config is not None, "{}: mission config is None".format(label))
    require(bool(config.mission_id), "{}: mission_id is empty".format(label))

    # Validate objectives
    objectives = list(config.objectives)
    require(len(objectives) > 0, "{}: mission must define at least one objective".format(label))
    objective_ids = set()
    for obj in objectives:
        obj_id = str(obj.objective_id)
        require(bool(obj_id), "{}: objective_id is empty".format(label))
        require(obj_id not in objective_ids, "{}: duplicate objective_id '{}'".format(label, obj_id))
        objective_ids.add(obj_id)
        require(math.isfinite(obj.target_value), "{}: objective '{}' target_value is not finite".format(label, obj_id))

    # Validate events
    events = list(config.events)
    event_ids = set()
    for evt in events:
        evt_id = str(evt.event_id)
        require(bool(evt_id), "{}: event_id is empty".format(label))
        require(evt_id not in event_ids, "{}: duplicate event_id '{}'".format(label, evt_id))
        event_ids.add(evt_id)
        require(math.isfinite(evt.trigger_time_seconds), "{}: event '{}' trigger time is not finite".format(label, evt_id))
        require(evt.trigger_time_seconds >= 0.0, "{}: event '{}' trigger time must be nonnegative".format(label, evt_id))
        require(math.isfinite(evt.float_parameter), "{}: event '{}' float_parameter is not finite".format(label, evt_id))

    print("Verified mission definition '{}' ({} objectives, {} events)".format(label, len(objectives), len(events)))


def verify_solar_event_emergency_factory():
    config = unreal.EdenMissionDefinitionDataAsset.create_solar_event_emergency_definition()
    validate_mission_definition(config, "SolarEventEmergencyFactory")


def main():
    print("Beginning mission asset verification...")
    verify_solar_event_emergency_factory()
    print("Mission asset verification completed successfully.")


if __name__ == "__main__":
    main()
