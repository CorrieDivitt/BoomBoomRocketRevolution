// Track length and tempo on the song lists (--show_song_length, default on).
//
// The song list is one menu widget drawn by sub_82094D40. For every row it looks up the
// row's text by name (sub_8208D7E0, e.g. "TRK3" = title of track 3), turns it into a
// drawable string (sub_8208EB38) and draws it (sub_820908D8: r3 renderer, r4 colours,
// r5 string, r6 alignment (1 = centred, 2 = ends at the position), f1 size, f2 widest allowed; the position is the
// renderer's current transform, font style at 0x5C(r1)).
// Right after a "TRK<n>" row has been drawn we draw two short texts the same way, a
// little smaller: the track's length starting at the left end of the bar, and at the
// right end its tempo as it will feel at the current scroll speed (the song's BPM times
// --scroll_speed, see src/modifiers.cpp).
//
// Both come from the chart: stored string <n> of the text manager holds the song's file
// name (what System.GetStoredString(n) returns), and <name>_Easy.lvl starts with
// "SongBPM <n>" and "SongLength <seconds>" lines. Songs whose chart is not a loose file
// get neither.

#include "widescreen.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <string>

REXCVAR_DEFINE_BOOL(show_song_length, true, "BBR", "Show each track's length on the song lists");
REXCVAR_DEFINE_DOUBLE(song_length_offset, 204.0, "BBR",
                      "How far from the title's centre the track length starts (to the left) and the "
                      "tempo ends (to the right), in layout units");
REXCVAR_DEFINE_DOUBLE(song_info_scale, 0.82, "BBR", "Size of the track length and tempo, as a part of the title's size");
REXCVAR_DEFINE_DOUBLE(song_info_drop, 0.0, "BBR",
                      "Moves the track length and tempo down (negative: up) within the bar, layout units");

REX_EXTERN(__imp__sub_82094D40);
REX_EXTERN(__imp__sub_8208D7E0);
REX_EXTERN(__imp__sub_8208EB38);
REX_EXTERN(__imp__sub_820908D8);

namespace {
constexpr uint32_t kTextManager = 0x821D48C8;  // 128 stored strings of 256 chars at its start
constexpr uint32_t kRenderer = 0x821DCAC8;
constexpr uint32_t kTransform = 0x10004C;       // renderer: transform used for the next text
constexpr uint32_t kStringFormat = 0x820043D0;  // second argument the menu passes to sub_8208EB38
constexpr uint32_t kLengthSlot = 124;           // stored string that carries the length text
constexpr uint32_t kNameSlot = 125;             // ... and one that holds the name "STR124"

bool g_in_menu = false;
// True while a leaderboard screen is up: its song list shows the plain titles.
std::atomic<bool> g_plain_lists{false};
int g_row_track = -1;  // track number of the row whose title was just looked up

std::filesystem::path ContentFolder() {
  static std::filesystem::path folder = [] {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::path dir = rex::filesystem::GetExecutableFolder(); !dir.empty(); dir = dir.parent_path()) {
      for (const char* name : {"assets", "Game Files"}) {
        if (fs::exists(dir / name / "default.xex", ec)) return dir / name / "Content";
      }
      if (dir == dir.parent_path()) break;
    }
    return fs::path();
  }();
  return folder;
}

std::string Lower(std::string text) {
  for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// Every "<song>_Easy.lvl" we can find as a file: the game's Content folder and anything
// under the user folder (installed packs, if they are unpacked there). Key = song name,
// lower case.
const std::map<std::string, std::filesystem::path>& Charts() {
  static const std::map<std::string, std::filesystem::path> charts = [] {
    namespace fs = std::filesystem;
    std::map<std::string, fs::path> found;
    const std::string ending = "_easy.lvl";
    for (const fs::path& root : {ContentFolder(), rex::filesystem::GetUserFolder() / "boomboomrocket"}) {
      std::error_code ec;
      if (root.empty() || !fs::is_directory(root, ec)) continue;
      for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
           !ec && it != end; it.increment(ec)) {
        const std::string name = Lower(it->path().filename().string());
        if (name.size() > ending.size() && name.compare(name.size() - ending.size(), ending.size(), ending) == 0) {
          found.emplace(name.substr(0, name.size() - ending.size()), it->path());
        }
      }
    }
    REXLOG_INFO("[songs] found {} chart files to read track lengths from", found.size());
    return found;
  }();
  return charts;
}

struct SongInfo {
  std::string length;  // "3:05", "" when unknown
  double bpm = 0.0;    // 0 when unknown
};

// For a song file name as the game stores it ("Carmen_Overture.xma").
const SongInfo& InfoFor(const std::string& stored) {
  static std::map<std::string, SongInfo> cache;
  const auto it = cache.find(stored);
  if (it != cache.end()) return it->second;
  std::string song = Lower(stored);
  const size_t dot = song.rfind('.');
  if (dot != std::string::npos) song.resize(dot);
  SongInfo info;
  const auto chart = Charts().find(song);
  if (chart != Charts().end()) {
    std::ifstream in(chart->second);
    std::string line;
    for (int i = 0; i < 12 && std::getline(in, line); ++i) {
      if (line.rfind("SongBPM", 0) == 0) {
        const double bpm = std::strtod(line.c_str() + 7, nullptr);
        if (bpm > 0) info.bpm = bpm;
      } else if (line.rfind("SongLength", 0) == 0) {
        const double seconds = std::strtod(line.c_str() + 10, nullptr);
        if (seconds > 0) {
          const int total = int(seconds + 0.5);
          char buffer[16];
          std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
          info.length = buffer;
        }
      }
    }
  }
  REXLOG_INFO("[songs] '{}': length {}, {} BPM", stored, info.length.empty() ? "unknown" : info.length, info.bpm);
  return cache[stored] = info;
}

