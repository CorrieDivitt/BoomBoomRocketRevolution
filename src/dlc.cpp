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
  return magic && ReadBE32(header + 0x344) == 2 && ReadBE32(header + 0x360) == title_id;
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
  const std::string& configured = REXCVAR_GET(dlc_path);
  if (!configured.empty()) {
    Collect(fs::path(configured), candidates);
  } else {
    std::error_code ec;
    for (fs::path dir = rex::filesystem::GetExecutableFolder(); !dir.empty(); dir = dir.parent_path()) {
      if (fs::is_directory(dir / "DLC", ec)) {
        Collect(dir / "DLC", candidates);
        break;
      }
      if (dir == dir.parent_path()) break;
    }
  }

  for (const fs::path& package : candidates) {
    if (!IsDlcPackage(package, title_id)) continue;
    const std::string name = package.filename().string();

    rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
    data.device_id = static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
    data.content_type = rex::system::XContentType::kMarketplaceContent;
    data.title_id = title_id;
    data.xuid = 0;
    data.set_file_name(name);
    if (content->ContentExists(0, data)) {
      REXLOG_INFO("[dlc] {} is already installed", name);
      continue;
    }
    const auto result = content->InstallContent(package);
    if (result == 0) {
      REXLOG_INFO("[dlc] installed {}", name);
    } else {
      REXLOG_ERROR("[dlc] could not install {} (error {:#x})", name, uint32_t(result));
    }
  }
}
}  // namespace bbr
