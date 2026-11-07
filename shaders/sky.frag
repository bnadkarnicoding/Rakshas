#version 450
// sky.frag — a natural sky: gradient, sun disc and soft glow, the moon, and stars.
#include "common.glsl"

layout(location = 0) in vec2 v_ndc;
layout(location = 0) out vec4 out_color;

float hash13(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.zyx + 31.32);
  return fract((p.x + p.y) * p.z);
}

// Sparse point stars on a fixed celestial sphere.
float stars(vec3 dir) {
  vec3 p = dir * 260.0;
  vec3 cell = floor(p);
  float h = hash13(cell);
  if (h < 0.992) return 0.0;
  vec3 star = cell + vec3(hash13(cell + 11.1), hash13(cell + 23.7), hash13(cell + 37.3));
  float d = length(p - star);
  float brightness = (h - 0.992) / 0.008;
  return brightness * brightness * smoothstep(0.45, 0.05, d);
}

void main() {
  vec4 p = scene.inv_view_proj * vec4(v_ndc, 1.0, 1.0);
  vec3 dir = normalize(p.xyz / p.w - scene.camera_pos.xyz);
  float up = dir.y;

  vec3 sky = mix(scene.sky_horizon.rgb, scene.sky_zenith.rgb, pow(clamp(up, 0.0, 1.0), 0.45));

  // Stars: only at night, high in the sky, dimmed near the hazy horizon.
  float night = scene.moon_dir.w;
  sky += vec3(0.9, 0.92, 1.0) * stars(dir) * night * smoothstep(0.02, 0.25, up) * 1.4;

  // Sun: disc and soft glow, only when above the horizon.
  vec3 S = scene.sun_dir.xyz;
  float sd = max(dot(dir, S), 0.0);
  float sun_up = smoothstep(-0.05, 0.02, S.y);
  vec3 sun_light = scene.light_color.rgb * sun_up;
  sky += sun_light * (0.05 * pow(sd, 6.0) + 0.25 * pow(sd, 120.0));
  sky += sun_light * 6.0 * smoothstep(0.99955, 0.99975, sd);

  // Moon: a pale disc, faint by day and clear at night.
  vec3 M = scene.moon_dir.xyz;
  float md = max(dot(dir, M), 0.0);
  float moon_up = smoothstep(-0.03, 0.03, M.y);
  float moon_vis = moon_up * mix(0.15, 1.0, night);
  sky += vec3(0.85, 0.87, 0.92) * 0.9 * moon_vis * smoothstep(0.99935, 0.99955, md);
  sky += vec3(0.25, 0.28, 0.35) * 0.04 * moon_vis * pow(md, 200.0);

  // Below the horizon: the far land is lost in haze, so stay close to the
  // horizon colour with just a hint of the ground's warmth.
  vec3 below = mix(scene.sky_horizon.rgb, scene.ground_color.rgb, 0.25);
  sky = mix(sky, below, smoothstep(0.0, 0.1, -up));

  out_color = vec4(tonemap(sky), 1.0);
}
