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
//
// The first notes of a song: one whose launch would fall before the song starts is built
// by the game with a shortened flight and a starting point part of the way up its path
// (0x820687C0), so that it is already in the air when the song begins. At a higher speed
// such a note often has time for its whole path, so the full path is put back first: the
// starting point and flight time are taken from a full-length note on the same lane (same
// end, and a path that runs through the shortened note's starting point). If even the faster flight does not fit before the song starts, the
// arrow still begins part of the way up - there is nothing earlier to show.

#include "input_offset.h"
#include "autoplay.h"
#include "modifiers.h"
#include "settings.h"
#include "widescreen.h"
#include "midasm_hooks.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

REXCVAR_DEFINE_DOUBLE(scroll_speed, 1.0, "BBR",
                      "Scroll speed multiplier: arrows travel this many times faster and are spaced "
                      "further apart; timing against the music is unchanged (0.25 to 8)");
REXCVAR_DEFINE_BOOL(scroll_speed_buttons, true, "BBR",
                    "LB / RB lower / raise the scroll speed during a song");
REXCVAR_DEFINE_BOOL(rocket_acceleration, false, "BBR",
                    "Rocket Acceleration: false = Default (the game's own movement), true = Static (arrows move at "
                    "one even speed along their path)");
REXCVAR_DEFINE_BOOL(horizontal_drift, true, "BBR",
                    "Arrows drift sideways towards where their firework goes off (the original). Off: they go "
                    "straight up from where they start");
REXCVAR_DEFINE_BOOL(no_background, false, "BBR",
                    "Hide the city during a song: arrows and fireworks on black, for practice without distractions");

#if defined(_WIN32)
// SDK patch 0005 (patches/rexglue-sdk, rex/graphics/hidden_shaders.h).
namespace rex::graphics {
void SetHiddenDraws(const uint64_t* vertex_hashes, const uint64_t* pixel_hashes, size_t count);
void SetHiddenDrawsActive(bool active);
void RequestDrawDump(uint32_t frames);
}  // namespace rex::graphics
#endif

REX_EXTERN(__imp__sub_82065E70);
REX_EXTERN(__imp__sub_82065958);
REX_EXTERN(__imp__sub_82064C50);
REX_EXTERN(__imp__sub_8206A328);

namespace {
constexpr uint32_t kSongStart = 0x821BE9C8;       // float: song time notes are built from
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
  double flight;    // the game's own flight time
  double arrival;   // song time it must arrive
  bool changed;     // we have rewritten it or its arrow at some point
  bool shortened;   // built already part of the way up (see "The first notes" above)
  uint32_t kind;    // note +0x3C
  float start[3];   // note +0x20
  float end[3];     // note +0x2C
};
std::mutex g_mutex;
std::unordered_map<uint32_t, Note> g_notes;  // by note address
thread_local uint32_t t_new_note = 0;
thread_local bool t_building = false;
std::atomic<bool> g_notice{false};
// Flight times of the notes built for the song being played (full-length ones only), for
// the measure lines. A build more than a second after the previous one starts a new song.
std::vector<double> g_song_flights;
std::vector<uint32_t> g_song_notes;  // addresses of the notes built for the song being played
std::chrono::steady_clock::time_point g_last_build{};
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
// The song's usual flight time at 1x (median of its full-length notes); 0 if none built yet.
// Call with g_mutex held.
double BaseFlightLocked() {
  if (g_song_flights.empty()) return 0.0;
  std::vector<double> flights = g_song_flights;
  std::nth_element(flights.begin(), flights.begin() + flights.size() / 2, flights.end());
  return flights[flights.size() / 2];
}


