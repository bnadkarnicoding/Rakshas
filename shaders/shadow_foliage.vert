#version 450
// shadow_foliage.vert — trees and grass casting shadows (sway included).
#include "common.glsl"
#include "foliage.glsl"

void main() {
  vec3 n;
  gl_Position = scene.light_view_proj * vec4(foliage_world_position(n), 1.0);
}
