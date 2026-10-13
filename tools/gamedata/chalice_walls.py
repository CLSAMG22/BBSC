"""Per Chalice block: which fog-wall SFX are co-op walls (given to 12907600) and which are not.

A block is one m29_*.msb.dcx with the m29_*.emevd.dcx of the same name. The IDs are reused from
block to block, so an ID is only safe to suppress everywhere if no block uses it for a wall that
is NOT a co-op wall. This checks exactly that.
"""
import collections
import glob
import os
import struct
import sys

import bbfiles
import emevd
import msb

sys.stdout.reconfigure(encoding='utf-8')
COOP_TEMPLATES = (7600, 12907600)

coop_sfx = collections.Counter()   # sfx id -> blocks where it is a co-op wall
coop_obj = collections.Counter()
other_sfx = collections.defaultdict(set)  # sfx id -> {(event that switches it, name)}
blocks = 0
missing_script = []
for path in sorted(glob.glob(os.path.join(bbfiles.GAME, 'map', 'mapstudio', 'm29*', '*.msb.dcx'))):
    stem = os.path.basename(path).replace('.msb.dcx', '')
    walls = {}
    for e in msb.Msb(path).events:
        if e['type'] == 'SFX' and len(e['type_data']) >= 4:
            fid = struct.unpack_from('<i', e['type_data'])[0]
            if 820000 <= fid < 840000 and e['entity_id'] > 0:
                walls[e['entity_id']] = e['name']
    if not walls:
        continue
    blocks += 1
    epath = os.path.join(bbfiles.GAME, 'event', os.path.basename(os.path.dirname(path)),
                         stem + '.emevd.dcx')
    if not os.path.exists(epath):
        epath = os.path.join(bbfiles.GAME, 'event', stem + '.emevd.dcx')
    if not os.path.exists(epath):
        missing_script.append(stem)
        continue
    ev = emevd.Emevd(epath)
    coop_here = set()
    for eid, instrs in ev.events:
        for bank, iid, args in instrs:
            ints = struct.unpack_from('<%di' % (len(args) // 4), args) if len(args) >= 4 else ()
            if bank == 2000 and iid == 0 and len(ints) >= 4 and ints[1] in COOP_TEMPLATES:
                coop_here.add(ints[3])
                coop_obj[ints[2]] += 1
    for sid, name in walls.items():
        if sid in coop_here:
            coop_sfx[sid] += 1
            continue
        # not a co-op wall in this block: record what switches it here
        users = set()
        for eid, instrs in ev.events:
            for bank, iid, args in instrs:
                ints = struct.unpack_from('<%di' % (len(args) // 4), args) if len(args) >= 4 else ()
                if sid in ints:
                    users.add('%d:%d[%d]%s' % (eid, bank, iid,
                                              ('->%d' % ints[1]) if bank == 2000 and iid == 0 else ''))
        other_sfx[sid].add((name, ','.join(sorted(users)) or 'unused'))

print('blocks with walls:', blocks, ' missing scripts:', len(missing_script), missing_script[:5])
print('co-op SFX ids:', len(coop_sfx), ' co-op objects:', len(coop_obj))
overlap = sorted(set(coop_sfx) & set(other_sfx))
print('ids that are co-op in one block and something else in another:', len(overlap))
for sid in overlap[:40]:
    print('  ', sid, 'coop in', coop_sfx[sid], 'blocks; else:', sorted(other_sfx[sid])[:3])
kinds = collections.Counter()
for sid, uses in other_sfx.items():
    for name, how in uses:
        kinds[(name.split('_')[0] + '_' + name.split('_')[1] if '_' in name else name, how.split(',')[0][:60])] += 1
print('non co-op wall uses (sample):')
for k, n in kinds.most_common(40):
    print('  ', n, k)
