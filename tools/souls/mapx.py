"""Map export for the cinematic (system Python): picks the map pieces and objects around a point from
a map's MSB, decodes their textures to PNG, and writes a JSON the Blender importer reads.

    python mapx.py <map id, e.g. m24_00_00_00> <x> <y> <z> <radius> <out.json> [max_tex]

MSB part record (Bloodborne): +0x28 position, +0x34 rotation (degrees), +0x40 scale; game space Y-up.
Map piece "m000400" -> map/<map>/<map>_000400.flver.dcx; objects "o000100" -> obj/o000100.objbnd.dcx.
Textures come from map/<area>/<area>_000N.tpfbhd (+ .patch) and the object's own TPF.
"""
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "gamedata"))

import flver  # noqa: E402
import msb  # noqa: E402
import souls  # noqa: E402
import tpf  # noqa: E402

GAME = r"D:/GAMES/shadPS4QtLauncher-win64-qt-2026-09-01-bf2c711/Games/CUSA03173/dvdroot_ps4"
TEXOUT = r"art/game-models/maptex"
FAR = os.environ.get("MAP_FAR") == "1"  # keep the distant city (for skyline shots)


def parts_with_transforms(map_id):
    m = msb.Msb(os.path.join(GAME, "map", "MapStudio", map_id + ".msb.dcx"))
    d = m.data
    out = []
    for o, p in zip(m.lists.get("PARTS_PARAM_ST", []), m.parts):
        pos = struct.unpack_from("<3f", d, o + 0x28)
        rot = struct.unpack_from("<3f", d, o + 0x34)
        scl = struct.unpack_from("<3f", d, o + 0x40)
        q = dict(p)
        q.update(pos=pos, rot=rot, scale=scl)
        out.append(q)
    return out


def texture_index(area):
    idx = {}
    folder = os.path.join(GAME, "map", area)
    for f in sorted(os.listdir(folder)):
        if f.endswith(".tpfbhd"):
            idx.update(souls.read_bxf4(os.path.join(folder, f)))
    return idx


def decode(name, getter, max_size):
    """Decode texture `name` (no extension) to TEXOUT/name.png; getter() -> TPF textures list."""
    out = os.path.join(TEXOUT, name + ".png")
    if os.path.exists(out):
        return out
    try:
        for t in getter():
            if t["name"].lower() == name.lower():
                import io
                im = Image.open(io.BytesIO(tpf.dds_bytes(t)))
                im.load()
                if max(im.size) > max_size:
                    s = max_size / max(im.size)
                    im = im.resize((max(1, int(im.size[0] * s)), max(1, int(im.size[1] * s))), Image.LANCZOS)
                im.save(out)
                return out
    except Exception as exc:  # noqa: BLE001
        print("tex fail", name, exc)
    return None


def model_path(map_id, p):
    if p["type"] == "MapPiece":
        return os.path.join(GAME, "map", map_id, "%s_%s.flver.dcx" % (map_id, p["model"][1:]))
    return os.path.join(GAME, "obj", p["model"] + ".objbnd.dcx")


def model_boxes(map_id, parts):
    """Bounding box of every map piece / object model used by the map (cached)."""
    cache = os.path.join(r"art/game-models/maps", map_id + "_boxes.json")
    boxes = {}
    if os.path.exists(cache):
        with open(cache, encoding="utf-8") as fh:
            boxes = json.load(fh)
    changed = False
    for p in parts:
        if p["type"] not in ("MapPiece", "Object") or not p["model"] or p["model"] in boxes:
            continue
        path = model_path(map_id, p)
        box = None
        if os.path.exists(path):
            try:
                f, _ = flver.load(path)
                pos = [m["attrs"]["position"][:, :3] for m in f.meshes if "position" in m["attrs"]]
                if pos:
                    pos = np.concatenate(pos)
                    box = [pos.min(0).tolist(), pos.max(0).tolist()]
            except Exception as exc:  # noqa: BLE001
                print("box fail", p["model"], exc)
        boxes[p["model"]] = box
        changed = True
    if changed:
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        with open(cache, "w", encoding="utf-8") as fh:
            json.dump(boxes, fh)
    return {k: v for k, v in boxes.items() if v}


