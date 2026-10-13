"""Minimal readers for FromSoftware containers as used by Bloodborne (PS4), read-only.

    DCX  - compressed wrapper ("DCX\\0" + DCS/DCP/DCA headers, big-endian; DFLT = zlib)
    BND4 - archive of named files (chrbnd, anibnd, partsbnd, objbnd, ...)

Usage:  python souls.py list <file.dcx|file.bnd>      list what is inside
        python souls.py extract <file> <out_dir>       write the inner files
"""
import os
import struct
import sys
import zlib


def read_dcx(data):
    """Return the decompressed payload of a DCX file (or the data itself if it is not DCX)."""
    if data[:4] != b"DCX\0":
        return data
    usize, csize = struct.unpack(">II", data[0x1C:0x24])
    fmt = data[0x28:0x2C]
    if fmt == b"DFLT":
        start = data.index(b"DCA\0") + 8
        return zlib.decompress(data[start:start + csize])
    raise ValueError("unsupported DCX compression %r" % fmt)


def _reverse_bits(b):
    return int("{:08b}".format(b)[::-1], 2)


def read_bnd4(data):
    """Return [(id, name, bytes)] of a BND4 archive."""
    if data[:4] != b"BND4":
        raise ValueError("not BND4")
    big = data[8] == 1
    bit_big = data[9] == 1
    e = ">" if big else "<"
    count = struct.unpack_from(e + "i", data, 0x0C)[0]
    header_size = struct.unpack_from(e + "q", data, 0x20)[0]
    unicode = data[0x30] == 1
    raw = data[0x31]
    fmt = raw if bit_big else _reverse_bits(raw)
    has_ids = bool(fmt & 0x02)
    has_names = bool(fmt & 0x0C)
    long_off = bool(fmt & 0x10)
    compressed = bool(fmt & 0x20)
    out = []
    p = 0x40
    for _ in range(count):
        q = p + 8
        csize = struct.unpack_from(e + "q", data, q)[0]
        q += 8
        if compressed:
            q += 8
        if long_off:
            off = struct.unpack_from(e + "q", data, q)[0]
            q += 8
        else:
            off = struct.unpack_from(e + "I", data, q)[0]
            q += 4
        fid = None
        if has_ids:
            fid = struct.unpack_from(e + "i", data, q)[0]
            q += 4
        name = ""
        if has_names:
            noff = struct.unpack_from(e + "I", data, q)[0]
            q += 4
            if unicode:
                end = noff
                while data[end:end + 2] != b"\0\0":
                    end += 2
                name = data[noff:end].decode("utf-16-le" if not big else "utf-16-be")
            else:
                end = data.index(b"\0", noff)
                name = data[noff:end].decode("shift_jis", "replace")
        blob = data[off:off + csize]
        out.append((fid, name, read_dcx(blob)))
        p += header_size
    return out


def read_bxf4(bhd_path, bdt_path=None):
    """Split archive: BHF4 header (same layout as BND4) + BDT data file. Returns {name: (bdt, offset,
    size)} without reading the data; a .patch pair next to it overrides entries."""
    bdt_path = bdt_path or bhd_path[:-3] + "bdt"
    out = {}
    for bhd, bdt in ((bhd_path, bdt_path), (bhd_path + ".patch", bdt_path + ".patch")):
        if not os.path.exists(bhd) or not os.path.exists(bdt):
            continue
        with open(bhd, "rb") as fh:
            data = fh.read()
        if data[:4] != b"BHF4":
            continue
        fake = b"BND4" + data[4:]
        for entry in _bnd4_headers(fake):
            fid, name, off, size = entry
            out[name.replace("\\", "/").split("/")[-1].lower()] = (bdt, off, size)
    return out


def bxf_read(entry):
    bdt, off, size = entry
    with open(bdt, "rb") as fh:
        fh.seek(off)
        return read_dcx(fh.read(size))


def _bnd4_headers(data):
    big = data[8] == 1
    bit_big = data[9] == 1
    e = ">" if big else "<"
    count = struct.unpack_from(e + "i", data, 0x0C)[0]
    header_size = struct.unpack_from(e + "q", data, 0x20)[0]
    unicode = data[0x30] == 1
    raw = data[0x31]
    fmt = raw if bit_big else _reverse_bits(raw)
    has_ids, has_names = bool(fmt & 0x02), bool(fmt & 0x0C)
    long_off, compressed = bool(fmt & 0x10), bool(fmt & 0x20)
    out = []
    p = 0x40
    for _ in range(count):
        q = p + 8
        csize = struct.unpack_from(e + "q", data, q)[0]
        q += 16 if compressed else 8
        if long_off:
            off = struct.unpack_from(e + "q", data, q)[0]
            q += 8
        else:
            off = struct.unpack_from(e + "I", data, q)[0]
            q += 4
        fid = None
        if has_ids:
            fid = struct.unpack_from(e + "i", data, q)[0]
            q += 4
        name = ""
        if has_names:
            noff = struct.unpack_from(e + "I", data, q)[0]
            end = noff
            if unicode:
                while data[end:end + 2] != b"\0\0":
                    end += 2
                name = data[noff:end].decode("utf-16-le")
            else:
                end = data.index(b"\0", noff)
                name = data[noff:end].decode("shift_jis", "replace")
        out.append((fid, name, off, csize))
        p += header_size
    return out


def open_any(path):
    with open(path, "rb") as fh:
        return read_dcx(fh.read())


if __name__ == "__main__":
    cmd, path = sys.argv[1], sys.argv[2]
    data = open_any(path)
    files = read_bnd4(data)
    if cmd == "list":
        print("%s: %d files (%d bytes unpacked)" % (os.path.basename(path), len(files), len(data)))
        for fid, name, blob in files:
            print("  %8s  %10d  %-4s  %s" % (fid, len(blob), blob[:4].decode("latin-1").strip("\0"), name))
    elif cmd == "extract":
        out_dir = sys.argv[3]
        for fid, name, blob in files:
            rel = name.replace("\\", "/").split(":/")[-1].lstrip("/")
            dst = os.path.join(out_dir, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as fh:
                fh.write(blob)
        print("extracted", len(files), "files to", out_dir)
