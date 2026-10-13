"""TPF texture packs (Bloodborne PS4): list entries; decode to PNG.
PS4 texture data is tiled: BC blocks in 8x8-block tiles, Morton (Z) order inside a tile.
    python tpf.py list <container>            python tpf.py png <container> <out_dir>"""
import io
import os
import struct
import sys

import souls

# PS4 TPF format byte -> (DXGI format, bytes per 4x4 block or per pixel, is_block)
FORMATS = {
    0: (71, 8, True),     # BC1 (DXT1)
    1: (71, 8, True),
    3: (74, 16, True),    # BC2
    5: (77, 16, True),    # BC3 (DXT5)
    9: (87, 4, False),    # B8G8R8A8
    10: (87, 4, False),
    22: (10, 8, False),   # R16G16B16A16_FLOAT
    23: (77, 16, True),
    24: (80, 8, True),    # BC4
    25: (77, 16, True),
    33: (77, 16, True),
    36: (83, 16, True),   # BC5
    37: (83, 16, True),
    38: (95, 16, True),   # BC6H
    102: (98, 16, True),  # BC7
    103: (98, 16, True),
    104: (98, 16, True),
    105: (95, 16, True),
    106: (98, 16, True),
    107: (98, 16, True),
    108: (71, 8, True),
    109: (71, 8, True),
    110: (80, 8, True),
    111: (83, 16, True),
    112: (98, 16, True),
    113: (98, 16, True),
}


def read_tpf(data):
    if data[:4] != b"TPF\0":
        raise ValueError("not TPF")
    count = struct.unpack_from("<i", data, 8)[0]
    platform, flag2, enc = data[12], data[13], data[14]
    out = []
    p = 0x10
    for _ in range(count):
        off, size = struct.unpack_from("<Ii", data, p)
        fmt, typ, mips, flags1 = data[p + 8:p + 12]
        q = p + 12
        w = h = None
        if platform != 0:
            w, h = struct.unpack_from("<hh", data, q)
            q += 4
            q += 8  # texture count, unk
        name_off, flags2 = struct.unpack_from("<ii", data, q)
        q += 8
        if platform == 4:
            dxgi = struct.unpack_from("<i", data, q)[0]
            q += 4
        else:
            dxgi = None
        end = name_off
        if enc == 1:
            while data[end:end + 2] != b"\0\0":
                end += 2
            name = data[name_off:end].decode("utf-16-le")
        else:
            name = data[name_off:data.index(b"\0", name_off)].decode("shift_jis", "replace")
        out.append(dict(name=name, fmt=fmt, type=typ, mips=mips, w=w, h=h, dxgi=dxgi,
                        data=data[off:off + size]))
        p = q
    return platform, out


def morton(i):
    x = y = 0
    for b in range(8):
        x |= ((i >> (2 * b)) & 1) << b
        y |= ((i >> (2 * b + 1)) & 1) << b
    return x, y


def deswizzle(data, bw, bh, bpb, tile=8):
    """Tiled PS4 layout -> linear: bw x bh blocks of bpb bytes."""
    tw = (bw + tile - 1) // tile
    th = (bh + tile - 1) // tile
    out = bytearray(bw * bh * bpb)
    src = 0
    order = [morton(i) for i in range(tile * tile)]
    for ty in range(th):
        for tx in range(tw):
            for x, y in order:
                bx, by = tx * tile + x, ty * tile + y
                if bx < bw and by < bh and src + bpb <= len(data):
                    dst = (by * bw + bx) * bpb
                    out[dst:dst + bpb] = data[src:src + bpb]
                src += bpb
    return bytes(out)


def dds_bytes(tex):
    """Top mip of a PS4 texture as a DDS file (DX10 header)."""
    dxgi = tex["dxgi"] or FORMATS.get(tex["fmt"], (98, 16, True))[0]
    dxgi = {72: 71, 75: 74, 78: 77, 99: 98}.get(dxgi, dxgi)  # sRGB variants decode the same
    info = FORMATS.get(tex["fmt"])
    bpb = info[1] if info else 16
    block = info[2] if info else True
    w, h = tex["w"], tex["h"]
    if block:
        bw, bh = max(1, (w + 3) // 4), max(1, (h + 3) // 4)
    else:
        bw, bh = w, h
    top = deswizzle(tex["data"], bw, bh, bpb)
    hdr = struct.pack("<4sIIIIIII44sIIIIIIIIIIIII", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000, h, w,
                      len(top), 1, 1, b"\0" * 44, 32, 0x4, 0x30315844, 0, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0)
    dx10 = struct.pack("<IIIII", dxgi, 3, 0, 1, 0)
    return hdr + dx10 + top


def to_png(tex, path):
    from PIL import Image
    im = Image.open(io.BytesIO(dds_bytes(tex)))
    im.load()
    im.save(path)


def textures_in(container):
    data = souls.open_any(container)
    if data[:4] == b"TPF\0":
        return read_tpf(data)[1]
    out = []
    for _, name, blob in souls.read_bnd4(data):
        if blob[:4] == b"TPF\0":
            out += read_tpf(blob)[1]
    return out


if __name__ == "__main__":
    cmd, path = sys.argv[1], sys.argv[2]
    texs = textures_in(path)
    if cmd == "list":
        for t in texs:
            print("%-40s fmt %3d dxgi %s %4sx%-4s mips %2d bytes %d" % (t["name"], t["fmt"], t["dxgi"], t["w"], t["h"], t["mips"], len(t["data"])))
    else:
        out = sys.argv[3]
        os.makedirs(out, exist_ok=True)
        for t in texs:
            try:
                to_png(t, os.path.join(out, t["name"] + ".png"))
                print("ok ", t["name"])
            except Exception as e:  # noqa: BLE001
                print("ERR", t["name"], e)
