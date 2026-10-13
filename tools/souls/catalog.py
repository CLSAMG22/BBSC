"""Stick-figure contact sheets of a character's animations, to find moves by eye.
    python catalog.py <chr> <out_dir> <prefix>[,<prefix>...] [cells_per_sheet]
Each cell: five poses (blue -> red over time) seen from the side, offset by root motion."""
import glob
import os
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np
from PIL import Image, ImageDraw

import hka
import hkx
import rig

RAW = r"art/game-models/raw"
CELL = 150


def poses(args):
    path, sk = args
    try:
        an = hka.load(path)
    except Exception as exc:  # noqa: BLE001
        return path, None, str(exc)
    by_track = {b: t for t, b in enumerate(an.track_to_bone)}
    out = []
    for k in range(5):
        fr = (an.frames - 1) * k / 4.0
        s = an.sample(fr)
        loc = [s[by_track[i]] if i in by_track else sk["ref"][i] for i in range(len(sk["bones"]))]
        w = rig.havok_world(sk, loc)
        p = np.array([m[:3, 3] for m in w])
        if an.root is not None:
            rs = an.root[min(int(round(fr)), len(an.root) - 1)]
            ang = rs[3]
            c, s_ = np.cos(ang), np.sin(ang)
            R = np.array([[c, 0, s_], [0, 1, 0], [-s_, 0, c]])
            p = p @ R.T + rs[:3]
        out.append(p)
    return path, (np.array(out), an.duration), None


def main():
    chr_id, out_dir, prefixes = sys.argv[1], sys.argv[2], sys.argv[3].split(",")
    per = int(sys.argv[4]) if len(sys.argv) > 4 else 96
    d = os.path.join(RAW, chr_id + "_anims")
    with open(os.path.join(d, "skeleton.hkx"), "rb") as fh:
        sk = hkx.read_skeleton(hkx.Packfile(fh.read()))
    files = sorted(f for f in glob.glob(os.path.join(d, "*.hkx"))
                   if any(os.path.basename(f).startswith(p) for p in prefixes))
    os.makedirs(out_dir, exist_ok=True)
    with ProcessPoolExecutor(8) as ex:
        res = list(ex.map(poses, [(f, sk) for f in files], chunksize=8))
    parents = sk["parents"]
    # scale from the reference pose height
    ref_h = max(1.0, np.ptp(np.array([m[:3, 3] for m in rig.havok_world(sk, sk["ref"])])[:, 1]))
    cols = 12
    for sheet in range(0, len(res), per):
        chunk = res[sheet:sheet + per]
        rows = (len(chunk) + cols - 1) // cols
        img = Image.new("RGB", (cols * CELL, rows * CELL), (18, 18, 22))
        dr = ImageDraw.Draw(img)
        for i, (path, data, err) in enumerate(chunk):
            x0, y0 = (i % cols) * CELL, (i // cols) * CELL
            dr.rectangle((x0, y0, x0 + CELL - 1, y0 + CELL - 1), outline=(45, 45, 55))
            name = os.path.basename(path)[:-4]
            if data is None:
                dr.text((x0 + 3, y0 + 3), name + " ERR", fill=(255, 80, 80))
                continue
            P, dur = data
            span = max(ref_h * 1.2, np.ptp(P[:, :, 2]) + 0.6, np.ptp(P[:, :, 1]) + 0.4)
            sc = (CELL - 20) / span
            cz = (P[:, :, 2].max() + P[:, :, 2].min()) / 2
            for k in range(5):
                col = (int(60 + 48 * k), 90, int(255 - 48 * k))
                pts = P[k]
                for b, p in enumerate(parents):
                    if p < 0:
                        continue
                    a = (x0 + CELL / 2 + (pts[b, 2] - cz) * sc, y0 + CELL - 12 - pts[b, 1] * sc)
                    c = (x0 + CELL / 2 + (pts[p, 2] - cz) * sc, y0 + CELL - 12 - pts[p, 1] * sc)
                    dr.line([a, c], fill=col, width=1)
            dr.text((x0 + 3, y0 + 2), name.replace("a0", "")[:12], fill=(230, 230, 120))
            dr.text((x0 + 3, y0 + CELL - 12), "%.1fs" % dur, fill=(160, 160, 160))
        img.save(os.path.join(out_dir, "%s_%s_%02d.png" % (chr_id, "+".join(prefixes), sheet // per)))
    print("sheets for", len(res), "animations")


if __name__ == "__main__":
    main()
