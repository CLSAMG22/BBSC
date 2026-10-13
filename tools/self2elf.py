import struct, sys, hashlib
d = open(sys.argv[1], 'rb').read()
assert d[:4] == b'\x4f\x15\x3d\x1d'
hdr_size, meta_size, file_size, n_entries = struct.unpack_from('<HHQH', d, 0x0C)
entries = [struct.unpack_from('<QQQQ', d, 0x20 + i*0x20) for i in range(n_entries)]
eo = 0x20 + n_entries*0x20
assert d[eo:eo+4] == b'\x7fELF', hex(eo)
e_phoff, = struct.unpack_from('<Q', d, eo+0x20)
e_phentsize, e_phnum = struct.unpack_from('<HH', d, eo+0x36)
phdrs = [struct.unpack_from('<IIQQQQQQ', d, eo+e_phoff+i*e_phentsize) for i in range(e_phnum)]
out_size = max(p[2]+p[5] for p in phdrs if p[5])
out = bytearray(max(out_size, e_phoff+e_phnum*e_phentsize))
out[:e_phoff+e_phnum*e_phentsize] = d[eo:eo+e_phoff+e_phnum*e_phentsize]
for props, off, fsz, msz in entries:
    if not (props & 0x800): continue          # has segment index
    seg = (props >> 20) & 0xFFF
    if props & 0x8: print('compressed segment', seg); continue
    p = phdrs[seg]
    out[p[2]:p[2]+p[5]] = d[off:off+p[5]]
open(sys.argv[2], 'wb').write(out)
print(len(out), hashlib.sha256(out).hexdigest())
