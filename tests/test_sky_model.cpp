// tests/test_sky_model.cpp — the day/night cycle behaves naturally.
#include "sky_model.h"
#include "test.h"

using namespace rk;

TEST(sun_is_high_at_noon_and_down_at_midnight) {
  CHECK(evaluate_sky(0.5f).sun_elevation > 0.7f);
  CHECK(evaluate_sky(0.0f).sun_elevation < -0.7f);
}

TEST(light_always_comes_from_above) {
  for (int i = 0; i < 48; ++i) {
    SkyState s = evaluate_sky(static_cast<float>(i) / 48.0f);
    CHECK(s.light_dir.y >= 0.0f);
    CHECK_NEAR(length(s.light_dir), 1.0f, 1e-4f);
  }
}

TEST(night_is_dim_but_not_black) {
  SkyState night = evaluate_sky(0.0f);
  SkyState noon = evaluate_sky(0.5f);
  CHECK(luminance(night.light_color) > 0.0f);
  CHECK(luminance(night.light_color) < 0.1f * luminance(noon.light_color));
}

TEST(sunrise_horizon_is_warmer_than_noon) {
  SkyState dawn = evaluate_sky(0.26f);
  SkyState noon = evaluate_sky(0.5f);
  float dawn_warmth = dawn.sky_horizon.x / dawn.sky_horizon.z;
  float noon_warmth = noon.sky_horizon.x / noon.sky_horizon.z;
  CHECK(dawn_warmth > noon_warmth * 1.5f);
}

TEST(time_wraps_around) {
  SkyState a = evaluate_sky(0.3f);
  SkyState b = evaluate_sky(1.3f);
  SkyState c = evaluate_sky(-0.7f);
  CHECK_NEAR(a.sun_elevation, b.sun_elevation, 1e-4f);
  CHECK_NEAR(a.sun_elevation, c.sun_elevation, 1e-4f);
}

TEST(stars_only_at_night_and_moon_opposite_sun) {
  CHECK(evaluate_sky(0.0f).night > 0.99f);
  CHECK(evaluate_sky(0.5f).night < 0.01f);
  SkyState s = evaluate_sky(0.4f);
  CHECK(dot(s.sun_dir, s.moon_dir) < 0.0f);
}
