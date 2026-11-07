// core/math.h — the engine's own small math library.
//
// Conventions (keep these consistent everywhere):
//   * Right-handed world, +Y is up, the camera looks down -Z in view space.
//   * Mat4 is column-major: element (col, row) lives at m[col * 4 + row],
//     matching GLSL's default layout so matrices upload without transposing.
//   * Projection targets Vulkan clip space: depth 0..1, and Y is flipped so
//     that +Y in the world appears up on screen.
#pragma once
#include <cmath>

namespace rk {

constexpr float kPi = 3.14159265358979323846f;

inline float radians(float degrees) { return degrees * (kPi / 180.0f); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float e0, float e1, float x) {
  float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

struct Vec2 {
  float x = 0, y = 0;
};
inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }

struct Vec3 {
  float x = 0, y = 0, z = 0;
};
struct Vec4 {
  float x = 0, y = 0, z = 0, w = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
  float len = length(a);
  return len > 0.0f ? a * (1.0f / len) : Vec3{};
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

struct Mat4 {
  float m[16] = {};

  float& at(int col, int row) { return m[col * 4 + row]; }
  float at(int col, int row) const { return m[col * 4 + row]; }

  static Mat4 identity() {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
  }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
  Mat4 r;
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float s = 0.0f;
      for (int k = 0; k < 4; ++k) s += a.at(k, row) * b.at(col, k);
      r.at(col, row) = s;
    }
  }
  return r;
}

inline Vec4 operator*(const Mat4& a, Vec4 v) {
  return {a.at(0, 0) * v.x + a.at(1, 0) * v.y + a.at(2, 0) * v.z + a.at(3, 0) * v.w,
          a.at(0, 1) * v.x + a.at(1, 1) * v.y + a.at(2, 1) * v.z + a.at(3, 1) * v.w,
          a.at(0, 2) * v.x + a.at(1, 2) * v.y + a.at(2, 2) * v.z + a.at(3, 2) * v.w,
          a.at(0, 3) * v.x + a.at(1, 3) * v.y + a.at(2, 3) * v.z + a.at(3, 3) * v.w};
}

inline Mat4 translate(Vec3 t) {
  Mat4 r = Mat4::identity();
  r.at(3, 0) = t.x;
  r.at(3, 1) = t.y;
  r.at(3, 2) = t.z;
  return r;
}

inline Mat4 scale(Vec3 s) {
  Mat4 r;
  r.at(0, 0) = s.x;
  r.at(1, 1) = s.y;
  r.at(2, 2) = s.z;
  r.at(3, 3) = 1.0f;
  return r;
}

// Rotation about +Y by `angle` radians (counter-clockwise looking down from +Y).
inline Mat4 rotate_y(float angle) {
  Mat4 r = Mat4::identity();
  float c = std::cos(angle), s = std::sin(angle);
  r.at(0, 0) = c;
  r.at(0, 2) = -s;
  r.at(2, 0) = s;
  r.at(2, 2) = c;
  return r;
}

// Right-handed view matrix: camera at `eye` looking at `target`.
inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) {
  Vec3 f = normalize(target - eye);
  Vec3 s = normalize(cross(f, up));
  Vec3 u = cross(s, f);
  Mat4 r = Mat4::identity();
  r.at(0, 0) = s.x;  r.at(1, 0) = s.y;  r.at(2, 0) = s.z;
  r.at(0, 1) = u.x;  r.at(1, 1) = u.y;  r.at(2, 1) = u.z;
  r.at(0, 2) = -f.x; r.at(1, 2) = -f.y; r.at(2, 2) = -f.z;
  r.at(3, 0) = -dot(s, eye);
  r.at(3, 1) = -dot(u, eye);
  r.at(3, 2) = dot(f, eye);
  return r;
}

// Perspective projection for Vulkan: depth 0 at `near_z`, 1 at `far_z`, Y flipped.
inline Mat4 perspective(float fovy_radians, float aspect, float near_z, float far_z) {
  float f = 1.0f / std::tan(fovy_radians * 0.5f);
  Mat4 r;
  r.at(0, 0) = f / aspect;
  r.at(1, 1) = -f;
  r.at(2, 2) = far_z / (near_z - far_z);
  r.at(2, 3) = -1.0f;
  r.at(3, 2) = (near_z * far_z) / (near_z - far_z);
  return r;
}

// Rotation as a unit quaternion (x, y, z, w), as stored in glTF files.
struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
};

inline Quat operator*(Quat a, Quat b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Quat normalize(Quat q) {
  float len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return len > 0 ? Quat{q.x / len, q.y / len, q.z / len, q.w / len} : Quat{};
}

inline Quat axis_angle(Vec3 axis, float angle) {
  Vec3 a = normalize(axis);
  float s = std::sin(angle * 0.5f);
  return {a.x * s, a.y * s, a.z * s, std::cos(angle * 0.5f)};
}

// Blends two rotations along the shorter way round (normalised lerp: fast and
// smooth enough for animation frames that are close together).
inline Quat nlerp(Quat a, Quat b, float t) {
  float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  if (d < 0) b = {-b.x, -b.y, -b.z, -b.w};
  return normalize({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                    a.w + (b.w - a.w) * t});
}

inline Mat4 rotation_matrix(Quat q) {
  Mat4 r = Mat4::identity();
  float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
  r.at(0, 0) = 1 - 2 * (yy + zz);
  r.at(0, 1) = 2 * (xy + wz);
  r.at(0, 2) = 2 * (xz - wy);
  r.at(1, 0) = 2 * (xy - wz);
  r.at(1, 1) = 1 - 2 * (xx + zz);
  r.at(1, 2) = 2 * (yz + wx);
  r.at(2, 0) = 2 * (xz + wy);
  r.at(2, 1) = 2 * (yz - wx);
  r.at(2, 2) = 1 - 2 * (xx + yy);
  return r;
}

// Translation * rotation * scale (the order glTF uses).
inline Mat4 compose_trs(Vec3 t, Quat r, Vec3 s);

// Orthographic projection for Vulkan (depth 0 at near, 1 at far, Y flipped),
// for a right-handed view space looking down -Z. Used for sun/moon shadows.
inline Mat4 orthographic(float left, float right, float bottom, float top, float near_z,
                         float far_z) {
  Mat4 r;
  r.at(0, 0) = 2.0f / (right - left);
  r.at(1, 1) = -2.0f / (top - bottom);
  r.at(2, 2) = -1.0f / (far_z - near_z);
  r.at(3, 0) = -(right + left) / (right - left);
  r.at(3, 1) = (top + bottom) / (top - bottom);
  r.at(3, 2) = -near_z / (far_z - near_z);
  r.at(3, 3) = 1.0f;
  return r;
}

inline Mat4 compose_trs(Vec3 t, Quat r, Vec3 s) { return translate(t) * rotation_matrix(r) * scale(s); }

// General 4x4 inverse (cofactor expansion). Returns identity if singular.
inline Mat4 inverse(const Mat4& a) {
  const float* m = a.m;
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
           m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
           m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
           m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
           m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
           m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
           m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
           m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
           m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
           m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
           m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (std::fabs(det) < 1e-12f) return Mat4::identity();
  float inv_det = 1.0f / det;
  Mat4 r;
  for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * inv_det;
  return r;
}

}  // namespace rk
