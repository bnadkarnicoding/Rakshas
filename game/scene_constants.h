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
  Vec4 params;         // x: haze density, y: exposure, z: time (seconds)
  Vec4 shadow_params;  // x: 1 / shadow map size, y: normal offset, z: strength
};
static_assert(sizeof(SceneConstants) == 352, "SceneConstants must match common.glsl");

// Per-object push constants for lit / foliage shaders.
struct ObjectConstants {
  Mat4 model;
  Vec4 base_color;
  Vec4 params;  // lit: x = world-mapped texture, y = metres per repeat; foliage: z = wind, w = height
};
static_assert(sizeof(ObjectConstants) == 96, "ObjectConstants must match the shaders");

// Per-instance data for trees and grass (shaders/foliage.glsl).
struct FoliageInstance {
  Vec4 position_yaw;  // xyz base on the ground, w yaw (radians)
  Vec4 scale;         // x width scale, y height scale, z wind phase
  Vec4 tint;          // rgb colour variation
};
static_assert(sizeof(FoliageInstance) == 48, "FoliageInstance must match foliage.glsl");

inline Vec4 to_vec4(Vec3 v, float w = 0.0f) { return {v.x, v.y, v.z, w}; }

}  // namespace rk
