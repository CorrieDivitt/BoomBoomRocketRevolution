// DLC support (R-111): install the user's own DLC package files on startup.
//
// A DLC package is an STFS container ("LIVE" or "PIRS" magic) whose header carries the
// title id at 0x360 and the content type at 0x344 (2 = marketplace content). The SDK's
// content manager can unpack such a file into its content store
// (<user data>/0000000000000000/<title id>/00000002/<file name>/), which is where the
// game's XamContentCreateEnumerator / XamContentCreate calls then find it.

#include "dlc.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xcontent.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

REXCVAR_DEFINE_STRING(dlc_path, "", "BBR",
                      "DLC package file, or a folder of package files, to install on startup "
                      "(default: a folder named DLC next to the game)");

namespace bbr {
namespace {
namespace fs = std::filesystem;

uint32_t ReadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// True if the file is a marketplace-content package for the given title.
bool IsDlcPackage(const fs::path& path, uint32_t title_id) {
  std::FILE* f = nullptr;
#ifdef _WIN32
  _wfopen_s(&f, path.c_str(), L"rb");
#else
  f = std::fopen(path.c_str(), "rb");
#endif
  if (!f) return false;
  uint8_t header[0x364] = {};
  const size_t n = std::fread(header, 1, sizeof(header), f);
  std::fclose(f);
  if (n != sizeof(header)) return false;
  const bool magic = (header[0] == 'L' && header[1] == 'I' && header[2] == 'V' && header[3] == 'E') ||
                     (header[0] == 'P' && header[1] == 'I' && header[2] == 'R' && header[3] == 'S');
  // 0x58410859 is the title id the developers' internal test packages were built with.
  const uint32_t package_title = ReadBE32(header + 0x360);
  return magic && ReadBE32(header + 0x344) == 2 && (package_title == title_id || package_title == 0x58410859);
}

void Collect(const fs::path& where, std::vector<fs::path>& out) {
  std::error_code ec;
  if (fs::is_regular_file(where, ec)) {
    out.push_back(where);
  } else if (fs::is_directory(where, ec)) {
    for (const auto& entry : fs::directory_iterator(where, ec)) {
      if (entry.is_regular_file(ec)) out.push_back(entry.path());
    }
  }
}

// ---------------------------------------------------------------------------------------
// Repairing unfinished packs.
//
// Prototype packs were built before the pack format was final, so the retail game throws
// them out: its loader insists on UnlockFirework* keys in contents.txt, on every listed
// track file being present, and on an Unlock_firework_Icons.tga in the pack. We fix up
// our unpacked copy (never the user's package file); contents.txt.orig keeps the original.
std::string Trim(std::string v) {
  auto space = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!v.empty() && space(v.back())) v.pop_back();
  size_t i = 0;
  while (i < v.size() && space(v[i])) ++i;
  return v.substr(i);
}
std::string Lower(std::string v) {
  for (char& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
  return v;
}

struct Section {
  std::string name;                // "Pack", "Track_1", ... (empty for the text before the first one)
  std::vector<std::string> lines;  // everything after the [name] line
  bool drop = false;

  int Find(const std::string& key) const {
    for (size_t i = 0; i < lines.size(); ++i) {
      const std::string t = Trim(lines[i]);
      if (t.empty() || t[0] == '#') continue;
      const size_t eq = t.find('=');
      if (eq != std::string::npos && Lower(Trim(t.substr(0, eq))) == Lower(key)) return int(i);
    }
    return -1;
  }
  std::string Get(const std::string& key) const {
    const int i = Find(key);
    if (i < 0) return {};
    return Trim(lines[i].substr(lines[i].find('=') + 1));
  }
  void Set(const std::string& key, const std::string& value) {
    const int i = Find(key);
    const std::string line = key + " = " + value;
    if (i >= 0) { lines[i] = line; return; }
    // Insert after the last non-blank line so the section stays tidy.
    size_t at = lines.size();
    while (at > 0 && Trim(lines[at - 1]).empty()) --at;
    lines.insert(lines.begin() + at, line);
  }
};

// File lookup that ignores case, like the console's file system (the packs rely on it).
bool HasFile(const fs::path& dir, const std::string& file) {
  if (file.empty()) return false;
  std::error_code ec;
  const std::string wanted = Lower(file);
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (Lower(entry.path().filename().string()) == wanted) return true;
  }
  return false;
}

bool StartsWith(const std::string& v, const char* prefix) { return v.rfind(prefix, 0) == 0; }

// A blank (fully transparent) 512x256 32-bit TGA, the size the game's icon sheet uses.
void WriteBlankIconSheet(const fs::path& path) {
  std::ofstream out(path, std::ios::binary);
  const uint8_t header[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x02, 0x00, 0x01, 32, 8};
  out.write(reinterpret_cast<const char*>(header), sizeof(header));
  const std::vector<char> row(512 * 4, 0);
  for (int y = 0; y < 256; ++y) out.write(row.data(), std::streamsize(row.size()));
}

void RepairPack(const fs::path& dir, const fs::path& content_type_root, const std::string& name) {
  std::error_code ec;
  const fs::path contents = dir / "contents.txt";
  if (!fs::exists(contents, ec)) return;

  std::string text;
  {
    std::ifstream in(contents, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
  }
  std::vector<Section> sections(1);
  {
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const std::string t = Trim(line);
      if (t.size() > 2 && t.front() == '[' && t.back() == ']') {
        sections.push_back({Trim(t.substr(1, t.size() - 2)), {}, false});
      } else {
        sections.back().lines.push_back(line);
      }
    }
  }
  std::vector<std::string> notes;

  // Pack header: the unlock-icon cell size is mandatory.
  for (Section& s : sections) {
    if (s.name != "Pack") continue;
    if (s.Find("UnlockFireworkUSize") < 0) { s.Set("UnlockFireworkUSize", "0.125"); notes.push_back("added UnlockFireworkUSize"); }
    if (s.Find("UnlockFireworkVSize") < 0) { s.Set("UnlockFireworkVSize", "0.25"); notes.push_back("added UnlockFireworkVSize"); }
  }

  // Tracks: every listed music file must exist. A missing one is replaced by a music file
  // that is in the pack but not listed (the prototype lists a song that was swapped for
  // another); with nothing to swap in, the track is dropped.
  std::vector<std::string> listed, spare;
  for (const Section& s : sections) if (StartsWith(s.name, "Track_")) listed.push_back(Lower(s.Get("Filename")));
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    const std::string file = entry.path().filename().string();
    if (Lower(entry.path().extension().string()) == ".xma" &&
        std::find(listed.begin(), listed.end(), Lower(file)) == listed.end()) {
      spare.push_back(file);
    }
  }
  std::sort(spare.begin(), spare.end());
  std::vector<std::pair<std::string, std::string>> renamed;  // lower-case old name -> new name ("" = dropped)
  for (Section& s : sections) {
    if (!StartsWith(s.name, "Track_")) continue;
    const std::string file = s.Get("Filename");
    if (!HasFile(dir, file)) {
      if (!spare.empty()) {
        renamed.push_back({Lower(file), spare.front()});
        notes.push_back("track " + file + " is not in the pack, using " + spare.front());
        s.Set("Filename", spare.front());
        spare.erase(spare.begin());
      } else {
        renamed.push_back({Lower(file), ""});
        notes.push_back("dropped track " + file + " (not in the pack)");
        s.drop = true;
        continue;
      }
    }
    for (const char* key : {"UnlockFireworkUCoord_Easy", "UnlockFireworkVCoord_Easy", "UnlockFireworkUCoord_Medium",
                            "UnlockFireworkVCoord_Medium", "UnlockFireworkUCoord_Hard", "UnlockFireworkVCoord_Hard"}) {
      if (s.Find(key) < 0) { s.Set(key, "0.0"); notes.push_back(std::string("added ") + key + " to " + s.name); }
    }
  }

