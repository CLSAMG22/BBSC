"""Write docs/COOP-WALLS-AND-MESSAGES.md from the survey outputs and bbsc_messages.inc."""
import glob
import io
import os
import re
import struct
import sys

import bbfiles
import emevd

sys.stdout.reconfigure(encoding='utf-8')
HERE = os.path.dirname(__file__)
OUT = os.path.join(HERE, '..', '..', 'docs', 'COOP-WALLS-AND-MESSAGES.md')

AREAS = [('m24_00', 'Cathedral Ward'), ('m24_01', 'Central Yharnam'),
         ('m24_02', 'Upper Cathedral Ward'), ('m21', "Hunter's Dream"),
         ('m22', 'Hemwick Charnel Lane'), ('m23', 'Old Yharnam'), ('m25', 'Lecture Building'),
         ('m26', 'Nightmare Frontier'), ('m27', 'Forbidden Woods'), ('m28', "Yahar'gul"),
         ('m32', 'Byrgenwerth'), ('m33', 'Nightmare of Mensis'),
         ('m34', "Hunter's Nightmare (DLC)"), ('m35', 'Research Hall / Clocktower (DLC)'),
         ('m36', 'Fishing Hamlet (DLC)')]


def area(map_name):
    for prefix, name in AREAS:
        if map_name.startswith(prefix):
            return name
    return map_name


names = {}
for line in open(os.path.join(HERE, 'fog-ffx-range.tsv'), encoding='utf-8'):
    mp, ffx, eid, part, name = line.rstrip('\n').split('\t')
    names.setdefault(int(eid), (mp, name))

# Every InitializeEvent(slot, 7600, object, sfx) in the base game and DLC scripts.
pairs = []
for path in sorted(glob.glob(os.path.join(bbfiles.GAME, 'event', 'm*.emevd.dcx'))):
    stem = os.path.basename(path).replace('.emevd.dcx', '')
    for _, instrs in emevd.Emevd(path).events:
        for bank, iid, args in instrs:
            if bank == 2000 and iid == 0 and len(args) >= 16:
                slot, event, obj, sfx = struct.unpack_from('<4i', args)
                if event == 7600:
                    pairs.append((stem, str(obj), str(sfx)))
pairs = sorted(set(pairs))

out = io.open(OUT, 'w', encoding='utf-8', newline='\n')
w = out.write
w('# Co-op walls and co-op messages\n\n')
w('Generated 2026-09-25 from the game files by `tools/gamedata` (fog_survey.py, wall_scripts.py,\n'
  'chalice_walls.py, gen_messages.py, gen_docs.py). Nothing here was typed by hand.\n\n')
w('## Co-op fog walls (removed)\n\n')
w('A co-op wall is any wall switched by event script **7600** (common.emevd) or **12907600**\n'
  '(m29.emevd). BBSC refuses the draw / collision / fog-on calls those two scripts make for their\n'
  'own wall, so every such wall stays down in every map. Nothing else is touched: boss walls are\n'
  'switched by boss events and are never affected. The scripts only raise walls in multiplayer,\n'
  'so single player is unchanged.\n\n')
w('### Base game and DLC\n\n| Area | Wall object | Fog SFX | Name in the map file |\n|---|---|---|---|\n')
for mp, obj, sfx in pairs:
    name = names.get(int(sfx), ('', '(no fog SFX placed in the map)'))[1]
    w('| %s | %s | %s | %s |\n' % (area(mp), obj, sfx, name))
w('\n### Chalice dungeons\n\n'
  '8397 co-op wall placements (named マルチ閉じ込め壁) across 2566 blocks, all switched by 12907600.\n'
  'Their entity ids are reused between blocks - 79 of the 88 Chalice co-op fog ids are boss or door\n'
  'walls in other blocks - which is why BBSC recognises them by the script, never by id.\n\n')
w('### Boss walls (kept)\n\n'
  'Fog SFX in the xx03800-xx03899 range, switched by the boss events: for example Gascoigne\n'
  '2413810, Vicar Amelia 2413800, Rom 3203800, Ludwig 3403800, Lady Maria 3503800, Orphan\n'
  '3603800, and in the Chalices Sfx_Boss壁 2902690 / 2902691.\n\n')
w('## Co-op messages (replaced)\n\n'
  'English text in the event text file (category 30). Messages about really leaving - Silencing\n'
  'Blank, lost connection, adversaries - are unchanged. `/` marks a line break.\n\n'
  '| Id | Game text | BBSC text |\n|---|---|---|\n')
pattern = re.compile(r'\{(\d+), (\d+), u"(.*)"\}, // (.*)$')
for line in open(os.path.join(HERE, '..', '..', 'emulator', 'src', 'core', 'bbsc_messages.inc'), encoding='utf-8'):
    m = pattern.match(line.rstrip('\n'))
    if m:
        new = m.group(3).replace('\\n', ' / ').replace('|', '/')
        w('| %s | %s | %s |\n' % (m.group(2), m.group(4).replace('|', '/'), new))
out.close()
print(len(pairs), 'wall pairs ->', OUT)
