"""Size, skeleton and texture names of character models (to tell what they are)."""
import os
import sys

import numpy as np

import flver

sys.stdout.reconfigure(encoding="utf-8")
G = r"D:/GAMES/shadPS4QtLauncher-win64-qt-2026-09-01-bf2c711/Games/CUSA03173/dvdroot_ps4/chr"
for c in sys.argv[1:]:
    try:
        f, name = flver.load(os.path.join(G, c + ".chrbnd.dcx"))
    except Exception as e:  # noqa: BLE001
        print(c, "ERR", e)
        continue
    pos = [m["attrs"]["position"] for m in f.meshes if "position" in m["attrs"]]
    pos = np.concatenate(pos) if pos else np.zeros((1, 3))
    size = pos.max(0) - pos.min(0)
    special = [b["name"] for b in f.bones if any(k in b["name"].lower() for k in ("tail", "jaw", "wing", "ear"))][:3]
    tex = sorted({t["path"].replace("\\", "/").split("/")[-1] for t in f.textures})[:4]
    tv = sum(m["verts"] for m in f.meshes)
    print("%s size %4.2f x %4.2f x %4.2f  bones %3d verts %6d  %s  %s" % (c, size[0], size[1], size[2], len(f.bones), tv, special, tex))
