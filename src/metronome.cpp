// Calibration metronome.
//
// The game hands every block of mixed sound (256 samples x 6 channels, 48 kHz) to the
// console's audio driver through one call site: sub_8213E448 calls
// XAudioSubmitRenderDriverFrame at 0x8213E490 with r4 = the block (big-endian floats,
// one channel after another). A mid-asm hook there (BbrAudioFrameHook) lets us add our
// own clicks to the front left/right channels.
//
// Because the clicks are added at the same point where the music already is, they take
// exactly the same route to the speakers (host queue, device, anything after it), and we
// know to the sample where each click sits. That is what the calibration screen measures
// taps against: the "mixed" time, the same clock the game's song position uses.
//
// Sounds: calibration_sounds/beat.wav and subbeat.wav next to the exe (or in a parent
// folder) if present - PCM or float WAV, any sample rate, mono or stereo - otherwise two
// built-in clicks. The first beat of every bar plays beat.wav; the other beats, and the
// whole count-in, play subbeat.wav.

#include "metronome.h"
#include "midasm_hooks.h"
#include "widescreen.h"

#include <rex/filesystem.h>
#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace bbr {
void OnAudioFrameForHook(uint8_t* block);
namespace {
using Clock = std::chrono::steady_clock;
constexpr double kRate = 48000.0;
constexpr int kFrame = 256;      // samples per block and channel
constexpr int kChannels = 6;

struct Voice {
  const std::vector<float>* sound;
  int64_t start;  // sample index where it begins
};

std::mutex g_mutex;
// Audio clock: samples handed to the driver so far, and a smoothed mapping from the PC
// clock to that count (blocks arrive in small bursts, so the raw count moves in steps).
int64_t g_samples = 0;
bool g_anchor_valid = false;
Clock::time_point g_anchor_time;
double g_anchor_samples = 0;
// Metronome
bool g_running = false, g_audible = false;
double g_beat_samples = 0;   // samples per beat
double g_start_sample = 0;   // where beat 0 sits
int64_t g_next_event = 0;    // next beat index to schedule
int g_count_in = 4;          // beats before the first bar (all played with the lighter sound)
int g_bar = 4;               // beats per bar
std::vector<Voice> g_voices;
std::vector<float> g_beat_sound, g_sub_sound;
bool g_sounds_loaded = false;

double Seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

// Smoothed "samples mixed so far" at a point in time.
double SamplesAt(Clock::time_point t) {
  if (!g_anchor_valid) return double(g_samples);
  return g_anchor_samples + Seconds(t - g_anchor_time) * kRate;
}

std::vector<float> BuiltInClick(double hz, double seconds, double volume) {
  std::vector<float> out(size_t(seconds * kRate));
  for (size_t i = 0; i < out.size(); ++i) {
    const double t = double(i) / kRate;
    const double envelope = std::exp(-t * 90.0) * std::min(1.0, t * 2000.0);
    out[i] = float(volume * envelope * std::sin(2.0 * 3.14159265358979 * hz * t));
  }
  return out;
}

uint32_t LE32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
uint16_t LE16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

// Reads a WAV file into mono 48 kHz floats. Empty result = not usable.
std::vector<float> LoadWav(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return {};
  uint16_t format = 0, channels = 0, bits = 0;
  uint32_t rate = 0;
  const uint8_t* data = nullptr;
  size_t data_size = 0;
  for (size_t p = 12; p + 8 <= d.size();) {
    const uint32_t size = LE32(&d[p + 4]);
    const size_t body = p + 8;
    if (body + size > d.size()) break;
    if (std::memcmp(&d[p], "fmt ", 4) == 0 && size >= 16) {
      format = LE16(&d[body]);
      channels = LE16(&d[body + 2]);
      rate = LE32(&d[body + 4]);
      bits = LE16(&d[body + 14]);
      if (format == 0xFFFE && size >= 26) format = LE16(&d[body + 24]);  // WAVE_FORMAT_EXTENSIBLE
    } else if (std::memcmp(&d[p], "data", 4) == 0) {
      data = &d[body];
      data_size = size;
    }
    p = body + size + (size & 1);
  }
  const bool pcm = format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
  const bool flt = format == 3 && bits == 32;
  if (!data || !channels || !rate || (!pcm && !flt)) return {};
  const size_t bytes = bits / 8, frames = data_size / (bytes * channels);
  std::vector<float> mono(frames);
  for (size_t i = 0; i < frames; ++i) {
    double sum = 0;
    for (uint16_t c = 0; c < channels; ++c) {
      const uint8_t* s = data + (i * channels + c) * bytes;
      double v = 0;
      if (flt) { float f; std::memcpy(&f, s, 4); v = f; }
      else if (bits == 8) v = (double(s[0]) - 128.0) / 128.0;
      else if (bits == 16) v = double(int16_t(LE16(s))) / 32768.0;
      else if (bits == 24) v = double(int32_t((uint32_t(s[0]) << 8) | (uint32_t(s[1]) << 16) | (uint32_t(s[2]) << 24)) >> 8) / 8388608.0;
      else v = double(int32_t(LE32(s))) / 2147483648.0;
      sum += v;
    }
    mono[i] = float(sum / channels);
  }
  if (rate == uint32_t(kRate)) return mono;
  // Straight-line resampling is plenty for a click.
  const size_t out_frames = size_t(double(frames) * kRate / rate);
  std::vector<float> out(out_frames);
  for (size_t i = 0; i < out_frames; ++i) {
    const double pos = double(i) * rate / kRate;
    const size_t a = size_t(pos), b = std::min(a + 1, frames - 1);
    const double f = pos - double(a);
    out[i] = float(mono[std::min(a, frames - 1)] * (1.0 - f) + mono[b] * f);
  }
  return out;
}

void LoadSounds() {
  if (g_sounds_loaded) return;
  g_sounds_loaded = true;
  std::filesystem::path folder;
  std::error_code ec;
  for (auto dir = rex::filesystem::GetExecutableFolder(); !dir.empty(); dir = dir.parent_path()) {
    if (std::filesystem::is_directory(dir / "calibration_sounds", ec)) { folder = dir / "calibration_sounds"; break; }
    if (dir == dir.parent_path()) break;
  }
  if (!folder.empty()) {
    g_beat_sound = LoadWav(folder / "beat.wav");
    g_sub_sound = LoadWav(folder / "subbeat.wav");
  }
  REXLOG_INFO("[metronome] beat sound: {}, half-beat sound: {}", g_beat_sound.empty() ? "built-in" : "beat.wav",
              g_sub_sound.empty() ? "built-in" : "subbeat.wav");
  if (g_beat_sound.empty()) g_beat_sound = BuiltInClick(1760.0, 0.06, 0.6);
  if (g_sub_sound.empty()) g_sub_sound = BuiltInClick(880.0, 0.04, 0.3);
  // Keep them short so they never overlap the next click.
  const size_t limit = size_t(0.25 * kRate);
  if (g_beat_sound.size() > limit) g_beat_sound.resize(limit);
  if (g_sub_sound.size() > limit) g_sub_sound.resize(limit);
}

// Called for every block on its way to the driver. `block` = 6 x 256 big-endian floats,
// channel after channel (may be null in tests).
void OnAudioFrame(uint8_t* block) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const auto now = Clock::now();
  const int64_t frame_start = g_samples;
  g_samples += kFrame;

