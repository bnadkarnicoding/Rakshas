// game/shapes.h — simple placeholder geometry for gray-box scenes.
#pragma once
#include <vector>

#include "core/math.h"
#include "core/types.h"

namespace rk {

struct Vertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
};
static_assert(sizeof(Vertex) == 32, "Vertex layout is shared with the shaders");

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

// Placeholder tree parts, standing on y = 0 (about 6 m tall before scaling).
Mesh make_tree_trunk();
Mesh make_tree_canopy();   // faceted clusters of leaves (a stylised, low-detail look)
constexpr float kTreeHeight = 6.0f;

// A small clump of grass blades standing on y = 0.
Mesh make_grass_clump();
constexpr float kGrassHeight = 0.45f;

}  // namespace rk
