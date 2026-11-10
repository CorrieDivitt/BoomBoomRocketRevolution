// Widescreen / ultrawide support, step 1: correct 3D aspect ratio (R-402).
//
// The game renders a fixed 1280x720 image. Its cameras store {fov, aspect} as the first
// two floats (see docs/ANALYSIS.md); the init code (sub_8207ECD8) picks 16:9 or 4:3
// from XGetVideoMode. For other aspect ratios we keep the 1280x720 target, tell the
// cameras the real aspect ratio, and let the presenter stretch the image to the window
// ("anamorphic"), so the 3D scene gains extra view at the sides instead of black bars.
// Use --draw_resolution_scale_x/_y to restore horizontal sharpness.
//
// Known limitation of this step: 2D elements (menus, HUD) are stretched horizontally.

#include "widescreen.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

REXCVAR_DEFINE_STRING(aspect_ratio, "16:9", "BBR",
                      "3D aspect ratio: 16:9 (original), auto (match window), 21:9, 32:9, 16:10 "
                      "or a number such as 2.39");

REX_EXTERN(__imp__sub_8207ECD8);

namespace bbr {
namespace {
constexpr uint32_t kDataStart = 0x821A0000, kDataEnd = 0x822EE000;  // .data (holds the game object)
constexpr uint32_t kMainCameraAspect = 0x821C1420 + 0x104B4;        // game object camera, +4 = aspect
constexpr float kOriginalAspect = 16.0f / 9.0f;

std::function<double()> g_window_aspect;
std::mutex g_mutex;
std::vector<uint32_t> g_patched;  // guest addresses where we replaced a 16:9 aspect value
float g_written = 0.0f;
uint32_t g_frame = 0;
double g_logged = 0.0;

float LoadF(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
void StoreF(uint8_t* base, uint32_t addr, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(base + addr, &v, 4);
}

double TargetAspect() {
  const std::string& s = REXCVAR_GET(aspect_ratio);
  double value = 0.0;
  if (s == "auto") {
    value = g_window_aspect ? g_window_aspect() : 0.0;
  } else if (auto colon = s.find(':'); colon != std::string::npos) {
    const double w = std::atof(s.substr(0, colon).c_str()), h = std::atof(s.substr(colon + 1).c_str());
    value = h > 0 ? w / h : 0.0;
  } else {
    value = std::atof(s.c_str());
  }
  if (!(value >= 1.0 && value <= 6.0)) return kOriginalAspect;  // unknown / minimised window
  return value;
}
}  // namespace

void SetWindowAspectProvider(std::function<double()> provider) { g_window_aspect = std::move(provider); }

bool WidescreenRequested() {
  const std::string& s = REXCVAR_GET(aspect_ratio);
  return !(s.empty() || s == "16:9");
}

void ApplyAspect(uint8_t* base) {
  if (!WidescreenRequested()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  const float target = float(TargetAspect());

  // Cameras copy / initialise their aspect at different times, so look for the exact
  // 16:9 value in the game's data every half second or so and remember where it was.
  if (g_frame++ % 30 == 0) {
    for (uint32_t addr = kDataStart; addr < kDataEnd; addr += 4) {
      if (LoadF(base, addr) == kOriginalAspect &&
          std::find(g_patched.begin(), g_patched.end(), addr) == g_patched.end()) {
        g_patched.push_back(addr);
        REXLOG_INFO("[widescreen] camera aspect found at {:08X}", addr);
      }
    }
    if (std::find(g_patched.begin(), g_patched.end(), kMainCameraAspect) == g_patched.end()) {
      const float current = LoadF(base, kMainCameraAspect);
      if (current > 1.2f && current < 4.0f) g_patched.push_back(kMainCameraAspect);
    }
  }
  for (uint32_t addr : g_patched) {
    const float current = LoadF(base, addr);
    // Only touch values that are still the original or something we wrote earlier.
    if (current == kOriginalAspect || current == g_written) StoreF(base, addr, target);
  }
  g_written = target;
  if (std::fabs(target - g_logged) > 0.001) {
    g_logged = target;
    REXLOG_INFO("[widescreen] 3D aspect ratio set to {:.3f} ({} locations)", target, g_patched.size());
  }
}
}  // namespace bbr

// Game/camera initialisation: apply straight away so the first frames are right too.
REX_HOOK_RAW(sub_8207ECD8) {
  __imp__sub_8207ECD8(ctx, base);
  bbr::ApplyAspect(base);
}
