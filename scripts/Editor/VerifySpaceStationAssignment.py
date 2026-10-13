import unreal

EDITOR_ASSET_LIBRARY = unreal.EditorAssetLibrary
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary

MESH_PATH = "/Game/Eden/Art/Station/SM_SpaceStationModules"
MATERIAL_PATH = "/Game/Eden/Art/Station/M_SpaceStationModules"
MAP_PATH = "/Game/Eden/Maps/L_FlightSandbox"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


for path in [
    MESH_PATH,
    MATERIAL_PATH,
    "/Game/Eden/Art/Station/T_SpaceStation_Albedo",
    "/Game/Eden/Art/Station/T_SpaceStation_Normal",
    "/Game/Eden/Art/Station/T_SpaceStation_Metallic",
    "/Game/Eden/Art/Station/T_SpaceStation_Roughness",
    "/Game/Eden/Art/Station/T_SpaceStation_AO",
    "/Game/Eden/Art/Station/T_SpaceStation_Emissive",
]:
    require(EDITOR_ASSET_LIBRARY.does_asset_exist(path), "Missing {}".format(path))

mesh = EDITOR_ASSET_LIBRARY.load_asset(MESH_PATH)
require(mesh is not None, "Could not load mesh")

require(EDITOR_LEVEL_LIBRARY.load_level(MAP_PATH), "Could not load map")
labels = {actor.get_actor_label(): actor for actor in EDITOR_LEVEL_LIBRARY.get_all_level_actors()}
require("SM_FlightSandbox_Blocker" in labels, "Missing SM_FlightSandbox_Blocker")
blocker = labels["SM_FlightSandbox_Blocker"]
comp = blocker.get_component_by_class(unreal.StaticMeshComponent)
require(comp is not None, "No StaticMeshComponent")
assigned = comp.get_editor_property("static_mesh")
require(assigned is not None, "Blocker has no static mesh assigned")
require(assigned.get_path_name().startswith(MESH_PATH), "Blocker mesh is {}, expected {}".format(assigned.get_path_name(), MESH_PATH))
require(comp.get_collision_profile_name() == "BlockAll", "Collision profile is not BlockAll")

scale = blocker.get_actor_scale3d()
bounds = mesh.get_bounds()
extent = bounds.box_extent
longest = max(abs(extent.x), abs(extent.y), abs(extent.z)) * 2.0
world_longest = longest * max(abs(scale.x), abs(scale.y), abs(scale.z))

unreal.log(
    "Space station assignment verified. mesh={} scale={} mesh_longest_cm={:.1f} world_longest_cm={:.1f}".format(
        assigned.get_path_name(), scale, longest, world_longest
    )
)
