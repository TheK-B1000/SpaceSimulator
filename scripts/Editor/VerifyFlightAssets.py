import unreal

EDITOR_ASSET_LIBRARY = unreal.EditorAssetLibrary
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary
BLUEPRINT_EDITOR_LIBRARY = unreal.BlueprintEditorLibrary
SUBOBJECT_SUBSYSTEM = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
SUBOBJECT_LIBRARY = unreal.SubobjectDataBlueprintFunctionLibrary


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


def get_subobject_names(blueprint):
    names = []
    for handle in SUBOBJECT_SUBSYSTEM.k2_gather_subobject_data_for_blueprint(blueprint):
        data = SUBOBJECT_LIBRARY.get_data(handle)
        obj = SUBOBJECT_LIBRARY.get_object(data)
        if obj:
            names.append(obj.get_name())
        names.append(str(SUBOBJECT_LIBRARY.get_variable_name(data)))
        names.append(str(SUBOBJECT_LIBRARY.get_display_name(data)))
    return names


def key_to_text(key_value):
    return key_value.export_text()


def modifier_names(mapping):
    return [modifier.get_class().get_name() for modifier in mapping.get_editor_property("modifiers")]


def verify_input_mapping_context(imc, ia_translate, ia_rotate, ia_stabilize):
    mapping_data = imc.get_editor_property("default_key_mappings")
    mappings = mapping_data.get_editor_property("mappings")
    require(len(mappings) == 11, "Expected 11 flight input mappings, found {}".format(len(mappings)))

    expected = {
        ("IA_FlightTranslate", "W"): [],
        ("IA_FlightTranslate", "S"): ["InputModifierNegate"],
        ("IA_FlightTranslate", "D"): ["InputModifierSwizzleAxis"],
        ("IA_FlightTranslate", "A"): ["InputModifierNegate", "InputModifierSwizzleAxis"],
        ("IA_FlightTranslate", "SpaceBar"): ["InputModifierSwizzleAxis"],
        ("IA_FlightTranslate", "LeftControl"): ["InputModifierNegate", "InputModifierSwizzleAxis"],
        ("IA_FlightRotate", "MouseY"): [],
        ("IA_FlightRotate", "MouseX"): ["InputModifierSwizzleAxis"],
        ("IA_FlightRotate", "E"): ["InputModifierSwizzleAxis"],
        ("IA_FlightRotate", "Q"): ["InputModifierNegate", "InputModifierSwizzleAxis"],
        ("IA_FlightStabilize", "X"): [],
    }

    seen = {}
    for mapping in mappings:
        action = mapping.get_editor_property("action")
        key_value = mapping.get_editor_property("key")
        seen[(action.get_name(), key_to_text(key_value))] = modifier_names(mapping)

    for key, expected_modifiers in expected.items():
        require(key in seen, "Missing input mapping {}".format(key))
        require(
            seen[key] == expected_modifiers,
            "Mapping {} modifiers were {}, expected {}".format(key, seen[key], expected_modifiers),
        )

    require(ia_translate.get_editor_property("value_type") == unreal.InputActionValueType.AXIS3D, "IA_FlightTranslate is not Axis3D")
    require(ia_rotate.get_editor_property("value_type") == unreal.InputActionValueType.AXIS3D, "IA_FlightRotate is not Axis3D")
    require(ia_stabilize.get_editor_property("value_type") == unreal.InputActionValueType.BOOLEAN, "IA_FlightStabilize is not Digital/Boolean")


def verify_maps_settings():
    settings = unreal.GameMapsSettings.get_game_maps_settings()
    game_default_map = settings.get_editor_property("game_default_map")
    editor_startup_map = settings.get_editor_property("editor_startup_map")
    require(game_default_map.export_text() == "/Game/Eden/Maps/L_FlightSandbox.L_FlightSandbox", "GameDefaultMap mismatch")
    require(editor_startup_map.export_text() == "/Game/Eden/Maps/L_FlightSandbox.L_FlightSandbox", "EditorStartupMap mismatch")
    default_game_mode = settings.get_editor_property("global_default_game_mode")
    require(
        default_game_mode and default_game_mode.export_text() == "/Game/Eden/Blueprints/BP_EdenFlightGameMode.BP_EdenFlightGameMode_C",
        "GlobalDefaultGameMode mismatch",
    )