  // Keep the clock mapping close to the real count without following every burst.
  const double predicted = SamplesAt(now);
  const double error = double(g_samples) - predicted;
  if (!g_anchor_valid || std::fabs(error) > 0.2 * kRate) {
    g_anchor_valid = true;
    g_anchor_time = now;
    g_anchor_samples = double(g_samples);
  } else {
    g_anchor_samples = predicted + error * 0.02;
    g_anchor_time = now;
  }

  if (!g_running) {
    g_voices.clear();
    return;
  }
  // Start the clicks that begin inside this block. Accent = first beat of a bar, once
  // the count-in is over.
  for (;;) {
    const int64_t at = int64_t(std::llround(g_start_sample + double(g_next_event) * g_beat_samples));
    if (at >= frame_start + kFrame) break;
    if (at >= frame_start && g_audible) {
      const bool accent = g_next_event >= g_count_in && (g_next_event - g_count_in) % g_bar == 0;
      g_voices.push_back({accent ? &g_beat_sound : &g_sub_sound, at});
    }
    ++g_next_event;
  }
  if (!block || g_voices.empty()) return;

  float mix[kFrame] = {};
  for (auto it = g_voices.begin(); it != g_voices.end();) {
    const int64_t length = int64_t(it->sound->size());
    for (int64_t n = std::max(frame_start, it->start); n < std::min<int64_t>(frame_start + kFrame, it->start + length); ++n) {
      mix[n - frame_start] += (*it->sound)[size_t(n - it->start)];
    }
    it = (it->start + length <= frame_start + kFrame) ? g_voices.erase(it) : it + 1;
  }
  for (int channel = 0; channel < 2; ++channel) {  // front left, front right
    for (int i = 0; i < kFrame; ++i) {
      if (mix[i] == 0.0f) continue;
      uint8_t* p = block + (channel * kFrame + i) * 4;
      uint32_t bits;
      std::memcpy(&bits, p, 4);
      bits = __builtin_bswap32(bits);
      float sample;
      std::memcpy(&sample, &bits, 4);
      sample += mix[i];
      std::memcpy(&bits, &sample, 4);
      bits = __builtin_bswap32(bits);
      std::memcpy(p, &bits, 4);
    }
  }
}
}  // namespace

