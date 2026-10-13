"""Render a few frames of the rigged Hunter playing one animation (checks skinning + orientation).
    blender -b --factory-startup -P test_anim.py -- <anim> <out_prefix> [frames...]"""
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import lib_game as G  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
anim, out = argv[0], argv[1]
frames = [int(x) for x in argv[2:]] or [1, 20, 40, 60]
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
t = time.time()
ch = G.Character("Hunter", parts=["hd_m_2200", "bd_m_2200", "am_m_2200", "lg_m_2200"])
print("built in %.1f s" % (time.time() - t))
t = time.time()
n = ch.play(anim, start=1)
print("keyed %d frames in %.1f s" % (n, time.time() - t))
scn = bpy.context.scene
scn.render.engine = "BLENDER_EEVEE"
scn.render.resolution_x, scn.render.resolution_y = 640, 480
scn.eevee.taa_render_samples = 16
w = bpy.data.worlds.new("w")
scn.world = w
w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.25, 0.26, 0.3, 1)
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 3
sun.rotation_euler = (math.radians(50), 0, math.radians(30))
scn.collection.objects.link(sun)
bpy.ops.mesh.primitive_plane_add(size=20)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.lens = 35
scn.collection.objects.link(cam)
scn.camera = cam
for f in frames:
    scn.frame_set(f)
    pelvis = ch.arm.matrix_world @ ch.arm.pose.bones["Pelvis"].head
    cam.location = pelvis + Vector((3.2, -3.2, 0.6))
    cam.rotation_euler = (pelvis - cam.location).to_track_quat("-Z", "Y").to_euler()
    scn.render.filepath = "%s_%03d.png" % (out, f)
    bpy.ops.render.render(write_still=True)
print("DONE", n)
