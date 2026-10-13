"""Survey every Bloodborne map (base game, DLC, Chalice blocks) for fog walls.

Writes fog-survey.tsv: every event or part whose name mentions fog (霧) or that uses a
fog gate model, with its map, kind, entity id and name, so walls can be classified by name
(ボス = boss, マルチ = multiplayer/co-op) instead of by memory.
"""
import glob
import os
import sys

import bbfiles
import msb

sys.stdout.reconfigure(encoding='utf-8')
OUT = os.path.join(os.path.dirname(__file__), 'fog-survey.tsv')

paths = sorted(glob.glob(os.path.join(bbfiles.GAME, 'map', 'mapstudio', '**', '*.msb.dcx'),
                         recursive=True))
fog_models = set()
rows = []
for path in paths:
    name = os.path.basename(path).replace('.msb.dcx', '')
    try:
        m = msb.Msb(path)
    except Exception as error:  # noqa: BLE001 - report and keep going
        rows.append((name, 'ERROR', '', '', '', repr(error)))
        continue
    for e in m.events:
        if '霧' in e['name']:
            part = m.parts[e['part_index']]['name'] if 0 <= e['part_index'] < len(m.parts) else ''
            rows.append((name, 'event:' + str(e['type']), e['entity_id'], part, e['name'],
                         e['type_data'][:4].hex()))
    for p in m.parts:
        if '霧' in p['name'] or (p['model'] or '').startswith('o0003'):
            rows.append((name, 'part:' + str(p['type']), p['entity_id'], p['model'], p['name'],
                         '%.1f,%.1f,%.1f' % p['pos']))
with open(OUT, 'w', encoding='utf-8') as out:
    out.write('map\tkind\tentity\tpart_or_model\tname\textra\n')
    for r in rows:
        out.write('\t'.join(str(x) for x in r) + '\n')
print(len(paths), 'maps,', len(rows), 'rows ->', OUT)
