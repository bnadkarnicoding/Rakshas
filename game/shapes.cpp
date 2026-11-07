// game/shapes.cpp — box and ground meshes.
#include "shapes.h"

namespace rk {
namespace {

// Adds one quad facing `n`. `u` and `v` span the face with cross(u, v) == n,
// which makes the corner order counter-clockwise when seen from outside.
void add_face(Mesh& mesh, Vec3 center, Vec3 n, Vec3 u, Vec3 v) {
  u32 base = static_cast<u32>(mesh.vertices.size());
  mesh.vertices.push_back({center - u - v, n, {0, 1}});
  mesh.vertices.push_back({center + u - v, n, {1, 1}});
  mesh.vertices.push_back({center + u + v, n, {1, 0}});
  mesh.vertices.push_back({center - u + v, n, {0, 0}});
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
      float y = center_y + n.y * radius;
      m.vertices.push_back({{n.x * radius, y, n.z * radius}, n,
                            {static_cast<float>(j) / static_cast<float>(segments), 1.0f - y / height}});
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

namespace {

// Adds a triangle with its own flat normal (for the faceted canopy look).
void add_flat_triangle(Mesh& m, Vec3 a, Vec3 b, Vec3 c) {
  Vec3 n = normalize(cross(b - a, c - a));
  u32 base = static_cast<u32>(m.vertices.size());
  m.vertices.push_back({a, n, {0, 0}});
  m.vertices.push_back({b, n, {1, 0}});
  m.vertices.push_back({c, n, {0, 1}});
  for (u32 i : {0u, 1u, 2u}) m.indices.push_back(base + i);
}

// Low-poly blob: an octahedron subdivided once, squashed and pushed outward.
void add_leaf_cluster(Mesh& m, Vec3 center, Vec3 radii) {
  const Vec3 v[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  const int faces[8][3] = {{0, 2, 4}, {4, 2, 1}, {1, 2, 5}, {5, 2, 0},
                           {4, 3, 0}, {1, 3, 4}, {5, 3, 1}, {0, 3, 5}};
  auto place = [&](Vec3 p) {
    Vec3 q = normalize(p);
    return center + Vec3{q.x * radii.x, q.y * radii.y, q.z * radii.z};
  };
  for (const auto& f : faces) {
    Vec3 a = v[f[0]], b = v[f[1]], c = v[f[2]];
    Vec3 ab = (a + b) * 0.5f, bc = (b + c) * 0.5f, ca = (c + a) * 0.5f;
    add_flat_triangle(m, place(a), place(ab), place(ca));
    add_flat_triangle(m, place(ab), place(b), place(bc));
    add_flat_triangle(m, place(ca), place(bc), place(c));
    add_flat_triangle(m, place(ab), place(bc), place(ca));
  }
}

}  // namespace

Mesh make_tree_trunk() {
  // A slightly tapering, slightly leaning trunk with two short branches.
  Mesh m;
  const int seg = 8;
  auto ring = [&](float y, float r, float lean) {
    u32 base = static_cast<u32>(m.vertices.size());
    for (int j = 0; j <= seg; ++j) {
      float t = 2.0f * kPi * static_cast<float>(j) / static_cast<float>(seg);
      Vec3 n{std::cos(t), 0.0f, std::sin(t)};
      m.vertices.push_back({{n.x * r + lean, y, n.z * r}, n, {static_cast<float>(j) / seg, y}});
    }
    return base;
  };
  u32 r0 = ring(0.0f, 0.28f, 0.0f);
  u32 r1 = ring(2.2f, 0.2f, 0.15f);
  u32 r2 = ring(3.6f, 0.13f, 0.3f);
  for (u32 lo : {r0, r1}) {
    u32 hi = lo + static_cast<u32>(seg + 1);
    for (int j = 0; j < seg; ++j) {
      u32 a = lo + static_cast<u32>(j), b = a + 1, c = hi + static_cast<u32>(j) + 1, d = hi + static_cast<u32>(j);
      for (u32 idx : {a, d, c, a, c, b}) m.indices.push_back(idx);
    }
  }
  (void)r2;
  return m;
}

Mesh make_tree_canopy() {
  Mesh m;
  add_leaf_cluster(m, {0.3f, 4.4f, 0.0f}, {2.0f, 1.3f, 2.0f});
  add_leaf_cluster(m, {-1.1f, 3.9f, 0.6f}, {1.4f, 1.0f, 1.3f});
  add_leaf_cluster(m, {1.3f, 3.8f, -0.8f}, {1.3f, 0.9f, 1.4f});
  add_leaf_cluster(m, {0.0f, 5.3f, -0.3f}, {1.3f, 0.8f, 1.2f});
  return m;
}

Mesh make_grass_clump() {
  // Five thin blades fanning out from the centre. Each blade is two triangles
  // tapering to a point; normals lean upward so the grass catches sky light.
  Mesh m;
  const float angles[5] = {0.0f, 1.3f, 2.5f, 3.8f, 5.0f};
  const float lean[5] = {0.10f, 0.16f, 0.08f, 0.14f, 0.12f};
  const float height[5] = {0.45f, 0.38f, 0.42f, 0.33f, 0.40f};
  for (int i = 0; i < 5; ++i) {
    float a = angles[i];
    Vec3 dir{std::cos(a), 0.0f, std::sin(a)};
    Vec3 side{-dir.z, 0.0f, dir.x};
    Vec3 base = dir * 0.05f;
    Vec3 tip = base + dir * lean[i] + Vec3{0, height[i], 0};
    Vec3 n = normalize(Vec3{dir.x * 0.3f, 1.0f, dir.z * 0.3f});
    u32 b = static_cast<u32>(m.vertices.size());
    m.vertices.push_back({base - side * 0.025f, n, {0, 1}});
    m.vertices.push_back({base + side * 0.025f, n, {1, 1}});
    m.vertices.push_back({base + dir * (lean[i] * 0.5f) + Vec3{0, height[i] * 0.5f, 0} + side * 0.018f, n, {1, 0.5f}});
    m.vertices.push_back({base + dir * (lean[i] * 0.5f) + Vec3{0, height[i] * 0.5f, 0} - side * 0.018f, n, {0, 0.5f}});
    m.vertices.push_back({tip, n, {0.5f, 0}});
    for (u32 idx : {0u, 1u, 2u, 0u, 2u, 3u, 3u, 2u, 4u}) m.indices.push_back(b + idx);
  }
  return m;
}

}  // namespace rk
