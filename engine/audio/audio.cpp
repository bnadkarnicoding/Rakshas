// audio/audio.cpp — the mixer. Device output and decoding use miniaudio
// (approved list); mixing, voices and smoothing are ours.
#include "audio/audio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "core/log.h"
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#define MA_NO_GENERATION
#define MA_NO_ENCODING
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#include "miniaudio/miniaudio.h"  // implementation compiled in miniaudio_impl.cpp
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace rk::audio {
namespace {

constexpr float kPi = 3.14159265358979f;

struct Voice {
  bool active = false;
  u32 generation = 0;
  u32 sound = 0;
  double position = 0.0;
  bool loop = false;
  float pitch = 1.0f;
  float gain = 0.0f, target_gain = 0.0f, gain_step = 0.0f;  // per-sample glide
  float pan = 0.0f, target_pan = 0.0f;
  bool stopping = false;
};

}  // namespace

struct Mixer::Impl {
  mutable std::mutex lock;
  std::vector<std::vector<float>> sounds{{}};  // index 0 = invalid
  Voice voices[kMaxVoices];
  float master = 1.0f;
  ma_device device{};
  bool device_ok = false;

  Voice* find(VoiceHandle h) {
    u32 index = h.id & 0xFFu;
    u32 gen = h.id >> 8;
    if (!h.valid() || index >= kMaxVoices) return nullptr;
    Voice& v = voices[index];
    return (v.active && v.generation == gen) ? &v : nullptr;
  }

  static void glide(Voice& v, float target, float seconds) {
    v.target_gain = target;
    float samples = std::max(1.0f, seconds * static_cast<float>(kSampleRate));
    v.gain_step = (target - v.gain) / samples;
  }

  void mix(float* out, u32 frames) {
    std::memset(out, 0, sizeof(float) * frames * 2);
    for (Voice& v : voices) {
      if (!v.active) continue;
      const std::vector<float>& s = sounds[v.sound];
      const double length = static_cast<double>(s.size());
      if (s.empty()) {
        v.active = false;
        continue;
      }
      for (u32 i = 0; i < frames; ++i) {
        // Glide volume toward its target (no clicks), and pan smoothly.
        if (v.gain != v.target_gain) {
          v.gain += v.gain_step;
          if ((v.gain_step > 0 && v.gain >= v.target_gain) || (v.gain_step < 0 && v.gain <= v.target_gain))
            v.gain = v.target_gain;
        }
        v.pan += (v.target_pan - v.pan) * 0.0005f;
        if (v.stopping && v.gain <= 0.0f) {
          v.active = false;
          break;
        }
        // Linear interpolation between neighbouring samples (handles pitch).
        size_t i0 = static_cast<size_t>(v.position);
        size_t i1 = i0 + 1 < s.size() ? i0 + 1 : (v.loop ? 0 : i0);
        float frac = static_cast<float>(v.position - static_cast<double>(i0));
        float sample = s[i0] + (s[i1] - s[i0]) * frac;
        // Equal-power pan keeps loudness steady as a sound moves across.
        float angle = (v.pan + 1.0f) * 0.25f * kPi;
        float g = v.gain * master;
        out[i * 2] += sample * std::cos(angle) * g;
        out[i * 2 + 1] += sample * std::sin(angle) * g;
        v.position += v.pitch;
        if (v.position >= length) {
          if (v.loop) {
            v.position -= length;
          } else {
            v.active = false;
            break;
          }
        }
      }
    }
    // Gentle safety limiter so many sounds together never distort harshly.
    for (u32 i = 0; i < frames * 2; ++i) out[i] = std::tanh(out[i]);
  }
};

namespace {
void device_callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
  auto* impl = static_cast<Mixer::Impl*>(device->pUserData);
  std::lock_guard<std::mutex> guard(impl->lock);
  impl->mix(static_cast<float*>(output), frames);
}
}  // namespace

Mixer::Mixer() : impl_(std::make_unique<Impl>()) {}

Mixer::~Mixer() {
  if (impl_->device_ok) ma_device_uninit(&impl_->device);
}

SoundHandle Mixer::load(const std::string& path) {
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, kSampleRate);
  ma_decoder decoder;
  if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
    log_warn("sound not found or unreadable: %s", path.c_str());
    return {};
  }
  std::vector<float> samples;
  float chunk[4096];
  for (;;) {
    ma_uint64 read = 0;
    if (ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &read) != MA_SUCCESS || read == 0) break;
    samples.insert(samples.end(), chunk, chunk + read);
  }
  ma_decoder_uninit(&decoder);
  if (samples.empty()) {
    log_warn("sound is empty: %s", path.c_str());
    return {};
  }
  return add(std::move(samples));
}

