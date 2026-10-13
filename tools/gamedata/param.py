"""Bloodborne PARAM / PARAMDEF reader (just enough to read rows by field name).

PARAMDEF: +0x08 u16 field count, +0x0A u16 field size (0xD0), +0x30 u64 fields offset; field:
+0x00 display name (UTF-16, 0x40), +0x40 display type (8), +0x64 byte count (u32), +0x70 internal
type (0x20), +0x90 internal name (0x20, "name:bits" for bitfields, "name[n]" for arrays). PARAM (64-bit): +0x0A u16 row count, rows from 0x40, 0x18 each:
u32 id, pad, u64 data offset, u64 name offset.
"""
import os
import re
import struct

import bbfiles


def read_paramdef(data):
    count, size = struct.unpack_from('<HH', data, 0x08)
    base = struct.unpack_from('<Q', data, 0x30)[0]
    fields = []
    for i in range(count):
        f = data[base + i * size: base + (i + 1) * size]
        byte_count = struct.unpack_from('<I', f, 0x64)[0]
        itype = f[0x70:0x90].split(b'\0')[0].decode()
        iname = f[0x90:0xB0].split(b'\0')[0].decode()
        fields.append((iname, itype, byte_count))
    return fields



def layout(fields):
    """[(name, offset, byte count, signed, float, bit or None, bits)]; enum types use their byte count."""
    out, off, bit_off, bit_unit = [], 0, 0, 0
    for name, itype, byte_count in fields:
        m = re.match(r'(.+):(\d+)$', name)
        if m:
            bits = int(m.group(2))
            unit = byte_count * 8
            if bit_unit != unit or bit_off + bits > unit:
                if bit_unit:
                    off += bit_unit // 8
                bit_unit, bit_off = unit, 0
            out.append((m.group(1), off, byte_count, False, False, bit_off, bits))
            bit_off += bits
            continue
        if bit_unit:
            off += bit_unit // 8
            bit_unit, bit_off = 0, 0
        m = re.match(r'(.+)\[(\d+)\]$', name)
        out.append((m.group(1) if m else name, off, byte_count, itype.startswith('s'), itype == 'f32',
                    None, byte_count * 8))
        off += byte_count
    if bit_unit:
        off += bit_unit // 8
    return out, off


def read_param(data):
    count = struct.unpack_from('<H', data, 0x0A)[0]
    rows = {}
    for i in range(count):
        rid, _, doff = struct.unpack_from('<IIQ', data, 0x40 + i * 0x18)
        rows[rid] = doff
    return rows


def load(name):
    defs = bbfiles.read_bnd4(bbfiles.read_dcx(os.path.join(bbfiles.GAME, 'paramdef', 'paramdef.paramdefbnd.dcx')))
    params = bbfiles.read_bnd4(bbfiles.read_dcx(os.path.join(bbfiles.GAME, 'param', 'gameparam', 'gameparam.parambnd.dcx')))
    lay, size = layout(read_paramdef(defs[name + '.paramdef'][1]))
    data = params[name + '.param'][1]
    return lay, size, data, read_param(data)


def row_values(lay, data, offset, wanted=None):
    vals = {}
    for name, off, size, signed, is_float, bit, bits in lay:
        if wanted and name not in wanted:
            continue
        raw = data[offset + off: offset + off + size]
        if bit is not None:
            vals[name] = (int.from_bytes(raw, 'little') >> bit) & ((1 << bits) - 1)
        elif is_float:
            vals[name] = struct.unpack('<f', raw)[0]
        elif size in (1, 2, 4):
            vals[name] = int.from_bytes(raw, 'little', signed=signed)
    return vals
