// foliage.glsl — instanced trees and grass, swaying gently in the wind.
// Include after common.glsl.

layout(push_constant) uniform Object {
  mat4 model;        // unused for foliage (instances carry their own placement)
  vec4 base_color;
  vec4 params;       // z: wind strength (m), w: mesh height (m)
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 inst_position_yaw;  // xyz: base on the ground, w: yaw (radians)
layout(location = 4) in vec4 inst_scale;         // x: width scale, y: height scale, z: wind phase
layout(location = 5) in vec4 inst_tint;          // rgb: colour variation

vec3 rotate_y(vec3 v, float a) {
  float c = cos(a), s = sin(a);
  return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

vec3 foliage_world_position(out vec3 world_normal) {
  vec3 local = in_position * vec3(inst_scale.x, inst_scale.y, inst_scale.x);
  vec3 p = rotate_y(local, inst_position_yaw.w);
  world_normal = rotate_y(in_normal, inst_position_yaw.w);
  // Wind: the higher up the plant, the more it moves. Slow and natural.
  float bend = in_position.y / max(object.params.w, 0.01);
  bend *= bend;
  float t = scene.params.z;
  float phase = inst_scale.z;
  vec2 gust = vec2(sin(t * 1.3 + phase) + 0.4 * sin(t * 2.9 + phase * 1.7),
                   cos(t * 1.1 + phase * 1.3)) * object.params.z * bend;
  p.xz += gust;
  return inst_position_yaw.xyz + p;
}
