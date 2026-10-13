"""For every fog-wall SFX found by effect ID, find the event script that sets it up.

Reads fog-ffx-range.tsv (from the survey) and every EMEVD. For each SFX entity id, records every
InitializeEvent (2000[0]: slot, event_id, params...) that passes it, so walls can be grouped by
the template event that drives them (co-op wall template vs boss fight events).
"""
import collections
import glob
import os
import struct
import sys

import bbfiles
import emevd

sys.stdout.reconfigure(encoding='utf-8')
HERE = os.path.dirname(__file__)

sfx_ids = collections.defaultdict(set)
for line in open(os.path.join(HERE, 'fog-ffx-range.tsv'), encoding='utf-8'):
    mp, ffx, eid, part, name = line.rstrip('\n').split('\t')
    sfx_ids[int(eid)].add((mp[:6] if mp.startswith('m29') else mp[:12], name))

uses = collections.defaultdict(list)  # sfx id -> [(file, event, bank, id, args ints)]
for path in sorted(glob.glob(os.path.join(bbfiles.GAME, 'event', '**', '*.emevd.dcx'),
                             recursive=True)):
    f = os.path.basename(path).replace('.emevd.dcx', '')
    e = emevd.Emevd(path)
    for eid, instrs in e.events:
        for j, (bank, iid, args) in enumerate(instrs):
            ints = struct.unpack_from('<%di' % (len(args) // 4), args) if len(args) >= 4 else ()
            for v in ints:
                if v in sfx_ids:
                    uses[v].append((f, eid, bank, iid, ints))

with open(os.path.join(HERE, 'wall-scripts.tsv'), 'w', encoding='utf-8') as out:
    for sid in sorted(sfx_ids):
        names = sorted({n for _, n in sfx_ids[sid]})
        if not uses[sid]:
            out.write('%d\t%s\t-\t-\t-\n' % (sid, names[0]))
        for f, eid, bank, iid, ints in uses[sid]:
            out.write('%d\t%s\t%s\t%d\t%d[%d] %s\n' % (sid, names[0], f, eid, bank, iid,
                                                     ' '.join(str(x) for x in ints)))
print('done')
