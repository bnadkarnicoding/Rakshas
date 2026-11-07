// game/sky_model.cpp — hand-tuned day/night colours (linear light).
#include "sky_model.h"

namespace rk {

SkyState evaluate_sky(float time_of_day) {
  float t = time_of_day - std::floor(time_of_day);  // wrap into 0..1
  float theta = (t - 0.25f) * 2.0f * kPi;             // 0 at sunrise, pi/2 at noon

  // The sun rises in the east (+X), arcs over the southern sky (-Z), sets in the west.
  Vec3 sun_dir = normalize(Vec3{std::cos(theta), std::sin(theta) * 0.85f, -0.45f});
  float elev = sun_dir.y;

  SkyState s;
  s.sun_elevation = elev;

  float day = smoothstepf(-0.05f, 0.35f, elev);
  float dusk = std::exp(-(elev / 0.12f) * (elev / 0.12f)) * 0.85f;  // peaks at the horizon

  // Direct light: warm and low at dawn/dusk, near-white at noon, dim blue moon at night.
  float sun_strength = smoothstepf(-0.02f, 0.25f, elev);
  Vec3 sun_col = lerp(Vec3{1.0f, 0.45f, 0.18f}, Vec3{1.0f, 0.93f, 0.82f}, smoothstepf(0.0f, 0.5f, elev));
  float moon_strength = smoothstepf(0.0f, 0.2f, -elev);
  Vec3 moon_col = Vec3{0.06f, 0.08f, 0.13f};
  if (elev >= 0.0f) {
    s.light_dir = sun_dir;
    s.light_color = sun_col * (2.2f * sun_strength);
  } else {
    s.light_dir = normalize(Vec3{-sun_dir.x, -sun_dir.y, sun_dir.z});
    s.light_color = moon_col * moon_strength;
  }

  Vec3 zenith = lerp(Vec3{0.008f, 0.012f, 0.03f}, Vec3{0.18f, 0.36f, 0.75f}, day);
  Vec3 horizon = lerp(Vec3{0.02f, 0.025f, 0.05f}, Vec3{0.62f, 0.72f, 0.85f}, day);
  s.sky_zenith = lerp(zenith, Vec3{0.12f, 0.14f, 0.30f}, dusk * 0.6f);
  s.sky_horizon = lerp(horizon, Vec3{0.95f, 0.50f, 0.25f}, dusk);
  s.ground_bounce = Vec3{0.30f, 0.24f, 0.17f} * (0.05f + 0.25f * day);
  return s;
}

}  // namespace rk
