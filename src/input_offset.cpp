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
// To keep the game as forgiving as it was at 60 frames a second, the precise press still
// counts as a span one 60 Hz frame wide (8.3 ms either side of the press).
//
// Hit timing (--hit_timing): after each hit a small number next to the arrow shows how
// far the press was from the rocket's time, in milliseconds (+ early, - late). The HUD
// script shows it; the text travels through the game's "stored strings" (128 slots of
// 256 characters at the start of the text manager object, shown by a text item whose
// text is "STR<n>" - see sub_8208D7E0).
//
// Assist tick (--assist_tick): a click placed in the outgoing sound exactly where each
// rocket's time falls in the music, whether or not anything is pressed.
//
// Input offset: moves the judged time. Positive = presses count as made that many
// milliseconds earlier (input or display delay), negative = later. Only judging is
// affected; arrows and music are not.

#include "input_offset.h"
#include "metronome.h"
#include "midasm_hooks.h"
#include "widescreen.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <mutex>
#include <string>
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

REXCVAR_DEFINE_BOOL(hit_timing, true, "BBR",
                    "Show how early (+) or late (-) each hit was, in milliseconds, next to its arrow");
REXCVAR_DEFINE_BOOL(assist_tick, false, "BBR",
                    "Play a click at the exact moment of every rocket, placed in the music stream");

REXCVAR_DEFINE_DOUBLE(assist_tick_volume, 2.5, "BBR", "Loudness of the assist tick (1 = as recorded). Range 0.1..8");

REX_EXTERN(__imp__sub_82066700);
REX_EXTERN(__imp__sub_82064978);

namespace {
using Clock = std::chrono::steady_clock;

constexpr uint32_t kPressSpanEnd = 0x821BE9CC;    // song time of the previous frame
constexpr uint32_t kPressSpanStart = 0x821BE9D0;  // song time of the frame before that
constexpr double kFreshPressSeconds = 0.06;       // a press older than this is not "this" press
constexpr double kSameFrameSeconds = 0.004;       // judge calls this close together are one frame
constexpr double kHalfSpanSeconds = 0.5 / 60.0;   // precise presses keep a 60 Hz frame of leeway
constexpr uint32_t kAccuracySlots = 0x821A0450;   // float per player: accuracy of the last judged press
constexpr uint32_t kUnjudged = 0x7FC0BB12;        // marker (a NaN) to see whether a press was judged
constexpr uint32_t kFirstStoredString = 120;      // STR120..STR123 carry the readout texts
constexpr uint32_t kStoredStrings = 4;

uint32_t LoadU32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return __builtin_bswap32(v);
}
void StoreU32(uint8_t* base, uint32_t addr, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + addr, &v, 4);
}

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
std::atomic<int> g_shoulder_steps{0};
std::atomic<double> g_shoulder_time{0.0};

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
  uint16_t previous_shoulders[4] = {0, 0, 0, 0};
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
        previous_shoulders[pad] = 0;
        continue;
      }
      connected[pad] = true;
      const uint16_t buttons = state.buttons & kWatched;
      if (buttons & ~previous[pad]) g_last_press.store(Now(), std::memory_order_relaxed);
      previous[pad] = buttons;
      // LB (0x0100) / RB (0x0200): scroll speed down / up (src/modifiers.cpp).
      const uint16_t shoulders = state.buttons & 0x0300;
      const uint16_t pressed = shoulders & ~previous_shoulders[pad];
      previous_shoulders[pad] = shoulders;
      if (pressed) {
        g_shoulder_steps.fetch_add(((pressed & 0x0200) ? 1 : 0) - ((pressed & 0x0100) ? 1 : 0));
        g_shoulder_time.store(Now(), std::memory_order_relaxed);
      }
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

// A rocket's time: [[rocket + 0x7C] + 0x48]. 0 when the rocket has none.
bool RocketTime(uint8_t* base, uint32_t rocket, double* out) {
  if (rocket < 0x10000) return false;
  const uint32_t info = LoadU32(base, rocket + 0x7C);
  if (info < 0x10000) return false;
  *out = LoadFloat(base, info + 0x48);
  return true;
}

