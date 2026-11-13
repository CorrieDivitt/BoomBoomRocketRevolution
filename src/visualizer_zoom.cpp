// Visualizer camera pull-back (hold A; X goes twice as far).
//
// The Visualizer camera update (sub_82088580) follows GAME:\content\visualiser.spl. Each update
// it takes the point P on the path at the current time and the path's direction, then:
//   - the camera's position, P + direction * d, goes to the camera (game + 0x103FC) at +0x50
//     (game + 0x1044C), with d = 2500 + 1600 z;
//   - the point it looks at, P raised by 500 (the int at 0x821A0A9C), is built at r1+112
//     (x, y, z) and handed to sub_82061018 at 0x82088B78, which turns the camera to face it
//     (forward = normalize(target - camera+0x50)) and builds the view.
// So the camera sits ahead on the path and looks back at it. RB zooms in by raising z (game +
// 0x12E60, 0.03 an update up to 1), which narrows the field of view.
//
// Holding A moves the camera itself: at 0x82088B78, just before the view is built, the camera
// position is pushed further away from the point it looks at by --visualizer_pull_back units
// and raised by --visualizer_pull_back_lift; holding X does the same twice as far. The amount
// glides towards its goal (0, 1 for A, 2 for X; X wins if both are held), so moving between
// A, X and neither is always smooth. The game rewrites the position every update before this
// point, so nothing adds up. The look-at point stays, so the view keeps facing the same spot
// with more of the city and sky around it. (First version, 2026-11-13, moved the look-at point
// instead - it was taken for the eye - and only tilted the view.)
//
// A is pad byte +132 and X +134 (controls 4 and 6 for sub_82063288); the Visualizer reads
// neither. Pad state: game + 0x12AD4 + 152 * [game + 0x12F08]; sub_82063DE8 fills the button
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
constexpr uint32_t kCameraPosOffset = 0x1044C;  // camera (game + 0x103FC) + 0x50: x, y, z
constexpr uint32_t kPadIndexOffset = 0x12F08;   // u32: which pad state drives the camera
constexpr uint32_t kPadStatesOffset = 0x12AD4;  // 152 bytes each
constexpr uint32_t kPadStateSize = 152;
constexpr uint32_t kButtonA = 132;              // bytes in a pad state, 1 while held
constexpr uint32_t kButtonX = 134;
constexpr uint32_t kGameState = 0x821A0578;     // +336 byte and +180 u32: both 0 when not paused
constexpr uint32_t kTargetOnStack = 112;        // r1 + 112: the look-at point x, y, z
constexpr float kGlide = 0.08f;                 // share of the way to the goal covered each update

float g_amount = 0.0f;  // 0..2: multiples of the pull-back
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
    const uint8_t* buttons = base + game + kPadStatesOffset + pad * kPadStateSize;
    const float goal = pad >= 4 ? 0.0f : buttons[kButtonX] ? 2.0f : buttons[kButtonA] ? 1.0f : 0.0f;
    const float before = g_amount;
    g_amount += (goal - g_amount) * kGlide;
    if (std::fabs(goal - g_amount) < 0.002f) g_amount = goal;
    if (before == 0.0f && g_amount > 0.0f) {
      REXLOG_INFO("[visualizer] camera pulling back (A / X): camera ({:.0f} {:.0f} {:.0f}), looking at ({:.0f} {:.0f} "
                  "{:.0f}), back {:.0f}, lift {:.0f}",
                  LoadFloat(base + game + kCameraPosOffset), LoadFloat(base + game + kCameraPosOffset + 4),
                  LoadFloat(base + game + kCameraPosOffset + 8), LoadFloat(base + r1.u32 + kTargetOnStack),
                  LoadFloat(base + r1.u32 + kTargetOnStack + 4), LoadFloat(base + r1.u32 + kTargetOnStack + 8),
                  back, lift);
    }
  }
  if (g_amount <= 0.0f) return;

  const double t = g_amount;
  uint8_t* camera = base + game + kCameraPosOffset;
  const uint8_t* target = base + r1.u32 + kTargetOnStack;
  double c[3], away[3], length = 0.0;
  for (int i = 0; i < 3; ++i) {
    c[i] = LoadFloat(camera + 4 * i);
    away[i] = c[i] - double(LoadFloat(target + 4 * i));
    length += away[i] * away[i];
  }
  length = std::sqrt(length);
  if (!std::isfinite(length) || length < 1.0) return;
  for (int i = 0; i < 3; ++i) c[i] += away[i] / length * back * t;
  c[1] += lift * t;
  for (int i = 0; i < 3; ++i) {
    if (!std::isfinite(c[i])) return;
  }
  for (int i = 0; i < 3; ++i) StoreFloat(camera + 4 * i, float(c[i]));
}
