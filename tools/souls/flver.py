"""FLVER2 model reader (Bloodborne PS4 flavour), read-only: bones, materials + texture paths,
meshes (positions, normals, UVs, bone indices/weights) and LOD0 triangles.

    python flver.py <file.flver | container.dcx>   prints a summary
"""
import struct
import sys

import numpy as np

import souls

# layout member types -> (size, decoder)
TYPES = {
    0x01: 8, 0x02: 12, 0x03: 16, 0x10: 4, 0x11: 4, 0x12: 4, 0x13: 4, 0x15: 4, 0x16: 8,
    0x18: 8, 0x1A: 8, 0x2E: 8, 0x2F: 4, 0xF0: 0,
}
SEM = {0: "position", 1: "weights", 2: "bones", 3: "normal", 5: "uv", 6: "tangent", 7: "bitangent", 10: "color"}


class Flver:
    def __init__(self, data):
        if data[:6] != b"FLVER\0":
            raise ValueError("not a FLVER")
        self.e = e = "<" if data[6:8] == b"L\0" else ">"
        self.d = data
        (self.version, self.data_off, _, n_dummy, n_mat, n_bone, n_mesh, n_vb) = struct.unpack_from(e + "8i", data, 0x08)
        self.bbox = (struct.unpack_from(e + "3f", data, 0x28), struct.unpack_from(e + "3f", data, 0x34))
        self.index_size = data[0x48]
        self.unicode = data[0x49] == 1
        n_fs, n_lay, n_tex = struct.unpack_from(e + "3i", data, 0x50)
        p = 0x80 + n_dummy * 0x40
        self.materials = []
        for _ in range(n_mat):
            name_o, mtd_o, tcount, tindex = struct.unpack_from(e + "4i", data, p)
            self.materials.append(dict(name=self.s(name_o), mtd=self.s(mtd_o), tex=(tindex, tcount)))
            p += 0x20
        self.bones = []
        for _ in range(n_bone):
            t = struct.unpack_from(e + "3f", data, p)
            name_o = struct.unpack_from(e + "i", data, p + 0x0C)[0]
            r = struct.unpack_from(e + "3f", data, p + 0x10)
            parent, child = struct.unpack_from(e + "2h", data, p + 0x1C)
            s = struct.unpack_from(e + "3f", data, p + 0x20)
            self.bones.append(dict(name=self.s(name_o), t=t, r=r, s=s, parent=parent))
            p += 0x80
        mesh_hdr = []
        for _ in range(n_mesh):
            (dyn,) = struct.unpack_from("B", data, p)
            mat, _, _, def_bone, bcount, _bb, boff, fscount, fsoff, vbcount, vboff = struct.unpack_from(e + "11i", data, p + 4)
            mesh_hdr.append((mat, def_bone, bcount, boff, fscount, fsoff, vbcount, vboff))
            p += 0x30
        facesets = []
        for _ in range(n_fs):
            flags, strip, cull, _u = struct.unpack_from(e + "I??h", data, p)
            count, ioff = struct.unpack_from(e + "2i", data, p + 8)
            isize = struct.unpack_from(e + "i", data, p + 0x18)[0] if self.version > 0x20005 else 0
            facesets.append(dict(flags=flags, strip=strip, count=count, off=ioff, isize=isize or self.index_size))
            p += 0x20
        vbs = []
        for _ in range(n_vb):
            bidx, lay, vsize, vcount = struct.unpack_from(e + "4i", data, p)
            boff = struct.unpack_from(e + "i", data, p + 0x1C)[0]
            vbs.append(dict(layout=lay, size=vsize, count=vcount, off=boff))
            p += 0x20
        layouts = []
        for _ in range(n_lay):
            mcount, _, _, moff = struct.unpack_from(e + "4i", data, p)
            members = []
            for k in range(mcount):
                _u0, soff, typ, sem, idx = struct.unpack_from(e + "2i2Ii", data, moff + k * 0x14)
                members.append((soff, typ, sem, idx))
            layouts.append(members)
            p += 0x10
        self.textures = []
        for _ in range(n_tex):
            path_o, type_o = struct.unpack_from(e + "2i", data, p)
            self.textures.append(dict(path=self.s(path_o), type=self.s(type_o)))
            p += 0x20
        self.meshes = []
        uv_factor = 2048.0 if self.version >= 0x2000F else 1024.0
        for mat, def_bone, bcount, boff, fscount, fsoff, vbcount, vboff in mesh_hdr:
            bone_map = list(struct.unpack_from(e + "%di" % bcount, data, boff)) if bcount else []
            fs_ids = struct.unpack_from(e + "%di" % fscount, data, fsoff)
            vb_ids = struct.unpack_from(e + "%di" % vbcount, data, vboff)
            attrs = {}
            nverts = vbs[vb_ids[0]]["count"]
            for vb_id in vb_ids:
                vb = vbs[vb_id]
                raw = np.frombuffer(data, np.uint8, vb["size"] * vb["count"], self.data_off + vb["off"])
                raw = raw.reshape(vb["count"], vb["size"])
                for soff, typ, sem, idx in layouts[vb["layout"]]:
                    key = SEM.get(sem, "s%d" % sem) + ("" if idx == 0 else str(idx))
                    size = TYPES.get(typ, 0)
                    chunk = np.ascontiguousarray(raw[:, soff:soff + size])
                    attrs[key] = self.decode(chunk, typ, sem, uv_factor, key, attrs)
            tris = None
            for fid in fs_ids:
                fs = facesets[fid]
                if fs["flags"] & 0xFFFFFFFF not in (0,):
                    continue  # LODs / motion blur sets
                tris = self.faces(fs)
                break
            if tris is None and fs_ids:
                tris = self.faces(facesets[fs_ids[0]])
            self.meshes.append(dict(material=mat, bone_map=bone_map, default_bone=def_bone,
                                    verts=nverts, attrs=attrs, tris=tris))

    def s(self, off):
        d = self.d
        if off <= 0 or off >= len(d):
            return ""
        if self.unicode:
            end = off
            while d[end:end + 2] != b"\0\0":
                end += 2
            return d[off:end].decode("utf-16-le", "replace")
        return d[off:d.index(b"\0", off)].decode("shift_jis", "replace")

    def decode(self, c, typ, sem, uv_factor, key, attrs):
        e = self.e
        if typ == 0x01:
            return c.view(e + "f4").reshape(-1, 2)
        if typ == 0x02:
            return c.view(e + "f4").reshape(-1, 3)
        if typ == 0x03:
            return c.view(e + "f4").reshape(-1, 4)
        if typ in (0x10, 0x11, 0x13, 0x2F):
            b = c.astype(np.float32)
            if sem == 2:  # bone indices as bytes
                return c.astype(np.int32)
            if sem in (3, 6, 7):
                return (b - 127.0) / 127.0
            return b / 255.0
        if typ == 0x12:
            return c.view(e + "i2").reshape(-1, 2).astype(np.float32) / 32767.0
        if typ == 0x15:
            return c.view(e + "i2").reshape(-1, 2).astype(np.float32) / uv_factor
        if typ == 0x16:
            return c.view(e + "i2").reshape(-1, 4).astype(np.float32) / uv_factor
        if typ == 0x18:
            return c.view(e + "i2").reshape(-1, 4).astype(np.int32)
        if typ in (0x1A, 0x2E):
            v = c.view(e + "i2").reshape(-1, 4).astype(np.float32)
            return v / 32767.0 if sem == 1 else v
        return c

    def faces(self, fs):
        e = self.e
        dt = e + ("u2" if fs["isize"] == 16 else "u4")
        idx = np.frombuffer(self.d, dt, fs["count"], self.data_off + fs["off"]).astype(np.int64)
        if not fs["strip"]:
            return idx.reshape(-1, 3)
        restart = 0xFFFF if fs["isize"] == 16 else 0xFFFFFFFF
        tris = []
        flip = False
        for i in range(len(idx) - 2):
            a, b, c = idx[i], idx[i + 1], idx[i + 2]
            if a == restart or b == restart or c == restart:
                flip = False
                continue
            if a != b and b != c and a != c:
                tris.append((a, c, b) if flip else (a, b, c))
            flip = not flip
        return np.array(tris, dtype=np.int64).reshape(-1, 3)


def load(path, inner_ext=".flver"):
    data = souls.open_any(path)
    if data[:4] == b"BND4":
        for _, name, blob in souls.read_bnd4(data):
            if name.lower().endswith(inner_ext):
                return Flver(blob), name
        raise ValueError("no %s inside" % inner_ext)
    return Flver(data), path


if __name__ == "__main__":
    f, name = load(sys.argv[1])
    print(name, "version %x" % f.version, "bbox", [round(x, 2) for x in f.bbox[0]], [round(x, 2) for x in f.bbox[1]])
    print("bones", len(f.bones), [b["name"] for b in f.bones[:8]])
    print("materials", len(f.materials), [(m["name"], m["mtd"].split("\\")[-1]) for m in f.materials[:6]])
    print("textures", len(f.textures), [t["path"].split("\\")[-1] for t in f.textures[:8]])
    tv = sum(m["verts"] for m in f.meshes)
    tt = sum(len(m["tris"]) for m in f.meshes if m["tris"] is not None)
    print("meshes", len(f.meshes), "verts", tv, "tris", tt, "attrs", sorted(f.meshes[0]["attrs"].keys()) if f.meshes else [])
