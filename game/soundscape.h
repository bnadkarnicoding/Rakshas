// game/soundscape.h — what the player hears, decided from where they are and
// what is happening: footsteps in time with the stride, wind that rises in the
// open and on high ground, leaves among trees, birds by day, insects by night,
// and stillness when a duel is about to begin (CorePrinciples Tier 3 #2, #5).
#pragma once
#include <string>
#include <vector>

#include "audio/audio.h"
#include "core/math.h"

namespace rk {

struct SoundscapeInputs {
  float sun_elevation = 0.5f;  // -1..1
  float night = 0.0f;          // 0 day .. 1 night
  float tree_density = 0.0f;   // 0 open ground .. 1 among trees
  float height = 0.0f;         // metres above the land's base level
  float duel = 0.0f;           // 0..1, how strongly a duel framing is active
  bool on_bare_ground = false; // village dirt rather than grass
  // From the player's animation:
  float stride_phase = 0.0f;   // 0..1 through a two-step cycle
  float move_blend = 0.0f;     // 0 standing .. 1 moving
  float run_blend = 0.0f;      // 0 walking .. 1 running
};

// Volume of each part of the soundscape (1 = as designed). Loaded from
// content/sounds/mix.rk so the balance can be tuned without code.
struct SoundMix {
  float footsteps = 1.0f;
  float wind = 1.0f;
  float leaves = 1.0f;
  float birds = 1.0f;
  float insects = 1.0f;
};

struct AmbienceLevels {
  float wind = 0.0f;
  float leaves = 0.0f;
  float insects = 0.0f;
  float birds_per_minute = 0.0f;
  float stillness = 1.0f;      // multiplies all ambience; drops toward a duel
};

AmbienceLevels ambience_levels(const SoundscapeInputs& in);

// Number of footfalls (left at phase 0.25, right at 0.75) passed when the
// stride phase moves from `previous` to `current` (wrapping at 1).
int footfalls_between(float previous, float current);

class Soundscape {
 public:
  // Loads the sounds from `sound_dir` (e.g. content/sounds/) into the mixer.
  void load(audio::Mixer& mixer, const std::string& sound_dir);
  void set_mix(const SoundMix& mix) { mix_ = mix; }
  const SoundMix& mix() const { return mix_; }
  void update(const SoundscapeInputs& in, float dt);
  int footsteps_played() const { return footsteps_played_; }
  int birds_played() const { return birds_played_; }

 private:
  audio::Mixer* mixer_ = nullptr;
  SoundMix mix_;
  std::vector<audio::SoundHandle> steps_dirt_, steps_grass_, birds_;
  audio::SoundHandle wind_, leaves_, insects_;
  audio::VoiceHandle wind_voice_, leaves_voice_, insects_voice_;
  float last_phase_ = 0.0f;
  float bird_timer_ = 1.5f;
  u32 random_ = 0x2468ACE1u;
  int last_step_ = -1;
  int footsteps_played_ = 0;
  int birds_played_ = 0;

  float random01();
};

}  // namespace rk
