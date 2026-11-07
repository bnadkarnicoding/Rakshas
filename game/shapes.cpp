// game/shapes.cpp — box and ground meshes.
#include "shapes.h"

namespace rk {
namespace {

// Adds one quad facing `n`. `u` and `v` span the face with cross(u, v) == n,
// which makes the corner order counter-clockwise when seen from outside.
void add_face(Mesh& mesh, Vec3 center, Vec3 n, Vec3 u, Vec3 v) {
  u32 base = static_cast<u32>(mesh.vertices.size());
  mesh.vertices.push_back({center - u - v, n});
  mesh.vertices.push_back({center + u - v, n});
  mesh.vertices.push_back({center + u + v, n});
  mesh.vertices.push_back({center - u + v, n});
  for (u32 i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + i);
}

}  // namespace

Mesh make_box() {
  Mesh m;
  const float h = 0.5f;
  add_face(m, {h, 0, 0}, {1, 0, 0}, {0, 0, -h}, {0, h, 0});
  add_face(m, {-h, 0, 0}, {-1, 0, 0}, {0, 0, h}, {0, h, 0});
  add_face(m, {0, h, 0}, {0, 1, 0}, {h, 0, 0}, {0, 0, -h});
  add_face(m, {0, -h, 0}, {0, -1, 0}, {h, 0, 0}, {0, 0, h});
  add_face(m, {0, 0, h}, {0, 0, 1}, {h, 0, 0}, {0, h, 0});
  add_face(m, {0, 0, -h}, {0, 0, -1}, {-h, 0, 0}, {0, h, 0});
  return m;
}

Mesh make_capsule(float radius, float height, int segments, int rings) {
  Mesh m;
  const float top_center = height - radius;
  const float bottom_center = radius;
  const int rows = 2 * rings + 2;  // top cap rows + bottom cap rows (equator doubled)
  for (int i = 0; i < rows; ++i) {
    float phi;
    float center_y;
    if (i <= rings) {
      phi = 0.5f * kPi * static_cast<float>(i) / static_cast<float>(rings);
      center_y = top_center;
    } else {
      phi = 0.5f * kPi + 0.5f * kPi * static_cast<float>(i - rings - 1) / static_cast<float>(rings);
      center_y = bottom_center;
    }
    for (int j = 0; j <= segments; ++j) {
      float t = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(segments);
      Vec3 n{std::sin(phi) * std::cos(t), std::cos(phi), std::sin(phi) * std::sin(t)};
      m.vertices.push_back({{n.x * radius, center_y + n.y * radius, n.z * radius}, n});
    }
  }
  const u32 stride = static_cast<u32>(segments + 1);
  for (int i = 0; i + 1 < rows; ++i) {
    for (int j = 0; j < segments; ++j) {
      u32 a = static_cast<u32>(i) * stride + static_cast<u32>(j);
      u32 b = a + 1;
      u32 d = a + stride;
      u32 c = d + 1;
      for (u32 idx : {a, b, c, a, c, d}) m.indices.push_back(idx);
    }
  }
  return m;
}

Mesh make_ground(float half_size) {
  Mesh m;
  add_face(m, {0, 0, 0}, {0, 1, 0}, {half_size, 0, 0}, {0, 0, -half_size});
  return m;
}

}  // namespace rk
