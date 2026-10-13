"""Unpack a character's animation archives (skeleton + HKX clips) into RAW/<chr>_anims (system Python).
    python extract_anims.py c5020 c5000 c2630 ..."""
import glob
import os
import sys

import souls

GAME = r"D:/GAMES/shadPS4QtLauncher-win64-qt-2026-09-01-bf2c711/Games/CUSA03173/dvdroot_ps4"
RAW = r"art/game-models/raw"

for chr_id in sys.argv[1:]:
    out = os.path.join(RAW, chr_id + "_anims")
    if os.path.exists(os.path.join(out, ".done")):
        continue
    os.makedirs(out, exist_ok=True)
    n = 0
    for path in sorted(glob.glob(os.path.join(GAME, "chr", chr_id + "*.anibnd.dcx"))):
        base = os.path.basename(path)
        if not (base.startswith(chr_id + ".") or base.startswith(chr_id + "_")):
            continue
        for _, name, blob in souls.read_bnd4(souls.open_any(path)):
            low = name.replace("\\", "/").lower()
            if low.endswith(".hkx") and "/hkx/" in low:
                with open(os.path.join(out, os.path.basename(low)), "wb") as fh:
                    fh.write(blob)
                n += 1
    open(os.path.join(out, ".done"), "w").close()
    print(chr_id, n, "files")
