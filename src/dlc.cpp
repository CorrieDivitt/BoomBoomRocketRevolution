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

#include <cstdint>
#include <cstdio>
#include <filesystem>
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