std::string MetronomeCommand(const std::string& command, const std::string& key, const std::string& value) {
  if (command == "bbrr.metro.start") {
    LoadSounds();
    std::lock_guard<std::mutex> lock(g_mutex);
    const double beat_ms = std::clamp(std::strtod(key.c_str(), nullptr), 200.0, 2000.0);
    g_beat_samples = beat_ms * kRate / 1000.0;
    int audible = 1, count_in = 4, bar = 4, lead = 1;
    std::sscanf(value.c_str(), "%d,%d,%d,%d", &audible, &count_in, &bar, &lead);
    g_audible = audible != 0;
    g_count_in = std::clamp(count_in, 0, 64);
    g_bar = std::clamp(bar, 1, 16);
    // Beat 0 comes `lead` beats from now; nothing is scheduled before it.
    g_start_sample = SamplesAt(Clock::now()) + g_beat_samples * std::clamp(lead, 1, 16);
    g_next_event = 0;
    g_voices.clear();
    g_running = true;
    return "ok";
  }
  if (command == "bbrr.metro.stop") {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_running = false;
    return "ok";
  }
  if (command == "bbrr.metro.pos") {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_running || g_beat_samples <= 0) return "-100";
    char text[32];
    std::snprintf(text, sizeof(text), "%.4f", (SamplesAt(Clock::now()) - g_start_sample) / g_beat_samples);
    return text;
  }
  return "";
}

void OnAudioFrameForHook(uint8_t* block) { OnAudioFrame(block); }
}  // namespace bbr

#ifndef BBR_METRONOME_TEST
// Mid-asm hook at 0x8213E490 (see config/default.toml): r4 = the block about to be submitted.
void BbrAudioFrameHook(PPCRegister& r4) {
  uint8_t* base = bbr::GuestBase();
  if (!base || r4.u32 == 0) {
    bbr::OnAudioFrameForHook(nullptr);
    return;
  }
#if defined(_WIN32)
  uint8_t* block = base + r4.u32 + (r4.u32 >= 0xE0000000u ? 0x1000u : 0u);
#else
  uint8_t* block = base + r4.u32;
#endif
  bbr::OnAudioFrameForHook(block);
}
#endif