// Where each rocket's arrow was last drawn (screen position, -1..1 both ways).
struct ArrowPlace {
  uint32_t rocket = 0;
  float x = 0, y = 0;
};
constexpr int kArrows = 64;
ArrowPlace g_arrows[kArrows];
int g_arrow_next = 0;

// Rockets that already have their assist tick.
struct Ticked {
  uint32_t rocket;
  double time;
};
std::deque<Ticked> g_ticked;

// Hits waiting to be shown by the HUD script.
struct Shown {
  uint32_t slot;
  float x;
  int grade;  // as the HUD's judgement text: 6 Perfect, 5 Awesome, 3 Good, 1 OK
};
std::mutex g_hit_mutex;
std::deque<Shown> g_hits;
uint32_t g_next_slot = 0;
constexpr uint32_t kTextManager = 0x821D48C8;  // the game's text manager (stored strings at its start)

struct HitStats {
  int count = 0;
  double sum = 0, sum_squares = 0;
} g_hit_stats;

// The whole song so far, for the line on the results screen. Starts again when a hit's
// rocket time is well before the previous hit's (new song, restart; in endurance play
// also every lap).
struct SongStats {
  int count = 0, early = 0, late = 0;
  double sum = 0, sum_squares = 0;
  double last_time = 0;
} g_song_stats;
std::mutex g_song_mutex;
constexpr uint32_t kSummarySlot = 126;  // stored string that carries the results line

// How far the music that is heard trails the song clock (same sum as src/song_clock.cpp).
double AudioLag() {
  double lag = 0.0;
  if (rex::cvar::GetFlagByName("audio_sync_compensation") != "false") {
    int frames = std::atoi(rex::cvar::GetFlagByName("audio_maxqframes").c_str());
    if (frames <= 0) frames = 8;
    lag += std::clamp(frames, 4, 64) * 256.0 / 48000.0;
  }
  lag += std::clamp(std::strtod(rex::cvar::GetFlagByName("audio_offset_ms").c_str(), nullptr), -500.0, 500.0) / 1000.0;
  return lag;
}

void ReportHit(uint8_t* base, uint32_t rocket, double rocket_time, double early_seconds, float accuracy) {
  const double ms = early_seconds * 1000.0;
  {
    std::lock_guard<std::mutex> lock(g_song_mutex);
    if (rocket_time < g_song_stats.last_time - 5.0) g_song_stats = SongStats{};
    g_song_stats.last_time = rocket_time;
    g_song_stats.count++;
    g_song_stats.sum += ms;
    g_song_stats.sum_squares += ms * ms;
    if (ms > 0.0) g_song_stats.early++;
    if (ms < 0.0) g_song_stats.late++;
  }
  g_hit_stats.count++;
  g_hit_stats.sum += ms;
  g_hit_stats.sum_squares += ms * ms;
  if (g_hit_stats.count >= 40) {
    const double mean = g_hit_stats.sum / g_hit_stats.count;
    const double spread = std::sqrt(std::max(0.0, g_hit_stats.sum_squares / g_hit_stats.count - mean * mean));
    REXLOG_INFO("[input] last {} hits: on average {:+.1f} ms (+ early, - late), spread {:.1f} ms", g_hit_stats.count,
                mean, spread);
    g_hit_stats = HitStats{};
  }
  const uint32_t manager = kTextManager;
  if (!REXCVAR_GET(hit_timing)) return;
  float x = 0.0f;
  bool found = false;
  for (const ArrowPlace& a : g_arrows) {
    if (a.rocket == rocket) { x = a.x; found = true; }
  }
  if (!found) return;
  std::lock_guard<std::mutex> lock(g_hit_mutex);
  const uint32_t slot = kFirstStoredString + (g_next_slot++ % kStoredStrings);
  char text[32];
  std::snprintf(text, sizeof(text), "%+.2f", ms);
  std::memcpy(base + manager + slot * 256, text, std::strlen(text) + 1);
  // Screen position -> 640x480 layout units (the layout is 480 * aspect units wide,
  // centred on x = 320). The HUD script puts the number just above the target line, in
  // line with the arrow, in the colour of the judgement (same limits as the game).
  const double aspect = bbr::UiAspectRatio();
  const int grade = accuracy >= 0.95f ? 6 : accuracy > 0.8f ? 5 : accuracy > 0.4f ? 3 : 1;
  if (g_hits.size() < 16) g_hits.push_back({slot, float(320.0 + x * 240.0 * aspect), grade});
}

