// game/soundscape.cpp — the rules behind the world's sound.
#include "soundscape.h"

namespace rk {

AmbienceLevels ambience_levels(const SoundscapeInputs& in) {
  AmbienceLevels a;
  float trees = clampf(in.tree_density, 0.0f, 1.0f);
  float open = 1.0f - trees;
  float daylight = smoothstepf(-0.05f, 0.15f, in.sun_elevation);
  float high = smoothstepf(4.0f, 12.0f, in.height);

  a.wind = (0.08f + 0.10f * open + 0.18f * high) * lerpf(1.0f, 0.8f, in.night);
  a.leaves = 0.32f * trees * (0.7f + a.wind);
  a.insects = 0.32f * in.night;
  a.birds_per_minute = daylight * (6.0f + 14.0f * trees);
  // Stillness before the strike: the world hushes as a duel framing takes hold.
  a.stillness = 1.0f - 0.8f * clampf(in.duel, 0.0f, 1.0f);
  return a;
}

int footfalls_between(float previous, float current) {
  float travelled = current - previous;
  if (travelled < 0.0f) travelled += 1.0f;  // wrapped past 1
  if (travelled <= 0.0f || travelled >= 0.5f) return travelled >= 0.5f ? 1 : 0;
  int count = 0;
  for (float strike : {0.25f, 0.75f}) {
    float d = strike - previous;
    if (d <= 0.0f) d += 1.0f;
    if (d <= travelled) ++count;
  }
  return count;
}

float Soundscape::random01() {
  random_ ^= random_ << 13;
  random_ ^= random_ >> 17;
  random_ ^= random_ << 5;
  return static_cast<float>(random_ & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
}

void Soundscape::load(audio::Mixer& mixer, const std::string& dir) {
  mixer_ = &mixer;
  for (int i = 1; i <= 4; ++i) {
    std::string n = std::to_string(i);
    if (auto s = mixer.load(dir + "footstep_dirt_" + n + ".wav"); s.valid()) steps_dirt_.push_back(s);
    if (auto s = mixer.load(dir + "footstep_grass_" + n + ".wav"); s.valid()) steps_grass_.push_back(s);
    if (auto s = mixer.load(dir + "bird_" + n + ".wav"); s.valid()) birds_.push_back(s);
  }
  wind_ = mixer.load(dir + "wind_loop.wav");
  leaves_ = mixer.load(dir + "leaves_loop.wav");
  insects_ = mixer.load(dir + "insects_loop.wav");
  // Ambient loops run all the time; their volumes follow the world.
  wind_voice_ = mixer.play(wind_, {0.0f, 0.0f, 1.0f, true});
  leaves_voice_ = mixer.play(leaves_, {0.0f, 0.0f, 1.0f, true});
  insects_voice_ = mixer.play(insects_, {0.0f, 0.0f, 1.0f, true});
}

void Soundscape::update(const SoundscapeInputs& in, float dt) {
  if (!mixer_) return;
  AmbienceLevels a = ambience_levels(in);
  const float glide = 0.6f;  // seconds: ambience drifts, never jumps
  mixer_->set(wind_voice_, a.wind * a.stillness * mix_.wind, 0.0f, 1.0f, glide);
  mixer_->set(leaves_voice_, a.leaves * a.stillness * mix_.leaves, 0.0f, 1.0f, glide);
  mixer_->set(insects_voice_, a.insects * a.stillness * mix_.insects, 0.0f, 1.0f, glide);

  // Birds: occasional calls from either side, fewer in the open, none at night.
  bird_timer_ -= dt;
  if (bird_timer_ <= 0.0f) {
    float rate = a.birds_per_minute * a.stillness * a.stillness;
    if (rate > 0.05f && !birds_.empty()) {
      auto s = birds_[static_cast<size_t>(random01() * static_cast<float>(birds_.size())) % birds_.size()];
      mixer_->play(s, {0.40f * a.stillness * mix_.birds, random01() * 1.6f - 0.8f, 0.9f + 0.2f * random01(), false});
      ++birds_played_;
    }
    float mean_gap = rate > 0.05f ? 60.0f / rate : 5.0f;
    bird_timer_ = mean_gap * (0.5f + random01());
  }

  // Footsteps, exactly when each foot lands.
  int falls = in.move_blend > 0.4f ? footfalls_between(last_phase_, in.stride_phase) : 0;
  last_phase_ = in.stride_phase;
  const auto& set = in.on_bare_ground ? steps_dirt_ : steps_grass_;
  for (int i = 0; i < falls && !set.empty(); ++i) {
    int pick = static_cast<int>(random01() * static_cast<float>(set.size())) % static_cast<int>(set.size());
    if (pick == last_step_ && set.size() > 1) pick = (pick + 1) % static_cast<int>(set.size());
    last_step_ = pick;
    float gain = (0.42f + 0.40f * in.run_blend) * (1.0f - 0.3f * clampf(in.duel, 0.0f, 1.0f)) * mix_.footsteps;
    float pan = (footsteps_played_ % 2 == 0 ? -0.08f : 0.08f);
    mixer_->play(set[static_cast<size_t>(pick)], {gain, pan, 0.93f + 0.14f * random01(), false});
    ++footsteps_played_;
  }
}

}  // namespace rk