float LoadFloat(const uint8_t* at) {
  uint32_t bits;
  std::memcpy(&bits, at, 4);
  bits = __builtin_bswap32(bits);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
void StoreFloat(uint8_t* at, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, 4);
  bits = __builtin_bswap32(bits);
  std::memcpy(at, &bits, 4);
}

// What the title's draw call was given, to repeat it for the extra texts.
struct TitleCall {
  PPCRegister r4, r7, r8, r9, r10, f1, f2, f3;
};

// Draws `text` like the title, `shift` layout units to the side of the title's centre:
// alignment 0 = starts there, 2 = ends there.
void DrawBeside(PPCContext& ctx, uint8_t* base, const TitleCall& title, const std::string& text, uint32_t alignment,
                float shift) {
  if (text.empty()) return;
  std::memcpy(base + kTextManager + kLengthSlot * 256, text.c_str(), text.size() + 1);
  char name[16];
  std::snprintf(name, sizeof(name), "STR%u", kLengthSlot);
  std::memcpy(base + kTextManager + kNameSlot * 256, name, std::strlen(name) + 1);

  ctx.r3.u64 = kTextManager;
  ctx.r4.u64 = kTextManager + kNameSlot * 256;
  __imp__sub_8208D7E0(ctx, base);  // -> the text as the game's wide string
  ctx.r5.u64 = ctx.r3.u32;
  ctx.r4.u64 = kStringFormat;
  ctx.r3.u64 = kRenderer;
  __imp__sub_8208EB38(ctx, base);  // -> drawable string
  ctx.r5.u64 = ctx.r3.u32;
  ctx.r3.u64 = kRenderer;
  ctx.r4 = title.r4;
  ctx.r6.u64 = alignment;
  ctx.r7 = title.r7;
  ctx.r8 = title.r8;
  ctx.r9 = title.r9;
  ctx.r10 = title.r10;
  ctx.f1.f64 = title.f1.f64 * std::clamp(REXCVAR_GET(song_info_scale), 0.3, 1.5);
  ctx.f2 = title.f2;
  ctx.f3 = title.f3;
  // The text is placed by the renderer's current transform (a 4x4 matrix whose
  // translation is its fourth row); move it for this one draw.
  uint8_t* x_slot = base + kRenderer + kTransform + 0x30;
  uint8_t* y_slot = x_slot + 4;
  const float x = LoadFloat(x_slot), y = LoadFloat(y_slot);
  StoreFloat(x_slot, x + shift);
  StoreFloat(y_slot, y + float(REXCVAR_GET(song_info_drop)));
  __imp__sub_820908D8(ctx, base);
  StoreFloat(x_slot, x);
  StoreFloat(y_slot, y);
}
}  // namespace

// Called with the name of every UI script the game loads. The leaderboard screens (Main\Lb...,
// Main\Leaderboards) list songs too, but a length and tempo beside a score table is clutter.
// Only a screen of the Main or Ingame group changes the answer: helper scripts and popups
// load while a screen is up.
void bbr::SongLengthNoteScreen(const char* name) {
  if (!name) return;
  const bool is_main = std::strncmp(name, "Main\\", 5) == 0 || std::strncmp(name, "Main/", 5) == 0;
  const bool ingame = std::strncmp(name, "Ingame", 6) == 0;
  if (!is_main && !ingame) return;
  const bool leaderboard = is_main && (std::strncmp(name + 5, "Lb", 2) == 0 || std::strncmp(name + 5, "Leaderboards", 12) == 0);
  g_plain_lists.store(leaderboard, std::memory_order_relaxed);
}

REX_HOOK_RAW(sub_82094D40) {
  g_in_menu = true;
  g_row_track = -1;
  __imp__sub_82094D40(ctx, base);
  g_in_menu = false;
}

REX_HOOK_RAW(sub_8208D7E0) {
  if (g_in_menu && ctx.r3.u32 == kTextManager && ctx.r4.u32 >= 0x10000) {
    const char* name = reinterpret_cast<const char*>(base + ctx.r4.u32);
    g_row_track = -1;
    if (std::strncmp(name, "TRK", 3) == 0 && std::isdigit(static_cast<unsigned char>(name[3]))) {
      const int track = std::atoi(name + 3);
      if (track >= 0 && track < 120) g_row_track = track;
    }
  }
  __imp__sub_8208D7E0(ctx, base);
}

REX_HOOK_RAW(sub_820908D8) {
  const int track = g_row_track;
  g_row_track = -1;
  if (!g_in_menu || track < 0 || !REXCVAR_GET(show_song_length) || g_plain_lists.load(std::memory_order_relaxed) ||
      ctx.r3.u32 != kRenderer || ctx.r6.u32 != 1) {
    __imp__sub_820908D8(ctx, base);
    return;
  }
  const TitleCall title{ctx.r4, ctx.r7, ctx.r8, ctx.r9, ctx.r10, ctx.f1, ctx.f2, ctx.f3};
  __imp__sub_820908D8(ctx, base);

  char song[64] = {};
  std::memcpy(song, base + kTextManager + uint32_t(track) * 256, sizeof(song) - 1);
  const SongInfo& info = InfoFor(song);
  const float offset = float(REXCVAR_GET(song_length_offset));
  DrawBeside(ctx, base, title, info.length, 0, -offset);
  if (info.bpm > 0) {
    const double speed = std::clamp(std::strtod(rex::cvar::GetFlagByName("scroll_speed").c_str(), nullptr), 0.25, 8.0);
    char bpm[16];
    std::snprintf(bpm, sizeof(bpm), "%d", int(info.bpm * speed + 0.5));
    DrawBeside(ctx, base, title, bpm, 2, offset);
  }
}
