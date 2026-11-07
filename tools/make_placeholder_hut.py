"""Generates a *placeholder* round hut model: content/models/hut_round.gltf.

The model is written in glTF 2.0, the same format Blender exports
(File > Export > glTF 2.0). A modeller can replace this file with a hand-made
hut and the engine will load it the same way. Mud walls with a doorway,
and a conical thatch roof with an overhang.

Usage:  python tools/make_placeholder_hut.py
Needs:  numpy
"""
import json
import math
import os
import struct

import numpy as np

OUT = os.path.join(os.path.dirname(__file__), "..", "content", "models")

R_OUT = 1.8        # wall outer radius (m)
THICK = 0.18       # wall thickness
H = 2.1            # wall height
DOOR_H = 1.65      # doorway height
DOOR_HALF = math.radians(17)  # doorway half-angle, centred on +Z (the hut's front)
SEG = 40
ROOF_R = 2.55      # roof edge radius (overhang)
ROOF_EDGE_Y = H - 0.3
ROOF_APEX_Y = H + 1.75


class MeshBuilder:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx = [], [], [], []

    def vert(self, p, n, uv):
        self.pos.append(p)
        self.nrm.append(n)
        self.uv.append(uv)
        return len(self.pos) - 1

    def quad(self, a, b, c, d):
        self.tri(a, b, c)
        self.tri(a, c, d)

    def tri(self, a, b, c):
        # Make sure the winding is counter-clockwise when seen from the side
        # the normals point to (the engine's front-face convention).
        pa, pb, pc = (np.array(self.pos[i]) for i in (a, b, c))
        face = np.cross(pb - pa, pc - pa)
        avg = np.array(self.nrm[a]) + np.array(self.nrm[b]) + np.array(self.nrm[c])
        if np.dot(face, avg) < 0:
            b, c = c, b
        self.idx += [a, b, c]


def angle_of(i):
    return 2 * math.pi * i / SEG


def in_doorway(a):
    # Angle measured from +Z toward +X.
    d = (a + math.pi) % (2 * math.pi) - math.pi
    return abs(d) < DOOR_HALF


def ring_point(r, a, y):
    return (r * math.sin(a), y, r * math.cos(a))


def build_walls():
    m = MeshBuilder()
    r_in = R_OUT - THICK
    for i in range(SEG):
        a0, a1 = angle_of(i), angle_of(i + 1)
        mid = 0.5 * (a0 + a1)
        bottom = DOOR_H if in_doorway(mid) else 0.0
        u0, u1 = 4 * i / SEG, 4 * (i + 1) / SEG
        for r, sign in ((R_OUT, 1), (r_in, -1)):
            n0 = (sign * math.sin(a0), 0, sign * math.cos(a0))
            n1 = (sign * math.sin(a1), 0, sign * math.cos(a1))
            v = [m.vert(ring_point(r, a0, bottom), n0, (u0, 1 - bottom / H)),
                 m.vert(ring_point(r, a1, bottom), n1, (u1, 1 - bottom / H)),
                 m.vert(ring_point(r, a1, H), n1, (u1, 0)),
                 m.vert(ring_point(r, a0, H), n0, (u0, 0))]
            m.quad(*v)
        # top rim
        up = (0, 1, 0)
        v = [m.vert(ring_point(r_in, a0, H), up, (u0, 0)), m.vert(ring_point(R_OUT, a0, H), up, (u0, 0.05)),
             m.vert(ring_point(R_OUT, a1, H), up, (u1, 0.05)), m.vert(ring_point(r_in, a1, H), up, (u1, 0))]
        m.quad(*v)
        if bottom > 0:  # underside of the lintel over the doorway
            down = (0, -1, 0)
            v = [m.vert(ring_point(r_in, a0, bottom), down, (u0, 0)),
                 m.vert(ring_point(r_in, a1, bottom), down, (u1, 0)),
                 m.vert(ring_point(R_OUT, a1, bottom), down, (u1, 0.05)),
                 m.vert(ring_point(R_OUT, a0, bottom), down, (u0, 0.05))]
            m.quad(*v)
    # door jambs (the two sides of the opening)
    for a, side in ((-DOOR_HALF, -1), (DOOR_HALF, 1)):
        n = (-side * math.cos(a), 0, side * math.sin(a))  # faces into the opening
        v = [m.vert(ring_point(r_in, a, 0), n, (0, 1)), m.vert(ring_point(R_OUT, a, 0), n, (0.05, 1)),
             m.vert(ring_point(R_OUT, a, DOOR_H), n, (0.05, 1 - DOOR_H / H)),
             m.vert(ring_point(r_in, a, DOOR_H), n, (0, 1 - DOOR_H / H))]
        m.quad(*v)
    return m


