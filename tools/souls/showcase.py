"""Builds art/game-models/BBSC-game-models.blend: the Hunter (Hunter set 2200), a Yharnam
huntsman (c2630), the werewolf-type beast (c7500) and the Cleric Beast (c5020), textured, lit and
framed, plus a preview render. Models are in their bind (T) pose - no animation yet.
    blender -b --factory-startup -P showcase.py"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import blender_flver as BF  # noqa: E402

OUT = r"art/game-models"
scn = bpy.context.scene
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)

figures = [
    ("Hunter", "parts:hd_m_2200+bd_m_2200+am_m_2200+lg_m_2200", Vector((0.0, 0.0, 0.0))),
    ("Huntsman c2630", "c2630", Vector((-2.4, 0.4, 0.0))),
    ("Beast c7500", "c7500", Vector((2.6, 0.3, 0.0))),
    ("Cleric Beast c5020", "c5020", Vector((0.4, 9.0, 0.0))),
]
for label, spec, loc in figures:
    obs = BF.load_model(spec)
    root = bpy.data.objects.new(label, None)
    scn.collection.objects.link(root)
    for o in obs:
        o.parent = root
    root.location = loc

# ground, light, camera
bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 5, 0))
ground = bpy.context.object
ground.name = "Ground"
gm = bpy.data.materials.new("Ground")
gm.use_nodes = True
gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.05, 0.05, 0.055, 1)
gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.85
ground.data.materials.append(gm)


def light(name, kind, loc, target, energy, color, size=1.0):
    ld = bpy.data.lights.new(name, kind)
    ld.energy = energy
    ld.color = color
    if kind == "AREA":
        ld.size = size
    lo = bpy.data.objects.new(name, ld)
    lo.location = loc
    lo.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    scn.collection.objects.link(lo)


light("Key", "AREA", (-3.5, -5.0, 5.0), (0, 1, 1.2), 1600, (1.0, 0.92, 0.85), 3.0)
light("Rim", "AREA", (3.0, 6.0, 4.0), (0, 1, 1.4), 1400, (0.6, 0.7, 1.0), 3.0)
light("Fill", "AREA", (4.5, -4.0, 2.0), (0, 1, 1.0), 300, (0.8, 0.85, 1.0), 4.0)
world = bpy.data.worlds.new("World")
scn.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.02, 0.022, 0.028, 1)
cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
cam.data.lens = 32
cam.location = (0.6, -9.5, 3.0)
cam.rotation_euler = (Vector((0.4, 2.5, 2.4)) - cam.location).to_track_quat("-Z", "Y").to_euler()
scn.collection.objects.link(cam)
scn.camera = cam

scn.render.engine = "BLENDER_EEVEE"
scn.render.resolution_x, scn.render.resolution_y = 1920, 1080
scn.eevee.taa_render_samples = 64
scn.view_settings.view_transform = "AgX"
scn.view_settings.look = "AgX - Medium High Contrast"
scn.render.filepath = os.path.join(OUT, "preview", "showcase.png")
bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "BBSC-game-models.blend"))
print("SHOWCASE SAVED")
