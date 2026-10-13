"""Cast line-up: both hunters, Gascoigne (human) and Lady Maria built from parts, idle pose."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import lib_game as G  # noqa: E402

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
cast = [
    ("HunterA", dict(outfit=2200, right="wp_a_0800", left="wp_a_2500")),
    ("HunterB", dict(outfit=9200, right="wp_a_1030", left="wp_a_2600")),
    ("Gascoigne", dict(outfit=4200, right="wp_a_1020", left="wp_a_2600")),
    ("Maria", dict(outfit=9300, gender="f", right="wp_a_1703", left="wp_a_2520")),
]
for i, (name, kw) in enumerate(cast):
    ch = G.hunter(name, **kw)
    ch.play("a000_000000", start=1, loc=(i * 1.6 - 2.4, 0, 0), yaw=math.pi, frames=2)
scn = bpy.context.scene
scn.frame_set(1)
scn.render.engine = "BLENDER_EEVEE"
scn.render.resolution_x, scn.render.resolution_y = 1600, 800
scn.eevee.taa_render_samples = 32
w = bpy.data.worlds.new("w")
scn.world = w
w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.05, 0.055, 0.07, 1)
for loc, e, c in (((-3, -4, 4), 900, (1, .9, .8)), ((3, 3, 3), 700, (.6, .7, 1)), ((4, -3, 1.5), 200, (.8, .85, 1))):
    ld = bpy.data.lights.new("l", "AREA")
    ld.energy, ld.color, ld.size = e, c, 2.5
    lo = bpy.data.objects.new("l", ld)
    lo.location = loc
    lo.rotation_euler = (Vector((0, 0, 1.2)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    scn.collection.objects.link(lo)
bpy.ops.mesh.primitive_plane_add(size=30)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.lens = 40
cam.location = (0, -7.2, 1.4)
cam.rotation_euler = (Vector((0, 0, 1.05)) - cam.location).to_track_quat("-Z", "Y").to_euler()
scn.collection.objects.link(cam)
scn.camera = cam
scn.render.filepath = r"art/game-models/preview/cast.png"
bpy.ops.render.render(write_still=True)
cam.data.lens = 90
cam.location = (-2.4, -2.2, 1.6)
cam.rotation_euler = (Vector((-2.4, 0, 1.58)) - cam.location).to_track_quat("-Z", "Y").to_euler()
scn.render.filepath = r"art/game-models/preview/cast_face.png"
bpy.ops.render.render(write_still=True)
print("CAST DONE")
