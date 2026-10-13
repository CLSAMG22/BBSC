"""Havok 2014 packfile reader (Bloodborne PS4: 64-bit pointers, little-endian), read-only.

Packfile: header (0x40) + predicate padding, then section headers (0x40 each in file version 11):
tag[20], absoluteDataStart, local/global/virtual fixup offsets, exports, imports, end (all relative
to absoluteDataStart). __classnames__ holds (u32 signature, u8 0x09, name\\0) entries; __data__ holds
the objects. Local fixups (src, dst) patch pointers inside __data__; global fixups (src, section, dst)
point across sections; virtual fixups (src, section, name offset) give each object's class.

    python hkx.py <file.hkx>     lists the objects
"""
import struct
import sys


class Section:
    def __init__(self, data, off):
        self.tag = data[off:off + 19].split(b"\0")[0].decode()
        (self.start, self.local, self.glob, self.virt, self.exports, self.imports, self.end) = struct.unpack_from("<7I", data, off + 20)
        self.data = data[self.start:self.start + self.end]


class Packfile:
    def __init__(self, data):
        if data[:8] != bytes.fromhex("57e0e05710c0c010"):
            raise ValueError("not a Havok packfile")
        self.version = struct.unpack_from("<i", data, 12)[0]
        ptr, le = data[16], data[17]
        if ptr != 8 or le != 1:
            raise ValueError("expected 64-bit little-endian")
        nsec = struct.unpack_from("<i", data, 20)[0]
        pad = struct.unpack_from("<h", data, 0x3E)[0] if self.version >= 11 else 0
        base = 0x40 + pad
        hdr = 0x40 if self.version >= 11 else 0x30
        self.sections = [Section(data, base + i * hdr) for i in range(nsec)]
        names = {}
        cn = next(s for s in self.sections if s.tag == "__classnames__")
        p = 0
        while p + 5 < len(cn.data):
            sig = struct.unpack_from("<I", cn.data, p)[0]
            if sig == 0xFFFFFFFF:
                break
            end = cn.data.index(b"\0", p + 5)
            names[p + 5] = cn.data[p + 5:end].decode()
            p = end + 1
        self.classnames = names
        d = next(s for s in self.sections if s.tag == "__data__")
        self.d = d
        self.ptrs = {}
        loc = d.data[d.local:d.glob]
        for i in range(0, len(loc) - 7, 8):
            src, dst = struct.unpack_from("<ii", loc, i)
            if src != -1:
                self.ptrs[src] = ("data", dst)
        glo = d.data[d.glob:d.virt]
        for i in range(0, len(glo) - 11, 12):
            src, sec, dst = struct.unpack_from("<iii", glo, i)
            if src != -1:
                self.ptrs[src] = (self.sections[sec].tag, dst)
        self.objects = []  # (offset, class name)
        vir = d.data[d.virt:d.exports]
        for i in range(0, len(vir) - 11, 12):
            src, sec, noff = struct.unpack_from("<iii", vir, i)
            if src != -1:
                self.objects.append((src, names.get(noff, "?")))
        self.objects.sort()

    # ---- helpers for reading objects in __data__
    def ptr(self, off):
        t = self.ptrs.get(off)
        return t[1] if t and t[0] == "__data__" or (t and t[0] == "data") else None

    def u8(self, off):
        return self.d.data[off]

    def i16(self, off):
        return struct.unpack_from("<h", self.d.data, off)[0]

    def i32(self, off):
        return struct.unpack_from("<i", self.d.data, off)[0]

    def f32(self, off):
        return struct.unpack_from("<f", self.d.data, off)[0]

    def string(self, off):
        p = self.ptr(off)
        if p is None:
            return None
        return self.d.data[p:self.d.data.index(b"\0", p)].decode("utf-8", "replace")

    def array(self, off):
        """hkArray at off: (data offset or None, size)."""
        return self.ptr(off), self.i32(off + 8)

    def of_class(self, name):
        return [o for o, c in self.objects if c == name]


def read_skeleton(pf):
    """hkaSkeleton -> dict(name, parents, bones, ref=[(t, q, s)])."""
    off = pf.of_class("hkaSkeleton")[0]
    name = pf.string(off + 0x10)
    pa, pn = pf.array(off + 0x18)
    parents = [pf.i16(pa + 2 * i) for i in range(pn)]
    ba, bn = pf.array(off + 0x28)
    bones = [pf.string(ba + 0x10 * i) for i in range(bn)]
    ra, rn = pf.array(off + 0x38)
    ref = []
    for i in range(rn):
        o = ra + 48 * i
        t = struct.unpack_from("<3f", pf.d.data, o)
        q = struct.unpack_from("<4f", pf.d.data, o + 16)
        s = struct.unpack_from("<3f", pf.d.data, o + 32)
        ref.append((t, q, s))
    return dict(name=name, parents=parents, bones=bones, ref=ref)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as fh:
        pf = Packfile(fh.read())
    print("version", pf.version, "sections", [s.tag for s in pf.sections])
    for off, cls in pf.objects:
        print("  %6x  %s" % (off, cls))
