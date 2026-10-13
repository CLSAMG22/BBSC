"""Bloodborne talk script (ESD, "fsSL" long format) reader.

Layout read off the files themselves (2026-09-25), all offsets relative to the data start 0x6C:
  header (after "fsSL"): 1, 2, 2, 0x54, data size, 6, 0x48, 1, 0x20, group count, 0x48, state count,
          0x38, condition count, 0x18, command count, 0x10, arg count, condition offsets offset,
          condition offsets count, name offset, name length, ...
  data:   int 1, 4 ints, int 0, long groups offset, long group count, long name offset,
          long name length, long -1, long -1
  group:  long id, long states offset, long state count, long first state offset
  state:  long id, long conditions (offsets to conditions) + count, long entry commands + count,
          long exit commands + count, long while commands + count
  condition: long target state offset (-1: none), long pass commands + count,
          long subconditions (offsets) + count, long evaluator offset + length
  command: int bank, int id, long args + count;  arg: long expression offset + length

Expressions are a stack bytecode: 0x00-0x7F push b-64, 0x80 float, 0x81 double, 0x82 int32,
0x84-0x8A call with 0-6 arguments (the function id is pushed first), 0x8C + 0x8D - (0x8E, 0x8F
multiply/divide, order not checked), 0x91 <= 0x92 >= 0x93 < 0x94 > 0x95 == 0x96 !=, 0x98 &&,
0x99 ||, 0x9A !, 0xA1 end, 0xA5 UTF-16 string, 0xA6 after every call (passes the value on),
0xA7-0xAE set register, 0xAF-0xB6 get register, 0xB7 abort-if-false, 0xB8 machine arg,
0xB9 last call result, 0xBA stack value.
"""
import struct

DATA = 0x6C
# The comparisons were checked against the scripts (2026-09-25): 0x95 is by far the most used and is
# "flag == 1"; distances are compared with 0x93/0x94 ("f1() > 3 -> out of talk range").
BINOPS = {0x8C: '+', 0x8D: '-', 0x8E: '*', 0x8F: '/', 0x91: '<=', 0x92: '>=', 0x93: '<',
          0x94: '>', 0x95: '==', 0x96: '!=', 0x98: '&&', 0x99: '||'}


class Call:
    def __init__(self, fid, args):
        self.fid, self.args = fid, args

    def __repr__(self):
        return 'f%s(%s)' % (self.fid, ', '.join(map(str, self.args)))


def decode(expr):
    """Expression bytes -> (text, [Call]) with every call found (nested ones included)."""
    stack, calls, i = [], [], 0
    while i < len(expr):
        b = expr[i]
        i += 1
        if b <= 0x7F:
            stack.append(b - 64)
        elif b == 0x80:
            stack.append(struct.unpack_from('<f', expr, i)[0])
            i += 4
        elif b == 0x81:
            stack.append(struct.unpack_from('<d', expr, i)[0])
            i += 8
        elif b == 0x82:
            stack.append(struct.unpack_from('<i', expr, i)[0])
            i += 4
        elif 0x84 <= b <= 0x8A:
            n = b - 0x84
            args = stack[len(stack) - n:] if n else []
            del stack[len(stack) - n:]
            call = Call(stack.pop(), args)
            calls.append(call)
            stack.append(call)
        elif b in BINOPS:
            r = stack.pop()
            l = stack.pop()
            stack.append('(%s %s %s)' % (l, BINOPS[b], r))
        elif b == 0x9A:
            stack.append('!%s' % (stack.pop(),))
        elif b == 0xA1:
            break
        elif b == 0xA5:
            end = i
            while expr[end:end + 2] != b'\x00\x00':
                end += 2
            stack.append(repr(expr[i:end].decode('utf-16-le', 'replace')))
            i = end + 2
        elif b == 0xA6:
            pass
        elif 0xA7 <= b <= 0xAE:
            stack.append('r%d = %s' % (b - 0xA7, stack.pop()))
        elif 0xAF <= b <= 0xB6:
            stack.append('r%d' % (b - 0xAF))
        elif b == 0xB7:
            stack.append('abort_if_false(%s)' % (stack.pop(),))
        elif b == 0xB8:
            stack.append('arg[%s]' % (stack.pop(),))
        elif b == 0xB9:
            stack.append('result')
        elif b == 0xBA:
            stack.append('stackval')
        else:
            stack.append('?%02X' % b)
    return (' ; '.join(map(str, stack)), calls)


