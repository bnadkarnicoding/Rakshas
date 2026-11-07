// tests/test_audio.cpp — the mixer, run offline (no speakers needed).
#include <cmath>

#include "audio/audio.h"
#include "test.h"

using namespace rk;
using namespace rk::audio;

static std::vector<float> render(Mixer& m, u32 frames) {
  std::vector<float> out(frames * 2);
  m.render(out.data(), frames);
  return out;
}

TEST(one_shot_plays_then_ends) {
  Mixer m;
  SoundHandle s = m.add(std::vector<float>(480, 0.5f));
  VoiceHandle v = m.play(s, {});
  CHECK(m.playing(v));
  std::vector<float> out = render(m, 1000);
  CHECK(out[10] > 0.1f);
  CHECK(std::fabs(out[1500]) < 1e-6f);  // after the sound ended
  CHECK(!m.playing(v));
}

TEST(loops_keep_playing) {
  Mixer m;
  SoundHandle s = m.add(std::vector<float>(480, 0.5f));
  VoiceHandle v = m.play(s, {1.0f, 0.0f, 1.0f, true});
  std::vector<float> out = render(m, 5000);
  CHECK(m.playing(v));
  CHECK(out[2 * 4999] > 0.1f);
}

TEST(volume_changes_glide_without_clicks) {
  Mixer m;
  SoundHandle s = m.add(std::vector<float>(48000, 0.8f));
  VoiceHandle v = m.play(s, {0.0f, 0.0f, 1.0f, true});
  m.set(v, 1.0f, 0.0f, 1.0f, 0.1f);
  std::vector<float> out = render(m, 9600);
  float worst = 0.0f;
  for (size_t i = 2; i < out.size(); i += 2) worst = std::fmax(worst, std::fabs(out[i] - out[i - 2]));
  CHECK(worst < 0.01f);
  CHECK(out[2 * 9000] > 0.4f);  // reached full volume
}

TEST(pan_moves_sound_between_ears) {
  Mixer m;
  SoundHandle s = m.add(std::vector<float>(4800, 0.5f));
  m.play(s, {1.0f, -1.0f, 1.0f, false});
  std::vector<float> out = render(m, 100);
  CHECK(out[2 * 50] > 0.3f);              // left
  CHECK(std::fabs(out[2 * 50 + 1]) < 0.01f);  // right silent
}

TEST(stop_fades_out_and_frees_the_voice) {
  Mixer m;
  SoundHandle s = m.add(std::vector<float>(480, 0.5f));
  VoiceHandle v = m.play(s, {1.0f, 0.0f, 1.0f, true});
  m.stop(v, 0.01f);
  render(m, 2000);
  CHECK(!m.playing(v));
  CHECK(m.active_voices() == 0);
}

TEST(too_many_sounds_reuse_voices) {
  Mixer m;
  SoundHandle s = m.add(std::vector<float>(48000, 0.1f));
  for (int i = 0; i < 50; ++i) m.play(s, {});
  CHECK(m.active_voices() == kMaxVoices);
  std::vector<float> out = render(m, 256);
  for (float x : out) CHECK(std::fabs(x) <= 1.0f);  // limiter keeps it in range
}

TEST(sound_files_decode) {
  Mixer m;
  SoundHandle wind = m.load(std::string(RK_SOURCE_DIR) + "/content/sounds/wind_loop.wav");
  CHECK(wind.valid());
  CHECK(!m.load("/no/such/sound.wav").valid());
}
