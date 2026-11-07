#version 450
// lit.frag — natural light only: direct sun/moon (with shadows) + soft sky and
// ground light + aerial haze.
#include "common.glsl"

layout(set = 0, binding = 1) uniform sampler2DShadow shadow_map;

layout(push_constant) uniform Object {
  mat4 model;
  vec4 base_color;
} object;

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 0) out vec4 out_color;

// 1 = fully lit, 0 = fully in shadow. Soft edges from 3x3 filtered samples.
float sun_visibility(vec3 world_pos, vec3 N, vec3 L) {
  // Nudge the lookup point off the surface to avoid self-shadowing speckles.
  float grazing = 1.0 - clamp(dot(N, L), 0.0, 1.0);
  vec3 p = world_pos + N * scene.shadow_params.y * (0.5 + grazing);
  vec4 clip = scene.light_view_proj * vec4(p, 1.0);
  vec3 ndc = clip.xyz / clip.w;
  vec2 uv = ndc.xy * 0.5 + 0.5;
  if (ndc.z >= 1.0) return 1.0;

  float texel = scene.shadow_params.x;
  float lit = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      lit += texture(shadow_map, vec3(uv + vec2(x, y) * texel, ndc.z));
  lit /= 9.0;

  // Fade shadows out near the edge of the shadow map instead of cutting off.
  float edge = min(min(uv.x, uv.y), min(1.0 - uv.x, 1.0 - uv.y));
  return mix(1.0, lit, smoothstep(0.0, 0.2, edge));
}

void main() {
  vec3 N = normalize(v_normal);
  vec3 L = scene.light_dir.xyz;

  float ndotl = max(dot(N, L), 0.0);
  float vis = ndotl > 0.0 ? sun_visibility(v_world_pos, N, L) : 1.0;
  vis = mix(1.0, vis, scene.shadow_params.z);
  vec3 direct = scene.light_color.rgb * ndotl * vis;

  // Hemisphere light: sky from above, warm bounce from the ground below.
  vec3 ambient = mix(scene.ground_color.rgb, scene.sky_zenith.rgb, N.y * 0.5 + 0.5);
  vec3 color = object.base_color.rgb * (direct + ambient);

  // Aerial haze: distant things melt into the horizon colour.
  float dist = length(v_world_pos - scene.camera_pos.xyz);
  float haze = 1.0 - exp(-dist * scene.params.x);
  color = mix(color, scene.sky_horizon.rgb, haze);

  out_color = vec4(tonemap(color), 1.0);
}
