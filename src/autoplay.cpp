// Autoplay (BBRR Settings > Modifiers > Autoplay, --autoplay).
//
// Every rocket is hit at exactly its time: 0 ms, Perfect. It works through the game's own
// input path, so the game sees ordinary button presses:
//   - sub_820E6E50 is the game's controller read (XamInputGetState for one pad). The game
//     calls it more than once per frame (menus and gameplay each read the pad; ~190 reads a
//     second at 120 fps), so a button is held down for kHoldSeconds - long enough for every
//     reader to see it - and then left up for kGapSeconds before it can go down again.
//   - The press-judging hook (src/input_offset.cpp, sub_82066700) judges a press for a rocket
//     Autoplay pressed at the rocket's own time instead of the frame's, so the result is 0 ms.
//     The player's own presses are judged as usual.
//
// Which button a rocket wants (2026-11-12, from rockets logged in play): byte +8 is 0, 2, 4 or
// 6 (byte +1 holds the matching bit), the arrow colour is the button (green A, red B, blue X,
// yellow Y) and the game lists its colours Green, Red, Blue, Yellow. So byte 8 / 2 indexes
// A, B, X, Y.
//
// No score from Autoplay is ever kept. A song counts as autoplayed if Autoplay was on at any
// moment from its Get Ready countdown to its end (restarting the song starts a clean attempt):
//   - leaderboards (src/leaderboards.cpp) ignore that song's score;
//   - the results screen (ui_scripts/Ingame/LevelBreakdown.lua) skips the game's profile
//     save, and keeps skipping it for the rest of the run, because the game also holds its
//     best scores in memory and a later save would write the autoplayed one too.

#include "autoplay.h"

#include "input_offset.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

REXCVAR_DEFINE_BOOL(autoplay, false, "BBR",
                    "Every rocket is hit at exactly its time. For showing the game and testing: a song in which "
                    "it was on at any moment saves no score");

REX_EXTERN(__imp__sub_820E6E50);

namespace {
using Clock = std::chrono::steady_clock;
constexpr uint16_t kButtons[4] = {0x1000, 0x2000, 0x4000, 0x8000};  // XInput A, B, X, Y
constexpr char kButtonNames[] = "ABXY";
constexpr double kLead = 0.002;         // press when the rocket is due within this (song seconds)
constexpr double kHoldSeconds = 0.035;  // a button stays down this long...
constexpr double kGapSeconds = 0.03;    // ...and up at least this long before it goes down again

std::atomic<bool> g_used_song{false}, g_used_run{false};
std::atomic<bool> g_song_on{false};

struct Rocket {
  double due = 0;
  int button = -1;      // index into kButtons
  bool pressed = false; // Autoplay pressed for it
};
std::mutex g_mutex;
std::unordered_map<uint32_t, Rocket> g_rockets;
Clock::time_point g_down_until[4]{}, g_up_until[4]{};
// Diagnostics, once per song.
int g_reads = 0, g_presses = 0, g_judged = 0, g_logged = 0;
bool g_reported = false;
double g_song_started = 0;

int ButtonFor(int value) { return (value / 2) % 4; }
bool StartsWith(const char* name, const char* prefix) { return std::strncmp(name, prefix, std::strlen(prefix)) == 0; }
uint32_t Load32(uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
}  // namespace

bool bbr::AutoplayOn() { return REXCVAR_GET(autoplay); }
bool bbr::AutoplayUsedThisSong() { return g_used_song.load(); }
bool bbr::AutoplayUsedThisRun() { return g_used_run.load(); }

void bbr::AutoplayNoteScreen(const char* name) {
  if (!name || !StartsWith(name, "Ingame\\GetReady")) return;
  // A new attempt at a song: it is clean unless Autoplay is on now.
  g_used_song.store(REXCVAR_GET(autoplay));
  std::lock_guard<std::mutex> lock(g_mutex);
  g_rockets.clear();
  g_reads = g_presses = g_judged = g_logged = 0;
  g_reported = false;
  g_song_started = 0;
}

void bbr::AutoplayUpdate(uint8_t* base, bool song_on) {
  (void)base;
  g_song_on.store(song_on);
  if (song_on && REXCVAR_GET(autoplay) && !g_used_song.exchange(true)) {
    REXLOG_INFO("[autoplay] on during this song: its score will not be saved");
  }
  if (g_used_song.load()) g_used_run.store(true);
}

void bbr::AutoplayNoteArrow(uint8_t* base, uint32_t rocket, double due) {
  if (!REXCVAR_GET(autoplay) || !base || rocket < 0x10000) return;
  const int value = base[rocket + 8];
  std::lock_guard<std::mutex> lock(g_mutex);
  Rocket& r = g_rockets[rocket];
  if (std::fabs(r.due - due) > 0.001) {  // a new rocket in this slot
    r = Rocket{};
    if (g_logged < 4) {
      ++g_logged;
      REXLOG_INFO("[autoplay] rocket due {:.3f}: value {} -> button {}", due, value, kButtonNames[ButtonFor(value)]);
    }
  }
  r.due = due;
  r.button = ButtonFor(value);
}

bool bbr::AutoplayPressedFor(uint32_t rocket) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_rockets.find(rocket);
  return it != g_rockets.end() && it->second.pressed;
}

