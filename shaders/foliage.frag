#version 450
// foliage.frag — leaves, trunks and grass blades under natural light.
#include "common.glsl"
#include "lighting.glsl"

layout(push_constant) uniform Object {
  mat4 model;
  vec4 base_color;
  vec4 params;
} object;

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_tint;
layout(location = 0) out vec4 out_color;

void main() {
  vec3 N = normalize(v_normal);
  if (!gl_FrontFacing) N = -N;  // thin grass blades are seen from both sides
  out_color = vec4(shade(object.base_color.rgb * v_tint, v_world_pos, N), 1.0);
}
