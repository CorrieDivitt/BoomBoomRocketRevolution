// Track length on the song lists (--show_song_length, default on).
//
// The song list is one menu widget drawn by sub_82094D40. For every row it looks up the
// row's text by name (sub_8208D7E0, e.g. "TRK3" = title of track 3), turns it into a
// drawable string (sub_8208EB38) and draws it (sub_820908D8: r3 renderer, r4 colours,
// r5 string, r6 alignment (1 = centred), f1 size, f2 widest allowed; the position is the
// renderer's current transform, font style at 0x5C(r1)).
// Right after a "TRK<n>" row has been drawn we draw a second, short text the same way,
// moved towards the right end of the bar: the track's length.
//
// The length comes from the chart: stored string <n> of the text manager holds the song's
// file name (what System.GetStoredString(n) returns), and <name>_Easy.lvl starts with a
// "SongLength <seconds>" line. Songs whose chart is not a loose file (downloaded packs)
// get no length.

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>

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
REXCVAR_DEFINE_DOUBLE(song_length_offset, 205.0, "BBR",
                      "How far right of the title's centre the track length is drawn (layout units)");

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

// "3:05" for a song file name as the game stores it ("Carmen_Overture.xma"), "" when unknown.
std::string LengthText(const std::string& stored) {
  static std::map<std::string, std::string> cache;
  const auto it = cache.find(stored);
  if (it != cache.end()) return it->second;
  std::string song = Lower(stored);
  const size_t dot = song.rfind('.');
  if (dot != std::string::npos) song.resize(dot);
  std::string text;
  const auto chart = Charts().find(song);
  if (chart != Charts().end()) {
    std::ifstream in(chart->second);
    std::string line;
    for (int i = 0; i < 12 && std::getline(in, line); ++i) {
      if (line.rfind("SongLength", 0) != 0) continue;
      const double seconds = std::strtod(line.c_str() + 10, nullptr);
      if (seconds > 0) {
        const int total = int(seconds + 0.5);
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
        text = buffer;
      }
      break;
    }
  }
  REXLOG_INFO("[songs] length of '{}': {}", stored, text.empty() ? "unknown" : text);
  cache[stored] = text;
  return text;
}
}  // namespace

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
  if (!g_in_menu || track < 0 || !REXCVAR_GET(show_song_length) || ctx.r3.u32 != kRenderer || ctx.r6.u32 != 1) {
    __imp__sub_820908D8(ctx, base);
    return;
  }
  // Keep the arguments of the title's draw call to repeat it for the length.
  const PPCRegister r4 = ctx.r4, r6 = ctx.r6, r7 = ctx.r7, r8 = ctx.r8, r9 = ctx.r9, r10 = ctx.r10;
  const PPCRegister f1 = ctx.f1, f2 = ctx.f2, f3 = ctx.f3;
  __imp__sub_820908D8(ctx, base);

  char song[64] = {};
  std::memcpy(song, base + kTextManager + uint32_t(track) * 256, sizeof(song) - 1);
  const std::string text = LengthText(song);
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
  ctx.r4 = r4;
  ctx.r6 = r6;
  ctx.r7 = r7;
  ctx.r8 = r8;
  ctx.r9 = r9;
  ctx.r10 = r10;
  ctx.f1 = f1;
  ctx.f2 = f2;
  ctx.f3 = f3;
  // The text is placed by the renderer's current transform (a 4x4 matrix whose
  // translation is its fourth row); move it right for this one draw.
  uint8_t* x_slot = base + kRenderer + kTransform + 0x30;
  uint32_t saved;
  std::memcpy(&saved, x_slot, 4);
  uint32_t bits = __builtin_bswap32(saved);
  float x;
  std::memcpy(&x, &bits, 4);
  x += float(REXCVAR_GET(song_length_offset));
  std::memcpy(&bits, &x, 4);
  bits = __builtin_bswap32(bits);
  std::memcpy(x_slot, &bits, 4);
  __imp__sub_820908D8(ctx, base);
  std::memcpy(x_slot, &saved, 4);
}
