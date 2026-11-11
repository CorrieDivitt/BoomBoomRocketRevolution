// Press judging: input offset (--input_offset_ms) and precise press times (--precise_input).
//
// How the game judges a press (see docs/TIMING.md):
//   sub_82064978(obj, time) keeps the last three song times once per frame:
//     0x821BE9C8 = this frame, 0x821BE9CC = previous frame, 0x821BE9D0 = the one before.
//   sub_82066700 judges a press against a rocket: with T = the rocket's target time and
//     W = [0x821BE9D0, 0x821BE9CC] (a whole frame's time span),
//     accuracy = 1 - |T - clamp(T, W)| / (TargetWidth / 2).
// So the original game only knows a press to the nearest frame, and how early it counts
// the press depends on the frame rate.
//
// Precise input: a small thread reads the controller about 1000 times a second and notes
// the moment a button goes down. When the game then judges that press, W is replaced by
// the single song time at which the button really went down (song time runs with the
// clock, so it is the latest song time minus the time since the press). Without a fresh
// controller press (keyboard, non-XInput pads) the game's own span is used.
//
// Input offset: moves the judged time. Positive = presses count as made that many
// milliseconds earlier (input or display delay), negative = later. Only judging is
// affected; arrows and music are not.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_DOUBLE(input_offset_ms, 0.0, "BBR",
                      "Input offset in milliseconds: positive = presses are judged as made earlier "
                      "(compensates input/display delay). Range -300..300");
REXCVAR_DEFINE_BOOL(precise_input, true, "BBR",
                    "Judge presses by the moment the controller button went down (read ~1000 times a "
                    "second) instead of by whole frames");
REXCVAR_DEFINE_DOUBLE(input_frame_credit_ms, 8.0, "BBR",
                      "With precise_input: presses count this much earlier, standing in for the head "
                      "start the game's frame-based judging gives");

REX_EXTERN(__imp__sub_82066700);
REX_EXTERN(__imp__sub_82064978);

namespace {
using Clock = std::chrono::steady_clock;

constexpr uint32_t kPressSpanEnd = 0x821BE9CC;    // song time of the previous frame
constexpr uint32_t kPressSpanStart = 0x821BE9D0;  // song time of the frame before that
constexpr double kFreshPressSeconds = 0.15;       // a press older than this is not "this" press

float LoadFloat(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
void StoreFloat(uint8_t* base, uint32_t addr, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(base + addr, &v, 4);
}
double Now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

// Song time handed to the game each frame, with the moment it was handed over. Only the
// game thread touches these.
struct Anchor {
  double host = 0.0;
  double time = 0.0;
};
constexpr int kAnchors = 64;
Anchor g_anchors[kAnchors];
int g_anchor_count = 0;  // total ever stored

// Moment of the latest button press seen by the controller thread (0 = none yet).
std::atomic<double> g_last_press{0.0};
std::atomic<bool> g_thread_started{false};

#if defined(_WIN32)
struct PadState {  // XINPUT_STATE, declared here so no XInput header/library is needed
  uint32_t packet;
  uint16_t buttons;
  uint8_t left_trigger, right_trigger;
  int16_t lx, ly, rx, ry;
};
using GetStateFn = uint32_t(__stdcall*)(uint32_t, PadState*);

void ControllerThread() {
  HMODULE xinput = nullptr;
  for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
    xinput = LoadLibraryW(name);
    if (xinput) break;
  }
  GetStateFn get_state = xinput ? reinterpret_cast<GetStateFn>(GetProcAddress(xinput, "XInputGetState")) : nullptr;
  if (!get_state) {
    REXLOG_WARN("[input] XInput not available: presses are judged by frame");
    return;
  }
  if (HMODULE winmm = LoadLibraryW(L"winmm.dll")) {  // 1 ms sleeps
    using PeriodFn = uint32_t(__stdcall*)(uint32_t);
    if (auto begin = reinterpret_cast<PeriodFn>(GetProcAddress(winmm, "timeBeginPeriod"))) begin(1);
  }
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  REXLOG_INFO("[input] precise press timing: reading the controller every millisecond");
  // Face buttons and d-pad: the buttons that set off rockets.
  constexpr uint16_t kWatched = 0xF000 | 0x000F;
  bool connected[4] = {true, true, true, true};
  uint16_t previous[4] = {0, 0, 0, 0};
  double recheck = 0.0;
  for (;;) {
    const double now = Now();
    // Asking about an unplugged pad is slow, so those are only looked at now and then.
    const bool check_all = now >= recheck;
    if (check_all) recheck = now + 2.0;
    for (uint32_t pad = 0; pad < 4; ++pad) {
      if (!connected[pad] && !check_all) continue;
      PadState state{};
      if (get_state(pad, &state) != 0) {
        connected[pad] = false;
        previous[pad] = 0;
        continue;
      }
      connected[pad] = true;
      const uint16_t buttons = state.buttons & kWatched;
      if (buttons & ~previous[pad]) g_last_press.store(Now(), std::memory_order_relaxed);
      previous[pad] = buttons;
    }
    Sleep(1);
  }
}
#endif

void StartControllerThread() {
  if (g_thread_started.exchange(true)) return;
#if defined(_WIN32)
  std::thread(ControllerThread).detach();
#endif
}

// Song time at host moment `when`, or false when the song clock is not running steadily
// (paused, just started, just jumped).
bool SongTimeAt(double when, double* out) {
  if (g_anchor_count < 8) return false;
  const Anchor& latest = g_anchors[(g_anchor_count - 1) % kAnchors];
  // Check against a frame from a little while back that time is passing at normal speed.
  const int back = std::min(g_anchor_count - 1, kAnchors - 1);
  const Anchor* old = nullptr;
  for (int i = 4; i <= back; ++i) {
    const Anchor& a = g_anchors[(g_anchor_count - 1 - i) % kAnchors];
    old = &a;
    if (latest.host - a.host >= 0.2) break;
  }
  if (!old || latest.host - old->host < 0.03 || latest.host - old->host > 1.0) return false;
  const double rate = (latest.time - old->time) / (latest.host - old->host);
  if (rate < 0.9 || rate > 1.1) return false;
  *out = latest.time + (when - latest.host);
  return true;
}

struct Stats {
  int count = 0;
  double sum_diff = 0, min_diff = 1e9, max_diff = -1e9, sum_age = 0;
} g_stats;
}  // namespace

