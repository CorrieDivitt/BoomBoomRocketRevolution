// boomboomrocket - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

#include "dlc.h"
#include "frame_stats.h"
#include "settings.h"
#include "widescreen.h"
#include <rex/cvar.h>
#include <rex/filesystem.h>

#include <filesystem>

class BoomBoomRocketApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<BoomBoomRocketApp>(new BoomBoomRocketApp(ctx, "boomboomrocket",
        PPCImageConfig));
  }

  // Alt+F4 / close button: the SDK's shutdown can hang and leave the process behind.
  bool OnWindowCloseRequested() override {
    bbr::ExitSoon();
    return true;
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // Use the Xenos GPU emulation plugin (rexgpu-xenos.dll, staged next to the exe
  // by CMake) unless --gpu_plugin says otherwise. Without it there is no GPU and
  // the game crashes during D3D setup.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
    // Full game, not the XBLA trial: the game checks bit 0 of XamContentGetLicenseMask.
    // Only applied when license_mask is still at its default 0, so --license_mask=0
    // on the command line can't be told apart - use --license_mask=2 to test trial mode.
    if (rex::cvar::GetFlagByName("license_mask") == "0")
      rex::cvar::SetFlagByName("license_mask", "1");
    // Saved settings from the in-game Revolution settings screen (command line wins).
    bbr::LoadSettings();
    // Non-16:9 aspect ratios: stretch the 1280x720 image over the whole window
    // (the cameras are given the real aspect ratio in src/widescreen.cpp).
    if (bbr::WidescreenRequested()) {
      rex::cvar::SetFlagByName("present_letterbox", "false");
      rex::cvar::SetFlagByName("present_safe_area_x", "100");
      rex::cvar::SetFlagByName("present_safe_area_y", "100");
    }
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // Feed the F3 debug overlay ("Guest: N FPS") from our per-frame hook.
  void OnPostSetup() override {
    // Install the user's own DLC package files (see src/dlc.cpp) before the game looks for them.
    bbr::InstallDlcPackages(runtime());
    SetGuestFrameStats(bbr::GetFrameStats);
    bbr::SetWindowAspectProvider([this]() -> double {
      auto* w = window();
      if (!w) return 0.0;
      const uint32_t width = w->GetActualPhysicalWidth(), height = w->GetActualPhysicalHeight();
      return height ? double(width) / double(height) : 0.0;
    });
  }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}

  // If --game_data_root isn't given, look for the game files ourselves: an
  // "assets" or "Game Files" folder (containing default.xex) next to the exe
  // or in any parent folder - so running from out/build/<preset> finds the repo's.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::path dir = rex::filesystem::GetExecutableFolder(); !dir.empty();
         dir = dir.parent_path()) {
      for (const char* name : {"assets", "Game Files"}) {
        if (fs::exists(dir / name / "default.xex", ec)) {
          paths.game_data_root = dir / name;
          return;
        }
      }
      if (dir == dir.parent_path()) break;
    }
  }
};
