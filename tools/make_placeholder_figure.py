"""Generates a *placeholder* animated human figure: content/models/mannequin.gltf.

Styled like a wooden artist's mannequin: rigid segments joined at the
shoulders, elbows, hips and knees, with no face (it stands in for the player,
who is "yourself", Tenet 2.4). It has a skeleton (skin) and three looping
animations: "idle", "walk" and "run".

The animations are simple, mathematically generated cycles so the engine's
animation system can be built and tested. Natural motion comes later from
motion capture or an animator, exported from Blender in glTF with the same
animation names.

The figure faces -Z (the game's "forward" for characters).

Usage:  python tools/make_placeholder_figure.py
Needs:  numpy
"""
import json
import math
import os

import numpy as np

OUT = os.path.join(os.path.dirname(__file__), "..", "content", "models")

# name, parent, rest position in model space (metres)
JOINTS = [
    ("hips", None, (0.0, 0.95, 0.0)),
    ("spine", "hips", (0.0, 1.10, 0.0)),
    ("chest", "spine", (0.0, 1.30, 0.0)),
    ("neck", "chest", (0.0, 1.52, 0.0)),
    ("head", "neck", (0.0, 1.60, 0.0)),
    ("upper_arm_l", "chest", (-0.20, 1.45, 0.0)),
    ("forearm_l", "upper_arm_l", (-0.20, 1.16, 0.0)),
    ("hand_l", "forearm_l", (-0.20, 0.90, 0.0)),
    ("upper_arm_r", "chest", (0.20, 1.45, 0.0)),
    ("forearm_r", "upper_arm_r", (0.20, 1.16, 0.0)),
    ("hand_r", "forearm_r", (0.20, 0.90, 0.0)),
    ("thigh_l", "hips", (-0.10, 0.92, 0.0)),
    ("shin_l", "thigh_l", (-0.10, 0.49, 0.0)),
    ("foot_l", "shin_l", (-0.10, 0.07, 0.0)),
    ("thigh_r", "hips", (0.10, 0.92, 0.0)),
    ("shin_r", "thigh_r", (0.10, 0.49, 0.0)),
    ("foot_r", "shin_r", (0.10, 0.07, 0.0)),
]
INDEX = {name: i for i, (name, _, _) in enumerate(JOINTS)}

# Body segments: (joint they move with, centre, radii)
SEGMENTS = [
    ("hips", (0, 0.95, 0), (0.17, 0.11, 0.11)),
    ("spine", (0, 1.13, 0), (0.13, 0.11, 0.10)),
    ("chest", (0, 1.37, 0), (0.19, 0.15, 0.12)),
    ("neck", (0, 1.56, 0), (0.05, 0.06, 0.05)),
    ("head", (0, 1.67, 0), (0.10, 0.12, 0.11)),
]
for side, sx in (("l", -1), ("r", 1)):
    x = 0.20 * sx
    SEGMENTS += [
        ("upper_arm_" + side, (x, 1.45, 0), (0.06, 0.06, 0.06)),   # shoulder ball
        ("upper_arm_" + side, (x, 1.31, 0), (0.05, 0.15, 0.05)),
        ("forearm_" + side, (x, 1.16, 0), (0.048, 0.048, 0.048)),  # elbow ball
        ("forearm_" + side, (x, 1.03, 0), (0.045, 0.13, 0.045)),
        ("hand_" + side, (x, 0.84, 0), (0.035, 0.07, 0.05)),
    ]
    x = 0.10 * sx
    SEGMENTS += [
        ("thigh_" + side, (x, 0.70, 0), (0.075, 0.22, 0.075)),
        ("shin_" + side, (x, 0.49, 0), (0.065, 0.065, 0.065)),     # knee ball
        ("shin_" + side, (x, 0.27, 0), (0.06, 0.22, 0.06)),
        ("foot_" + side, (x, 0.045, -0.06), (0.05, 0.045, 0.12)),
    ]


def ellipsoid(center, radii, rings=10, segments=14):
    pos, nrm, idx = [], [], []
    for i in range(rings + 1):
        phi = math.pi * i / rings
        for j in range(segments + 1):
            t = 2 * math.pi * j / segments
            n = np.array([math.sin(phi) * math.cos(t), math.cos(phi), math.sin(phi) * math.sin(t)])
            p = np.array(center) + n * np.array(radii)
            nn = n / np.array(radii)
            pos.append(p)
            nrm.append(nn / np.linalg.norm(nn))
    stride = segments + 1
    for i in range(rings):
        for j in range(segments):
            a = i * stride + j
            b, d = a + 1, a + stride
            c = d + 1
            idx += [a, b, c, a, c, d]
    # Make the winding counter-clockwise seen from outside (engine convention).
    out = []
    for k in range(0, len(idx), 3):
        a, b, c = idx[k:k + 3]
        face = np.cross(pos[b] - pos[a], pos[c] - pos[a])
        if np.linalg.norm(face) < 1e-12:
            continue
        if np.dot(face, nrm[a] + nrm[b] + nrm[c]) < 0:
            b, c = c, b
        out += [a, b, c]
    return pos, nrm, out


