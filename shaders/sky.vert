#version 450
// sky.vert — one full-screen triangle; no vertex buffer needed.
#include "common.glsl"

layout(location = 0) out vec2 v_ndc;

void main() {
  vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;
  v_ndc = pos;
  gl_Position = vec4(pos, 1.0, 1.0);
}
