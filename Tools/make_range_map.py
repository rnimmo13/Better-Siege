# Creates /Game/Siege/Maps/Lvl_Range: the Embassy's lighting plus the range game mode. The range itself is built by code.
#   UnrealEditor-Cmd.exe Seige.uproject -EnablePlugins=PythonScriptPlugin -ExecutePythonScript="Tools/make_range_map.py" -unattended -nullrhi
import unreal

SOURCE = "/Game/Siege/Maps/Lvl_Embassy"
TARGET = "/Game/Siege/Maps/Lvl_Range"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

COPIED = {
    unreal.DirectionalLight: ("light_component", ("intensity", "light_color", "temperature", "use_temperature", "atmosphere_sun_light", "cast_shadows")),
    unreal.SkyLight: ("light_component", ("intensity", "light_color", "real_time_capture", "source_type")),
    unreal.SkyAtmosphere: (None, ()),
    unreal.VolumetricCloud: (None, ()),
    unreal.ExponentialHeightFog: ("component", ("fog_density", "fog_height_falloff")),
}

les.load_level(SOURCE)
captured = []
for actor in eas.get_all_level_actors():
    for cls, (comp_name, props) in COPIED.items():
        if isinstance(actor, cls):
            values = {}
            if comp_name:
                comp = actor.get_editor_property(comp_name)
                for prop in props:
                    try:
                        values[prop] = comp.get_editor_property(prop)
                    except Exception as err:
                        unreal.log_warning("skipping %s: %s" % (prop, err))
            captured.append((cls, comp_name, actor.get_actor_location(), actor.get_actor_rotation(), values))

# a new level must be saved and reopened before actors spawned into it are kept
les.new_level(TARGET, False)
les.save_current_level()
les.load_level(TARGET)

for cls, comp_name, location, rotation, values in captured:
    actor = eas.spawn_actor_from_class(cls, location, rotation)
    if comp_name:
        comp = actor.get_editor_property(comp_name)
        comp.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        for prop, value in values.items():
            comp.set_editor_property(prop, value)

world = editor.get_editor_world()
world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Script/Seige.SiegeRangeGameMode"))

les.save_current_level()
