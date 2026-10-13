# Import Free Space Station Modules FBX into Content/Eden/Art/Station,
# build a simple PBR material, and assign the mesh to SM_FlightSandbox_Blocker.
# Run via UnrealEditor-Cmd -ExecutePythonScript=...

import unreal

EDITOR_ASSET_LIBRARY = unreal.EditorAssetLibrary
EDITOR_LEVEL_LIBRARY = unreal.EditorLevelLibrary
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MATERIAL_EDITING = unreal.MaterialEditingLibrary

PROJECT_DIR = unreal.Paths.project_dir()
FBX_PATH = unreal.Paths.combine([PROJECT_DIR, "Saved/ImportStaging/SpaceStationModules/source/p_92_spacestation_merge.fbx"])
TEX_DIR = unreal.Paths.combine([PROJECT_DIR, "Saved/ImportStaging/SpaceStationModules/textures"])

DEST_DIR = "/Game/Eden/Art/Station"
MESH_PATH = DEST_DIR + "/SM_SpaceStationModules"
MATERIAL_PATH = DEST_DIR + "/M_SpaceStationModules"
MAP_PATH = "/Game/Eden/Maps/L_FlightSandbox"
BLOCKER_LABEL = "SM_FlightSandbox_Blocker"
# Target longest-axis size in Unreal units (cm). Sandbox cube was ~1m; station should read larger.
TARGET_LONGEST_AXIS_CM = 2500.0


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def ensure_dir(path):
    if not EDITOR_ASSET_LIBRARY.does_directory_exist(path):
        EDITOR_ASSET_LIBRARY.make_directory(path)


def import_textures():
    texture_files = {
        "T_SpaceStation_Albedo": "spaceship_albedo.jpg",
        "T_SpaceStation_AO": "spaceship_AO.jpg",
        "T_SpaceStation_Emissive": "spaceship_emissive.jpg",
        "T_SpaceStation_Metallic": "spaceship_metallic.jpg",
        "T_SpaceStation_Normal": "spaceship_normal.png",
        "T_SpaceStation_Roughness": "spaceship_roughness.jpg",
    }
    imported = {}
    for asset_name, filename in texture_files.items():
        source = unreal.Paths.combine([TEX_DIR, filename])
        require(unreal.Paths.file_exists(source), "Missing texture {}".format(source))
        destination = "{}/{}".format(DEST_DIR, asset_name)
        if EDITOR_ASSET_LIBRARY.does_asset_exist(destination):
            tex = EDITOR_ASSET_LIBRARY.load_asset(destination)
            unreal.log("Reusing texture {}".format(destination))
        else:
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", source)
            task.set_editor_property("destination_path", DEST_DIR)
            task.set_editor_property("destination_name", asset_name)
            task.set_editor_property("automated", True)
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("save", True)
            ASSET_TOOLS.import_asset_tasks([task])
            tex = EDITOR_ASSET_LIBRARY.load_asset(destination)
            require(tex is not None, "Failed to import texture {}".format(destination))
            unreal.log("Imported texture {}".format(destination))

        if asset_name == "T_SpaceStation_Normal":
            try:
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
                tex.set_editor_property("srgb", False)
            except Exception as exc:
                unreal.log_warning("Could not set normal map flags: {}".format(exc))
        elif asset_name in ("T_SpaceStation_Metallic", "T_SpaceStation_Roughness", "T_SpaceStation_AO"):
            try:
                tex.set_editor_property("srgb", False)
                tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
            except Exception as exc:
                unreal.log_warning("Could not set linear texture flags on {}: {}".format(asset_name, exc))

        EDITOR_ASSET_LIBRARY.save_asset(destination, only_if_is_dirty=False)
        imported[asset_name] = tex
    return imported


