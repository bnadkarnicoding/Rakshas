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

Mesh make_ground(float half_size) {
  Mesh m;
  add_face(m, {0, 0, 0}, {0, 1, 0}, {half_size, 0, 0}, {0, 0, -half_size});
  return m;
}

}  // namespace rk
