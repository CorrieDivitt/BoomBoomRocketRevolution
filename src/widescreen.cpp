// Widescreen / ultrawide support, step 1: correct 3D aspect ratio (R-402).
//
// The game renders a fixed 1280x720 image. Its cameras store {fov, aspect} as the first
// two floats (see docs/ANALYSIS.md); the init code (sub_8207ECD8) picks 16:9 or 4:3
// from XGetVideoMode. For other aspect ratios we keep the 1280x720 target, tell the
// cameras the real aspect ratio, and let the presenter stretch the image to the window
// ("anamorphic"), so the 3D scene gains extra view at the sides instead of black bars.
// Use --draw_resolution_scale_x/_y to restore horizontal sharpness.
//
// Step 2: the 2D UI renderer is authored at 640x480 and, for 16:9, scales x by 0.75
// and offsets it by 106.67 to centre it. BbrUiTransformHook (a mid-function hook, see
// config/default.toml) recomputes that scale and offset for the real aspect ratio,
// which cancels the stretch and keeps the 2D layer centred at its native shape.
//
// Step 3: quads that covered the whole 16:9 screen (menu dimmers, fades) would now end
// at the old 16:9 border, so BbrUiQuadHook pushes their side edges out to the real
// screen edges just before the quad is copied to the vertex buffer.

#include "widescreen.h"
#include "midasm_hooks.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
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
std::atomic<uint8_t*> g_base{nullptr};  // guest memory base, for the mid-asm hooks

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
  g_base.store(base, std::memory_order_relaxed);
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

// Mid-asm hook in the UI renderer, see src/midasm_hooks.h.
void BbrUiTransformHook(PPCRegister& f31, PPCRegister& f30, PPCRegister& r9) {
  if (r9.u32 == 0 || !bbr::WidescreenRequested()) return;  // 4:3 path or original 16:9
  double aspect;
  {
    std::lock_guard<std::mutex> lock(bbr::g_mutex);
    aspect = bbr::TargetAspect();
  }
  f31.f64 = (f31.f64 / 0.75) * (4.0 / 3.0) / aspect;  // 640-wide design -> 480*aspect wide
  f30.f64 = (480.0 * aspect - 640.0) / 2.0;            // centre it
}

namespace {
double UiAspect() {
  std::lock_guard<std::mutex> lock(bbr::g_mutex);
  return bbr::TargetAspect();
}
bool Near(double a, double b) { return std::fabs(a - b) < 1e-5; }
}  // namespace

// vScale.x: the 16:9 value is (2/640) * 0.75. Clip-space x = x * scale + offset.
void BbrUiScaleXHook(PPCRegister& f0) {
  if (!bbr::WidescreenRequested() || !Near(f0.f64, 1.5 / 640.0)) return;
  f0.f64 = double(float((2.0 / 640.0) * (4.0 / 3.0) / UiAspect()));
}

// vOffset.x: the 16:9 value is -0.75 (so x = 320 lands on the screen centre).
void BbrUiOffsetXHook(PPCRegister& f0) {
  if (!bbr::WidescreenRequested() || !Near(f0.f64, -0.75)) return;
  f0.f64 = double(float(-(4.0 / 3.0) / UiAspect()));
}

// UI quad about to be drawn: r26 = element, which starts with 4 vertices of 0x20 bytes,
// each beginning with x, y in 640x480 design units (the 16:9 screen is x = -106.67..746.67).
// A quad that reaches both side edges of the 16:9 screen is a full-screen cover, so move
// those edges to the real screen edges (320 -/+ 240 * aspect). Setting (not adding) the
// value keeps this safe to repeat on elements that live for more than one frame.
void BbrUiQuadHook(PPCRegister& r26) {
  if (!bbr::WidescreenRequested()) return;
  uint8_t* base = bbr::g_base.load(std::memory_order_relaxed);
  if (!base || r26.u32 == 0) return;
  constexpr float kLeft = -100.0f, kRight = 740.0f;  // just inside the 16:9 edges
  float x[4];
  bool left = false, right = false;
  for (int i = 0; i < 4; ++i) {
    x[i] = bbr::LoadF(base, r26.u32 + i * 0x20);
    left |= x[i] <= kLeft;
    right |= x[i] >= kRight;
  }
  if (!left || !right) return;
  const double aspect = UiAspect();
  if (aspect <= 16.0 / 9.0 + 0.01) return;
  const float new_left = float(320.0 - 240.0 * aspect), new_right = float(320.0 + 240.0 * aspect);
  for (int i = 0; i < 4; ++i) {
    if (x[i] <= kLeft) bbr::StoreF(base, r26.u32 + i * 0x20, new_left);
    else if (x[i] >= kRight) bbr::StoreF(base, r26.u32 + i * 0x20, new_right);
  }
}
