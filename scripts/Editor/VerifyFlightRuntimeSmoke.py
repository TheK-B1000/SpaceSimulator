import unreal

EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


require(EDITOR_LEVEL_LIBRARY.load_level("/Game/Eden/Maps/L_FlightSandbox"), "Could not load L_FlightSandbox")

pawn_class = unreal.load_class(None, "/Game/Eden/Blueprints/BP_EdenSpacecraftPawn.BP_EdenSpacecraftPawn_C")
require(pawn_class is not None, "Missing BP_EdenSpacecraftPawn generated class")

pawn = EDITOR_LEVEL_LIBRARY.spawn_actor_from_class(
    pawn_class,
    unreal.Vector(500.0, 0.0, 200.0),
    unreal.Rotator(0.0, 0.0, 0.0),
    True,
)
require(pawn is not None, "Could not spawn transient BP_EdenSpacecraftPawn")

movement = pawn.get_flight_movement_component()
require(movement is not None, "Pawn missing flight movement component")

command = unreal.EdenFlightInputCommand()
command.set_editor_property("translation_input", unreal.Vector(1.0, 0.0, 0.0))
command.set_editor_property("rotation_input", unreal.Vector(0.0, 0.0, 0.0))
command.set_editor_property("stabilization_enabled", True)

pawn.apply_flight_input_command(command, 0.5)

location = pawn.get_actor_location()
velocity = movement.get_editor_property("velocity")

require(location.x < 850.0, "Pawn tunneled through the blocking cube. Location={}".format(location))
require(velocity.x <= 1.0, "Blocking response did not remove inward X velocity. Velocity={}".format(velocity))

EDITOR_LEVEL_LIBRARY.destroy_actor(pawn)

unreal.log("Flight runtime smoke passed. Post-hit location={} velocity={}".format(location, velocity))
