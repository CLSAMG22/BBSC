import struct, sys, re
d = open(sys.argv[1],'rb').read()
names = {}
for m in re.finditer(r'STUB\("([^"]+)",\s*(\w+)\)', open(sys.argv[2]).read()):
    names[m.group(1)] = m.group(2)
phoff, = struct.unpack_from('<Q', d, 0x20); n, = struct.unpack_from('<H', d, 0x38)
ph = [struct.unpack_from('<IIQQQQQQ', d, phoff+i*0x38) for i in range(n)]
dyn = next(p for p in ph if p[0]==2); dld = next(p for p in ph if p[0]==0x61000000)
tags = {}
o = dyn[2]
while True:
    t, v = struct.unpack_from('<QQ', d, o); o += 16
    if t == 0: break
    tags.setdefault(t, v)
base = dld[2]
strtab = base + tags[0x61000035]; symtab = base + tags[0x61000039]
want = set(sys.argv[3:])
for rel_t, sz_t in ((0x61000029, 0x6100002D), (0x6100002F, 0x61000031)):
    ro = base + tags[rel_t]; cnt = tags[sz_t] // 24
    for i in range(cnt):
        off, info, add = struct.unpack_from('<QQq', d, ro + i*24)
        sym = info >> 32
        nm_off, = struct.unpack_from('<I', d, symtab + sym*24)
        e = d.index(b'\0', strtab+nm_off); s = d[strtab+nm_off:e].decode()
        nid = s.split('#')[0]
        fn = names.get(nid, '?')
        if fn in want: print(hex(off), fn, s)
