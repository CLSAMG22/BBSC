"""List the functions an eboot imports and the GOT slot the emulator fills for each.

    python eboot_imports.py <eboot.bin> [name-filter]

Reads the (fake-signed) SELF, finds the ELF inside, and walks the SCE dynamic data the way the
emulator's loader does: DT_SCE_JMPREL relocations (R_X86_64_JUMP_SLOT) against DT_SCE_SYMTAB
symbols whose names are "<nid>#<lib>#<module>". NIDs are named from the emulator's
src/core/aerolib/aerolib.inl. Slot = image-relative (BBSC offset) address of the GOT entry.
"""

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
AEROLIB = os.path.join(HERE, "..", "..", "src-shadp2p", "src", "core", "aerolib", "aerolib.inl")

PT_DYNAMIC = 2
PT_SCE_DYNLIBDATA = 0x61000000
DT_SCE_JMPREL = 0x61000029
DT_SCE_PLTRELSZ = 0x6100002D
DT_SCE_STRTAB = 0x61000035
DT_SCE_STRSZ = 0x61000037
DT_SCE_SYMTAB = 0x61000039
DT_SCE_SYMTABSZ = 0x6100003F


def nid_names():
    names = {}
    with open(AEROLIB, encoding="utf-8", errors="replace") as f:
        for m in re.finditer(r'STUB\("([^"]+)",\s*([A-Za-z0-9_]+)\)', f.read()):
            names[m.group(1)] = m.group(2)
    return names


def imports(path):
    data = open(path, "rb").read()
    # SELF header: the ELF header follows the segment table.
    if data[:4] == b"\x7fELF":
        elf = 0
    else:
        seg_count = struct.unpack_from("<H", data, 0x18)[0]
        elf = 0x20 + seg_count * 0x20
        assert data[elf:elf + 4] == b"\x7fELF", "no ELF header after the SELF header"
    phoff, = struct.unpack_from("<Q", data, elf + 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, elf + 0x36)
    phdrs = []
    for i in range(phnum):
        p = elf + phoff + i * phentsize
        ptype, flags, offset, vaddr, paddr, filesz, memsz, align = struct.unpack_from("<IIQQQQQQ", data, p)
        phdrs.append((ptype, offset, vaddr, filesz))
    dyn = next(p for p in phdrs if p[0] == PT_DYNAMIC)
    lib = next(p for p in phdrs if p[0] == PT_SCE_DYNLIBDATA)
    # SELF segment table: entry = props, offset, filesz, memsz; a "blocked" entry (bit 11) holds
    # program header number (props >> 20) & 0xFFF at that file offset (as the emulator's loader).
    seg_file = {}
    if elf:
        for i in range(struct.unpack_from("<H", data, 0x18)[0]):
            props, offset, filesz, memsz = struct.unpack_from("<QQQQ", data, 0x20 + i * 0x20)
            if props & 0x800:
                seg_file[(props >> 20) & 0xFFF] = offset

    def read_segment(p):
        index = phdrs.index(p)
        if index in seg_file:
            off = seg_file[index]
        else:
            # Not stored on its own (PT_DYNAMIC): it lies inside a stored one (the dynlib data).
            off = p[1]
            for other, stored in seg_file.items():
                o = phdrs[other]
                if o[1] <= p[1] and p[1] + p[3] <= o[1] + o[3]:
                    off = stored + (p[1] - o[1])
                    break
        return data[off:off + p[3]]

    dyn_bytes = read_segment(dyn)
    tags = {}
    for i in range(0, len(dyn_bytes), 16):
        tag, val = struct.unpack_from("<qQ", dyn_bytes, i)
        if tag == 0:
            break
        tags[tag] = val
    lib_bytes = read_segment(lib)
    strtab = lib_bytes[tags[DT_SCE_STRTAB]:tags[DT_SCE_STRTAB] + tags[DT_SCE_STRSZ]]
    symtab = lib_bytes[tags[DT_SCE_SYMTAB]:tags[DT_SCE_SYMTAB] + tags[DT_SCE_SYMTABSZ]]
    jmprel = lib_bytes[tags[DT_SCE_JMPREL]:tags[DT_SCE_JMPREL] + tags[DT_SCE_PLTRELSZ]]
    out = []
    for i in range(0, len(jmprel), 24):
        r_offset, r_info, r_addend = struct.unpack_from("<QQq", jmprel, i)
        sym = r_info >> 32
        name_off, = struct.unpack_from("<I", symtab, sym * 24)
        name = strtab[name_off:strtab.index(b"\0", name_off)].decode("ascii", "replace")
        out.append((r_offset, name))
    return out


def main():
    path = sys.argv[1]
    flt = sys.argv[2].lower() if len(sys.argv) > 2 else ""
    names = nid_names()
    for slot, sym in imports(path):
        nid = sym.split("#")[0]
        fn = names.get(nid, "?")
        if flt in fn.lower() or flt in sym.lower():
            print("{:#010x}  {:<40} {}".format(slot, fn, sym))


if __name__ == "__main__":
    main()
