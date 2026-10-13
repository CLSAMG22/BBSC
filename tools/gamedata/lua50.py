"""Lua 5.0 bytecode reader + disassembler for the game's compiled scripts (script/*.luabnd.dcx).

Header seen in global_event.lua: 1B 4C 75 61 50 (5.0), little endian, int 4, size_t 8,
Instruction 4, op 6 / A 8 / B 9 / C 9 bits, lua_Number 8 (double).
  python lua50.py <luabnd name> <file in bnd> [global function name ...]
prints each named global function (and its nested functions) as readable instructions.
"""
import os
import struct
import sys

import bbfiles

OPS = ['MOVE', 'LOADK', 'LOADBOOL', 'LOADNIL', 'GETUPVAL', 'GETGLOBAL', 'GETTABLE', 'SETGLOBAL',
       'SETUPVAL', 'SETTABLE', 'NEWTABLE', 'SELF', 'ADD', 'SUB', 'MUL', 'DIV', 'POW', 'UNM', 'NOT',
       'CONCAT', 'JMP', 'EQ', 'LT', 'LE', 'TEST', 'CALL', 'TAILCALL', 'RETURN', 'FORLOOP',
       'TFORLOOP', 'TFORPREP', 'SETLIST', 'SETLISTO', 'CLOSE', 'CLOSURE']
MAXSTACK = 250
BX_OPS = {'LOADK', 'GETGLOBAL', 'SETGLOBAL', 'CLOSURE'}
SBX_OPS = {'JMP', 'FORLOOP', 'TFORPREP'}


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def byte(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def int(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]
        self.p += 4
        return v

    def size(self):
        v = struct.unpack_from('<Q', self.d, self.p)[0]
        self.p += 8
        return v

    def num(self):
        v = struct.unpack_from('<d', self.d, self.p)[0]
        self.p += 8
        return v

    def string(self):
        n = self.size()
        if n == 0:
            return None
        s = self.d[self.p:self.p + n - 1].decode('latin-1')
        self.p += n
        return s


class Proto:
    pass


def load_function(r):
    f = Proto()
    f.source = r.string()
    f.line = r.int()
    f.nups, f.nparams, f.vararg, f.maxstack = r.byte(), r.byte(), r.byte(), r.byte()
    r.p += 4 * r.int()                      # line info
    for _ in range(r.int()):                # locals
        r.string()
        r.int()
        r.int()
    f.upvalues = [r.string() for _ in range(r.int())]
    r.int()  # one more (empty) list in this build, between the upvalue names and the constants
    f.k = []
    for _ in range(r.int()):
        t = r.byte()
        if t == 3:
            f.k.append(r.num())
        elif t == 4:
            f.k.append(r.string())
        elif t == 0:
            f.k.append(None)
        else:
            raise ValueError('constant type %d' % t)
    f.p = [load_function(r) for _ in range(r.int())]
    f.code = [struct.unpack_from('<I', r.d, r.p + 4 * i)[0] for i in range(r.int())]
    r.p += 4 * len(f.code)
    return f


def load(data):
    assert data[:5] == b'\x1bLua\x50', data[:5]
    r = Reader(data)
    r.p = 14 + 8  # header (14 bytes) + test number (double)
    return load_function(r)


def fmt_k(f, i):
    v = f.k[i]
    return repr(v) if isinstance(v, str) else ('%g' % v if isinstance(v, float) else 'nil')


def rk(f, x):
    return 'K[%s]' % fmt_k(f, x - MAXSTACK) if x >= MAXSTACK else 'R%d' % x


def disassemble(f, name, out=print, depth=0):
    pad = '  ' * depth
    out('%sfunction %s (params %d, upvalues %d)' % (pad, name, f.nparams, f.nups))
    for pc, ins in enumerate(f.code):
        op = OPS[ins & 0x3F]
        c = (ins >> 6) & 0x1FF
        b = (ins >> 15) & 0x1FF
        a = (ins >> 24) & 0xFF
        bx = (ins >> 6) & 0x3FFFF
        sbx = bx - 131071
        if op in BX_OPS:
            arg = '%d %s' % (a, fmt_k(f, bx)) if op != 'CLOSURE' else '%d proto%d' % (a, bx)
        elif op in SBX_OPS:
            arg = '%d -> %d' % (a, pc + 1 + sbx)
        elif op in ('SETTABLE', 'ADD', 'SUB', 'MUL', 'DIV', 'POW', 'EQ', 'LT', 'LE'):
            arg = '%d %s %s' % (a, rk(f, b), rk(f, c))
        elif op in ('GETTABLE', 'SELF'):
            arg = '%d R%d %s' % (a, b, rk(f, c))
        else:
            arg = '%d %d %d' % (a, b, c)
        out('%s  %3d %-9s %s' % (pad, pc, op, arg))
    for i, sub in enumerate(f.p):
        disassemble(sub, '%s.proto%d' % (name, i), out, depth + 1)


def globals_defined(main):
    """{global name: proto} for `name = function ... end` at the top level."""
    found = {}
    last_closure = {}
    for ins in main.code:
        op = OPS[ins & 0x3F]
        a = (ins >> 24) & 0xFF
        bx = (ins >> 6) & 0x3FFFF
        if op == 'CLOSURE':
            last_closure[a] = main.p[bx]
        elif op == 'SETGLOBAL' and a in last_closure:
            found[main.k[bx]] = last_closure.pop(a)
    return found


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8')
    bnd, name = sys.argv[1], sys.argv[2]
    d = bbfiles.read_bnd4(bbfiles.read_dcx(os.path.join(bbfiles.GAME, 'script', bnd + '.luabnd.dcx')))
    main = load(d[name][1])
    funcs = globals_defined(main)
    for want in sys.argv[3:]:
        if want in funcs:
            disassemble(funcs[want], want)
        else:
            print('no global function', want)