REX_HOOK_RAW(sub_82064978) {
  if (REXCVAR_GET(precise_input)) {
    StartControllerThread();
    g_anchors[g_anchor_count % kAnchors] = {Now(), ctx.f1.f64};
    ++g_anchor_count;
  }
  __imp__sub_82064978(ctx, base);
}

REX_HOOK_RAW(sub_82066700) {
  const double shift = std::clamp(REXCVAR_GET(input_offset_ms), -300.0, 300.0) / 1000.0;
  const float end = LoadFloat(base, kPressSpanEnd), start = LoadFloat(base, kPressSpanStart);

  bool precise = false;
  double press_time = 0.0;
  if (REXCVAR_GET(precise_input)) {
    const double pressed = g_last_press.load(std::memory_order_relaxed), now = Now();
    if (pressed > 0.0 && now - pressed < kFreshPressSeconds && SongTimeAt(pressed, &press_time)) {
      precise = true;
      // For tuning: how the real press time compares with the game's frame span.
      const double diff = press_time - 0.5 * (double(start) + double(end));
      g_stats.sum_diff += diff;
      g_stats.min_diff = std::min(g_stats.min_diff, diff);
      g_stats.max_diff = std::max(g_stats.max_diff, diff);
      g_stats.sum_age += now - pressed;
      if (++g_stats.count >= 40) {
        REXLOG_INFO("[input] last {} presses: real press time vs frame span centre avg {:.1f} ms (min {:.1f}, max {:.1f}); "
                    "press reached the game after avg {:.1f} ms; credit {:.1f} ms, input offset {:.0f} ms",
                    g_stats.count, g_stats.sum_diff / g_stats.count * 1000.0, g_stats.min_diff * 1000.0,
                    g_stats.max_diff * 1000.0, g_stats.sum_age / g_stats.count * 1000.0,
                    REXCVAR_GET(input_frame_credit_ms), shift * 1000.0);
        g_stats = Stats{};
      }
      press_time -= std::clamp(REXCVAR_GET(input_frame_credit_ms), 0.0, 100.0) / 1000.0;
    }
  }

  if (!precise && shift == 0.0) {
    __imp__sub_82066700(ctx, base);
    return;
  }
  if (precise) {
    StoreFloat(base, kPressSpanEnd, float(press_time - shift));
    StoreFloat(base, kPressSpanStart, float(press_time - shift));
  } else {
    StoreFloat(base, kPressSpanEnd, float(double(end) - shift));
    StoreFloat(base, kPressSpanStart, float(double(start) - shift));
  }
  __imp__sub_82066700(ctx, base);
  StoreFloat(base, kPressSpanEnd, end);
  StoreFloat(base, kPressSpanStart, start);
}
