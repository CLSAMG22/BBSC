"""Import an exported map area and render a few views (checks placement, rotation, textures)."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import lib_game as G  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
js, out = argv[0], argv[1]
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
col, obs = G.load_map(js)
print("placed", len(obs))
scn = bpy.context.scene
scn.render.engine = "BLENDER_EEVEE"
scn.render.resolution_x, scn.render.resolution_y = 960, 540
scn.eevee.taa_render_samples = 16
w = bpy.data.worlds.new("w")
scn.world = w
w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.35, 0.4, 0.5, 1)
w.node_tree.nodes["Background"].inputs[1].default_value = 1.0
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 3
sun.rotation_euler = (math.radians(40), 0, math.radians(35))
scn.collection.objects.link(sun)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.lens = 24
cam.data.clip_end = 1000
scn.collection.objects.link(cam)
scn.camera = cam
start = Vector(G.game_to_blender_point((21.2, -9.5, 100.6)))
views = [(Vector((0, 0, 1.7)), 0), (Vector((0, 0, 1.7)), 90), (Vector((0, 0, 1.7)), 180), (Vector((0, 0, 1.7)), 270),
         (Vector((-25, -25, 30)), None)]
for k, (off, yaw) in enumerate(views):
    cam.location = start + off
    if yaw is None:
        tgt = start
    else:
        tgt = cam.location + Vector((math.cos(math.radians(yaw)), math.sin(math.radians(yaw)), 0.05))
    cam.rotation_euler = (tgt - cam.location).to_track_quat("-Z", "Y").to_euler()
    scn.render.filepath = "%s_%d.png" % (out, k)
    bpy.ops.render.render(write_still=True)
print("DONE")