  // Models: follow renamed tracks, drop models whose file or track is gone.
  for (Section& s : sections) {
    if (!StartsWith(s.name, "Model_")) continue;
    const std::string track = Lower(s.Get("HiddenInTrack"));
    for (const auto& [from, to] : renamed) {
      if (track != from) continue;
      if (to.empty()) { s.drop = true; } else { s.Set("HiddenInTrack", to); }
    }
    const std::string file = s.Get("Filename");
    if (!s.drop && !file.empty() && !HasFile(dir, file)) s.drop = true;
    if (s.drop) notes.push_back("dropped model " + file);
  }

  // The unlock-icon sheet is mandatory too: borrow it from another installed pack, or
  // make a blank one.
  const fs::path icons = dir / "Unlock_firework_Icons.tga";
  if (!fs::exists(icons, ec)) {
    bool copied = false;
    for (const auto& other : fs::directory_iterator(content_type_root, ec)) {
      const fs::path candidate = other.path() / "Unlock_firework_Icons.tga";
      if (other.path() != dir && fs::exists(candidate, ec)) {
        copied = fs::copy_file(candidate, icons, ec);
        if (copied) break;
      }
    }
    if (!copied) WriteBlankIconSheet(icons);
    notes.push_back(copied ? "copied Unlock_firework_Icons.tga from another pack" : "created a blank Unlock_firework_Icons.tga");
  }

