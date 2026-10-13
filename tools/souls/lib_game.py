"""Blender side of the game-asset pipeline: rigged, skinned, textured characters from the game's own
files, driven by its Havok animations.

    ch = Character("Hunter", parts=["hd_m_2200", "bd_m_2200", "am_m_2200", "lg_m_2200"])
    ch.play("a000_009000", start=1, loc=(0, 0, 0), yaw=0)        # keyframes on the timeline

Game space is Y-up; Blender is Z-up: every matrix is converted with S = swap(Y, Z) (S M S).
Pose keys: matrix_basis = rest^-1 @ parent_rest @ parent_pose^-1 @ pose (armature space).
"""
import glob
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Matrix  # noqa: E402

import flver  # noqa: E402
import hka  # noqa: E402
import hkx  # noqa: E402
import rig  # noqa: E402
import souls  # noqa: E402

GAME = r"D:/GAMES/shadPS4QtLauncher-win64-qt-2026-09-01-bf2c711/Games/CUSA03173/dvdroot_ps4"
RAW = r"art/game-models/raw"
TEXDIR = r"art/game-models/tex"
S = np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, 1, 0, 0], [0, 0, 0, 1]], dtype=np.float64)


def to_blender(m):
    return S @ m @ S


# ---------------------------------------------------------------- files
def anibnd_dir(chr_id):
    """Unpack <chr>.anibnd (skeleton + TAE) and every <chr>_*.anibnd into RAW/<chr>_anims once."""
    out = os.path.join(RAW, chr_id + "_anims")
    stamp = os.path.join(out, ".done")
    if not os.path.exists(stamp):
        os.makedirs(out, exist_ok=True)
        for path in sorted(glob.glob(os.path.join(GAME, "chr", chr_id + "*.anibnd.dcx"))):
            base = os.path.basename(path)
            if not (base.startswith(chr_id + ".") or base.startswith(chr_id + "_")):
                continue
            try:
                files = souls.read_bnd4(souls.open_any(path))
            except Exception as exc:  # noqa: BLE001
                print("anibnd", base, exc)
                continue
            for _, name, blob in files:
                low = name.replace("\\", "/").lower()
                if low.endswith(".hkx") and ("/hkx/" in low):
                    with open(os.path.join(out, os.path.basename(low)), "wb") as fh:
                        fh.write(blob)
        open(stamp, "w").close()
    return out


def skeleton(chr_id):
    path = os.path.join(anibnd_dir(chr_id), "skeleton.hkx")
    with open(path, "rb") as fh:
        return hkx.read_skeleton(hkx.Packfile(fh.read()))


def anim_path(chr_id, anim):
    p = os.path.join(anibnd_dir(chr_id), anim.lower() + ".hkx")
    return p if os.path.exists(p) else None


# ---------------------------------------------------------------- materials
def material_for(f, fm, container_texs, name):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.72
    start, count = fm["tex"]
    have = {n.lower(): n for n in os.listdir(TEXDIR)} if os.path.isdir(TEXDIR) else {}
    found = {}
    for t in f.textures[start:start + count]:
        stem = t["path"].replace("\\", "/").split("/")[-1].rsplit(".", 1)[0]
        low = stem.lower()
        for suffix in ("_a", "_n", "_r"):
            if low.endswith(suffix) and suffix not in found and low + ".png" in have:
                found[suffix] = os.path.join(TEXDIR, have[low + ".png"])
    if "_a" in found:
        tn = nt.nodes.new("ShaderNodeTexImage")
        tn.image = bpy.data.images.load(found["_a"], check_existing=True)
        nt.links.new(tn.outputs["Color"], bsdf.inputs["Base Color"])
        nt.links.new(tn.outputs["Alpha"], bsdf.inputs["Alpha"])
    else:  # texture not found: weathered iron / wood rather than plain white
        bsdf.inputs["Base Color"].default_value = (0.055, 0.045, 0.038, 1)
        bsdf.inputs["Roughness"].default_value = 0.55
    if "_n" in found:
        tn = nt.nodes.new("ShaderNodeTexImage")
        tn.image = bpy.data.images.load(found["_n"], check_existing=True)
        tn.image.colorspace_settings.name = "Non-Color"
        nm = nt.nodes.new("ShaderNodeNormalMap")
        nm.inputs["Strength"].default_value = 0.8
        nt.links.new(tn.outputs["Color"], nm.inputs["Color"])
        nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
    return mat


