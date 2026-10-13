"""Bloodborne EMEVD (event script) reader: events and their instructions with raw arguments.

Layout (64-bit EVD, read off the files 2026-09-25):
  header: +0x10 long event_count, +0x18 events_off, +0x20 instr_count, +0x28 instr_off,
          +0x50 param_count, +0x58 param_off, +0x70 args_len, +0x78 args_off, +0x88 strings_off
  event (0x30): long id, long instr_count, long instr_off, long param_count, long param_off,
                int rest_behavior, int pad
  instruction (0x20): int bank, int id, long args_len, long args_off (from args base), long layer
"""
import struct

import bbfiles


class Emevd:
    def __init__(self, path):
        d = bbfiles.read_dcx(path)
        assert d[:4] == b'EVD\x00', d[:4]
        self.data = d
        (event_count, events_off, instr_count, instr_off) = struct.unpack_from('<qqqq', d, 0x10)
        fields = struct.unpack_from('<16q', d, 0x10)
        self.header = fields
        self.args_off = fields[13]  # +0x70 args_len, +0x78 args_off
        self.events = []
        for k in range(event_count):
            e = events_off + k * 0x30
            eid, n, ioff, pn, poff = struct.unpack_from('<qqqqq', d, e)
            instrs = []
            for j in range(n):
                i = instr_off + ioff + j * 0x20
                bank, iid, alen, aoff = struct.unpack_from('<iiqq', d, i)
                args = d[self.args_off + aoff:self.args_off + aoff + alen] if alen else b''
                instrs.append((bank, iid, args))
            self.events.append((eid, instrs))

    def instructions_with(self, value):
        """Every (event id, index, bank, id, args) whose arguments contain the int32 value."""
        needle = struct.pack('<i', value)
        out = []
        for eid, instrs in self.events:
            for j, (bank, iid, args) in enumerate(instrs):
                for p in range(0, len(args) - 3, 4):
                    if args[p:p + 4] == needle:
                        out.append((eid, j, bank, iid, args))
                        break
        return out