class Esd:
    def __init__(self, data):
        assert data[:4] == b'fsSL', data[:4]
        self.data = data
        h = struct.unpack_from('<27i', data, 4)
        self.group_count, self.state_count = h[9], h[11]
        groups_off, group_count = struct.unpack_from('<qq', data, DATA + 0x18)
        assert group_count == self.group_count
        self.groups = {}
        for g in range(group_count):
            gid, states_off, count, first = struct.unpack_from('<4q', data, DATA + groups_off + g * 0x20)
            states = {}
            for s in range(count):
                states[states_off + s * 0x48] = self._state(states_off + s * 0x48)
            self.groups[gid] = {'states': states, 'first': first}

    def q(self, off, n=1):
        return struct.unpack_from('<%dq' % n, self.data, DATA + off)

    def _commands(self, off, count):
        out = []
        for c in range(count):
            bank, cid = struct.unpack_from('<ii', self.data, DATA + off + c * 0x18)
            args_off, args_count = self.q(off + c * 0x18 + 8, 2)
            args = []
            for a in range(args_count):
                eo, el = self.q(args_off + a * 0x10, 2)
                args.append(decode(self.data[DATA + eo:DATA + eo + el]))
            out.append((bank, cid, args))
        return out

    def _condition(self, off):
        target, pass_off, pass_count, sub_off, sub_count, ev_off, ev_len = self.q(off, 7)
        subs = [self._condition(o) for o in self.q(sub_off, sub_count)] if sub_count else []
        return {'target': target, 'pass': self._commands(pass_off, pass_count), 'subs': subs,
                'eval': decode(self.data[DATA + ev_off:DATA + ev_off + ev_len])}

    def _state(self, off):
        sid, c_off, c_count, en_off, en_count, ex_off, ex_count, wh_off, wh_count = self.q(off, 9)
        conds = [self._condition(o) for o in self.q(c_off, c_count)] if c_count else []
        return {'id': sid, 'conditions': conds, 'entry': self._commands(en_off, en_count),
                'exit': self._commands(ex_off, ex_count), 'while': self._commands(wh_off, wh_count)}

    def all_calls(self):
        """Every condition function call: [(group id, state id, Call)]."""
        out = []

        def walk(gid, sid, cond):
            out.extend((gid, sid, c) for c in cond['eval'][1])
            for bank, cid, args in cond['pass']:
                for _, calls in args:
                    out.extend((gid, sid, c) for c in calls)
            for sub in cond['subs']:
                walk(gid, sid, sub)

        for gid, group in self.groups.items():
            for state in group['states'].values():
                for cond in state['conditions']:
                    walk(gid, state['id'], cond)
                for key in ('entry', 'exit', 'while'):
                    for bank, cid, args in state[key]:
                        for _, calls in args:
                            out.extend((gid, state['id'], c) for c in calls)
        return out

    def all_commands(self):
        """Every command: [(group id, state id, bank, id, [arg text])]."""
        out = []

        def walk(gid, sid, cond):
            for bank, cid, args in cond['pass']:
                out.append((gid, sid, bank, cid, [t for t, _ in args]))
            for sub in cond['subs']:
                walk(gid, sid, sub)

        for gid, group in self.groups.items():
            for state in group['states'].values():
                for key in ('entry', 'exit', 'while'):
                    for bank, cid, args in state[key]:
                        out.append((gid, state['id'], bank, cid, [t for t, _ in args]))
                for cond in state['conditions']:
                    walk(gid, state['id'], cond)
        return out
