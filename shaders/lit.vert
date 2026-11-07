#version 450
// lit.vert — transforms solid shapes into the world.
#include "common.glsl"

layout(push_constant) uniform Object {
  mat4 model;
  vec4 base_color;
} object;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;

void main() {
  vec4 world = object.model * vec4(in_position, 1.0);
  v_world_pos = world.xyz;
  // Valid for rotations and axis-aligned scales (all we use in rung 1).
  v_normal = mat3(object.model) * in_normal;
  gl_Position = scene.view_proj * world;
}
