// Smooth song clock (fixes arrow judder).
//
// The game positions arrows from the music's playback position (docs/TIMING.md):
// sub_82080928 refreshes the player's sample counters from the XAudio voice, then
//   sub_82080A40 -> seconds from player+0xC4 (used for the frame delta time)
//   sub_82080B00 -> seconds from player+0xCC (song time used for beats / arrows)
// The voice position only advances when an audio buffer is processed, i.e. in
// chunks of several milliseconds, not continuously. Sampled once per video frame
// that gives uneven steps (e.g. 5 ms, 11 ms, 5 ms ...) and the scrolling judders at
// any frame rate.
//
// Fix: after each refresh we replace the two counters with a smoothed estimate that
// advances with the host clock and is gently steered toward the real audio position
// (so it can never drift away from the music). The raw values are put back before
// the game's own refresh runs, so its loop/wrap logic is unaffected.
//
// --smooth_song_clock=false restores the original behaviour for A/B comparison.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

REXCVAR_DEFINE_BOOL(smooth_song_clock, true, "BBR",
                    "Smooth the music position used for arrow movement (fixes judder)");

REX_EXTERN(__imp__sub_82080928);

namespace {
using Clock = std::chrono::steady_clock;

uint8_t* Host(uint8_t* base, uint32_t addr) {
#if defined(_WIN32)
  return base + addr + (addr >= 0xE0000000u ? 0x1000u : 0u);
#else
  return base + addr;
#endif
}
uint32_t Load32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, Host(base, addr), 4);
  return __builtin_bswap32(v);
}
void Store32(uint8_t* base, uint32_t addr, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(Host(base, addr), &v, 4);
}

// Music player object fields.
constexpr uint32_t kVoice = 0x00, kPlaying = 0xB8, kPaused = 0xB9;
constexpr uint32_t kPosRaw = 0xC4, kPosLooped = 0xCC, kRate = 0xF0;

struct State {
  bool valid = false;    // estimate is tracking
  bool applied = false;  // our values are currently in the object
  uint32_t raw_c4 = 0, raw_cc = 0, wrote_c4 = 0, wrote_cc = 0;
  double est = 0;        // smoothed position, in samples
  double rate = 0;       // samples per host second
  Clock::time_point last, last_change, window_start;
  uint32_t window_raw = 0;
  // diagnostics
  Clock::time_point report_start;
  uint32_t frames = 0, updates = 0;
  double max_step_ms = 0;
};

std::mutex g_mutex;
std::unordered_map<uint32_t, State> g_states;

double Seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }
}  // namespace

REX_HOOK_RAW(sub_82080928) {
  const uint32_t player = ctx.r3.u32;
  bool active;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    State& s = g_states[player];
    if (s.applied) {  // give the game its real values back before it refreshes them
      if (Load32(base, player + kPosRaw) == s.wrote_c4) Store32(base, player + kPosRaw, s.raw_c4);
      if (Load32(base, player + kPosLooped) == s.wrote_cc) Store32(base, player + kPosLooped, s.raw_cc);
      s.applied = false;
    }
    active = *Host(base, player + kPlaying) && Load32(base, player + kVoice) &&
             !*Host(base, player + kPaused);
  }

  __imp__sub_82080928(ctx, base);

  std::lock_guard<std::mutex> lock(g_mutex);
  State& s = g_states[player];
  const uint32_t nominal = Load32(base, player + kRate);
  if (!active || nominal == 0) {
    s.valid = false;
    return;
  }
  const auto now = Clock::now();
  const uint32_t raw_c4 = Load32(base, player + kPosRaw);
  const uint32_t raw_cc = Load32(base, player + kPosLooped);

  // Diagnostics: how coarse is the audio position? Logged every 10 s of playback.
  if (s.frames == 0) s.report_start = now;
  ++s.frames;
  if (s.valid && raw_c4 != s.raw_c4) {
    ++s.updates;
    if (raw_c4 > s.raw_c4)
      s.max_step_ms = std::max(s.max_step_ms, 1000.0 * (raw_c4 - s.raw_c4) / nominal);
  }
  if (Seconds(now - s.report_start) >= 10.0) {
    REXLOG_INFO("[songclock] last 10 s: {} frames, {} audio position updates, biggest step {:.1f} ms, "
                "smoothing {}",
                s.frames, s.updates, s.max_step_ms, REXCVAR_GET(smooth_song_clock) ? "on" : "off");
    s.frames = s.updates = 0;
    s.max_step_ms = 0;
  }

  auto snap = [&] {
    s.est = raw_c4;
    s.rate = nominal;
    s.valid = true;
    s.last_change = s.window_start = now;
    s.window_raw = raw_c4;
  };

  if (!s.valid || raw_c4 < s.raw_c4) {  // first frame, new song, seek or loop
    snap();
  } else {
    const double dt = Seconds(now - s.last);
    if (raw_c4 != s.raw_c4) s.last_change = now;
    const bool stalled = Seconds(now - s.last_change) > 0.05;  // audio not advancing: hold still
    const double predicted = stalled ? s.est : s.est + dt * s.rate;
    const double error = double(raw_c4) - predicted;
    if (dt > 0.25 || std::fabs(error) > 0.25 * nominal) {
      snap();
    } else {
      const double gain = 1.0 - std::exp(-dt / 0.5);  // ~0.5 s time constant
      s.est = std::max(s.est, predicted + error * gain);  // never run backwards
    }
    // Follow tempo changes (if the music is played faster or slower than its nominal rate).
    const double window = Seconds(now - s.window_start);
    if (stalled) {
      s.window_start = now;
      s.window_raw = raw_c4;
    } else if (window >= 2.0 && raw_c4 != s.raw_c4) {
      const double measured = (double(raw_c4) - s.window_raw) / window;
      const double ratio = measured / nominal;
      if (std::fabs(ratio - 1.0) < 0.03) {
        s.rate = nominal;
      } else if (ratio > 0.25 && ratio < 4.0) {
        s.rate = 0.5 * s.rate + 0.5 * measured;
      }
      s.window_start = now;
      s.window_raw = raw_c4;
    }
  }
  s.last = now;
  s.raw_c4 = raw_c4;
  s.raw_cc = raw_cc;

  if (!REXCVAR_GET(smooth_song_clock)) return;
  const int64_t delta = std::llround(s.est) - int64_t(raw_c4);
  const int64_t new_cc = int64_t(raw_cc) + delta;
  if (new_cc < 0 || new_cc > 0xFFFFFFFFll) return;
  s.wrote_c4 = uint32_t(int64_t(raw_c4) + delta);
  s.wrote_cc = uint32_t(new_cc);
  Store32(base, player + kPosRaw, s.wrote_c4);
  Store32(base, player + kPosLooped, s.wrote_cc);
  s.applied = true;
}
