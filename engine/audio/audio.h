// audio/audio.h — the engine's sound: our own mixer, with miniaudio used only
// to decode files and to send the finished mix to the speakers.
//
//   * Sounds are decoded once into memory (mono, 48 kHz floats).
//   * Voices play sounds: looping (wind) or one-shot (a footstep), each with a
//     volume, a left/right pan and a pitch. Volume and pan changes are smoothed
//     so nothing clicks.
//   * The mixer can run on a real audio device or "offline", writing samples
//     into a buffer. Offline mode is used by tests and for recording WAV files
//     in the cloud workspace, which has no speakers.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/types.h"

namespace rk::audio {

constexpr u32 kSampleRate = 48000;
constexpr u32 kMaxVoices = 32;

struct SoundHandle {
  u32 id = 0;
  bool valid() const { return id != 0; }
};

struct VoiceHandle {
  u32 id = 0;  // includes a generation counter, so stale handles are ignored
  bool valid() const { return id != 0; }
};

struct PlayParams {
  float gain = 1.0f;   // 0..1 (and a little above)
  float pan = 0.0f;    // -1 left .. +1 right
  float pitch = 1.0f;  // playback speed (1 = original)
  bool loop = false;
  float fade_in = 0.0f;  // seconds
};

class Mixer {
 public:
  Mixer();
  ~Mixer();
  Mixer(const Mixer&) = delete;
  Mixer& operator=(const Mixer&) = delete;

  // Decodes a sound file (wav, flac, mp3). Returns an invalid handle on failure.
  SoundHandle load(const std::string& path);
  // Adds already-decoded mono samples at kSampleRate (used by tests).
  SoundHandle add(std::vector<float> samples);

  VoiceHandle play(SoundHandle sound, const PlayParams& params);
  // New target volume/pan/pitch; volume glides there over `seconds`.
  void set(VoiceHandle voice, float gain, float pan, float pitch = 1.0f, float seconds = 0.05f);
  void stop(VoiceHandle voice, float fade_seconds = 0.05f);
  bool playing(VoiceHandle voice) const;
  void set_master(float gain);
  u32 active_voices() const;

  // Mixes `frames` stereo frames (interleaved L, R) into `out`.
  void render(float* out, u32 frames);

  // Starts sending the mix to the default audio device. Returns false (and the
  // mixer keeps working offline) if there is no device.
  bool start_device();
  bool device_running() const;

  struct Impl;  // the mixing state (shared with the audio device's thread)

 private:
  std::unique_ptr<Impl> impl_;
};

// Writes interleaved stereo float samples as a 16-bit WAV file.
bool write_wav(const std::string& path, const std::vector<float>& stereo, u32 sample_rate = kSampleRate);

}  // namespace rk::audio
