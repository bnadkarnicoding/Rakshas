#version 450
// sky.frag — a simple natural sky: gradient, sun disc and soft glow around it.
#include "common.glsl"

layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 out_color;

void main() {
  vec4 p = scene.inv_view_proj * vec4(v_ndc, 1.0, 1.0);
  vec3 dir = normalize(p.xyz / p.w - scene.camera_pos.xyz);
  float up = dir.y;

  vec3 sky = mix(scene.sky_horizon.rgb, scene.sky_zenith.rgb, pow(clamp(up, 0.0, 1.0), 0.45));
  // Below the horizon: fade towards the dusty ground tone.
  sky = mix(sky, scene.ground_color.rgb * 0.8, smoothstep(0.0, 0.15, -up));

  vec3 L = scene.sun_dir.xyz;
  float sd = max(dot(dir, L), 0.0);
  float above = smoothstep(-0.05, 0.02, L.y);
  vec3 glow = scene.sun_color.rgb * (0.05 * pow(sd, 6.0) + 0.25 * pow(sd, 120.0));
  vec3 disc = scene.sun_color.rgb * 6.0 * smoothstep(0.99955, 0.99975, sd);
  sky += (glow + disc) * above;

  out_color = vec4(tonemap(sky), 1.0);
}
