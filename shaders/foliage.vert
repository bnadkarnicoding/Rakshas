#version 450
// foliage.vert — places one tree or grass clump per instance.
#include "common.glsl"
#include "foliage.glsl"

layout(location = 0) out vec3 v_world_pos;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec3 v_tint;

void main() {
  vec3 n;
  v_world_pos = foliage_world_position(n);
  v_normal = n;
  v_tint = inst_tint.rgb;
  gl_Position = scene.view_proj * vec4(v_world_pos, 1.0);
}
