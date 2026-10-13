"""Skeleton maths shared by the Blender importer: FLVER bind pose and Havok animation -> world matrices
(game space: Y-up). Column-vector convention: world = parent_world @ local.
FLVER bone local = T @ Ry @ Rz @ Rx @ S (SoulsFormats: Scale * RotX * RotZ * RotY * Translation, row vectors)."""
import numpy as np


def rot_x(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_z(a):
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def quat_mat(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def trs(t, r3, s):
    m = np.eye(4)
    m[:3, :3] = r3 @ np.diag(s)
    m[:3, 3] = t
    return m


def flver_local(b):
    rx, ry, rz = b["r"]
    return trs(b["t"], rot_y(ry) @ rot_z(rz) @ rot_x(rx), b["s"])


def flver_world(bones):
    out = [None] * len(bones)

    def get(i):
        if out[i] is None:
            p = bones[i]["parent"]
            loc = flver_local(bones[i])
            out[i] = loc if p < 0 else get(p) @ loc
        return out[i]
    for i in range(len(bones)):
        get(i)
    return out


def havok_world(sk, locals_):
    """locals_: [(t, q, s)] per Havok bone -> world matrices."""
    out = [None] * len(sk["bones"])
    for i, p in enumerate(sk["parents"]):  # Havok stores parents before children
        loc = trs(locals_[i][0], quat_mat(locals_[i][1]), locals_[i][2])
        out[i] = loc if p < 0 else out[p] @ loc
    return out


def pose_worlds(flver_bones, sk, anim_locals):
    """World matrices for every FLVER bone at one frame: animated bones follow the Havok chain,
    the rest (cloth, face, helpers) keep their bind offset from their FLVER parent."""
    hw = havok_world(sk, anim_locals)
    by_name = {n: i for i, n in enumerate(sk["bones"])}
    out = [None] * len(flver_bones)

    def get(i):
        if out[i] is None:
            b = flver_bones[i]
            h = by_name.get(b["name"])
            if h is not None:
                out[i] = hw[h]
            else:
                p = b["parent"]
                loc = flver_local(b)
                out[i] = loc if p < 0 else get(p) @ loc
        return out[i]
    for i in range(len(flver_bones)):
        get(i)
    return out