def create_or_update_material(textures):
    if EDITOR_ASSET_LIBRARY.does_asset_exist(MATERIAL_PATH):
        material = EDITOR_ASSET_LIBRARY.load_asset(MATERIAL_PATH)
        unreal.log("Reusing material {}".format(MATERIAL_PATH))
    else:
        material = ASSET_TOOLS.create_asset(
            "M_SpaceStationModules",
            DEST_DIR,
            unreal.Material,
            unreal.MaterialFactoryNew(),
        )
        require(material is not None, "Failed to create {}".format(MATERIAL_PATH))

    def sample(texture_asset, name):
        node = MATERIAL_EDITING.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, 0)
        node.set_editor_property("texture", texture_asset)
        node.set_editor_property("desc", name)
        return node

    # Clear old expressions so re-runs are deterministic.
    for expression in list(MATERIAL_EDITING.get_material_expressions(material)):
        MATERIAL_EDITING.delete_material_expression(material, expression)

    albedo = sample(textures["T_SpaceStation_Albedo"], "Albedo")
    normal = sample(textures["T_SpaceStation_Normal"], "Normal")
    metallic = sample(textures["T_SpaceStation_Metallic"], "Metallic")
    roughness = sample(textures["T_SpaceStation_Roughness"], "Roughness")
    ao = sample(textures["T_SpaceStation_AO"], "AO")
    emissive = sample(textures["T_SpaceStation_Emissive"], "Emissive")

    MATERIAL_EDITING.connect_material_property(albedo, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    MATERIAL_EDITING.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    MATERIAL_EDITING.connect_material_property(metallic, "R", unreal.MaterialProperty.MP_METALLIC)
    MATERIAL_EDITING.connect_material_property(roughness, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    MATERIAL_EDITING.connect_material_property(ao, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    MATERIAL_EDITING.connect_material_property(emissive, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    MATERIAL_EDITING.layout_material_expressions(material)
    MATERIAL_EDITING.recompile_material(material)
    EDITOR_ASSET_LIBRARY.save_asset(MATERIAL_PATH, only_if_is_dirty=False)
    return material


def import_static_mesh(material):
    require(unreal.Paths.file_exists(FBX_PATH), "Missing FBX {}".format(FBX_PATH))

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", FBX_PATH)
    task.set_editor_property("destination_path", DEST_DIR)
    task.set_editor_property("destination_name", "SM_SpaceStationModules")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)

    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)

    static_data = options.get_editor_property("static_mesh_import_data")
    static_data.set_editor_property("combine_meshes", True)
    static_data.set_editor_property("auto_generate_collision", True)
    static_data.set_editor_property("generate_lightmap_u_vs", True)
    try:
        static_data.set_editor_property("convert_scene_unit", True)
        static_data.set_editor_property("force_front_x_axis", False)
    except Exception:
        pass

    task.set_editor_property("options", options)
    ASSET_TOOLS.import_asset_tasks([task])

    mesh = EDITOR_ASSET_LIBRARY.load_asset(MESH_PATH)
    require(mesh is not None, "Failed to import mesh {}".format(MESH_PATH))

    try:
        mesh.set_material(0, material)
    except Exception:
        # Fall back to editor property if set_material is unavailable.
        mats = mesh.get_editor_property("static_materials")
        if mats and len(mats) > 0:
            mats[0].set_editor_property("material_interface", material)
            mesh.set_editor_property("static_materials", mats)

    EDITOR_ASSET_LIBRARY.save_asset(MESH_PATH, only_if_is_dirty=False)
    unreal.log("Imported mesh {}".format(MESH_PATH))
    return mesh


def assign_to_blocker(mesh):
    require(EDITOR_LEVEL_LIBRARY.load_level(MAP_PATH), "Could not load {}".format(MAP_PATH))
    labels = {actor.get_actor_label(): actor for actor in EDITOR_LEVEL_LIBRARY.get_all_level_actors()}
    require(BLOCKER_LABEL in labels, "Missing actor {}".format(BLOCKER_LABEL))
    blocker = labels[BLOCKER_LABEL]
    mesh_component = blocker.get_component_by_class(unreal.StaticMeshComponent)
    require(mesh_component is not None, "Blocker has no StaticMeshComponent")

    mesh_component.set_editor_property("static_mesh", mesh)
    mesh_component.set_collision_profile_name("BlockAll")

    bounds = mesh.get_bounds()
    box_extent = bounds.box_extent
    longest = max(abs(box_extent.x), abs(box_extent.y), abs(box_extent.z)) * 2.0
    require(longest > 0.0, "Imported mesh has zero bounds")
    scale = TARGET_LONGEST_AXIS_CM / longest
    blocker.set_actor_scale3d(unreal.Vector(scale, scale, scale))

    unreal.log(
        "Assigned {} to {}. Mesh longest axis={:.1f}cm, applied scale={:.4f}".format(
            MESH_PATH, BLOCKER_LABEL, longest, scale
        )
    )

    # Persist the map.
    world = EDITOR_LEVEL_LIBRARY.get_editor_world()
    require(world is not None, "No editor world after loading map")
    saved = unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH)
    require(saved, "Failed to save {}".format(MAP_PATH))
    EDITOR_ASSET_LIBRARY.save_directory("/Game/Eden/Art/Station", only_if_is_dirty=False, recursive=True)


ensure_dir(DEST_DIR)
textures = import_textures()
material = create_or_update_material(textures)
mesh = import_static_mesh(material)
assign_to_blocker(mesh)
unreal.log("Space station import and blocker assignment completed.")
