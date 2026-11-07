#version 450
// lit.vert — transforms solid shapes and models into the world.
#include "common.glsl"

layout(push_constant) uniform Object {
  mat4 model;
  vec4 base_color;
  vec4 params;  // x: 1 = map texture from world position, y: texture metres per repeat
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec2 v_uv;

void main() {
  vec4 world = object.model * vec4(in_position, 1.0);
  v_world_pos = world.xyz;
  // Valid for rotations and axis-aligned scales (all we use for now).
  v_normal = mat3(object.model) * in_normal;
  v_uv = in_uv;
  gl_Position = scene.view_proj * world;
}
