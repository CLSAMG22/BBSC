"""Bloodborne MSB reader (models, events, points, parts) - just what the fog wall survey needs.

Record layouts were read off the files themselves (2026-09-25):
  list:   int version, int count (records + 1), long name_offset, long offsets[count]
          (the last offset is the next list, 0 at the end)
  event:  +0x00 long name_off, +0x08 int event_index, +0x0C int type, +0x10 int type_index,
          +0x18 long base_off, +0x20 long type_off;
          base: +0x00 int part_index, +0x04 int point_index, +0x08 int entity_id
  part:   +0x00 long description_off, +0x08 long name_off, +0x10 int instance_id, +0x14 int type,
          +0x18 int type_index, +0x1C int model_index, +0x20 long sib_off,
          +0x28 float[3] position, +0x34 float[3] rotation, +0x40 float[3] scale,
          +0xB0 long entity_off, +0xB8 long type_off;  entity: +0x00 int entity_id
"""
import struct

import bbfiles


def _wstr(data, off):
    end = off
    while data[end:end + 2] != b'\x00\x00':
        end += 2
    return data[off:end].decode('utf-16-le', 'replace')


EVENT_TYPES = {0: 'Light', 1: 'Sound', 2: 'SFX', 3: 'WindSFX', 4: 'Treasure', 5: 'Generator',
               6: 'Message', 7: 'ObjAct', 8: 'SpawnPoint', 9: 'MapOffset', 10: 'Navmesh',
               11: 'Environment', 12: 'PseudoMultiplayer', 13: 'WindSFX2', 14: 'PatrolInfo',
               15: 'PlatoonInfo', 20: 'Other'}
PART_TYPES = {0: 'MapPiece', 1: 'Object', 2: 'Enemy', 3: 'Item', 4: 'Player', 5: 'Collision',
              6: 'NPCWander', 7: 'Protoboss', 8: 'Navmesh', 9: 'DummyObject', 10: 'DummyEnemy',
              11: 'ConnectCollision'}


class Msb:
    def __init__(self, path):
        self.path = path
        data = bbfiles.read_dcx(path)
        assert data[:4] == b'MSB ', data[:4]
        self.lists = {}
        off = 0x10
        while off:
            version, count = struct.unpack_from('<ii', data, off)
            name = _wstr(data, struct.unpack_from('<q', data, off + 8)[0])
            offsets = struct.unpack_from('<%dq' % count, data, off + 16)
            self.lists[name] = list(offsets[:-1])
            off = offsets[-1]
        self.data = data
        self.models = [_wstr(data, o + struct.unpack_from('<q', data, o)[0])
                       for o in self.lists.get('MODEL_PARAM_ST', [])]
        self.points = [_wstr(data, o + struct.unpack_from('<q', data, o)[0])
                       for o in self.lists.get('POINT_PARAM_ST', [])]
        self.parts = [self._part(o) for o in self.lists.get('PARTS_PARAM_ST', [])]
        self.events = [self._event(o) for o in self.lists.get('EVENT_PARAM_ST', [])]

    def _part(self, o):
        d = self.data
        name = _wstr(d, o + struct.unpack_from('<q', d, o + 0x08)[0])
        ptype, type_index, model = struct.unpack_from('<iii', d, o + 0x14)
        pos = struct.unpack_from('<3f', d, o + 0x28)
        entity_off = struct.unpack_from('<q', d, o + 0xB0)[0]
        entity_id = struct.unpack_from('<i', d, o + entity_off)[0] if entity_off else -1
        return {'name': name, 'type': PART_TYPES.get(ptype, ptype), 'model_index': model,
                'model': self.models[model] if 0 <= model < len(self.models) else None,
                'pos': pos, 'entity_id': entity_id}

    def _event(self, o):
        d = self.data
        name = _wstr(d, o + struct.unpack_from('<q', d, o)[0])
        index, etype, type_index = struct.unpack_from('<iii', d, o + 0x08)
        base_off, type_off = struct.unpack_from('<qq', d, o + 0x18)
        part_index, point_index, entity_id = struct.unpack_from('<iii', d, o + base_off)
        type_data = d[o + type_off:o + type_off + 0x10] if type_off else b''
        return {'name': name, 'type': EVENT_TYPES.get(etype, etype), 'index': index,
                'part_index': part_index, 'point_index': point_index, 'entity_id': entity_id,
                'type_data': type_data}
