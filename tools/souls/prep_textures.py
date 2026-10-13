"""Decode the textures of parts / characters into TEXDIR (system Python; skips what exists).
    python prep_textures.py hd_m_2200 bd_m_2200 wp_a_0800 c5020 ...   (chr ids also pull <id>_2.tpf)"""
import os
import sys

import tpf

GAME = r"D:/GAMES/shadPS4QtLauncher-win64-qt-2026-09-01-bf2c711/Games/CUSA03173/dvdroot_ps4"
TEXDIR = r"art/game-models/tex"


def containers(name):
    if name.startswith("c") and name[1:5].isdigit():
        yield os.path.join(GAME, "chr", name + ".chrbnd.dcx")
        yield os.path.join(GAME, "chr", name + "_2.tpf.dcx")
    elif name == "common_body":
        yield os.path.join(GAME, "parts", "common_body.tpf.dcx")
    else:
        yield os.path.join(GAME, "parts", name + ".partsbnd.dcx")


_MAP_IDX = None


def map_index():
    """Every texture in the map archives (enemy textures live there as m24_01_<chr>_<name>)."""
    global _MAP_IDX
    if _MAP_IDX is None:
        import glob
        import souls
        _MAP_IDX = {}
        for bhd in sorted(glob.glob(os.path.join(GAME, "map", "*", "*.tpfbhd"))):
            try:
                for k, v in souls.read_bxf4(bhd).items():
                    _MAP_IDX.setdefault(k.lower(), v)
            except Exception:  # noqa: BLE001
                pass
    return _MAP_IDX


def from_maps(name, have):
    """Decode a character's textures that are only stored in the map archives."""
    import flver
    import souls
    try:
        f, _ = flver.load(os.path.join(GAME, "chr", name + ".chrbnd.dcx"))
    except Exception:  # noqa: BLE001
        return 0
    idx = map_index()
    done = 0
    for t in f.textures:
        stem = t["path"].replace("\\", "/").split("/")[-1].rsplit(".", 1)[0]
        if stem.lower() + ".png" in have or stem.upper().startswith("SYSTEX"):
            continue
        key = next((k for k in idx if k.endswith("_" + stem.lower() + ".tpf.dcx")), None)
        if key is None:
            continue
        try:
            for tex in tpf.read_tpf(souls.bxf_read(idx[key]))[1]:
                tpf.to_png(tex, os.path.join(TEXDIR, stem + ".png"))
                have.add(stem.lower() + ".png")
                done += 1
                break
        except Exception as exc:  # noqa: BLE001
            print("fail", stem, exc)
    return done


def main():
    os.makedirs(TEXDIR, exist_ok=True)
    have = {n.lower() for n in os.listdir(TEXDIR)}
    done = 0
    for name in sys.argv[1:]:
        for path in containers(name):
            if not os.path.exists(path):
                continue
            try:
                texs = tpf.textures_in(path)
            except Exception as exc:  # noqa: BLE001
                print("skip", path, exc)
                continue
            for t in texs:
                if t["name"].lower() + ".png" in have:
                    continue
                try:
                    tpf.to_png(t, os.path.join(TEXDIR, t["name"] + ".png"))
                    have.add(t["name"].lower() + ".png")
                    done += 1
                except Exception as exc:  # noqa: BLE001
                    print("fail", t["name"], exc)
        if name.startswith("c") and name[1:5].isdigit():
            done += from_maps(name, have)
    print("decoded", done, "textures")


if __name__ == "__main__":
    main()
