// Modifiers (BBRR Settings > Modifiers): changes to how a chart is presented or played.
//
// Scroll speed (--scroll_speed, default 1)
// ----------------------------------------
// How the game moves an arrow:
//   - sub_82065E70 builds every note when a song is loaded: f1 = flight time in seconds,
//     f2 = the time in the song it must arrive. The note (0x64 bytes, constructed by
//     sub_82065958) keeps launch time = f2 - f1 at +0x1C, where the arrow starts at +0x20,
//     where it ends (the target line) at +0x2C and the flight time at +0x38.
//   - sub_82064C50 (the note's per-frame check, r3 = note, f1 = song time) launches it
//     once the song time passes +0x1C (byte +4 = launched). That makes a "rocket" entry
//     (+0x3C seconds flown, +0x40 seconds left = flight time left + half the hit window)
//     and an arrow icon (+0 -> rocket, +4 -> note, +0x38 flight time).
//   - sub_8206A328 places the icon every frame at
//     start + (end - start) * (seconds flown / flight time).
// So flight time alone sets how fast arrows travel. The modifier divides it by the
// multiplier: arrows launch later and travel faster along the same path, arriving at
// exactly the same moment - the speed modifier of StepMania and Dance Dance Revolution.
//
// It can change in the middle of a song (LB / RB, or the settings screen while paused):
//   - a note not yet launched gets its launch time and flight time rewritten just before
//     the game checks it; one that should already be in the air is launched at once;
//   - an arrow in the air is put where it would be at the new speed, every frame: its
//     flight time becomes the new one and "seconds flown" becomes new flight time minus
//     the time it really has left. One that would not have been launched yet at the new
//     speed waits at its starting point.
// Nothing is touched while the multiplier is 1 and has not been changed during the song.

#include "input_offset.h"
#include "modifiers.h"
#include "settings.h"
#include "widescreen.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

REXCVAR_DEFINE_DOUBLE(scroll_speed, 1.0, "BBR",
                      "Scroll speed multiplier: arrows travel this many times faster and are spaced "
                      "further apart; timing against the music is unchanged (0.25 to 8)");
REXCVAR_DEFINE_BOOL(scroll_speed_buttons, true, "BBR",
                    "LB / RB lower / raise the scroll speed during a song");

REX_EXTERN(__imp__sub_82065E70);
REX_EXTERN(__imp__sub_82065958);
REX_EXTERN(__imp__sub_82064C50);
REX_EXTERN(__imp__sub_8206A328);

namespace {
constexpr uint32_t kChartHeader = 0x821A0564;     // float TargetWidth (the hit window)
constexpr uint32_t kHalf = 0x820006E4;            // the constant the game halves it with
constexpr uint32_t kTextManager = 0x821D48C8;     // stored strings, 256 bytes each
constexpr uint32_t kNoticeSlot = 127;             // STR127: "Speed 1.5x"
constexpr uint32_t kMenuSlot = 119;               // STR119: "1.5x" on the song select screen

float LoadFloat(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
void StoreFloat(uint8_t* base, uint32_t address, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(base + address, &v, 4);
}
uint32_t Load32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

struct Note {
  double flight;   // the game's own flight time
  double arrival;  // song time it must arrive
  bool changed;    // we have rewritten it or its arrow at some point
};
std::mutex g_mutex;
std::unordered_map<uint32_t, Note> g_notes;  // by note address
thread_local uint32_t t_new_note = 0;
thread_local bool t_building = false;
std::atomic<bool> g_notice{false};
double g_logged_speed = -1.0;

double Speed() { return std::clamp(REXCVAR_GET(scroll_speed), 0.25, 8.0); }

// The entry for a note, if what the game holds still matches what we recorded (the memory
// may have been freed and reused since).
bool Lookup(uint8_t* base, uint32_t note, Note* out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_notes.find(note);
  if (it == g_notes.end()) return false;
  const double arrival = double(LoadFloat(base, note + 0x1C)) + double(LoadFloat(base, note + 0x38));
  if (!(std::fabs(arrival - it->second.arrival) < 0.003)) {
    g_notes.erase(it);
    return false;
  }
  *out = it->second;
  return true;
}
void MarkChanged(uint32_t note) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_notes.find(note);
  if (it != g_notes.end()) it->second.changed = true;
}

// LB / RB: one step down / up through the settings screen's list of speeds. Returns
// whether a button was pressed.
bool TakeSteps() {
  const int steps = bbr::TakeShoulderSteps();
  if (steps == 0 || !REXCVAR_GET(scroll_speed_buttons)) return false;
  const int count = std::atoi(bbr::SettingsCommand("bbrr.choices", "scroll_speed", "").c_str());
  const int now = std::atoi(bbr::SettingsCommand("bbrr.get", "scroll_speed", "").c_str());
  if (count <= 0 || now <= 0) return false;
  const int wanted = std::clamp(now + steps, 1, count);
  if (wanted != now) {
    bbr::SettingsCommand("bbrr.set", "scroll_speed", std::to_string(wanted));
    bbr::SettingsCommand("bbrr.save", "", "");
    REXLOG_INFO("[modifiers] scroll speed set to {:.2f}x with {}", Speed(), steps > 0 ? "RB" : "LB");
  }
  return true;
}

// During a song: step, and show the new speed for a moment.
void HandleButtons(uint8_t* base) {
  if (!TakeSteps()) return;
  char text[32];
  std::snprintf(text, sizeof(text), "Speed %.4gx", Speed());
  std::memcpy(base + kTextManager + kNoticeSlot * 256, text, std::strlen(text) + 1);
  g_notice.store(true);
}
}  // namespace

