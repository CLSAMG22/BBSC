"""Readers for Bloodborne (CUSA03173 01.09) data files: DCX, BND4, FMG.

Used by the BBSC tools that list game text (custom prompts) and map parts (fog walls).
"""
import struct
import zlib

GAME = r'D:\GAMES\shadPS4QtLauncher-win64-qt-2026-09-01-bf2c711\Games\CUSA03173\dvdroot_ps4'


def read_dcx(path):
    """DCX container with DFLT (zlib) compression -> raw bytes."""
    data = open(path, 'rb').read()
    if data[:4] != b'DCX\x00':
        return data
    dca = data.find(b'DCA\x00')
    return zlib.decompress(data[dca + 8:])


def read_bnd4(data):
    """BND4 archive -> {file name (last path part): bytes}, in archive order."""
    assert data[:4] == b'BND4', data[:4]
    count = struct.unpack_from('<I', data, 0x0C)[0]
    unicode = data[0x30]
    entry_size = struct.unpack_from('<Q', data, 0x20)[0]
    out = {}
    for k in range(count):
        e = 0x40 + k * entry_size
        size = struct.unpack_from('<Q', data, e + 0x08)[0]
        if entry_size >= 0x24:
            offset, file_id, name_off = struct.unpack_from('<III', data, e + 0x18)
        else:
            offset, file_id, name_off = struct.unpack_from('<III', data, e + 0x10)
        if unicode:
            end = name_off
            while data[end:end + 2] != b'\x00\x00':
                end += 2
            name = data[name_off:end].decode('utf-16-le')
        else:
            name = data[name_off:data.index(b'\x00', name_off)].decode('shift-jis', 'replace')
        out[name.replace('/', '\\').split('\\')[-1]] = (file_id, data[offset:offset + size])
    return out


def read_fmg(data):
    """FMG (version 2, 64-bit string offsets) -> {id: text or None}."""
    version = data[2]
    count_groups = struct.unpack_from('<I', data, 0x0C)[0]
    count_strings = struct.unpack_from('<I', data, 0x10)[0]
    if version == 2:
        string_table = struct.unpack_from('<Q', data, 0x18)[0]
        groups_at, group_size = 0x28, 0x10
    else:
        string_table = struct.unpack_from('<I', data, 0x14)[0]
        groups_at, group_size = 0x1C, 0x0C
    out = {}
    for g in range(count_groups):
        base = groups_at + g * group_size
        index, first, last = struct.unpack_from('<iii', data, base)
        for i, msg_id in enumerate(range(first, last + 1)):
            k = index + i
            if version == 2:
                off = struct.unpack_from('<Q', data, string_table + k * 8)[0]
            else:
                off = struct.unpack_from('<I', data, string_table + k * 4)[0]
            if off == 0:
                out[msg_id] = None
                continue
            end = off
            while data[end:end + 2] != b'\x00\x00':
                end += 2
            out[msg_id] = data[off:end].decode('utf-16-le', 'replace')
    return out