def root_at(root, fr):
    """Root-motion sample at fractional frame `fr` (linear; yaw too)."""
    fr = min(max(fr, 0.0), len(root) - 1)
    i = int(fr)
    j = min(i + 1, len(root) - 1)
    w = fr - i
    return root[i] * (1 - w) + root[j] * w


# ---------------------------------------------------------------- characters
class Character:
    def __init__(self, name, parts=None, chr_id="c0000", tex_containers=(), mask=None):
        """mask: display-group numbers to show. Enemy models hold every variant (outfits, weapons);
        a material named "#NN#..." belongs to group NN and is dropped unless NN is in `mask`."""
        self.name = name
        self.mask = set(mask) if mask is not None else None
        self.chr_id = chr_id
        self.sk = skeleton(chr_id)
        base, _ = flver.load(os.path.join(GAME, "chr", chr_id + ".chrbnd.dcx"))
        flvers = []  # (flver, container path, label)
        if parts:
            for p in parts:
                path = os.path.join(GAME, "parts", p + ".partsbnd.dcx")
                pf, _ = flver.load(path)
                flvers.append((pf, path, p))
        else:
            flvers.append((base, os.path.join(GAME, "chr", chr_id + ".chrbnd.dcx"), chr_id))
        # Blender renames empty / repeated bone names: make them unique up front
        for fl in [base] + [pf for pf, _, _ in flvers]:
            seen = set()
            for i, b in enumerate(fl.bones):
                if not b["name"] or b["name"] in seen:
                    b["name"] = "%s_%d" % (b["name"] or "bone", i)
                seen.add(b["name"])
        # one bone list: the character's own skeleton, plus bones only a part has (cloth, straps)
        self.bones = [dict(b) for b in base.bones]
        names = {b["name"]: i for i, b in enumerate(self.bones)}
        for pf, _, _ in flvers:
            for b in pf.bones:
                if b["name"] not in names:
                    nb = dict(b)
                    pname = pf.bones[b["parent"]]["name"] if b["parent"] >= 0 else None
                    nb["parent_name"] = pname
                    names[b["name"]] = len(self.bones)
                    self.bones.append(nb)
        for b in self.bones:
            if "parent_name" in b:
                b["parent"] = names.get(b["parent_name"], -1) if b["parent_name"] else -1
        self.names = names
        self.rest_world = rig.flver_world(self.bones)
        self._build_armature()
        self.meshes = []
        for pf, path, label in flvers:
            self._build_mesh(pf, label)

    def _build_armature(self):
        arm = bpy.data.armatures.new(self.name + "_rig")
        ob = bpy.data.objects.new(self.name, arm)
        bpy.context.scene.collection.objects.link(ob)
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.mode_set(mode="EDIT")
        ebs = []
        for i, b in enumerate(self.bones):
            eb = arm.edit_bones.new(b["name"])
            m = to_blender(self.rest_world[i])
            eb.head = (0, 0, 0)
            eb.tail = (0, 0.05, 0)
            eb.matrix = Matrix(m.tolist())
            ebs.append(eb)
        for i, b in enumerate(self.bones):
            if b["parent"] >= 0:
                ebs[i].parent = ebs[b["parent"]]
        bpy.ops.object.mode_set(mode="OBJECT")
        self.arm = ob
        self.rest = {bn.name: np.array(bn.matrix_local) for bn in arm.bones}
        for pb in ob.pose.bones:
            pb.rotation_mode = "QUATERNION"

    def _shown(self, f, m):
        if self.mask is None:
            return True
        name = f.materials[m["material"]]["name"] or ""
        if name.startswith("#") and name.find("#", 1) > 1 and name[1:name.find("#", 1)].isdigit():
            return int(name[1:name.find("#", 1)]) in self.mask
        return True

    def _build_mesh(self, f, label):
        verts, faces, uvs, fmat, groups = [], [], [], [], []
        base = 0
        for m in f.meshes:
            if not self._shown(f, m):
                continue
            a = m["attrs"]
            if "position" not in a or m["tris"] is None or len(m["tris"]) == 0:
                continue
            p = a["position"][:, :3]
            verts.append(np.stack([p[:, 0], p[:, 2], p[:, 1]], 1))
            faces.append(m["tris"][:, ::-1] + base)
            fmat.append(np.full(len(m["tris"]), m["material"], dtype=np.int32))
            uv = a.get("uv")
            uvs.append(np.asarray(uv)[:, :2] if uv is not None else np.zeros((len(p), 2), np.float32))
            bi = a.get("bones")
            bw = a.get("weights")
            if bi is None:
                bi = np.full((len(p), 4), m["default_bone"], dtype=np.int64)
                bw = np.zeros((len(p), 4))
                bw[:, 0] = 1
            if bw is None:
                bw = np.zeros((len(p), 4))
                bw[:, 0] = 1
            bi = np.asarray(bi)[:, :4]
            if m["bone_map"]:
                bm = np.array(m["bone_map"])
                bi = bm[np.clip(bi, 0, len(bm) - 1)]
            groups.append((np.asarray(bi), np.asarray(bw)[:, :4], base))
            base += len(p)
        if not verts:
            return
        V = np.concatenate(verts)
        F = np.concatenate(faces)
        FM = np.concatenate(fmat)
        ok = (F < len(V)).all(1)
        F, FM = F[ok], FM[ok]
        me = bpy.data.meshes.new(label)
        me.from_pydata(V.tolist(), [], F.tolist())
        me.validate()
        uv = np.concatenate(uvs)
        layer = me.uv_layers.new(name="UV")
        loops = np.zeros(len(me.loops), dtype=np.int64)
        me.loops.foreach_get("vertex_index", loops)
        c = uv[loops].copy()
        c[:, 1] = 1.0 - c[:, 1]
        layer.data.foreach_set("uv", c.astype(np.float32).ravel())
        me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
        if len(me.polygons) == len(FM):
            me.polygons.foreach_set("material_index", FM)
        for k, fm in enumerate(f.materials):
            me.materials.append(material_for(f, fm, None, "%s_%s_%d" % (self.name, fm["name"] or "mat", k)))
        ob = bpy.data.objects.new(self.name + "_" + label, me)
        bpy.context.scene.collection.objects.link(ob)
        # skin: vertex groups by bone name
        vgs = {}
        for bi, bw, start in groups:
            for v in range(len(bi)):
                for k in range(4):
                    w = float(bw[v, k])
                    if w <= 0.0:
                        continue
                    bone = f.bones[int(bi[v, k])]["name"] if 0 <= int(bi[v, k]) < len(f.bones) else None
                    if bone is None:
                        continue
                    g = vgs.get(bone)
                    if g is None:
                        g = vgs[bone] = ob.vertex_groups.new(name=bone)
                    g.add([start + v], w, "ADD")
        mod = ob.modifiers.new("rig", "ARMATURE")
        mod.object = self.arm
        ob.parent = self.arm
        self.meshes.append(ob)

    # ---- animation
    def play(self, anim, start=1, fps=24.0, speed=1.0, loc=(0, 0, 0), yaw=0.0, root_motion=True,
             frames=None, clip=None, times=None):
        """Key the animation from `start`. clip=(t0, t1) seconds of the source; frames = how many
        timeline frames (default: the clip length / speed). loc/yaw place it in the world.
        times: explicit source time (s) for each timeline frame (speed ramps, freezes)."""
        path = anim_path(self.chr_id, anim)
        if path is None:
            raise FileNotFoundError(anim)
        an = hka.load(path)
        t0, t1 = clip if clip else (0.0, an.duration)
        n = frames or max(1, int(round((t1 - t0) / speed * fps)))
        if times is not None:
            n = len(times) - 1
        place = np.eye(4)
        place[:3, :3] = rig.rot_y(yaw)[:3, :3]
        place[:3, 3] = (loc[0], loc[2], loc[1])  # loc is Blender (x, y, z) -> game (x, z, y)
        by_track = {}
        for ti, bone in enumerate(an.track_to_bone):
            by_track[bone] = ti
        results = {}
        for k in range(n + 1):
            t = times[k] if times is not None else t0 + k * speed / fps
            t = min(max(t, 0.0), an.duration)
            fr = min(t / an.frame_dur, an.frames - 1)
            samples = an.sample(fr)
            locals_ = []
            for hi in range(len(self.sk["bones"])):
                ti = by_track.get(hi)
                locals_.append(samples[ti] if ti is not None else self.sk["ref"][hi])
            worlds = rig.pose_worlds(self.bones, self.sk, locals_)
            root = np.eye(4)
            if root_motion and an.root is not None:
                rs = root_at(an.root, fr)
                r0 = root_at(an.root, (times[0] if times is not None else t0) / an.frame_dur)
                root[:3, 3] = rs[:3] - r0[:3]
                root[:3, :3] = rig.rot_y(rs[3] - r0[3])
            worlds = [place @ root @ w for w in worlds]
            results[start + k] = [to_blender(w) for w in worlds]
        self._key(results)
        return n

    def play_loop(self, anim, start, frames, fps=24.0, speed=1.0, loc=(0, 0, 0), yaw=0.0):
        """Loop a cycle (walk / run) for `frames` timeline frames, root motion accumulating."""
        path = anim_path(self.chr_id, anim)
        an = hka.load(path)
        by_track = {bone: ti for ti, bone in enumerate(an.track_to_bone)}
        place = np.eye(4)
        place[:3, :3] = rig.rot_y(yaw)[:3, :3]
        place[:3, 3] = (loc[0], loc[2], loc[1])
        total = an.root[-1][:3] - an.root[0][:3] if an.root is not None else np.zeros(3)
        results = {}
        for k in range(frames + 1):
            t = k * speed / fps
            cyc = int(t // an.duration)
            lt = t - cyc * an.duration
            fr = min(lt / an.frame_dur, an.frames - 1)
            samples = an.sample(fr)
            locals_ = [samples[by_track[i]] if i in by_track else self.sk["ref"][i] for i in range(len(self.sk["bones"]))]
            worlds = rig.pose_worlds(self.bones, self.sk, locals_)
            root = np.eye(4)
            if an.root is not None:
                rs = root_at(an.root, fr)
                root[:3, 3] = rs[:3] - an.root[0][:3] + total * cyc
            results[start + k] = [to_blender(place @ root @ w) for w in worlds]
        self._key(results)
        return frames

    def _key(self, results):
        ob = self.arm
        if ob.animation_data is None:
            ob.animation_data_create()
        rest = self.rest
        names = [b["name"] for b in self.bones]
        parent = [b["parent"] for b in self.bones]
        rest_inv = {n: np.linalg.inv(rest[n]) for n in names}
        keys = {n: ([], [], [], []) for n in names}
        for frame in sorted(results):
            W = results[frame]
            for i, n in enumerate(names):
                p = parent[i]
                if p >= 0:
                    basis = rest_inv[n] @ rest[names[p]] @ np.linalg.inv(W[p]) @ W[i]
                else:
                    basis = rest_inv[n] @ W[i]
                mb = Matrix(basis.tolist())
                t, q, s = mb.decompose()
                kt, kq, ks, kf = keys[n]
                kt.append(t)
                kq.append(q)
                ks.append(s)
                kf.append(frame)
        for n, (kt, kq, ks, kf) in keys.items():
            pb = ob.pose.bones[n]
            prev = None
            for t, q, s, fr in zip(kt, kq, ks, kf):
                if prev is not None and prev.dot(q) < 0:
                    q = -q  # keep quaternions on one hemisphere so they interpolate the short way
                prev = q
                pb.location = t
                pb.rotation_quaternion = q
                pb.scale = s
                pb.keyframe_insert("location", frame=fr, group=n)
                pb.keyframe_insert("rotation_quaternion", frame=fr, group=n)
                pb.keyframe_insert("scale", frame=fr, group=n)


# ---------------------------------------------------------------- maps
def game_rot(rx, ry, rz):
    """MSB rotation (degrees) -> 3x3 in game space. Order Z, then X, then Y (DSMapStudio)."""
    r = np.radians
    return rig.rot_y(r(ry)) @ rig.rot_x(r(rx)) @ rig.rot_z(r(rz))


def static_mesh(f, name, textures, cache_mats):
    verts, faces, uvs, fmat = [], [], [], []
    base = 0
    for m in f.meshes:
        a = m["attrs"]
        if "position" not in a or m["tris"] is None or len(m["tris"]) == 0:
            continue
        p = a["position"][:, :3]
        verts.append(np.stack([p[:, 0], p[:, 2], p[:, 1]], 1))
        faces.append(m["tris"][:, ::-1] + base)
        fmat.append(np.full(len(m["tris"]), m["material"], dtype=np.int32))
        uv = a.get("uv")
        uvs.append(np.asarray(uv)[:, :2] if uv is not None else np.zeros((len(p), 2), np.float32))
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
    c = uv[loops].copy()
    c[:, 1] = 1.0 - c[:, 1]
    layer.data.foreach_set("uv", c.astype(np.float32).ravel())
    me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
    if len(me.polygons) == len(FM):
        me.polygons.foreach_set("material_index", FM)
    for k, fm in enumerate(f.materials):
        start, count = fm["tex"]
        a_png = n_png = None
        for t in f.textures[start:start + count]:
            low = t["path"].replace("\\", "/").split("/")[-1].rsplit(".", 1)[0].lower()
            if low.endswith("_a") and not a_png and low in textures:
                a_png = textures[low]
            if low.endswith("_n") and not n_png and low in textures:
                n_png = textures[low]
        key = (a_png, n_png)
        mat = cache_mats.get(key)
        if mat is None:
            mat = bpy.data.materials.new(os.path.basename(a_png or "plain")[:-4] if a_png else "plain")
            mat.use_nodes = True
            nt = mat.node_tree
            bsdf = nt.nodes["Principled BSDF"]
            bsdf.inputs["Roughness"].default_value = 0.78
            if a_png:
                tn = nt.nodes.new("ShaderNodeTexImage")
                tn.image = bpy.data.images.load(a_png, check_existing=True)
                nt.links.new(tn.outputs["Color"], bsdf.inputs["Base Color"])
                nt.links.new(tn.outputs["Alpha"], bsdf.inputs["Alpha"])
            else:
                bsdf.inputs["Base Color"].default_value = (0.08, 0.08, 0.08, 1)
            if n_png:
                tn = nt.nodes.new("ShaderNodeTexImage")
                tn.image = bpy.data.images.load(n_png, check_existing=True)
                tn.image.colorspace_settings.name = "Non-Color"
                nm = nt.nodes.new("ShaderNodeNormalMap")
                nm.inputs["Strength"].default_value = 0.7
                nt.links.new(tn.outputs["Color"], nm.inputs["Color"])
                nt.links.new(nm.outputs["Normal"], bsdf.inputs["Normal"])
            cache_mats[key] = mat
        me.materials.append(mat)
    return me


def load_map(json_path, collection=None):
    import json
    with open(json_path, encoding="utf-8") as fh:
        data = json.load(fh)
    col = collection or bpy.data.collections.new("map_" + os.path.basename(json_path)[:-5])
    if collection is None:
        bpy.context.scene.collection.children.link(col)
    meshes, mats = {}, {}
    for mname, info in data["models"].items():
        if not info["textures"]:
            continue  # untextured pieces are shadow / occlusion proxies (grey slabs over the real thing)
        if mname.startswith("m90"):
            continue  # sky domes: added per shot as emissive, camera-locked skies (trailer tlib.sky_dome)
        try:
            f, _ = flver.load(info["path"])
            meshes[mname] = static_mesh(f, mname, info["textures"], mats)
        except Exception as exc:  # noqa: BLE001
            print("map model fail", mname, exc)
    obs = []
    cx, cy, cz = data["center"]
    centre = np.array([cx, cz, cy])  # Blender space
    lod_near = float(os.environ.get("LOD_NEAR", "90"))
    for p in data["parts"]:
        me = meshes.get(p["model"])
        if me is None:
            continue
        m = np.eye(4)
        m[:3, :3] = game_rot(*p["rot"]) @ np.diag(p["scale"])
        m[:3, 3] = p["pos"]
        mw = Matrix(to_blender(m).tolist())
        if p["model"].startswith("m05"):
            # m05xxxx are the low-detail stand-ins of the pieces around them: keep only far ones
            lo = np.array([min(v.co[i] for v in me.vertices) for i in range(3)]) if len(me.vertices) else np.zeros(3)
            hi = np.array([max(v.co[i] for v in me.vertices) for i in range(3)]) if len(me.vertices) else np.zeros(3)
            c = np.array(mw @ Matrix.Translation(((lo + hi) / 2).tolist()).to_translation())
            if np.linalg.norm((c - centre)[:2]) < lod_near:
                continue
        ob = bpy.data.objects.new(p["name"], me)
        ob.matrix_world = mw
        col.objects.link(ob)
        obs.append(ob)
    return col, obs


def game_to_blender_point(p):
    return (p[0], p[2], p[1])


# ---------------------------------------------------------------- weapons and props on bones
def texdict():
    return {n[:-4].lower(): os.path.join(TEXDIR, n) for n in os.listdir(TEXDIR) if n.lower().endswith(".png")}


def attach_part(ch, part, bone, name=None):
    """A rigid part (weapon) following one bone of a Character."""
    path = os.path.join(GAME, "parts", part + ".partsbnd.dcx")
    f, _ = flver.load(path)
    me = static_mesh(f, part, texdict(), {})
    ob = bpy.data.objects.new(name or "%s_%s" % (ch.name, part), me)
    bpy.context.scene.collection.objects.link(ob)
    con = ob.constraints.new("CHILD_OF")
    con.target = ch.arm
    con.subtarget = bone
    con.inverse_matrix = Matrix.Identity(4)
    con.set_inverse_pending = False
    ch.meshes.append(ob)
    return ob


HUNTER_FACE_M = ["fc_m_0000", "fg_a_0100"]
HUNTER_FACE_F = ["fc_f_0000", "hr_a_0101", "fg_a_0100"]


def hunter(name, outfit, gender="m", right=None, left=None, face=True):
    """A hunter from an armor set id (e.g. 2200) with weapons in each hand."""
    parts = ["%s_%s_%04d" % (slot, gender, outfit) for slot in ("hd", "bd", "am", "lg")]
    if face:
        parts = (HUNTER_FACE_M if gender == "m" else HUNTER_FACE_F) + parts
    parts = [p for p in parts if os.path.exists(os.path.join(GAME, "parts", p + ".partsbnd.dcx"))]
    ch = Character(name, parts=parts)
    if right:
        attach_part(ch, right, "R_Weapon")
    if left:
        attach_part(ch, left, "L_Weapon")
    return ch


def yaw_facing(dx, dy):
    """Yaw so a character faces the Blender direction (dx, dy)."""
    return math.atan2(-dx, -dy)
