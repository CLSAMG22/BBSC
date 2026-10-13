"""Print a talk script (ESD) as readable text: groups, states (with their debug names), conditions."""
import os
import sys

import bbfiles
import esd

sys.stdout.reconfigure(encoding='utf-8')


def state_name(state):
    for bank, cid, args in state['entry']:
        if bank == 1 and cid == 0 and args:
            return args[0][0]
    return ''


def dump(e, out=print):
    index = {}
    for gid, group in e.groups.items():
        for off, state in group['states'].items():
            index[off] = (gid, state)
    for gid, group in e.groups.items():
        out('== group %d (0x%X)' % (gid, gid & 0xFFFFFFFF))
        for off, state in group['states'].items():
            out('  state %d %s' % (state['id'], state_name(state)))
            for key in ('entry', 'exit', 'while'):
                for bank, cid, args in state[key]:
                    if bank == 1 and cid == 0:
                        continue
                    out('    %s c%d_%d(%s)' % (key, bank, cid, ', '.join(t for t, _ in args)))

            def cond(c, depth):
                tgt = ''
                if c['target'] != -1 and c['target'] in index:
                    tg, ts = index[c['target']]
                    tgt = ' -> %d %s' % (ts['id'], state_name(ts))
                out('    %sif %s%s' % ('  ' * depth, c['eval'][0], tgt))
                for bank, cid, args in c['pass']:
                    out('    %s  do c%d_%d(%s)' % ('  ' * depth, bank, cid, ', '.join(t for t, _ in args)))
                for s in c['subs']:
                    cond(s, depth + 1)
            for c in state['conditions']:
                cond(c, 0)


if __name__ == '__main__':
    bnd, name = sys.argv[1], sys.argv[2]
    d = bbfiles.read_bnd4(bbfiles.read_dcx(
        os.path.join(bbfiles.GAME, 'script', 'talk', bnd + '.talkesdbnd.dcx')))
    dump(esd.Esd(d[name + '.esd'][1]))
