#version 450
// lit.frag — natural light only: direct sun/moon + soft sky/ground light + haze.
#include "common.glsl"

layout(push_constant) uniform Object {
  mat4 model;
  vec4 base_color;
} object;

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 0) out vec4 out_color;

void main() {
  vec3 N = normalize(v_normal);
  vec3 L = scene.sun_dir.xyz;

  vec3 direct = scene.sun_color.rgb * max(dot(N, L), 0.0);
  // Hemisphere light: sky from above, warm bounce from the ground below.
  vec3 ambient = mix(scene.ground_color.rgb, scene.sky_zenith.rgb, N.y * 0.5 + 0.5);
  vec3 color = object.base_color.rgb * (direct + ambient);

  // Aerial haze: distant things melt into the horizon colour.
  float dist = length(v_world_pos - scene.camera_pos.xyz);
  float haze = 1.0 - exp(-dist * scene.params.x);
  color = mix(color, scene.sky_horizon.rgb, haze);

  out_color = vec4(tonemap(color), 1.0);
}