def quat_axis(axis, angle):
    s = math.sin(angle / 2)
    return np.array([axis[0] * s, axis[1] * s, axis[2] * s, math.cos(angle / 2)])


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.array([aw * bx + ax * bw + ay * bz - az * by,
                     aw * by - ax * bz + ay * bw + az * bx,
                     aw * bz + ax * by - ay * bx + az * bw,
                     aw * bw - ax * bx - ay * by - az * bz])


def euler(x=0.0, y=0.0, z=0.0):
    """Rotation: roll about Z, then pitch about X, then turn about Y."""
    return quat_mul(quat_axis((0, 1, 0), y), quat_mul(quat_axis((1, 0, 0), x), quat_axis((0, 0, 1), z)))


# --- Animation cycles ---------------------------------------------------------
# Pitch about X: positive swings a hanging limb forward (toward -Z).

def walk_pose(phase, a):
    """a = amplitude set (walk or run). Returns {joint: (rotation quat)}, hips y."""
    p = 2 * math.pi * phase
    pose = {}
    for side, offset, arm_out in (("l", 0.0, -0.10), ("r", math.pi, 0.10)):
        q = p + offset
        swing = math.sin(q)
        moving_forward = max(0.0, math.cos(q))
        pose["thigh_" + side] = euler(x=a["thigh"] * swing + a["thigh_bias"])
        pose["shin_" + side] = euler(x=-(a["knee_min"] + a["knee"] * moving_forward))
        pose["foot_" + side] = euler(x=0.25 * a["thigh"] * math.sin(q - 0.6))
        pose["upper_arm_" + side] = euler(x=-a["arm"] * swing, z=arm_out)
        pose["forearm_" + side] = euler(x=a["elbow"] + 0.3 * a["arm"] * max(0.0, -swing))
        pose["hand_" + side] = euler()
    pose["hips"] = euler(y=0.06 * a["arm"] * math.sin(p), z=0.03 * math.sin(p))
    pose["spine"] = euler(x=a["lean"])
    pose["chest"] = euler(y=-0.10 * a["arm"] * math.sin(p))
    pose["neck"] = euler(x=-a["lean"] * 0.5)
    pose["head"] = euler()
    hips_y = 0.95 + a["bob"] * math.cos(2 * p) - a["crouch"]
    return pose, hips_y


WALK = dict(thigh=0.45, thigh_bias=0.0, knee_min=0.06, knee=0.55, arm=0.32, elbow=0.18,
            lean=-0.04, bob=0.022, crouch=0.01)
RUN = dict(thigh=0.80, thigh_bias=0.10, knee_min=0.25, knee=1.15, arm=0.65, elbow=1.15,
           lean=-0.20, bob=0.045, crouch=0.06)


def idle_pose(phase):
    p = 2 * math.pi * phase
    breathe = math.sin(p)
    pose = {name: euler() for name, _, _ in JOINTS}
    pose["chest"] = euler(x=0.02 * breathe)
    pose["head"] = euler(y=0.05 * math.sin(p * 0.5 + 0.4), x=-0.01 * breathe)
    for side, arm_out in (("l", -0.08), ("r", 0.08)):
        pose["upper_arm_" + side] = euler(x=0.02 * breathe, z=arm_out)
        pose["forearm_" + side] = euler(x=0.15)
    return pose, 0.95 - 0.004 * (breathe + 1)


def sample_clip(pose_fn, duration, samples):
    times = np.linspace(0.0, duration, samples + 1)
    rotations = {name: [] for name, _, _ in JOINTS}
    hips_y = []
    for t in times:
        pose, y = pose_fn((t / duration) % 1.0 if t < duration else 0.0)
        for name, _, _ in JOINTS:
            q = pose.get(name, euler())
            rotations[name].append(q / np.linalg.norm(q))
        hips_y.append(y)
    return times, rotations, hips_y