// Gives a shortened first note its whole path back. The game shortens it by sliding the
// starting point in a straight line towards the end (0x820687C0), so any full-length note
// that ends at the same place and whose path runs through this starting point is on the
// same lane: take its starting point and flight time. (An earlier version also asked for
// the same note kind at +0x3C; first notes are often a different kind, so it never matched.)
// Only for a note that has not been launched.
void RestoreFullPath(uint8_t* base, uint32_t note) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_notes.find(note);
  if (it == g_notes.end() || !it->second.shortened) return;
  Note& mine = it->second;
  mine.shortened = false;  // one attempt
  const Note* best = nullptr;
  for (const auto& [address, other] : g_notes) {
    if (other.shortened || address == note || !(other.flight > mine.flight + 0.01)) continue;
    bool same_end = true;
    for (int i = 0; i < 3; ++i) same_end &= std::fabs(other.end[i] - mine.end[i]) < 1e-2f;
    if (!same_end) continue;
    // Does this note's starting point lie on the other's path (end -> start)?
    double full[3], part[3], full_len = 0, part_len = 0, dot = 0;
    for (int i = 0; i < 3; ++i) {
      full[i] = double(other.start[i]) - other.end[i];
      part[i] = double(mine.start[i]) - mine.end[i];
      full_len += full[i] * full[i];
      part_len += part[i] * part[i];
      dot += full[i] * part[i];
    }
    if (full_len < 1e-9 || part_len < 1e-12) continue;
    const double cosine = dot / std::sqrt(full_len * part_len);
    if (cosine < 0.999 || part_len > full_len) continue;
    if (!best || other.flight < best->flight) best = &other;  // the most usual full path
  }
  if (!best) {
    // Notes rarely share an end (each has its own zone and zoom distance). Second way: all
    // rockets start from the launch area, so the full-length notes' starting points share
    // one coordinate (the axis on which they vary least). The full path starts where the
    // line from the end through this note's shortened start reaches that plane, and the
    // song's rockets all fly at one speed (path length / flight time).
    std::vector<const Note*> full;
    for (uint32_t address : g_song_notes) {
      auto o = g_notes.find(address);
      if (o != g_notes.end() && !o->second.shortened && address != note && o->second.flight > 0.05)
        full.push_back(&o->second);
    }
    if (full.size() >= 8) {
      int axis = -1;
      double plane = 0.0, spread_best = 1e30, typical_length = 0.0;
      std::vector<double> speeds;
      for (const Note* n : full) {
        double len = 0;
        for (int i = 0; i < 3; ++i) len += double(n->end[i] - n->start[i]) * double(n->end[i] - n->start[i]);
        len = std::sqrt(len);
        typical_length += len / full.size();
        speeds.push_back(len / n->flight);
      }
      for (int a = 0; a < 3; ++a) {
        std::vector<double> v;
        for (const Note* n : full) v.push_back(n->start[a]);
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        const double median = v[v.size() / 2];
        double spread = 0;
        for (double x : v) spread += std::fabs(x - median) / v.size();
        if (spread < spread_best) spread_best = spread, axis = a, plane = median;
      }
      std::nth_element(speeds.begin(), speeds.begin() + speeds.size() / 2, speeds.end());
      const double speed = speeds[speeds.size() / 2];
      const double from_end = double(mine.start[axis]) - mine.end[axis];
      const double t = std::fabs(from_end) > 1e-6 ? (plane - mine.end[axis]) / from_end : 0.0;
      if (axis >= 0 && spread_best < 0.02 * typical_length && t > 1.0 && t < 50.0 && speed > 1e-6) {
        float start[3];
        double len = 0;
        for (int i = 0; i < 3; ++i) {
          start[i] = float(mine.end[i] + (double(mine.start[i]) - mine.end[i]) * t);
          len += (double(start[i]) - mine.end[i]) * (double(start[i]) - mine.end[i]);
        }
        const double flight = std::sqrt(len) / speed;
        REXLOG_INFO("[modifiers] first note at {:.3f} s: full path rebuilt from the launch plane (axis {}), "
                    "flight {:.3f} s -> {:.3f} s",
                    mine.arrival, axis, mine.flight, flight);
        for (int i = 0; i < 3; ++i) {
          mine.start[i] = start[i];
          StoreFloat(base, note + 0x20 + 4 * i, start[i]);
        }
        mine.flight = flight;
        mine.changed = true;
        return;
      }
      REXLOG_INFO("[modifiers] first note at {:.3f} s (flight {:.3f} s): no full path found (launch plane spread "
                  "{:.3f} of path length {:.3f}, t {:.2f}), left as built",
                  mine.arrival, mine.flight, spread_best, typical_length, t);
      return;
    }
    REXLOG_INFO("[modifiers] first note at {:.3f} s (flight {:.3f} s, kind {:08X}): no full path found, left as built",
                mine.arrival, mine.flight, mine.kind);
    return;
  }
  REXLOG_INFO("[modifiers] first note at {:.3f} s: full path restored, flight {:.3f} s -> {:.3f} s", mine.arrival,
              mine.flight, best->flight);
  for (int i = 0; i < 3; ++i) {
    mine.start[i] = best->start[i];
    StoreFloat(base, note + 0x20 + 4 * i, best->start[i]);
  }
  mine.flight = best->flight;
  mine.changed = true;
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