std::string bbr::SpeedCommand(const std::string& command) {
  if (command == "bbrr.speed.poll") return g_notice.exchange(false) ? "1" : "";
  if (command == "bbrr.speed.step") {
    // Song select screen: LB / RB step the speed; the value to show goes into STR119.
    TakeSteps();
    uint8_t* base = bbr::GuestBase();
    if (!base) return "";
    char text[32];
    std::snprintf(text, sizeof(text), "%.4gx", Speed());
    char* slot = reinterpret_cast<char*>(base + kTextManager + kMenuSlot * 256);
    const bool changed = std::strcmp(slot, text) != 0;
    if (changed) std::memcpy(slot, text, std::strlen(text) + 1);
    return changed ? "1" : "0";
  }
  return "";
}

// Note constructor: remember which note sub_82065E70 is building.
REX_HOOK_RAW(sub_82065958) {
  if (t_building) t_new_note = ctx.r3.u32;
  __imp__sub_82065958(ctx, base);
}

REX_HOOK_RAW(sub_82065E70) {
  const double flight = ctx.f1.f64;
  const double arrival = ctx.f2.f64;
  t_building = true;
  t_new_note = 0;
  __imp__sub_82065E70(ctx, base);
  t_building = false;
  if (t_new_note < 0x10000 || !(flight > 0.001) || !std::isfinite(arrival)) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_notes.size() > 20000) g_notes.clear();  // left over from many songs ago
  g_notes[t_new_note] = {flight, arrival, false};
}

// A note's per-frame check, before it is launched.
REX_HOOK_RAW(sub_82064C50) {
  HandleButtons(base);
  const uint32_t note = ctx.r3.u32;
  const double speed = Speed();
  Note entry;
  if (note >= 0x10000 && base[note + 4] == 0 && Lookup(base, note, &entry) &&
      (entry.changed || std::fabs(speed - 1.0) > 1e-6)) {
    if (speed != g_logged_speed) {
      g_logged_speed = speed;
      REXLOG_INFO("[modifiers] scroll speed {:.2f}x: flight time {:.3f} s -> {:.3f} s", speed, entry.flight,
                  entry.flight / speed);
    }
    const double now = ctx.f1.f64;
    double flight = entry.flight / speed;
    const double left = entry.arrival - now;
    if (left > 0.02) {
      if (left < flight) {
        // Its launch time has come (or, after slowing down, already passed): launch in
        // this very call. The game launches when the song time is *past* +0x1C, so the
        // launch time has to be a little before now - writing "now" itself would put it
        // off again every frame. The arrow is then put where it belongs along the path
        // by the icon hook below.
        StoreFloat(base, note + 0x38, float(left));
        StoreFloat(base, note + 0x1C, float(now - 0.001));
        MarkChanged(note);
      } else if (std::fabs(flight - double(LoadFloat(base, note + 0x38))) > 1e-4) {
        StoreFloat(base, note + 0x38, float(flight));
        StoreFloat(base, note + 0x1C, float(entry.arrival - flight));
        MarkChanged(note);
      }
    }
  }
  __imp__sub_82064C50(ctx, base);
}

// An arrow icon is about to be placed for this frame.
REX_HOOK_RAW(sub_8206A328) {
  const uint32_t icon = ctx.r3.u32;
  if (icon >= 0x10000) {
    const uint32_t rocket = Load32(base, icon);
    const uint32_t note = Load32(base, icon + 4);
    const double speed = Speed();
    Note entry;
    if (rocket >= 0x10000 && note >= 0x10000 && base[rocket] <= 2 && Lookup(base, note, &entry) &&
        (entry.changed || std::fabs(speed - 1.0) > 1e-6)) {
      const double flight = entry.flight / speed;
      const double window = double(LoadFloat(base, kChartHeader)) * double(LoadFloat(base, kHalf));
      const double left = double(LoadFloat(base, rocket + 0x40)) - window;  // until it arrives
      const double flown = std::max(0.0, flight - left);
      if (std::isfinite(flown) && flight > 0.001) {
        if (std::fabs(flight - double(LoadFloat(base, icon + 0x38))) > 1e-4 ||
            std::fabs(flown - double(LoadFloat(base, rocket + 0x3C))) > 1e-3) {
          StoreFloat(base, icon + 0x38, float(flight));
          StoreFloat(base, rocket + 0x3C, float(flown));
          MarkChanged(note);
        }
      }
    }
  }
  __imp__sub_8206A328(ctx, base);
}