struct Stats {
  int count = 0;
  double sum_diff = 0, min_diff = 1e9, max_diff = -1e9, sum_age = 0;
} g_stats;
}  // namespace

REX_HOOK_RAW(sub_82064978) {
  if (REXCVAR_GET(precise_input)) StartControllerThread();
  g_anchors[g_anchor_count % kAnchors] = {Now(), ctx.f1.f64};
  ++g_anchor_count;
  __imp__sub_82064978(ctx, base);
}

REX_HOOK_RAW(sub_82066700) {
  // Nothing was pressed this frame: the game has nothing to judge either.
  if (LoadU32(base, ctx.r3.u32 + 0x18) == 0) {
    __imp__sub_82066700(ctx, base);
    return;
  }
  const double shift = std::clamp(REXCVAR_GET(input_offset_ms), -300.0, 300.0) / 1000.0;
  const float end = LoadFloat(base, kPressSpanEnd), start = LoadFloat(base, kPressSpanStart);

  bool precise = false;
  double press_time = 0.0;
  if (REXCVAR_GET(precise_input)) {
    const double pressed = g_last_press.load(std::memory_order_relaxed), now = Now();
    // A controller press belongs to the first frame that judges anything after it. Later
    // frames (a trigger pull, a key, a pad we do not read) must not reuse its time.
    static double bound_press = 0.0, bound_at = 0.0;
    if (pressed > 0.0 && pressed != bound_press && now - pressed < kFreshPressSeconds) {
      bound_press = pressed;
      bound_at = now;
    }
    if (pressed > 0.0 && pressed == bound_press && now - bound_at < kSameFrameSeconds &&
        SongTimeAt(pressed, &press_time)) {
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

  // Which rocket, whose press, and a marker to see whether a press gets judged here.
  const uint32_t rocket = ctx.r4.u32;
  const uint32_t player = LoadU32(base, ctx.r3.u32 + 0x5C);
  const uint32_t slot = kAccuracySlots + (player < 4 ? player : 0) * 4;
  const uint32_t accuracy_before = LoadU32(base, slot);
  StoreU32(base, slot, kUnjudged);

  double centre;
  if (precise) {
    centre = press_time - shift;
    StoreFloat(base, kPressSpanEnd, float(centre + kHalfSpanSeconds));
    StoreFloat(base, kPressSpanStart, float(centre - kHalfSpanSeconds));
  } else {
    centre = 0.5 * (double(start) + double(end)) - shift;
    StoreFloat(base, kPressSpanEnd, float(double(end) - shift));
    StoreFloat(base, kPressSpanStart, float(double(start) - shift));
  }
  __imp__sub_82066700(ctx, base);
  StoreFloat(base, kPressSpanEnd, end);
  StoreFloat(base, kPressSpanStart, start);

  const uint32_t accuracy_bits = LoadU32(base, slot);
  if (accuracy_bits == kUnjudged) {
    StoreU32(base, slot, accuracy_before);  // nothing was judged
    return;
  }
  double rocket_time;
  const float accuracy = LoadFloat(base, slot);
  if (accuracy > 0.0f && RocketTime(base, rocket, &rocket_time)) {
    const double early = rocket_time - centre;
    // Hits that are far off are worth a line each: was it the press or the way it was timed?
    if (std::fabs(early) > 0.030) {
      const double frame_centre = 0.5 * (double(start) + double(end)) - shift;
      REXLOG_INFO("[input] hit {:+.1f} ms: timed by {}, button down {:.1f} ms before it was judged; "
                  "the game's own frame timing would say {:+.1f} ms",
                  early * 1000.0, precise ? "controller press" : "frame (no fresh controller press)",
                  (Now() - g_last_press.load(std::memory_order_relaxed)) * 1000.0,
                  (rocket_time - frame_centre) * 1000.0);
    }
    ReportHit(base, rocket, rocket_time, early, accuracy);
  }
}

// Mid-asm hook at 0x8206A874 (see config/default.toml): an arrow icon is being drawn.
void BbrArrowIconHook(PPCRegister& r31, PPCRegister& r1) {
  uint8_t* base = bbr::GuestBase();
  if (!base || r31.u32 < 0x10000) return;
  const uint32_t rocket = LoadU32(base, r31.u32);
  if (rocket < 0x10000) return;
  const float w = LoadFloat(base, r1.u32 + 0x5C);
  if (w > 1e-4f) {
    ArrowPlace* place = nullptr;
    for (ArrowPlace& a : g_arrows) {
      if (a.rocket == rocket) place = &a;
    }
    if (!place) place = &g_arrows[g_arrow_next++ % kArrows];
    *place = {rocket, LoadFloat(base, r1.u32 + 0x50) / w, LoadFloat(base, r1.u32 + 0x54) / w};
  }

  // Assist tick: once the rocket's moment is close, place a click in the sound going
  // out now. A sound mixed now is heard one audio lag later, and the song clock runs
  // that lag behind the mixer, so the click belongs (rocket time - lag) on the song clock.
  if (!REXCVAR_GET(assist_tick) || g_anchor_count == 0) return;
  double rocket_time;
  if (!RocketTime(base, rocket, &rocket_time)) return;
  const Anchor& latest = g_anchors[(g_anchor_count - 1) % kAnchors];
  const double now = Now();
  if (now - latest.host > 0.1) return;  // song clock not running
  const double wait = rocket_time - AudioLag() - (latest.time + (now - latest.host));
  if (wait <= 0.0 || wait > 0.08) return;
  for (const Ticked& t : g_ticked) {
    if (t.rocket == rocket && std::fabs(t.time - rocket_time) < 0.001) return;
  }
  // Rockets that go off together share one click.
  bool together = false;
  for (const Ticked& t : g_ticked) together |= std::fabs(t.time - rocket_time) < 0.001;
  g_ticked.push_back({rocket, rocket_time});
  while (g_ticked.size() > 64) g_ticked.pop_front();
  if (!together) bbr::ScheduleTick(wait, REXCVAR_GET(assist_tick_volume));
}

int bbr::TakeShoulderSteps() {
  StartControllerThread();
  const int steps = g_shoulder_steps.exchange(0);
  if (steps == 0) return 0;
  return Now() - g_shoulder_time.load(std::memory_order_relaxed) < 0.25 ? steps : 0;
}

std::string bbr::HitCommand(const std::string& command) {
  if (command == "bbrr.hit.enabled") return REXCVAR_GET(hit_timing) ? "1" : "0";
  if (command == "bbrr.hit.summary") {
    // Results screen: write the song's timing line into its stored string.
    SongStats song;
    {
      std::lock_guard<std::mutex> lock(g_song_mutex);
      song = g_song_stats;
    }
    uint8_t* base = bbr::GuestBase();
    if (!REXCVAR_GET(hit_timing) || !base || song.count == 0) return "0";
    const double mean = song.sum / song.count;
    const double spread = std::sqrt(std::max(0.0, song.sum_squares / song.count - mean * mean));
    char text[128];
    std::snprintf(text, sizeof(text), "Timing  avg %+.1f ms   spread %.1f ms   early %d   late %d", mean, spread,
                  song.early, song.late);
    std::memcpy(base + kTextManager + kSummarySlot * 256, text, std::strlen(text) + 1);
    REXLOG_INFO("[input] song summary over {} hits: {}", song.count, text);
    return "1";
  }
  if (command == "bbrr.hit.poll") {
    std::lock_guard<std::mutex> lock(g_hit_mutex);
    if (g_hits.empty()) return "";
    const Shown hit = g_hits.front();
    g_hits.pop_front();
    char text[64];
    std::snprintf(text, sizeof(text), "%u,%.1f,%d", hit.slot, hit.x, hit.grade);
    return text;
  }
  return "";
}
