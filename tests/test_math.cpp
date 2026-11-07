// tests/test_math.cpp — core math and mesh winding.
#include "core/math.h"
#include "shapes.h"
#include "test.h"

using namespace rk;

static bool mat_near(const Mat4& a, const Mat4& b, float eps) {
  for (int i = 0; i < 16; ++i)
    if (std::fabs(a.m[i] - b.m[i]) > eps) return false;
  return true;
}

TEST(vector_basics) {
  Vec3 x{1, 0, 0}, y{0, 1, 0};
  Vec3 z = cross(x, y);
  CHECK_NEAR(z.z, 1.0f, 1e-6f);
  CHECK_NEAR(dot(x, y), 0.0f, 1e-6f);
  CHECK_NEAR(length(normalize(Vec3{3, 4, 12})), 1.0f, 1e-6f);
}

TEST(matrix_multiply_identity) {
  Mat4 t = translate({1, 2, 3}) * rotate_y(0.7f) * scale({2, 3, 4});
  CHECK(mat_near(t * Mat4::identity(), t, 1e-6f));
  CHECK(mat_near(Mat4::identity() * t, t, 1e-6f));
}

TEST(translate_moves_points) {
  Vec4 p = translate({1, 2, 3}) * Vec4{0, 0, 0, 1};
  CHECK_NEAR(p.x, 1.0f, 1e-6f);
  CHECK_NEAR(p.y, 2.0f, 1e-6f);
  CHECK_NEAR(p.z, 3.0f, 1e-6f);
}

TEST(rotate_y_quarter_turn) {
  // Counter-clockwise seen from above: +X turns toward -Z.
  Vec4 p = rotate_y(kPi * 0.5f) * Vec4{1, 0, 0, 0};
  CHECK_NEAR(p.x, 0.0f, 1e-6f);
  CHECK_NEAR(p.z, -1.0f, 1e-6f);
}

TEST(inverse_roundtrip) {
  Mat4 m = translate({4, -2, 7}) * rotate_y(1.1f) * scale({0.5f, 2.0f, 3.0f});
  CHECK(mat_near(inverse(m) * m, Mat4::identity(), 1e-5f));
  Mat4 vp = perspective(radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f) *
            look_at({3, 4, 5}, {0, 1, 0}, {0, 1, 0});
  CHECK(mat_near(inverse(vp) * vp, Mat4::identity(), 1e-4f));
}

TEST(look_at_puts_eye_at_origin) {
  Vec3 eye{3, 4, 5};
  Mat4 v = look_at(eye, {0, 1, 0}, {0, 1, 0});
  Vec4 p = v * Vec4{eye.x, eye.y, eye.z, 1};
  CHECK_NEAR(p.x, 0.0f, 1e-5f);
  CHECK_NEAR(p.y, 0.0f, 1e-5f);
  CHECK_NEAR(p.z, 0.0f, 1e-5f);
  // The target is straight ahead, i.e. on -Z in view space.
  Vec4 t = v * Vec4{0, 1, 0, 1};
  CHECK(t.z < 0.0f);
  CHECK_NEAR(t.x, 0.0f, 1e-5f);
}

TEST(perspective_vulkan_conventions) {
  Mat4 p = perspective(radians(60.0f), 1.0f, 0.5f, 50.0f);
  Vec4 near_pt = p * Vec4{0, 0, -0.5f, 1};
  Vec4 far_pt = p * Vec4{0, 0, -50.0f, 1};
  CHECK_NEAR(near_pt.z / near_pt.w, 0.0f, 1e-5f);  // depth 0 at near plane
  CHECK_NEAR(far_pt.z / far_pt.w, 1.0f, 1e-5f);    // depth 1 at far plane
  Vec4 up_pt = p * Vec4{0, 1, -5, 1};
  CHECK(up_pt.y / up_pt.w < 0.0f);                 // Vulkan: up on screen is -Y
}

TEST(box_triangles_face_outward) {
  Mesh box = make_box();
  CHECK(box.vertices.size() == 24);
  CHECK(box.indices.size() == 36);
  for (size_t i = 0; i < box.indices.size(); i += 3) {
    const Vertex& a = box.vertices[box.indices[i]];
    const Vertex& b = box.vertices[box.indices[i + 1]];
    const Vertex& c = box.vertices[box.indices[i + 2]];
    Vec3 face = cross(b.position - a.position, c.position - a.position);
    CHECK(dot(face, a.normal) > 0.0f);           // counter-clockwise from outside
    CHECK(dot(a.position, a.normal) > 0.0f);     // normal points away from centre
  }
}

TEST(ground_faces_up) {
  Mesh g = make_ground(10.0f);
  const Vertex& a = g.vertices[g.indices[0]];
  const Vertex& b = g.vertices[g.indices[1]];
  const Vertex& c = g.vertices[g.indices[2]];
  CHECK(cross(b.position - a.position, c.position - a.position).y > 0.0f);
}

TEST(orthographic_vulkan_conventions) {
  Mat4 o = orthographic(-10, 10, -5, 5, 1.0f, 101.0f);
  Vec4 n = o * Vec4{0, 0, -1.0f, 1};
  Vec4 f = o * Vec4{0, 0, -101.0f, 1};
  CHECK_NEAR(n.z, 0.0f, 1e-6f);
  CHECK_NEAR(f.z, 1.0f, 1e-6f);
  Vec4 corner = o * Vec4{10, 5, -50, 1};
  CHECK_NEAR(corner.x, 1.0f, 1e-6f);
  CHECK_NEAR(corner.y, -1.0f, 1e-6f);  // Y flipped like perspective()
}
