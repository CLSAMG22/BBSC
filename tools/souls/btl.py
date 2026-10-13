"""Bloodborne BTL (baked light placements), read-only. Header: +0x08 count, +0x0C names length,
names (UTF-16) from 0x40, then 0xC8-byte entries: +0x0C name offset, +0x14 type, +0x18 diffuse
colour (B, G, R, A bytes), +0x1C diffuse power, +0x28 radius, +0x34 position, +0x40 rotation."""
import struct

import souls


def read_btl(path):
    d = souls.open_any(path)
    count, names_len = struct.unpack_from("<ii", d, 0x08)
    base = 0x40 + names_len
    out = []
    for e in range(count):
        o = base + e * 0xC8
        name_off, _, typ = struct.unpack_from("<iii", d, o + 0x0C)
        b, g, r, a = d[o + 0x18:o + 0x1C]
        power = struct.unpack_from("<f", d, o + 0x1C)[0]
        radius = struct.unpack_from("<f", d, o + 0x28)[0]
        pos = struct.unpack_from("<3f", d, o + 0x34)
        rot = struct.unpack_from("<3f", d, o + 0x40)
        end = 0x40 + name_off
        while d[end:end + 2] != b"\0\0":
            end += 2
        name = d[0x40 + name_off:end].decode("utf-16-le", "replace")
        out.append(dict(name=name, type=typ, color=(r / 255.0, g / 255.0, b / 255.0), power=power,
                        radius=radius, pos=pos, rot=rot))
    return out
