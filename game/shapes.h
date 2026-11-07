// game/shapes.h — simple placeholder geometry for gray-box scenes.
#pragma once
#include <vector>

#include "core/math.h"
#include "core/types.h"

namespace rk {

struct Vertex {
  Vec3 position;
  Vec3 normal;
};

struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<u32> indices;  // counter-clockwise triangles = front faces
};

// Unit cube centred on the origin (-0.5..0.5 on each axis), outward normals.
Mesh make_box();

// Upright capsule standing on y = 0: total height `height`, radius `radius`.
Mesh make_capsule(float radius, float height, int segments = 20, int rings = 6);

// Flat square on the XZ plane at y = 0, facing up, `half_size` from centre to edge.
Mesh make_ground(float half_size);

}  // namespace rk
