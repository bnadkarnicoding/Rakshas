// game/scene_constants.h — CPU mirror of the SceneConstants block in
// shaders/common.glsl. Layout must match std140 exactly.
#pragma once
#include "core/math.h"

namespace rk {

struct SceneConstants {
  Mat4 view_proj;
  Mat4 inv_view_proj;
  Mat4 light_view_proj;
  Vec4 camera_pos;
  Vec4 light_dir;
  Vec4 light_color;
  Vec4 sun_dir;
  Vec4 moon_dir;       // w: night amount
  Vec4 sky_zenith;
  Vec4 sky_horizon;
  Vec4 ground_color;
  Vec4 params;         // x: haze density, y: exposure
  Vec4 shadow_params;  // x: 1 / shadow map size, y: normal offset, z: strength
};
static_assert(sizeof(SceneConstants) == 352, "SceneConstants must match common.glsl");

// Per-object push constants for lit.vert / lit.frag.
struct ObjectConstants {
  Mat4 model;
  Vec4 base_color;
};
static_assert(sizeof(ObjectConstants) == 80, "ObjectConstants must match lit shaders");

inline Vec4 to_vec4(Vec3 v, float w = 0.0f) { return {v.x, v.y, v.z, w}; }

}  // namespace rk