SoundHandle Mixer::add(std::vector<float> samples) {
  std::lock_guard<std::mutex> guard(impl_->lock);
  impl_->sounds.push_back(std::move(samples));
  return SoundHandle{static_cast<u32>(impl_->sounds.size() - 1)};
}

VoiceHandle Mixer::play(SoundHandle sound, const PlayParams& p) {
  std::lock_guard<std::mutex> guard(impl_->lock);
  if (!sound.valid() || sound.id >= impl_->sounds.size()) return {};
  // Use a free voice; if all are busy, take the quietest one-shot.
  int pick = -1;
  float quietest = 1e9f;
  for (u32 i = 0; i < kMaxVoices; ++i) {
    Voice& v = impl_->voices[i];
    if (!v.active) {
      pick = static_cast<int>(i);
      break;
    }
    if (!v.loop && v.gain < quietest) {
      quietest = v.gain;
      pick = static_cast<int>(i);
    }
  }
  if (pick < 0) return {};
  Voice& v = impl_->voices[pick];
  u32 gen = v.generation + 1;
  v = Voice{};
  v.generation = gen;
  v.active = true;
  v.sound = sound.id;
  v.loop = p.loop;
  v.pitch = std::max(0.05f, p.pitch);
  v.pan = v.target_pan = std::clamp(p.pan, -1.0f, 1.0f);
  if (p.fade_in > 0.0f) {
    v.gain = 0.0f;
    Impl::glide(v, p.gain, p.fade_in);
  } else {
    v.gain = v.target_gain = p.gain;
  }
  return VoiceHandle{(gen << 8) | static_cast<u32>(pick)};
}

void Mixer::set(VoiceHandle voice, float gain, float pan, float pitch, float seconds) {
  std::lock_guard<std::mutex> guard(impl_->lock);
  if (Voice* v = impl_->find(voice)) {
    if (v->stopping) return;
    Impl::glide(*v, std::max(0.0f, gain), seconds);
    v->target_pan = std::clamp(pan, -1.0f, 1.0f);
    v->pitch = std::max(0.05f, pitch);
  }
}

void Mixer::stop(VoiceHandle voice, float fade_seconds) {
  std::lock_guard<std::mutex> guard(impl_->lock);
  if (Voice* v = impl_->find(voice)) {
    v->stopping = true;
    Impl::glide(*v, 0.0f, std::max(fade_seconds, 1.0f / kSampleRate));
  }
}

bool Mixer::playing(VoiceHandle voice) const {
  std::lock_guard<std::mutex> guard(impl_->lock);
  return impl_->find(voice) != nullptr;
}

void Mixer::set_master(float gain) {
  std::lock_guard<std::mutex> guard(impl_->lock);
  impl_->master = std::max(0.0f, gain);
}

u32 Mixer::active_voices() const {
  std::lock_guard<std::mutex> guard(impl_->lock);
  u32 n = 0;
  for (const Voice& v : impl_->voices) n += v.active ? 1u : 0u;
  return n;
}

void Mixer::render(float* out, u32 frames) {
  std::lock_guard<std::mutex> guard(impl_->lock);
  impl_->mix(out, frames);
}

bool Mixer::start_device() {
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 2;
  config.sampleRate = kSampleRate;
  config.dataCallback = device_callback;
  config.pUserData = impl_.get();
  if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
    log_warn("no audio device available; continuing without sound");
    return false;
  }
  if (ma_device_start(&impl_->device) != MA_SUCCESS) {
    ma_device_uninit(&impl_->device);
    log_warn("could not start the audio device; continuing without sound");
    return false;
  }
  impl_->device_ok = true;
  return true;
}

bool Mixer::device_running() const { return impl_->device_ok; }

bool write_wav(const std::string& path, const std::vector<float>& stereo, u32 sample_rate) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  u32 data_bytes = static_cast<u32>(stereo.size() * 2);
  auto u32le = [&](u32 v) { std::fwrite(&v, 4, 1, f); };
  auto u16le = [&](u16 v) { std::fwrite(&v, 2, 1, f); };
  std::fwrite("RIFF", 1, 4, f);
  u32le(36 + data_bytes);
  std::fwrite("WAVEfmt ", 1, 8, f);
  u32le(16);
  u16le(1);  // PCM
  u16le(2);  // stereo
  u32le(sample_rate);
  u32le(sample_rate * 4);
  u16le(4);
  u16le(16);
  std::fwrite("data", 1, 4, f);
  u32le(data_bytes);
  for (float s : stereo) {
    float c = std::clamp(s, -1.0f, 1.0f);
    i16 v = static_cast<i16>(std::lround(c * 32767.0f));
    std::fwrite(&v, 2, 1, f);
  }
  std::fclose(f);
  return true;
}

}  // namespace rk::audio
