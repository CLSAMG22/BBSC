r"""List every fog wall in the game, with its entity ID, straight out of the map files.

No play-testing needed. Bloodborne's MSB map layouts name each part in Japanese, and FromSoft's
own naming classifies them for us: 霧壁 is "fog wall", ボス marks a boss gate, and マルチ marks the
co-op walls that box players into an area. The entity ID sits a short way after the name in the
same record.

The ID range is not guessed. CalcGetMultiWallEntityId (eboot 0x01338840) computes it as:

    id = block * 100000 + area * 10000 + n % 10000

so for m24_01 every valid ID is 2410000..2419999. Scanning forward from a name for the first u32
in that window finds the ID without having to model the whole record layout.

    python find-fog-walls.py                 # everything, grouped by map
    python find-fog-walls.py --multi         # only the co-op walls, which is what BBSC removes
    python find-fog-walls.py --multi --ids   # bare ids, ready for SHADPS4_BBSC_WALL_IDS

Output is written to tools/fog-walls.txt as well as the console.
"""
import argparse
import os
import re
import struct
import sys
import zlib

MAPSTUDIO = (r'D:\GAMES\shadPS4QtLauncher-win64-qt-2026-09-01-bf2c711\Games'
             r'\CUSA03173\dvdroot_ps4\map\mapstudio')
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fog-walls.txt')

# FromSoft's own naming, which classifies the walls for us:
FOG = '霧壁'      # "fog wall"
BOSS = 'ボス'     # "boss"   - the arena gates, which we keep
MULTI = 'マルチ'  # "multi"  - the co-op walls that box players into an area
STUCK = '埋まり'  # "buried" - stuck-detection regions and warp targets, not walls at all


def classify(label):
    if STUCK in label:
        return 'region'
    if BOSS in label:
        return 'boss'
    if MULTI in label:
        return 'multi'
    return 'other'


def decompress(path):
    data = open(path, 'rb').read()
    if not data.startswith(b'DCX'):
        return data
    for m in re.finditer(rb'\x78[\x01\x5e\x9c\xda]', data):
        try:
            return zlib.decompressobj().decompress(data[m.start():])
        except zlib.error:
            continue
    raise ValueError('no zlib stream in ' + path)


def utf16_at(buf, off, limit=200):
    end = off
    while end + 1 < len(buf) and buf[end:end + 2] != b'\x00\x00' and end - off < limit * 2:
        end += 2
    try:
        return buf[off:end].decode('utf-16-le')
    except UnicodeDecodeError:
        return None


def map_id_base(name):
    """m24_01_00_00 -> (24, 1, 2410000)."""
    m = re.match(r'm(\d\d)_(\d\d)', name)
    if not m:
        return None
    block, area = int(m.group(1)), int(m.group(2))
    return block, area, block * 100000 + area * 10000


def walls_in(path, name):
    buf = decompress(path)
    base = map_id_base(name)
    if base is None:
        return []
    block, area, low = base
    high = low + 9999

    found = []
    seen = set()
    for m in re.finditer(re.escape(FOG.encode('utf-16-le')), buf):
        label = utf16_at(buf, m.start())
        if not label:
            continue
        # the entity id is a little way after the name, inside the same record
        eid = None
        for off in range(m.start(), min(m.start() + 0x400, len(buf) - 4), 4):
            val = struct.unpack_from('<I', buf, off)[0]
            if low <= val <= high:
                eid = val
                break
        if eid is None or eid in seen:
            continue
        seen.add(eid)
        found.append({'id': eid, 'name': label, 'kind': classify(label), 'map': name})
    return sorted(found, key=lambda w: w['id'])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--multi', action='store_true',
                    help='only the co-op walls - what BBSC removes')
    ap.add_argument('--ids', action='store_true', help='print bare ids, comma separated')
    ap.add_argument('--mapstudio', default=MAPSTUDIO)
    args = ap.parse_args()

    if not os.path.isdir(args.mapstudio):
        print('map folder not found: ' + args.mapstudio)
        return 1

    # The chalice dungeons (m29_*) live in subfolders, ~2700 files, so this has to recurse.
    files = []
    for root, _dirs, names in os.walk(args.mapstudio):
        for n in sorted(names):
            if n.endswith('.msb.dcx'):
                files.append(os.path.join(root, n))
    files.sort()

    every = []
    for path in files:
        name = os.path.basename(path).split('.')[0]
        try:
            every.extend(walls_in(path, name))
        except Exception as exc:                      # a map we cannot read is not fatal
            print('  ! {}: {}'.format(name, exc), file=sys.stderr)

    chosen = [w for w in every if w['kind'] == 'multi'] if args.multi else every

    if args.ids:
        print(','.join(str(w['id']) for w in chosen))
        return 0

    counts = {}
    for w in every:
        counts[w['kind']] = counts.get(w['kind'], 0) + 1

    lines = []
    lines.append('Bloodborne fog walls, read from the map files ({} maps scanned)'.format(
        len(files)))
    lines.append('ID formula from CalcGetMultiWallEntityId (eboot 0x01338840):')
    lines.append('    id = block*100000 + area*10000 + n')
    lines.append('')
    lines.append('{} entries: {} boss gates, {} co-op (multi), {} other walls, '
                 '{} stuck-regions'.format(
                     len(every), counts.get('boss', 0), counts.get('multi', 0),
                     counts.get('other', 0), counts.get('region', 0)))
    lines.append('')
    lines.append('  boss    the arena gates - KEEP these')
    lines.append('  multi   co-op walls that box players into an area - what BBSC removes')
    lines.append('  other   area-transition walls, single player too')
    lines.append('  region  stuck-detection volumes and warp targets, not walls')
    lines.append('')
    current = None
    for w in chosen:
        if w['map'] != current:
            current = w['map']
            lines.append('')
            lines.append('== ' + current)
        lines.append('  {:<9} {:<7} {}'.format(w['id'], w['kind'], w['name']))
    lines.append('')
    lines.append('Co-op wall ids for SHADPS4_BBSC_WALL_IDS:')
    multi_ids = sorted({w['id'] for w in every if w['kind'] == 'multi'})
    lines.append(','.join(str(i) for i in multi_ids))
    lines.append('')
    lines.append('({} distinct ids; the same id repeats across a map variant)'.format(
        len(multi_ids)))

    text = '\n'.join(lines)
    print(text)
    with open(OUT, 'w', encoding='utf-8') as handle:
        handle.write(text + '\n')
    print('\nwritten to ' + OUT)
    return 0


if __name__ == '__main__':
    sys.exit(main())
