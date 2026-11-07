#version 450
// lit.frag — textured solid surfaces under natural light.
#include "common.glsl"
#include "lighting.glsl"

layout(set = 1, binding = 0) uniform sampler2D albedo_map;

layout(push_constant) uniform Object {
  mat4 model;
  vec4 base_color;
  vec4 params;  // x: 1 = map texture from world position, y: texture metres per repeat
} object;

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

// Projects the texture from the world's three axes and blends by surface angle,
// so terrain and blocks of any size never look stretched.
vec3 triplanar(vec3 p, vec3 n, float metres_per_repeat) {
  vec3 w = pow(abs(n), vec3(4.0));
  w /= (w.x + w.y + w.z);
  vec3 q = p / metres_per_repeat;
  return texture(albedo_map, q.zy).rgb * w.x + texture(albedo_map, q.xz).rgb * w.y +
         texture(albedo_map, q.xy).rgb * w.z;
}

void main() {
  vec3 N = normalize(v_normal);
  vec3 tex = object.params.x > 0.5 ? triplanar(v_world_pos, N, object.params.y)
                                   : texture(albedo_map, v_uv).rgb;
  out_color = vec4(shade(tex * object.base_color.rgb, v_world_pos, N), 1.0);
}