def build():
    # Geometry: every vertex moves rigidly with one joint (a mannequin).
    positions, normals, joints, weights, indices = [], [], [], [], []
    for joint_name, center, radii in SEGMENTS:
        p, n, idx = ellipsoid(center, radii)
        base = len(positions)
        positions += p
        normals += n
        joints += [[INDEX[joint_name], 0, 0, 0]] * len(p)
        weights += [[1.0, 0.0, 0.0, 0.0]] * len(p)
        indices += [base + i for i in idx]

    blob = bytearray()
    views, accessors = [], []

    def add(data, target=None):
        while len(blob) % 4:
            blob.append(0)
        view = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
        if target:
            view["target"] = target
        views.append(view)
        blob.extend(data)
        return len(views) - 1

    def accessor(array, comp, typ, target=None, minmax=False):
        a = {"bufferView": add(array.tobytes(), target), "componentType": comp, "count": len(array), "type": typ}
        if minmax:
            flat = array.reshape(len(array), -1)
            a["min"] = flat.min(axis=0).tolist()
            a["max"] = flat.max(axis=0).tolist()
        accessors.append(a)
        return len(accessors) - 1

    pos = np.array(positions, dtype=np.float32)
    attrs = {
        "POSITION": accessor(pos, 5126, "VEC3", 34962, minmax=True),
        "NORMAL": accessor(np.array(normals, dtype=np.float32), 5126, "VEC3", 34962),
        "JOINTS_0": accessor(np.array(joints, dtype=np.uint16), 5123, "VEC4", 34962),
        "WEIGHTS_0": accessor(np.array(weights, dtype=np.float32), 5126, "VEC4", 34962),
    }
    ind = accessor(np.array(indices, dtype=np.uint32), 5125, "SCALAR", 34963)

    # Skeleton nodes (0..n-1), then the mesh node.
    nodes = []
    for i, (name, parent, rest) in enumerate(JOINTS):
        parent_rest = JOINTS[INDEX[parent]][2] if parent else (0, 0, 0)
        node = {"name": name, "translation": [rest[k] - parent_rest[k] for k in range(3)]}
        children = [j for j, (_, p, _) in enumerate(JOINTS) if p == name]
        if children:
            node["children"] = children
        nodes.append(node)
    inverse_bind = []
    for _, _, rest in JOINTS:
        m = np.identity(4, dtype=np.float32)
        m[0:3, 3] = [-rest[0], -rest[1], -rest[2]]
        inverse_bind.append(m.T.flatten())  # glTF stores matrices column-major
    ibm = accessor(np.array(inverse_bind, dtype=np.float32), 5126, "MAT4")
    mesh_node = len(nodes)
    nodes.append({"name": "mannequin_body", "mesh": 0, "skin": 0})

    animations = []
    for clip_name, fn, duration, samples in (
            ("idle", idle_pose, 3.0, 24),
            ("walk", lambda ph: walk_pose(ph, WALK), 1.0, 24),
            ("run", lambda ph: walk_pose(ph, RUN), 0.66, 24)):
        times, rotations, hips_y = sample_clip(fn, duration, samples)
        t_acc = accessor(np.array(times, dtype=np.float32), 5126, "SCALAR", minmax=True)
        samplers, channels = [], []
        for name, _, _ in JOINTS:
            out = accessor(np.array(rotations[name], dtype=np.float32), 5126, "VEC4")
            samplers.append({"input": t_acc, "output": out, "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": INDEX[name], "path": "rotation"}})
        hips_t = np.array([[0.0, y, 0.0] for y in hips_y], dtype=np.float32)
        out = accessor(hips_t, 5126, "VEC3")
        samplers.append({"input": t_acc, "output": out, "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1, "target": {"node": INDEX["hips"], "path": "translation"}})
        animations.append({"name": clip_name, "samplers": samplers, "channels": channels})

    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "mannequin.bin"), "wb") as f:
        f.write(blob)
    doc = {
        "asset": {"version": "2.0", "generator": "Rakshas tools/make_placeholder_figure.py"},
        "scene": 0, "scenes": [{"nodes": [INDEX["hips"], mesh_node]}],
        "nodes": nodes,
        "skins": [{"name": "mannequin", "joints": list(range(len(JOINTS))), "inverseBindMatrices": ibm,
                   "skeleton": INDEX["hips"]}],
        "meshes": [{"name": "mannequin_body", "primitives": [{"attributes": attrs, "indices": ind, "material": 0}]}],
        "materials": [{"name": "wood", "pbrMetallicRoughness": {
            "baseColorFactor": [0.62, 0.42, 0.26, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.8}}],
        "animations": animations,
        "accessors": accessors, "bufferViews": views,
        "buffers": [{"uri": "mannequin.bin", "byteLength": len(blob)}],
    }
    with open(os.path.join(OUT, "mannequin.gltf"), "w") as f:
        json.dump(doc, f, indent=1)
    print(f"wrote mannequin.gltf ({len(indices) // 3} triangles, {len(JOINTS)} joints, "
          f"animations: {', '.join(a['name'] for a in animations)})")


if __name__ == "__main__":
    build()
