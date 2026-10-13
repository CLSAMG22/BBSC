"""Havok 2014 spline-compressed animation decoder (hkaSplineCompressedAnimation), read-only.

Object layout (Bloodborne, 64-bit, read off the files): +0x0C type, +0x10 duration, +0x14 transform
tracks, +0x20 extracted motion, +0x28 annotation tracks, +0x38 numFrames, +0x3C numBlocks,
+0x40 maxFramesPerBlock, +0x44 maskAndQuantizationSize, +0x48 blockDuration, +0x50 frameDuration,
+0x58 blockOffsets, +0x68 floatBlockOffsets, +0x98 data.
Binding: +0x18 animation, +0x20 transformTrackToBoneIndices. Reference frame (root motion):
+0x10 up, +0x20 forward, +0x30 duration, +0x38 samples (vec4 per frame).

Per block: one 4-byte mask per track (quantization byte: bits 0-1 translation, 2-5 rotation,
6-7 scale; then flag bytes for translation, rotation, scale: bits 0-3 static X/Y/Z/W,
bits 4-7 spline X/Y/Z/W), then each track's translation, rotation, scale, each 4-byte aligned.
A spline channel: u16 n (control points - 1), u8 degree, n+degree+2 byte knots (frames in the block).
"""
import math
import struct

import numpy as np

import hkx

POLAR32, THREECOMP40, THREECOMP48, THREECOMP24, STRAIGHT16, UNCOMPRESSED = range(6)
Q_SIZE = {POLAR32: 4, THREECOMP40: 5, THREECOMP48: 6, THREECOMP24: 3, STRAIGHT16: 2, UNCOMPRESSED: 16}
Q_ALIGN = {POLAR32: 4, THREECOMP40: 1, THREECOMP48: 2, THREECOMP24: 1, STRAIGHT16: 2, UNCOMPRESSED: 4}


