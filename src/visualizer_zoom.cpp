// Visualizer zoom out (X).
//
// In Visualizer mode the game's camera (sub_82088580, the update of the Visualizer camera that
// follows GAME:\content\visualiser.spl) zooms in while RB is held. It keeps a zoom amount z at
// game + 0x12E60: each update RB held adds 0.03 (up to 1), RB up takes 0.03 off (down to 0),
// skipped while paused. The field of view it stores at game + 0x103FC (radians) is
//     fov = 2 * atan(tan(22.5 deg) * 2850 / (2500 + 1600 z))
// so about 51 degrees at rest and 32 degrees fully zoomed in: a lens zoom, the camera does
// not move. Seen from the fireworks that is the camera coming 1 - 2500/4100 = 39% of the way
// closer (everything looks 1.64x larger).
//
// X now does the opposite: held, it eases in the same way (0.03 an update), and fully in it
// widens the view as if the camera had backed off --visualizer_zoom_out times the distance RB
// brings it closer (4 by default: about 101 degrees). Applied after the game's own update, on
// top of whatever RB is doing. Controller state: game + 0x12AD4 + 152 * [game + 0x12F08],
// button bytes from +124 (sub_82063DE8 fills them from XINPUT; X is +134, control 6 for
// sub_82063288, which the Visualizer does not use).
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_DOUBLE(visualizer_zoom_out, 4.0, "BBR",
                      "Visualizer: how far X zooms out, as a multiple of how far RB zooms in (0 turns X off)");

REX_EXTERN(__imp__sub_82088580);

namespace {
constexpr uint32_t kFovOffset = 0x103FC;        // float, radians
constexpr uint32_t kPadIndexOffset = 0x12F08;   // u32: which pad state drives the camera
constexpr uint32_t kPadStatesOffset = 0x12AD4;  // 152 bytes each
constexpr uint32_t kPadStateSize = 152;
constexpr uint32_t kButtonX = 134;              // byte in a pad state, 1 while held
constexpr uint32_t kGameState = 0x821A0578;     // +336 byte and +180 u32: both 0 when not paused
constexpr float kStep = 0.03f;                  // the game's own step for RB
// How much closer RB's full zoom looks: 1 - 2500 / (2500 + 1600).
constexpr double kZoomInMove = 1.0 - 2500.0 / 4100.0;

float g_out = 0.0f;  // 0..1, like the game's z
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

REX_HOOK_RAW(sub_82088580) {
  const uint32_t game = ctx.r3.u32;
  __imp__sub_82088580(ctx, base);

  const double factor = std::clamp(REXCVAR_GET(visualizer_zoom_out), 0.0, 10.0);
  const auto now = std::chrono::steady_clock::now();
  // Back in the Visualizer after a while: start from the game's own view.
  if (now - g_last > std::chrono::milliseconds(500)) g_out = 0.0f;
  g_last = now;
  if (!game || factor <= 0.0) return;

  const bool paused = base[kGameState + 336] != 0 || LoadU32(base + kGameState + 180) != 0;
  if (!paused) {
    const uint32_t pad = LoadU32(base + game + kPadIndexOffset);
    const bool held = pad < 4 && base[game + kPadStatesOffset + pad * kPadStateSize + kButtonX] != 0;
    const float before = g_out;
    g_out = held ? std::min(1.0f, g_out + kStep) : std::max(0.0f, g_out - kStep);
    if (before == 0.0f && g_out > 0.0f) REXLOG_INFO("[visualizer] zooming out (X), {}x the zoom-in", factor);
  }
  if (g_out <= 0.0f) return;

  // Backing off by k times RB's move makes everything look 1 + k * move times further away,
  // which is the tangent of the half view angle growing by that much.
  const float fov = LoadFloat(base + game + kFovOffset);
  if (!(fov > 0.0f && fov < 3.0f)) return;
  const double grow = 1.0 + factor * kZoomInMove * double(g_out);
  const double wider = 2.0 * std::atan(std::tan(double(fov) * 0.5) * grow);
  StoreFloat(base + game + kFovOffset, float(std::min(wider, 2.9)));
}
