// tests/test_soundscape.cpp — the rules deciding what the player hears.
#include "soundscape.h"
#include "test.h"

using namespace rk;

TEST(footfalls_land_twice_per_stride) {
  CHECK(footfalls_between(0.20f, 0.30f) == 1);
  CHECK(footfalls_between(0.30f, 0.70f) == 0);
  CHECK(footfalls_between(0.70f, 0.80f) == 1);
  CHECK(footfalls_between(0.95f, 0.30f) == 1);  // wraps past 1
  // Walking a full cycle in small steps gives exactly two footfalls.
  int total = 0;
  float phase = 0.0f;
  for (int i = 0; i < 60; ++i) {
    float next = phase + 1.0f / 60.0f;
    next -= std::floor(next);
    total += footfalls_between(phase, next);
    phase = next;
  }
  CHECK(total == 2);
}

TEST(forest_rustles_and_open_ground_is_windy) {
  SoundscapeInputs open, forest;
  forest.tree_density = 1.0f;
  AmbienceLevels o = ambience_levels(open), f = ambience_levels(forest);
  CHECK(o.wind > f.wind);
  CHECK(f.leaves > o.leaves);
  CHECK(f.birds_per_minute > o.birds_per_minute);
}

TEST(high_ground_is_windier) {
  SoundscapeInputs low, high;
  high.height = 12.0f;
  CHECK(ambience_levels(high).wind > ambience_levels(low).wind * 1.5f);
}

TEST(night_brings_insects_and_silences_birds) {
  SoundscapeInputs day, night;
  night.sun_elevation = -0.6f;
  night.night = 1.0f;
  CHECK(ambience_levels(night).insects > 0.2f);
  CHECK(ambience_levels(day).insects < 0.01f);
  CHECK(ambience_levels(night).birds_per_minute < 0.01f);
}

TEST(the_world_hushes_before_a_duel) {
  SoundscapeInputs calm, duel;
  duel.duel = 1.0f;
  CHECK(ambience_levels(duel).stillness < 0.25f);
  CHECK_NEAR(ambience_levels(calm).stillness, 1.0f, 1e-6f);
}

TEST(soundscape_plays_footsteps_in_time) {
  audio::Mixer mixer;
  Soundscape sound;
  sound.load(mixer, std::string(RK_SOURCE_DIR) + "/content/sounds/");
  SoundscapeInputs in;
  in.move_blend = 1.0f;
  // Two seconds of walking at one cycle per second -> four footsteps.
  for (int i = 1; i <= 120; ++i) {
    in.stride_phase = static_cast<float>(i) / 60.0f;
    in.stride_phase -= std::floor(in.stride_phase);
    sound.update(in, 1.0f / 60.0f);
  }
  CHECK(sound.footsteps_played() == 4);
  // Standing still: no more footsteps.
  in.move_blend = 0.0f;
  for (int i = 0; i < 120; ++i) sound.update(in, 1.0f / 60.0f);
  CHECK(sound.footsteps_played() == 4);
}