// No Background (--no_background): while a song is being played (game state 1 playing,
// 4 failing) the draws that make up the city and the sky behind it are skipped. The game
// clears the picture at the start of every frame (constant-colour rectangles, pixel shader
// 2E372EA28CC404B7), so what is left behind the arrows, fireworks and HUD is black. Menus keep
// their city.
//
// Which draws those are was read from a draw list of one frame during a song (2026-11-11), in
// drawing order: clears; sky (0C88/0AA0, one rectangle); distant backdrop (BBFF/9567, one
// rectangle list); the city mesh in two passes (C837/2B08 and 04F2/C9D2, ~175 draws each,
// 600k+ indices) with 6773/88B6 in between; city details (D1ED/238C, ~1000 small draws); then
// sky dome (04F2/B36F, 132 triangles, drawn before and after the city) and water quad
// (B90D/FB9F) - these two added after the first test still showed the sky; then
// fireworks, glow passes, the arrows (436F/9567 - same pixel shader as the backdrop, which is
// why hiding by pixel shader alone took the arrows away) and the HUD. Pairs are
// "vertex:pixel" shader hashes as the GPU emulation hashes the microcode; --no_background_draws
// replaces the list without a rebuild ("0" in either place = any shader). The GPU side can log
// a frame's draws again: the first time No Background is on in a run it logs two frames.
REXCVAR_DEFINE_STRING(no_background_draws,
                      "0C8863F526958F13:0AA01D3D0F524EAA,BBFFC644A2E5B64B:9567C79307ACC6F5,"
                      "C837C4CC9C032EC1:2B0852B5C96C9374,6773CFEB0ADA028C:88B6CCE2F49DB155,"
                      "04F226D9D2055B0F:C9D2A3BDADE621D6,D1EDFDE33AE1C2FF:238C407DFDDF25EC,"
                      "04F226D9D2055B0F:B36FF56DF18B9D4E,B90D917BD878F162:FB9F9B10B8537018",
                      "BBR",
                      "Draws No Background skips: comma-separated vertex:pixel shader hash pairs (hex, 0 = any)");

// A song "is on" from its Get Ready countdown until a screen that ends it, so the city goes
// before the countdown rather than with a jump when the first note starts. Pause, the
// settings pages and popups opened during a song do not end it.
namespace {
std::atomic<bool> g_song_screens{false};
bool StartsWith(const char* name, const char* prefix) { return std::strncmp(name, prefix, std::strlen(prefix)) == 0; }
}  // namespace

