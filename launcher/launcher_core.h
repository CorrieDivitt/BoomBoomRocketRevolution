// Boom Boom Rocket Revolution launcher: the parts that do not draw anything.
//
// Checks the player's own copy of the game (the unpacked folder with default.xex, or the
// original Xbox Live Arcade package), checks DLC packages, installs both next to the
// launcher, and reads and writes the game's settings file. Plain C++20, so it can be tested
// on any system; launcher/main.cpp is the Windows window around it.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace bbrr {
namespace fs = std::filesystem;

constexpr uint32_t kTitleId = 0x5841086A;      // Boom Boom Rocket
constexpr uint32_t kTestTitleId = 0x58410859;  // the developers' internal test packs
constexpr uint32_t kContentArcade = 0x000D0000;
constexpr uint32_t kContentMarketplace = 0x00000002;

// The folder names the game looks for next to its exe (src/boomboomrocket_app.h, src/dlc.h).
inline constexpr const char* kGameFolder = "Game Files";
inline constexpr const char* kDlcFolder = "DLC";
inline constexpr const char* kGameExe = "boomboomrocket.exe";

// ---------------------------------------------------------------------------------------
// SHA-256
class Sha256 {
 public:
  Sha256();
  void Update(const void* data, size_t size);
  std::string HexDigest();  // finishes; call once

 private:
  void Block(const uint8_t* p);
  uint32_t h_[8];
  uint8_t buffer_[64];
  size_t buffered_ = 0;
  uint64_t length_ = 0;
};
std::string Sha256Hex(const void* data, size_t size);

// Progress of a long job, shared with the window.
struct Progress {
  std::atomic<uint64_t> done{0}, total{0};
  std::atomic<bool> cancel{false};
  void SetStage(const std::string& stage);
  std::string Stage();

 private:
  std::mutex mutex_;
  std::string stage_;
};

// ---------------------------------------------------------------------------------------
// The game files as the port was tested with them (launcher/game_files.inc).
struct ManifestFile {
  const char* path;  // relative, '/' between folders
  uint64_t size;
  const char* sha256;
};
const std::vector<ManifestFile>& GameManifest();
const ManifestFile* ManifestXex();

struct XexInfo {
  bool ok = false;
  uint32_t title_id = 0, version = 0, base_version = 0, media_id = 0;
  std::string error;
};
XexInfo ReadXexInfo(const uint8_t* data, size_t size);

// ---------------------------------------------------------------------------------------
// An Xbox 360 content package (STFS: "LIVE", "PIRS" or "CON ").
class Package {
 public:
  struct Entry {
    std::string path;  // full path inside the package, '/' between folders
    bool directory = false, consecutive = false;
    uint32_t blocks = 0, start = 0, size = 0;
    uint16_t parent = 0xFFFF;
  };

  bool Open(const fs::path& path, std::string* error);       // reads the header only
  bool ReadAll(std::string* error);                           // whole file, then the file table
  bool Extract(const Entry& entry, std::vector<uint8_t>* out, std::string* error) const;
  const Entry* Find(const std::string& path) const;           // ignores case

  std::string magic;
  uint32_t content_type = 0, title_id = 0;
  std::string display_name;  // UTF-8
  std::vector<Entry> entries;

 private:
  bool ReadFileTable(std::string* error);
  uint64_t BlockOffset(uint32_t block) const;
  uint64_t HashOffset(uint32_t block) const;
  bool NextBlock(uint32_t block, uint32_t* next) const;

  fs::path path_;
  std::vector<uint8_t> data_;
  uint32_t header_size_ = 0, base_ = 0, file_table_block_ = 0;
  uint16_t file_table_blocks_ = 0;
  int shift_ = 0;
};

// ---------------------------------------------------------------------------------------
// The player's copy of the game.
enum class SourceKind { kNone, kFolder, kPackage };

struct GameSource {
  SourceKind kind = SourceKind::kNone;
  fs::path chosen;   // what the player picked
  fs::path folder;   // the unpacked game (kFolder)
  bool ok = false;             // can be installed
  bool known_version = false;  // default.xex is the version the port was made from
  uint32_t xex_version = 0;
  std::string summary;                // one line for the window
  std::vector<std::string> warnings;  // installable, but worth knowing
  std::vector<std::string> errors;    // why it cannot be installed
  uint64_t bytes = 0;                 // size of what would be installed
};

// Looks at a default.xex (or the folder holding one) or a game package. Hashes every file,
// so run it off the window's thread.
GameSource CheckGameSource(const fs::path& chosen, Progress* progress);

// Puts the game in <install_dir>/Game Files. Copies to a temporary folder first, so a failed
// or cancelled install leaves any earlier one in place.
bool InstallGame(const GameSource& source, const fs::path& install_dir, Progress* progress, std::string* error);

// What is in <install_dir>/Game Files now (quick: checks default.xex and that the files exist).
struct InstalledGame {
  bool present = false;     // default.xex of this game is there
  bool complete = false;    // every file of the tested copy is there
  bool known_version = false;
  std::string summary;
};
InstalledGame CheckInstalledGame(const fs::path& install_dir);

// ---------------------------------------------------------------------------------------
// DLC packages. Anything made for this game is accepted, the unfinished prototype packs too.
struct DlcInfo {
  fs::path path;
  std::string file_name;
  std::string title;      // the name in the package
  bool ok = false;        // a DLC package for this game
  bool prototype = false; // a developers' test pack (the game repairs what it can)
  bool readable = false;  // its file list could be read
  uint64_t size = 0;
  std::string error;
};
DlcInfo CheckDlc(const fs::path& path);
// Files (or folders of files) the player picked or dropped: every package found.
std::vector<fs::path> ExpandDlcPaths(const std::vector<fs::path>& picked);
bool InstallDlc(const DlcInfo& dlc, const fs::path& install_dir, std::string* error);
bool RemoveDlc(const DlcInfo& dlc, std::string* error);
std::vector<DlcInfo> ListInstalledDlc(const fs::path& install_dir);

// ---------------------------------------------------------------------------------------
// Documents\boomboomrocket\settings.ini: one "cvar = value" per line. Lines the launcher does
// not change are kept as they are.
class SettingsFile {
 public:
  static fs::path DefaultPath();
  bool Load(const fs::path& path);  // false if there is no file yet (not an error)
  bool Has(const std::string& cvar) const;
  std::string Get(const std::string& cvar, const std::string& fallback) const;
  void Set(const std::string& cvar, const std::string& value);
  bool Save(const fs::path& path, std::string* error) const;

 private:
  std::vector<std::string> lines_;
};

std::string FormatBytes(uint64_t bytes);

}  // namespace bbrr