void bbr::AutoplayNoteJudged(uint8_t* base, uint32_t rocket) {
  (void)base;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_rockets.find(rocket);
  if (it != g_rockets.end() && it->second.pressed) ++g_judged;
}

std::string bbr::AutoplayCommand(const std::string& command) {
  if (command == "bbrr.autoplay.nosave") return g_used_run.load() ? "1" : "0";
  return "";
}

// The game's controller read: sub_820E6E50(user index, XINPUT_STATE*) -> 0 or an error.
// XINPUT_STATE: +0 packet number, +4 buttons (16 bits), then triggers and sticks.
REX_HOOK_RAW(sub_820E6E50) {
  const uint32_t user = ctx.r3.u32, state = ctx.r4.u32;
  __imp__sub_820E6E50(ctx, base);
  if (ctx.r3.u32 == 0 && state >= 0x10000) {
    uint16_t read;
    std::memcpy(&read, base + state + 4, 2);
    bbr::NoteGuestButtons(user, __builtin_bswap16(read));
  }
  if (!REXCVAR_GET(autoplay) || user != 0 || state < 0x10000 || !g_song_on.load()) return;
  const Clock::time_point t = Clock::now();
  double now = 0;
  const bool have_time = bbr::SongTimeNow(&now);
  // While the song clock is moving (playing, not paused), the player's own face buttons are
  // taken out: a press of theirs would otherwise judge a rocket first, with their timing
  // instead of 0 ms. In the pause menu the clock stops and the buttons work again.
  static double last_song_time = -1;
  static Clock::time_point last_moved{};
  if (have_time && now != last_song_time) {
    last_song_time = now;
    last_moved = t;
  }
  const bool mask_player = have_time && t - last_moved < std::chrono::milliseconds(150);
  uint16_t add = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_reads;
    if (have_time) {
      if (g_song_started == 0) g_song_started = now;
      if (!g_reported && now - g_song_started > 10.0) {
        g_reported = true;
        REXLOG_INFO("[autoplay] first 10 s of play: {} pad reads, {} presses, {} of them judged", g_reads, g_presses,
                    g_judged);
      }
      for (auto it = g_rockets.begin(); it != g_rockets.end();) {
        Rocket& r = it->second;
        if (now - r.due > 1.0) {
          it = g_rockets.erase(it);
          continue;
        }
        if (!r.pressed && r.button >= 0 && r.due - now <= kLead && now - r.due < 0.05 &&
            t >= g_up_until[r.button]) {
          g_down_until[r.button] = t + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(kHoldSeconds));
          g_up_until[r.button] = g_down_until[r.button] +
                                 std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(kGapSeconds));
          r.pressed = true;
          ++g_presses;
        }
        ++it;
      }
    }
    for (int b = 0; b < 4; ++b) {
      if (t < g_down_until[b]) add |= kButtons[b];
    }
  }
  if (add == 0 && !mask_player) return;
  // Add the buttons (big-endian guest memory); a new packet number tells the game it changed.
  uint16_t buttons;
  std::memcpy(&buttons, base + state + 4, 2);
  buttons = __builtin_bswap16(buttons);
  if (mask_player) buttons &= uint16_t(~0xF000);  // A, B, X, Y
  buttons = uint16_t(buttons | add);
  if (ctx.r3.u32 != 0) {  // no controller: report one with only these buttons down
    std::memset(base + state, 0, 16);
    buttons = add;
    ctx.r3.u64 = 0;
  }
  buttons = __builtin_bswap16(buttons);
  std::memcpy(base + state + 4, &buttons, 2);
  const uint32_t packet = __builtin_bswap32(Load32(base, state) + 1);
  std::memcpy(base + state, &packet, 4);
}