void bbr::NoteScreenForNoBackground(const char* name) {
  if (!name) return;
  if (StartsWith(name, "Ingame\\GetReady") || StartsWith(name, "Ingame\\Hud")) {
    g_song_screens.store(true);
  } else if (StartsWith(name, "Ingame\\LevelComplete") || StartsWith(name, "Ingame\\LevelBreakdown") ||
             StartsWith(name, "Ingame\\GameOver") || StartsWith(name, "Ingame\\RetryMenu") ||
             StartsWith(name, "Ingame\\HighScore") || StartsWith(name, "Ingame\\FireworkUnlocked") ||
             StartsWith(name, "Ingame\\PostGame") || StartsWith(name, "Main\\")) {
    g_song_screens.store(false);
  }
}

void bbr::UpdateNoBackground(uint8_t* base) {
#if defined(_WIN32)
  // (Re)read the list whenever it changes.
  static std::string applied = "\x01";
  static size_t pair_count = 0;
  const std::string list = REXCVAR_GET(no_background_draws);
  if (list != applied) {
    applied = list;
    std::vector<uint64_t> vs, ps;
    size_t at = 0;
    while (at <= list.size()) {
      size_t comma = list.find(',', at);
      if (comma == std::string::npos) comma = list.size();
      const std::string item = list.substr(at, comma - at);
      const size_t colon = item.find(':');
      if (colon != std::string::npos) {
        vs.push_back(std::strtoull(item.substr(0, colon).c_str(), nullptr, 16));
        ps.push_back(std::strtoull(item.substr(colon + 1).c_str(), nullptr, 16));
      }
      at = comma + 1;
    }
    rex::graphics::SetHiddenDraws(vs.data(), ps.data(), vs.size());
    pair_count = vs.size();
    REXLOG_INFO("[modifiers] No Background: {} draw kinds to skip", pair_count);
  }
  const uint32_t state = Load32(base, 0x821A0578 + 4);  // 1 playing, 4 failing, 2 game over, 3 finished
  const bool song_on = g_song_screens.load() || state == 1 || state == 4;
  bbr::AutoplayUpdate(base, song_on);
  const bool hide = REXCVAR_GET(no_background) && song_on;
  static bool hiding = false;
  static bool dumped = false;
  static auto hiding_since = std::chrono::steady_clock::now();
  if (hide != hiding) {
    hiding = hide;
    hiding_since = std::chrono::steady_clock::now();
    REXLOG_INFO("[modifiers] No Background: city {}", hide ? "hidden" : "shown");
  }
  // Once per run, a few seconds into a song: log the draws (to check or adjust the list).
  if (hide && !dumped && std::chrono::steady_clock::now() - hiding_since > std::chrono::seconds(4)) {
    dumped = true;
    rex::graphics::RequestDrawDump(2);
  }
  rex::graphics::SetHiddenDrawsActive(hide && pair_count > 0);
#else
  (void)base;
#endif
}

double bbr::TypicalFlight() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return BaseFlightLocked() / Speed();
}

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
  const float song_start = LoadFloat(base, kSongStart);
  t_building = true;
  t_new_note = 0;
  __imp__sub_82065E70(ctx, base);
  t_building = false;
  if (t_new_note < 0x10000 || !(flight > 0.001) || !std::isfinite(arrival)) return;
  Note entry{flight, arrival, false, false, Load32(base, t_new_note + 0x3C), {}, {}};
  for (int i = 0; i < 3; ++i) {
    entry.start[i] = LoadFloat(base, t_new_note + 0x20 + 4 * i);
    entry.end[i] = LoadFloat(base, t_new_note + 0x2C + 4 * i);
  }
  // Launch time equal to the start of the song = the game shortened this one.
  entry.shortened = std::fabs((arrival - flight) - double(song_start)) < 1e-3;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_notes.size() > 20000) g_notes.clear();  // left over from many songs ago
  g_notes[t_new_note] = entry;
  const auto now = std::chrono::steady_clock::now();
  if (now - g_last_build > std::chrono::seconds(1)) g_song_flights.clear(), g_song_notes.clear();
  if (g_song_notes.size() < 8000) g_song_notes.push_back(t_new_note);
  g_last_build = now;
  if (!entry.shortened && g_song_flights.size() < 4000) g_song_flights.push_back(flight);
}