  bool rewrite = false;
  for (const std::string& n : notes) rewrite |= (n.find("Unlock_firework_Icons") == std::string::npos);
  if (rewrite) {
    // Sections are looked up as Track_1, Track_2, ... so renumber after any drops.
    int track = 0, model = 0;
    std::string out;
    for (Section& s : sections) {
      if (s.drop) continue;
      if (StartsWith(s.name, "Track_")) s.name = "Track_" + std::to_string(++track);
      if (StartsWith(s.name, "Model_")) s.name = "Model_" + std::to_string(++model);
      if (!s.name.empty()) out += "[" + s.name + "]\r\n";
      for (const std::string& line : s.lines) out += line + "\r\n";
    }
    const fs::path backup = dir / "contents.txt.orig";
    if (!fs::exists(backup, ec)) fs::copy_file(contents, backup, ec);
    std::ofstream file(contents, std::ios::binary | std::ios::trunc);
    file.write(out.data(), std::streamsize(out.size()));
  }
  for (const std::string& n : notes) REXLOG_INFO("[dlc] {}: {}", name, n);
}
}  // namespace

void InstallDlcPackages(rex::Runtime* runtime) {
  if (!runtime || !runtime->kernel_state()) return;
  auto* kernel_state = runtime->kernel_state();
  auto* content = kernel_state->content_manager();
  const uint32_t title_id = kernel_state->title_id();
  if (!content || !title_id) return;

  std::vector<fs::path> candidates;
  bool searched = false;
  const std::string& configured = REXCVAR_GET(dlc_path);
  if (!configured.empty()) {
    Collect(fs::path(configured), candidates);
    searched = true;
  } else {
    std::error_code ec;
    for (fs::path dir = rex::filesystem::GetExecutableFolder(); !dir.empty(); dir = dir.parent_path()) {
      if (fs::is_directory(dir / "DLC", ec)) {
        Collect(dir / "DLC", candidates);
        searched = true;
        break;
      }
      if (dir == dir.parent_path()) break;
    }
  }

  // The DLC folder decides what is active: packages found there get installed, and
  // installed packages whose file is no longer there get removed again (only our own
  // unpacked copy in the content store is deleted, never the user's package file).
  std::vector<std::string> wanted;
  const uint32_t device = static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
  const auto type = rex::system::XContentType::kMarketplaceContent;
  for (const fs::path& package : candidates) {
    if (!IsDlcPackage(package, title_id)) continue;
    const std::string name = package.filename().string();
    if (name.size() > 42) {
      REXLOG_WARN("[dlc] skipping '{}': rename it to 42 characters or fewer", name);
      continue;
    }
    wanted.push_back(name);

    rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
    data.device_id = device;
    data.content_type = type;
    data.title_id = title_id;
    data.xuid = 0;
    data.set_file_name(name);
    if (content->ContentExists(0, data)) {
      REXLOG_INFO("[dlc] {} is already installed", name);
    } else {
      const auto result = content->InstallContent(package);
      if (result != 0) {
        REXLOG_ERROR("[dlc] could not install {} (error {:#x})", name, uint32_t(result));
        continue;
      }
      REXLOG_INFO("[dlc] installed {}", name);
    }
    char title_dir[16];
    std::snprintf(title_dir, sizeof(title_dir), "%08X", title_id);
    const fs::path type_root = runtime->user_data_root() / "0000000000000000" / title_dir / "00000002";
    RepairPack(type_root / name, type_root, name);

    // The game only uses a pack whose licence mask is non-zero (XContentCreate's
    // pdwLicenseMask, checked in sub_82070348). Prototype / unlicensed packages carry no
    // licence flags, so mark every package the user supplied as licensed.
    rex::system::xam::XCONTENT_AGGREGATE_DATA stored{};
    if (content->ReadContentHeaderFile(name, 0, title_id, type, stored) != 0) stored = data;
    if (content->WriteContentHeaderFile(0, stored, 1) != 0) {
      REXLOG_WARN("[dlc] could not write the licence for {}", name);
    }
  }

  if (!searched) return;  // no DLC folder at all: leave whatever is installed alone
  for (auto& installed : content->ListContent(device, 0, type, title_id)) {
    const std::string name = installed.file_name();
    bool keep = false;
    for (const std::string& w : wanted) keep |= (w == name);
    if (keep) continue;
    const auto result = content->DeleteContent(0, installed);
    REXLOG_INFO("[dlc] removed {} (its package is no longer in the DLC folder){}", name,
                result == 0 ? "" : " - FAILED");
  }
}
}  // namespace bbr
