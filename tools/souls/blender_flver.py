"""Blender side: build meshes from FLVER files (via flver.py) and render clay line-up previews.
    blender -b --factory-startup -P blender_flver.py -- out.png c5020 c5040 ... [parts:hd_m_2200+bd_m_2200+...]
FLVER is Y-up, left-handed; Blender is Z-up, right-handed: (x, y, z) -> (x, z, y), winding reversed."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Vector  # noqa: E402

import flver  # noqa: E402
import tpf  # noqa: E402

TEXDIR = r"art/game-models/tex"

GAME = r"D:/GAMES/shadPS4QtLauncher-win64-qt-2026-09-01-bf2c711/Games/CUSA03173/dvdroot_ps4"


def mesh_from_flver(f, name):
    verts, faces, uvs, fmat = [], [], [], []
    base = 0
    for m in f.meshes:
        a = m["attrs"]
        if "position" not in a or m["tris"] is None or len(m["tris"]) == 0:
            continue
        p = a["position"][:, :3]
        verts.append(np.stack([p[:, 0], p[:, 2], p[:, 1]], 1))
        t = m["tris"][:, ::-1] + base
        faces.append(t)
        fmat.append(np.full(len(t), m["material"], dtype=np.int32))
        uv = a.get("uv")
        if uv is not None:
            uvs.append(np.asarray(uv)[:, :2])
        else:
            uvs.append(np.zeros((len(p), 2), np.float32))
        base += len(p)
    if not verts:
        return None
    V = np.concatenate(verts)
    F = np.concatenate(faces)
    FM = np.concatenate(fmat)
    ok = (F < len(V)).all(1)
    F, FM = F[ok], FM[ok]
    me = bpy.data.meshes.new(name)
    me.from_pydata(V.tolist(), [], F.tolist())
    me.validate()
    uv = np.concatenate(uvs)
    layer = me.uv_layers.new(name="UV")
    loops = np.zeros(len(me.loops), dtype=np.int64)
    me.loops.foreach_get("vertex_index", loops)
    coords = uv[loops].copy()
    coords[:, 1] = 1.0 - coords[:, 1]
    layer.data.foreach_set("uv", coords.astype(np.float32).ravel())
    me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
    if len(me.polygons) == len(FM):
        me.polygons.foreach_set("material_index", FM)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def textured_materials(f, container, ob):
    """One Blender material per FLVER material, with its albedo (_a) texture from the TPF."""
    texs = {t["name"].lower(): t for t in tpf.textures_in(container)}
    os.makedirs(TEXDIR, exist_ok=True)
    for fm in f.materials:
        mat = bpy.data.materials.new(fm["name"] or "mat")
        mat.use_nodes = True
        nt = mat.node_tree
        bsdf = nt.nodes["Principled BSDF"]
        bsdf.inputs["Roughness"].default_value = 0.7
        start, count = fm["tex"]
        albedo = None
        for t in f.textures[start:start + count]:
            stem = t["path"].replace("\\", "/").split("/")[-1].rsplit(".", 1)[0]
            if stem.lower().endswith("_a") and stem.lower() in texs:
                albedo = stem
                break
        if not albedo:  # textures kept in a shared pack: use whatever was decoded into TEXDIR
            have = {n.lower(): n for n in os.listdir(TEXDIR)} if os.path.isdir(TEXDIR) else {}
            for t in f.textures[start:start + count]:
                stem = t["path"].replace("\\", "/").split("/")[-1].rsplit(".", 1)[0]
                if stem.lower().endswith("_a") and stem.lower() + ".png" in have:
                    albedo = have[stem.lower() + ".png"][:-4]
                    break
        if albedo:
            png = os.path.join(TEXDIR, albedo + ".png")
            if not os.path.exists(png):
                try:
                    tpf.to_png(texs[albedo.lower()], png)
                except Exception as exc:  # noqa: BLE001
                    print("TEX FAIL", albedo, exc)
            if os.path.exists(png):
                img = bpy.data.images.load(png, check_existing=True)
                tn = nt.nodes.new("ShaderNodeTexImage")
                tn.image = img
                nt.links.new(tn.outputs["Color"], bsdf.inputs["Base Color"])
                nt.links.new(tn.outputs["Alpha"], bsdf.inputs["Alpha"])
        ob.data.materials.append(mat)


def load_model(spec):
    """'c5020' (a character) or 'parts:hd_m_2200+bd_m_2200' (parts assembled into one figure)."""
    obs = []
    if spec.startswith("parts:"):
        for p in spec[6:].split("+"):
            path = os.path.join(GAME, "parts", p + ".partsbnd.dcx")
            f, _ = flver.load(path)
            ob = mesh_from_flver(f, p)
            if ob:
                textured_materials(f, path, ob)
                obs.append(ob)
    else:
        path = os.path.join(GAME, "chr", spec + ".chrbnd.dcx")
        f, _ = flver.load(path)
        ob = mesh_from_flver(f, spec)
        if ob:
            textured_materials(f, path, ob)
            obs.append(ob)
    return obs


def clay():
    mat = bpy.data.materials.new("Clay")
    mat.use_nodes = True
    b = mat.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.55, 0.52, 0.5, 1)
    b.inputs["Roughness"].default_value = 0.6
    return mat


def render_lineup(out, specs, size=420):
    scn = bpy.context.scene
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    scn.render.engine = "BLENDER_EEVEE"
    scn.render.resolution_x = scn.render.resolution_y = size
    scn.eevee.taa_render_samples = 16
    scn.view_settings.view_transform = "AgX"
    w = bpy.data.worlds.new("w")
    scn.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.08, 0.085, 0.1, 1)
    sun = bpy.data.objects.new("key", bpy.data.lights.new("key", "SUN"))
    sun.data.energy = 4
    sun.rotation_euler = (math.radians(50), 0, math.radians(-35))
    scn.collection.objects.link(sun)
    rim = bpy.data.objects.new("rim", bpy.data.lights.new("rim", "SUN"))
    rim.data.energy = 2
    rim.rotation_euler = (math.radians(70), 0, math.radians(160))
    scn.collection.objects.link(rim)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 50
    scn.collection.objects.link(cam)
    scn.camera = cam
    mat = clay()
    tiles = []
    for spec in specs:
        obs = load_model(spec)
        if not obs:
            print("NO MESH", spec)
            continue
        for o in obs:
            if not o.data.materials:
                o.data.materials.append(mat)
        pts = np.concatenate([np.array([o.matrix_world @ Vector(c) for c in o.bound_box]) for o in obs])
        lo, hi = pts.min(0), pts.max(0)
        c = (lo + hi) / 2
        h = max(hi - lo)
        # the game faces -Y after the axis swap; look from the front, a little above
        cam.location = Vector((c[0] + h * 0.35, c[1] - h * 1.9, c[2] + h * 0.25))
        cam.rotation_euler = (Vector(c) - cam.location).to_track_quat("-Z", "Y").to_euler()
        path = out + "_" + spec.replace(":", "_").replace("+", "_")[:40] + ".png"
        scn.render.filepath = path
        bpy.ops.render.render(write_still=True)
        tiles.append((spec, path))
        for o in obs:
            o.hide_render = True
    print("TILES", tiles)


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:]
    render_lineup(argv[0], argv[1:])