def main():
    map_id = sys.argv[1]
    cx, cy, cz, radius = (float(v) for v in sys.argv[2:6])
    out_json = sys.argv[6]
    max_tex = int(sys.argv[7]) if len(sys.argv) > 7 else 1024
    area = map_id[:3]
    os.makedirs(TEXOUT, exist_ok=True)
    tex_idx = texture_index(area)
    parts = parts_with_transforms(map_id)
    boxes = model_boxes(map_id, parts)
    keep = []
    for p in parts:
        if p["type"] not in ("MapPiece", "Object") or not p["model"]:
            continue
        box = boxes.get(p["model"])
        if box is None:
            continue
        # sky domes (m90*) and the low-detail distant city (m008*) duplicate the real geometry
        if p["model"].startswith(("m90", "m008")) and not FAR:
            continue
        if np.linalg.norm(np.array(box[1]) - np.array(box[0])) > 420 and not FAR:
            continue
        # the part's box in the world (rotation about Y dominates; use the box radius to be safe)
        lo, hi = np.array(box[0]), np.array(box[1])
        centre = (lo + hi) / 2 * np.array(p["scale"])
        half = np.linalg.norm((hi - lo) / 2 * np.array(p["scale"]))
        ry = np.radians(p["rot"][1])
        c = np.array([centre[0] * np.cos(ry) + centre[2] * np.sin(ry), centre[1],
                      -centre[0] * np.sin(ry) + centre[2] * np.cos(ry)]) + np.array(p["pos"])
        if np.linalg.norm(c - np.array([cx, cy, cz])) > radius + half:
            continue
        keep.append(p)
    models = {}
    for p in keep:
        mname = p["model"]
        if mname in models:
            continue
        path = model_path(map_id, p)
        if not os.path.exists(path):
            continue
        try:
            f, _ = flver.load(path)
        except Exception as exc:  # noqa: BLE001
            print("flver fail", mname, exc)
            continue
        texs = {}
        own = None
        for t in f.textures:
            stem = t["path"].replace("\\", "/").split("/")[-1].rsplit(".", 1)[0]
            low = stem.lower()
            if not (low.endswith("_a") or low.endswith("_n")):
                continue
            key = low + ".tpf.dcx"
            if key in tex_idx:
                png = decode(stem, lambda k=key: tpf.read_tpf(souls.bxf_read(tex_idx[k]))[1], max_tex)
            else:
                if own is None:
                    try:
                        own = tpf.textures_in(path)
                    except Exception:  # noqa: BLE001
                        own = []
                png = decode(stem, lambda o=own: o, max_tex)
            if png:
                texs[low] = png
        models[mname] = dict(path=path, textures=texs)
    placed = [dict(model=p["model"], name=p["name"], type=p["type"], pos=p["pos"], rot=p["rot"], scale=p["scale"])
              for p in keep if p["model"] in models]
    import btl
    import glob
    lights = []
    for bpath in sorted(glob.glob(os.path.join(GAME, "map", map_id, "*.btl.dcx")))[:1]:
        for l in btl.read_btl(bpath):
            x, y, z = l["pos"]
            if (x - cx) ** 2 + (y - cy) ** 2 + (z - cz) ** 2 <= (radius + 20) ** 2:
                lights.append(l)
    with open(out_json, "w", encoding="utf-8") as fh:
        json.dump(dict(map=map_id, center=[cx, cy, cz], radius=radius, models=models, parts=placed,
                       lights=lights), fh, indent=0)
    print("map export: %d parts, %d models, textures in %s" % (len(placed), len(models), TEXOUT))


if __name__ == "__main__":
    main()
