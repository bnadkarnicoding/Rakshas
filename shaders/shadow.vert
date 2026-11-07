#version 450
// shadow.vert — renders shadow casters as seen from the sun or moon (depth only).
#include "common.glsl"

layout(push_constant) uniform Object {
  mat4 model;
  vec4 base_color;
} object;

layout(location = 0) in vec3 in_position;

void main() {
  gl_Position = scene.light_view_proj * object.model * vec4(in_position, 1.0);
}