class Reader:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def align(self, n):
        self.p = (self.p + n - 1) // n * n

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]
        self.p += 2
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.p)[0]
        self.p += 4
        return v

    def raw(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v


def quat(r, qt):
    """One quantized quaternion -> (x, y, z, w)."""
    if qt == THREECOMP48:
        x, y, z = struct.unpack("<3H", r.raw(6))
        shift = ((y >> 14) & 2) | ((x >> 15) & 1)
        neg = (z >> 15) & 1
        mask = (1 << 15) - 1
        f = 0.000043161
        v = [((c & mask) - (mask >> 1)) * f for c in (x, y, z)]
    elif qt == THREECOMP40:
        c = int.from_bytes(r.raw(5), "little")
        mask = (1 << 12) - 1
        f = 0.000345436
        v = [(((c >> (12 * i)) & mask) - (mask >> 1)) * f for i in range(3)]
        shift = (c >> 36) & 3
        neg = (c >> 38) & 1
    elif qt == THREECOMP24:
        c = int.from_bytes(r.raw(3), "little")
        mask = (1 << 7) - 1
        f = 0.707106781 / 63.0
        v = [(((c >> (7 * i)) & mask) - (mask >> 1)) * f for i in range(3)]
        shift = (c >> 21) & 3
        neg = (c >> 23) & 1
    elif qt == UNCOMPRESSED:
        return struct.unpack("<4f", r.raw(16))
    elif qt == POLAR32:
        c = struct.unpack("<I", r.raw(4))[0]
        # 10 bits phi, 18 bits theta... polar packing (rare in Bloodborne)
        r_ = (c >> 18) & 0x3FF
        phi_theta = c & 0x3FFFF
        phi = math.floor(math.sqrt(phi_theta))
        theta = 0.0
        if phi > 0:
            theta = (math.pi / 4) * (phi_theta - phi * phi) / phi
            phi = (math.pi / 2) * phi / 511.0
        rr = 1.0 - (r_ / 1023.0) ** 2
        rr = 1.0 - rr * rr
        rad = math.sqrt(max(0.0, 1.0 - rr * rr))
        x = math.sin(phi) * math.cos(theta) * rad
        y = math.sin(phi) * math.sin(theta) * rad
        z = math.cos(phi) * rad
        w = rr
        sx, sy, sz, sw = (c >> 28) & 1, (c >> 29) & 1, (c >> 30) & 1, (c >> 31) & 1
        return (-x if sx else x, -y if sy else y, -z if sz else z, -w if sw else w)
    else:
        raise ValueError("rotation quantization %d" % qt)
    w = math.sqrt(max(0.0, 1.0 - sum(c * c for c in v)))
    if neg:
        w = -w
    out = v[:shift] + [w] + v[shift:]
    return tuple(out)


def knot_span(degree, t, n, knots):
    if t >= knots[n + 1]:
        return n
    lo, hi = degree, n + 1
    mid = (lo + hi) // 2
    while t < knots[mid] or t >= knots[mid + 1]:
        if t < knots[mid]:
            hi = mid
        else:
            lo = mid
        mid = (lo + hi) // 2
    return mid


def spline_eval(degree, knots, cps, t):
    """cps: (n+1, k) array; knots: list. Returns (k,)."""
    n = len(cps) - 1
    span = knot_span(degree, t, n, knots)
    N = [1.0] + [0.0] * degree
    for i in range(1, degree + 1):
        for j in range(i - 1, -1, -1):
            den = knots[span + i - j] - knots[span - j]
            a = (t - knots[span - j]) / den if den else 0.0
            tmp = N[j] * a
            N[j + 1] += N[j] - tmp
            N[j] = tmp
    out = np.zeros(cps.shape[1])
    for i in range(degree + 1):
        out += cps[span - i] * N[i]
    return out


class Channel:
    """A vec3 or quaternion channel of one track in one block: static value or spline."""

    def __init__(self, value=None, degree=None, knots=None, cps=None):
        self.value = value
        self.degree, self.knots, self.cps = degree, knots, cps

    def at(self, t):
        if self.cps is None:
            return np.asarray(self.value, dtype=np.float64)
        return spline_eval(self.degree, self.knots, self.cps, t)


def read_vec3(r, flags, qt, default):
    spline = flags & 0x70
    static = flags & 0x07
    if spline:
        n = r.u16()
        degree = r.u8()
        knots = list(r.raw(n + degree + 2))
        r.align(4)
        rng = []
        for axis in range(3):
            if flags & (0x10 << axis):
                rng.append(("s", r.f32(), r.f32()))
            elif flags & (0x01 << axis):
                rng.append(("c", r.f32()))
            else:
                rng.append(("c", default))
        cps = np.zeros((n + 1, 3))
        for i in range(n + 1):
            for axis in range(3):
                if rng[axis][0] == "s":
                    if qt == 0:
                        q = r.u8() / 255.0
                    else:
                        q = r.u16() / 65535.0
                    cps[i, axis] = rng[axis][1] + (rng[axis][2] - rng[axis][1]) * q
                else:
                    cps[i, axis] = rng[axis][1]
        r.align(4)
        return Channel(degree=degree, knots=knots, cps=cps)
    value = [default] * 3
    for axis in range(3):
        if static & (1 << axis):
            value[axis] = r.f32()
    r.align(4)
    return Channel(value=value)


def read_quat(r, flags, qt):
    if flags & 0xF0:
        n = r.u16()
        degree = r.u8()
        knots = list(r.raw(n + degree + 2))
        r.align(Q_ALIGN[qt])
        cps = np.array([quat(r, qt) for _ in range(n + 1)])
        r.align(4)
        return Channel(degree=degree, knots=knots, cps=cps)
    if flags & 0x0F:
        r.align(Q_ALIGN[qt])
        v = quat(r, qt)
        r.align(4)
        return Channel(value=v)
    r.align(4)
    return Channel(value=(0.0, 0.0, 0.0, 1.0))


class Animation:
    def __init__(self, pf, rot_offset=0):
        d = pf.d.data
        o = pf.of_class("hkaSplineCompressedAnimation")[0]
        self.duration = pf.f32(o + 0x10)
        self.tracks = pf.i32(o + 0x14)
        self.frames = pf.i32(o + 0x38)
        self.blocks = pf.i32(o + 0x3C)
        self.per_block = pf.i32(o + 0x40)
        self.frame_dur = pf.f32(o + 0x50)
        bo, bn = pf.array(o + 0x58)
        offs = [struct.unpack_from("<I", d, bo + 4 * i)[0] for i in range(bn)]
        da, dn = pf.array(o + 0x98)
        data = d[da:da + dn]
        self.block_tracks = []
        for b in range(self.blocks):
            r = Reader(data, offs[b])
            masks = [r.raw(4) for _ in range(self.tracks)]
            r.align(4)
            tracks = []
            for m in masks:
                qt = m[0]
                tq, rq, sq = qt & 3, ((qt >> 2) & 0xF) + rot_offset, (qt >> 6) & 3
                t = read_vec3(r, m[1], tq, 0.0)
                q = read_quat(r, m[2], rq)
                s = read_vec3(r, m[3], sq, 1.0)
                tracks.append((t, q, s))
            self.block_tracks.append(tracks)
        # track -> bone
        self.track_to_bone = list(range(self.tracks))
        for bo_ in pf.of_class("hkaAnimationBinding"):
            ta, tn = pf.array(bo_ + 0x20)
            if ta is not None and tn:
                self.track_to_bone = [pf.i16(ta + 2 * i) for i in range(tn)]
        # root motion
        self.root = None
        rf = pf.of_class("hkaDefaultAnimatedReferenceFrame")
        if rf:
            sa, sn = pf.array(rf[0] + 0x38)
            if sa is not None:
                self.root = np.array([struct.unpack_from("<4f", d, sa + 16 * i) for i in range(sn)])

    def sample(self, frame):
        """Local (t, q, s) per track at a (float) frame index."""
        step = self.per_block - 1
        b = min(int(frame // step), self.blocks - 1)
        lt = frame - b * step
        out = []
        for t, q, s in self.block_tracks[b]:
            qq = q.at(lt)
            qq = qq / (np.linalg.norm(qq) or 1.0)
            out.append((t.at(lt), qq, s.at(lt)))
        return out


def load(path, rot_offset=0):
    with open(path, "rb") as fh:
        return Animation(hkx.Packfile(fh.read()), rot_offset)
