// game/sky_model.h — the day/night cycle: where the sun (or moon) is and what
// colour the light and sky are at a given time of day. Natural light only.
#pragma once
#include "core/math.h"

namespace rk {

struct SkyState {
  Vec3 light_dir;     // unit vector toward the active light (sun by day, moon by night)
  Vec3 light_color;   // colour * intensity of that light
  Vec3 sky_zenith;
  Vec3 sky_horizon;
  Vec3 ground_bounce;
  Vec3 sun_dir;                // toward the sun, even when it is below the horizon
  Vec3 moon_dir;               // toward the moon (opposite side of the sky from the sun)
  float night = 0.0f;          // 0 by day .. 1 in full night (stars)
  float sun_elevation = 0.0f;  // sine of the sun's angle above the horizon (-1..1)
};

// time_of_day: 0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset.
// Values outside 0..1 wrap around.
SkyState evaluate_sky(float time_of_day);

inline float luminance(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

}  // namespace rk
