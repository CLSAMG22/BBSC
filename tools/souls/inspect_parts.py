"""Print texture names of parts and the mesh layout of weapons (debug helper)."""
import os
import sys

import numpy as np

import flver

G = r"D:/GAMES/shadPS4QtLauncher-win64-qt-2026-09-01-bf2c711/Games/CUSA03173/dvdroot_ps4/parts"
T = r"art/game-models/tex"
have = {n.lower() for n in os.listdir(T)}
for p in sys.argv[1:]:
    f, _ = flver.load(os.path.join(G, p + ".partsbnd.dcx"))
    names = [t["path"].replace("\\", "/").split("/")[-1] for t in f.textures]
    print(p, "textures:", [(n, n.rsplit(".", 1)[0].lower() + ".png" in have) for n in names][:10])
    for m in f.meshes:
        pos = m["attrs"]["position"][:, :3]
        bi = m["attrs"].get("bones")
        used = sorted(set(np.asarray(bi)[:, 0].tolist()))[:4] if bi is not None else []
        print("   mesh mat %d default %s bones %s centre %s size %s" % (
            m["material"], f.bones[m["default_bone"]]["name"] if 0 <= m["default_bone"] < len(f.bones) else None,
            [f.bones[b]["name"] for b in used if b < len(f.bones)], pos.mean(0).round(2), np.ptp(pos, 0).round(2)))