def verify_blueprints(pawn_bp, controller_bp, game_mode_bp, ia_translate, ia_rotate, ia_stabilize, imc):
    pawn_names = get_subobject_names(pawn_bp)
    for expected_name in ["RequiredCollisionRoot", "DebugPlaceholderMesh", "DebugFlightCamera"]:
        require(expected_name in pawn_names, "BP_EdenSpacecraftPawn missing {}".format(expected_name))

    controller_cdo = get_cdo(controller_bp)
    require(controller_cdo.get_editor_property("FlightInputMappingContext") == imc, "Controller BP missing IMC_Flight reference")
    require(controller_cdo.get_editor_property("FlightTranslateAction") == ia_translate, "Controller BP missing IA_FlightTranslate reference")
    require(controller_cdo.get_editor_property("FlightRotateAction") == ia_rotate, "Controller BP missing IA_FlightRotate reference")
    require(controller_cdo.get_editor_property("FlightStabilizeAction") == ia_stabilize, "Controller BP missing IA_FlightStabilize reference")

    game_mode_cdo = get_cdo(game_mode_bp)
    require(game_mode_cdo.get_editor_property("DefaultPawnClass") == generated_class(pawn_bp), "GameMode BP default pawn mismatch")
    require(game_mode_cdo.get_editor_property("PlayerControllerClass") == generated_class(controller_bp), "GameMode BP player controller mismatch")
    hud_class = unreal.load_class(None, "/Script/EdenSpaceSimulator.EdenFlightHUD")
    require(hud_class is not None, "Missing EdenFlightHUD C++ class")
    require(game_mode_cdo.get_editor_property("hud_class") == hud_class, "GameMode BP HUD class mismatch")


def verify_map():
    require(EDITOR_LEVEL_LIBRARY.load_level("/Game/Eden/Maps/L_FlightSandbox"), "Could not load L_FlightSandbox")
    labels = {actor.get_actor_label(): actor for actor in EDITOR_LEVEL_LIBRARY.get_all_level_actors()}
    require("PlayerStart_FlightSandbox" in labels, "L_FlightSandbox missing PlayerStart_FlightSandbox")
    require("SM_FlightSandbox_Blocker" in labels, "L_FlightSandbox missing SM_FlightSandbox_Blocker")

    blocker = labels["SM_FlightSandbox_Blocker"]
    mesh_component = blocker.get_component_by_class(unreal.StaticMeshComponent)
    require(mesh_component is not None, "Blocking actor has no StaticMeshComponent")
    require(mesh_component.get_collision_profile_name() == "BlockAll", "Blocking actor collision profile is not BlockAll")


ia_translate = load_asset("/Game/Eden/Input/IA_FlightTranslate")
ia_rotate = load_asset("/Game/Eden/Input/IA_FlightRotate")
ia_stabilize = load_asset("/Game/Eden/Input/IA_FlightStabilize")
imc = load_asset("/Game/Eden/Input/IMC_Flight")
pawn_bp = load_asset("/Game/Eden/Blueprints/BP_EdenSpacecraftPawn")
controller_bp = load_asset("/Game/Eden/Blueprints/BP_EdenFlightPlayerController")
game_mode_bp = load_asset("/Game/Eden/Blueprints/BP_EdenFlightGameMode")
load_asset("/Game/Eden/Maps/L_FlightSandbox")

verify_input_mapping_context(imc, ia_translate, ia_rotate, ia_stabilize)
verify_blueprints(pawn_bp, controller_bp, game_mode_bp, ia_translate, ia_rotate, ia_stabilize, imc)
verify_map()
verify_maps_settings()

unreal.log("Flight asset verification passed.")