// A note's per-frame check, before it is launched.
REX_HOOK_RAW(sub_82064C50) {
  HandleButtons(base);
  const uint32_t note = ctx.r3.u32;
  const double speed = Speed();
  Note entry;
  if (note >= 0x10000 && base[note + 4] == 0 && Lookup(base, note, &entry) &&
      (entry.changed || std::fabs(speed - 1.0) > 1e-6)) {
    if (entry.shortened) {
      RestoreFullPath(base, note);
      Lookup(base, note, &entry);
    }
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

// Rocket Acceleration (--rocket_acceleration). sub_8206A328 places an arrow at
// start + (end - start) x u, where for the original rocket feel u = (flown / flight)^1.5 across
// (f31) and ((flown / flight)^1.5)^0.7 upwards (f11); both branches meet at 0x8206A418 with
// f30 = flown / flight. With the setting off both become f30: an even pace along the path.
// (The game has a byte it tests to skip the easing, 0x821A01C4, but it is the top byte of a
// value the game uses for something else - it must not be written.)
//
// The settings screen calls the two values Default (the game's own movement) and Static (the
// even pace). Named by what the option does rather than how it looks: on screen the original
// movement can look steady and the even pace like speeding up, because of the climb's perspective.
void BbrArrowEaseHook(PPCRegister& f30, PPCRegister& f31, PPCRegister& f11) {
  if (!REXCVAR_GET(rocket_acceleration)) return;
  f31.f64 = f30.f64;
  f11.f64 = f30.f64;
}

// Horizontal Drift (--horizontal_drift). A rocket flies a straight 3D path from its launch
// point to where its firework goes off, so on screen it drifts sideways as it climbs. With the
// setting off, the rocket keeps the x and z of its firework point and only its height follows
// the path, then is slid along the camera's left-right direction so it stays in the screen
// column it first appeared in, even through camera swings (see BbrArrowDriftHook). The view
// matrix is the game's own (rows at game + 0x103B0..0x103E0); NoteArrowProjection checks
// whether it is applied as p x M or M x p before it is used.
namespace {
constexpr uint32_t kViewMatrix = 0x821C1420 + 0x103B0;
std::atomic<int> g_matrix_rows_ok{0}, g_matrix_cols_ok{0};
std::atomic<int> g_matrix_order{0};  // 0 not known yet, 1 p x M (rows), 2 M x p (columns)

void LoadMatrix(uint8_t* base, float m[4][4]) {
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) m[r][c] = LoadFloat(base, kViewMatrix + r * 16 + c * 4);
}
// Element (i, j) of the matrix as used: clip_j = sum_i p_i * E(i, j), p = (x, y, z, 1).
float E(const float m[4][4], int order, int i, int j) { return order == 1 ? m[i][j] : m[j][i]; }
}  // namespace

void bbr::NoteArrowProjection(const float position[3], const float clip[4]) {
  if (g_matrix_order.load() != 0) return;
  uint8_t* base = bbr::GuestBase();
  if (!base || !(std::fabs(clip[3]) > 1e-4f)) return;
  float m[4][4];
  LoadMatrix(base, m);
  const float p[4] = {position[0], position[1], position[2], 1.0f};
  for (int order = 1; order <= 2; ++order) {
    float x = 0, w = 0;
    for (int i = 0; i < 4; ++i) x += p[i] * E(m, order, i, 0), w += p[i] * E(m, order, i, 3);
    const bool ok = std::fabs(w) > 1e-4f && std::fabs(x / w - clip[0] / clip[3]) < 0.01f;
    if (ok) (order == 1 ? g_matrix_rows_ok : g_matrix_cols_ok).fetch_add(1);
  }
  const int rows = g_matrix_rows_ok.load(), cols = g_matrix_cols_ok.load();
  if (rows + cols >= 30) {
    const int order = rows >= cols ? 1 : 2;
    g_matrix_order.store(order);
    REXLOG_INFO("[modifiers] arrow view matrix applied as {} ({} vs {} matches); Horizontal Drift can work",
                order == 1 ? "p x M" : "M x p", rows, cols);
  }
}

namespace {
struct DriftColumn {
  float start[3];  // the path's start (icon +32), to tell a new flight in the same icon slot
  float column;    // screen x / w where the rocket was first drawn this flight
};
std::mutex g_drift_mutex;
std::unordered_map<uint32_t, DriftColumn> g_drift_columns;  // by arrow icon
}  // namespace

void BbrArrowDriftHook(PPCRegister& r31, PPCRegister& r1) {
  if (REXCVAR_GET(horizontal_drift)) return;
  uint8_t* base = bbr::GuestBase();
  if (!base || r31.u32 < 0x10000) return;
  // 1. In the world: keep the x and z of the firework point (icon +44 / +52); only the height
  //    follows the game's path, so the rocket climbs straight up into the spot it would hit.
  const float ex = LoadFloat(base, r31.u32 + 44), ez = LoadFloat(base, r31.u32 + 52);
  if (!std::isfinite(ex) || !std::isfinite(ez)) return;
  float p[3] = {ex, LoadFloat(base, r1.u32 + 84), ez};
  // 2. On screen: a camera swing would still carry it sideways, so hold the screen column it
  //    was first drawn in by sliding it along the camera's own left-right direction (the
  //    gradient of clip x). Unlike solving for world x, that direction always moves the rocket
  //    across the screen, so the step stays small however the camera turns.
  const int order = g_matrix_order.load();
  if (order != 0) {
    float m[4][4];
    LoadMatrix(base, m);
    auto clip = [&](const float* q, float& x, float& w) {
      x = E(m, order, 3, 0) + q[0] * E(m, order, 0, 0) + q[1] * E(m, order, 1, 0) + q[2] * E(m, order, 2, 0);
      w = E(m, order, 3, 3) + q[0] * E(m, order, 0, 3) + q[1] * E(m, order, 1, 3) + q[2] * E(m, order, 2, 3);
    };
    float cx, cw;
    clip(p, cx, cw);
    if (cw > 1e-4f) {
      const float sx = LoadFloat(base, r31.u32 + 32), sy = LoadFloat(base, r31.u32 + 36), sz = LoadFloat(base, r31.u32 + 40);
      float column;
      {
        std::lock_guard<std::mutex> lock(g_drift_mutex);
        if (g_drift_columns.size() > 4096) g_drift_columns.clear();
        DriftColumn& d = g_drift_columns[r31.u32];
        if (d.start[0] != sx || d.start[1] != sy || d.start[2] != sz)
          d = {{sx, sy, sz}, std::clamp(cx / cw, -0.95f, 0.95f)};
        column = d.column;
      }
      const float r[3] = {E(m, order, 0, 0), E(m, order, 1, 0), E(m, order, 2, 0)};
      const float rr = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
      const float rw = r[0] * E(m, order, 0, 3) + r[1] * E(m, order, 1, 3) + r[2] * E(m, order, 2, 3);
      const float denom = rr - column * rw;
      if (rr > 1e-12f && denom > 0.25f * rr) {
        const float t = (column * cw - cx) / denom;
        float q[3] = {p[0] + t * r[0], p[1] + t * r[1], p[2] + t * r[2]};
        float qx, qw;
        clip(q, qx, qw);
        if (std::isfinite(t) && qw > 1e-4f && std::fabs(qx / qw - column) < 0.05f) {
          p[0] = q[0], p[1] = q[1], p[2] = q[2];
        }
      }
    }
  }
  StoreFloat(base, r1.u32 + 80, p[0]);
  StoreFloat(base, r1.u32 + 84, p[1]);
  StoreFloat(base, r1.u32 + 88, p[2]);
}