def build_roof():
    m = MeshBuilder()
    rise = ROOF_APEX_Y - ROOF_EDGE_Y
    slant = math.hypot(ROOF_R, rise)
    ny, nr = ROOF_R / slant, rise / slant
    for i in range(SEG):
        a0, a1 = angle_of(i), angle_of(i + 1)
        u0, u1 = 6 * i / SEG, 6 * (i + 1) / SEG
        am = 0.5 * (a0 + a1)
        # outer (top) surface
        e0 = m.vert(ring_point(ROOF_R, a0, ROOF_EDGE_Y), (nr * math.sin(a0), ny, nr * math.cos(a0)), (u0, 1))
        e1 = m.vert(ring_point(ROOF_R, a1, ROOF_EDGE_Y), (nr * math.sin(a1), ny, nr * math.cos(a1)), (u1, 1))
        ap = m.vert((0, ROOF_APEX_Y, 0), (nr * math.sin(am), ny, nr * math.cos(am)), (0.5 * (u0 + u1), 0))
        m.tri(e0, e1, ap)
        # underside, slightly lower so the roof has thickness
        e0 = m.vert(ring_point(ROOF_R, a0, ROOF_EDGE_Y - 0.06), (-nr * math.sin(a0), -ny, -nr * math.cos(a0)), (u0, 1))
        e1 = m.vert(ring_point(ROOF_R, a1, ROOF_EDGE_Y - 0.06), (-nr * math.sin(a1), -ny, -nr * math.cos(a1)), (u1, 1))
        ap = m.vert((0, ROOF_APEX_Y - 0.25, 0), (-nr * math.sin(am), -ny, -nr * math.cos(am)), (0.5 * (u0 + u1), 0))
        m.tri(e0, e1, ap)
        # thatch edge band
        out0, out1 = (math.sin(a0), 0, math.cos(a0)), (math.sin(a1), 0, math.cos(a1))
        v = [m.vert(ring_point(ROOF_R, a0, ROOF_EDGE_Y - 0.06), out0, (u0, 0.95)),
             m.vert(ring_point(ROOF_R, a1, ROOF_EDGE_Y - 0.06), out1, (u1, 0.95)),
             m.vert(ring_point(ROOF_R, a1, ROOF_EDGE_Y), out1, (u1, 1)),
             m.vert(ring_point(ROOF_R, a0, ROOF_EDGE_Y), out0, (u0, 1))]
        m.quad(*v)
    return m


def write_gltf(parts, name):
    os.makedirs(OUT, exist_ok=True)
    blob = bytearray()
    views, accessors, meshes_prims, materials, textures, images = [], [], [], [], [], []

    def add_view(data, target):
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        return len(views) - 1

    for part_name, mesh, texture in parts:
        pos = np.array(mesh.pos, dtype=np.float32)
        nrm = np.array(mesh.nrm, dtype=np.float32)
        nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
        uv = np.array(mesh.uv, dtype=np.float32)
        idx = np.array(mesh.idx, dtype=np.uint32)
        a_pos = len(accessors)
        accessors.append({"bufferView": add_view(pos.tobytes(), 34962), "componentType": 5126,
                          "count": len(pos), "type": "VEC3",
                          "min": pos.min(axis=0).tolist(), "max": pos.max(axis=0).tolist()})
        accessors.append({"bufferView": add_view(nrm.tobytes(), 34962), "componentType": 5126,
                          "count": len(nrm), "type": "VEC3"})
        accessors.append({"bufferView": add_view(uv.tobytes(), 34962), "componentType": 5126,
                          "count": len(uv), "type": "VEC2"})
        accessors.append({"bufferView": add_view(idx.tobytes(), 34963), "componentType": 5125,
                          "count": len(idx), "type": "SCALAR"})
        images.append({"uri": "../textures/" + texture})
        textures.append({"source": len(images) - 1})
        materials.append({"name": part_name, "pbrMetallicRoughness": {
            "baseColorTexture": {"index": len(textures) - 1}, "metallicFactor": 0.0, "roughnessFactor": 1.0}})
        meshes_prims.append({"attributes": {"POSITION": a_pos, "NORMAL": a_pos + 1, "TEXCOORD_0": a_pos + 2},
                             "indices": a_pos + 3, "material": len(materials) - 1})

    bin_name = name + ".bin"
    with open(os.path.join(OUT, bin_name), "wb") as f:
        f.write(blob)
    doc = {
        "asset": {"version": "2.0", "generator": "Rakshas tools/make_placeholder_hut.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": meshes_prims}],
        "materials": materials, "textures": textures, "images": images,
        "accessors": accessors, "bufferViews": views,
        "buffers": [{"uri": bin_name, "byteLength": len(blob)}],
    }
    with open(os.path.join(OUT, name + ".gltf"), "w") as f:
        json.dump(doc, f, indent=1)
    print("wrote", name + ".gltf", f"({sum(len(p[1].idx) // 3 for p in parts)} triangles)")


if __name__ == "__main__":
    write_gltf([("mud_walls", build_walls(), "mud_plaster.png"),
                ("thatch_roof", build_roof(), "thatch.png")], "hut_round")
