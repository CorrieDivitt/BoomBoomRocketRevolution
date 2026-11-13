// Visualizer camera pull-back (hold A).
//
// The Visualizer camera update (sub_82088580) follows GAME:\content\visualiser.spl. Each update
// it takes the point P on the path at the current time and the path's direction, then:
//   - the look-at point, P + direction * d, goes to the camera (game + 0x103FC) at +0x50
//     (game + 0x1044C), with d = 2500 + 1600 z;
//   - the eye, P raised by 500 (the int at 0x821A0A9C), is built at r1+112 (x, y, z) and handed
//     to the camera's position setter sub_82061018 at 0x82088B78.
// RB zooms in by raising z (game + 0x12E60, 0.03 an update up to 1), which narrows the field of
// view: a lens zoom, the camera itself does not move.
//
// Holding A moves the camera itself: at 0x82088B78, just before the eye is handed over, the eye
// is pushed back along the view direction (from the eye towards the look-at point) by up to
// --visualizer_pull_back units and raised by up to --visualizer_pull_back_lift, eased in and
// out at the game's own zoom step. The look-at point stays where it is, so the view keeps
// facing the same fireworks, now with the city in front of them. (For scale: the fireworks
// burst thousands of units from the path; the city lighting logs their centres.)
//
// A is pad byte +132 (control 4 for sub_82063288), which the Visualizer does not read. Pad state: game + 0x12AD4 + 152 * [game + 0x12F08]; sub_82063DE8 fills the button
// bytes from XINPUT. Skipped while paused, like RB.
#include "midasm_hooks.h"
#include "widescreen.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_DOUBLE(visualizer_pull_back, 6000.0, "BBR",
                      "Visualizer: how far holding A moves the camera back, in game units (0 turns it off)");
REXCVAR_DEFINE_DOUBLE(visualizer_pull_back_lift, 1500.0, "BBR",
                      "Visualizer: how far holding A also raises the camera, in game units");

namespace {
constexpr uint32_t kLookAtOffset = 0x1044C;     // camera (game + 0x103FC) + 0x50: x, y, z
constexpr uint32_t kPadIndexOffset = 0x12F08;   // u32: which pad state drives the camera
constexpr uint32_t kPadStatesOffset = 0x12AD4;  // 152 bytes each
constexpr uint32_t kPadStateSize = 152;
constexpr uint32_t kButtonA = 132;              // byte in a pad state, 1 while held
constexpr uint32_t kGameState = 0x821A0578;     // +336 byte and +180 u32: both 0 when not paused
constexpr uint32_t kEyeOnStack = 112;           // r1 + 112: the eye x, y, z
constexpr float kStep = 0.03f;                  // the game's own step for RB

float g_amount = 0.0f;  // 0..1
std::chrono::steady_clock::time_point g_last;

uint32_t LoadU32(const uint8_t* at) {
  uint32_t v;
  std::memcpy(&v, at, 4);
  return __builtin_bswap32(v);
}
float LoadFloat(const uint8_t* at) {
  const uint32_t bits = LoadU32(at);
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
}  // namespace

void BbrVisualizerEyeHook(PPCRegister& r1, PPCRegister& r31) {
  uint8_t* base = bbr::GuestBase();
  const uint32_t game = r31.u32;
  if (!base || game < 0x10000) return;
  const double back = std::clamp(REXCVAR_GET(visualizer_pull_back), 0.0, 100000.0);
  const double lift = std::clamp(REXCVAR_GET(visualizer_pull_back_lift), -20000.0, 20000.0);

  const auto now = std::chrono::steady_clock::now();
  // Back in the Visualizer after a while: start from the game's own view.
  if (now - g_last > std::chrono::milliseconds(500)) g_amount = 0.0f;
  g_last = now;
  if (back <= 0.0 && lift == 0.0) return;

  const bool paused = base[kGameState + 336] != 0 || LoadU32(base + kGameState + 180) != 0;
  if (!paused) {
    const uint32_t pad = LoadU32(base + game + kPadIndexOffset);
    const bool held = pad < 4 && base[game + kPadStatesOffset + pad * kPadStateSize + kButtonA] != 0;
    const float before = g_amount;
    g_amount = held ? std::min(1.0f, g_amount + kStep) : std::max(0.0f, g_amount - kStep);
    if (before == 0.0f && g_amount > 0.0f) {
      REXLOG_INFO("[visualizer] camera pulling back (A): eye ({:.0f} {:.0f} {:.0f}), look-at ({:.0f} {:.0f} {:.0f}), "
                  "back {:.0f}, lift {:.0f}",
                  LoadFloat(base + r1.u32 + kEyeOnStack), LoadFloat(base + r1.u32 + kEyeOnStack + 4),
                  LoadFloat(base + r1.u32 + kEyeOnStack + 8), LoadFloat(base + game + kLookAtOffset),
                  LoadFloat(base + game + kLookAtOffset + 4), LoadFloat(base + game + kLookAtOffset + 8), back,
                  lift);
    }
  }
  if (g_amount <= 0.0f) return;

  // Smooth in and out rather than at a constant speed.
  const double t = double(g_amount) * double(g_amount) * (3.0 - 2.0 * double(g_amount));
  uint8_t* eye = base + r1.u32 + kEyeOnStack;
  const uint8_t* look = base + game + kLookAtOffset;
  double e[3], dir[3], length = 0.0;
  for (int i = 0; i < 3; ++i) {
    e[i] = LoadFloat(eye + 4 * i);
    dir[i] = double(LoadFloat(look + 4 * i)) - e[i];
    length += dir[i] * dir[i];
  }
  length = std::sqrt(length);
  if (!std::isfinite(length) || length < 1.0) return;
  for (int i = 0; i < 3; ++i) e[i] -= dir[i] / length * back * t;
  e[1] += lift * t;
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(e[i])) return;
  }
  for (int i = 0; i < 3; ++i) StoreFloat(eye + 4 * i, float(e[i]));
}
