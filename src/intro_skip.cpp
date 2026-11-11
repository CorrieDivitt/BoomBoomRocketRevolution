// Skip the start-up splash screens (--skip_intro, default on).
//
// The front end is a set of Lua screens: Main\StartScreen.lua (EA / Pogo splashes, on
// timers) -> Main\Loading.lua (developer logo, legal text, waits for loading) ->
// Main\TitleScreen.lua ("press start"). StartScreen also kicks off game initialisation,
// so the screens cannot simply be dropped. Instead we run the UI clock much faster until
// the title screen is requested: the splash timers and fades finish in a moment, while
// real loading still takes as long as it needs.
//
//   sub_820A3FB0(timer @ 0x822E7224): per-frame UI clock, writes delta seconds to +8.
//   sub_820A3DA8(screen manager, name): Screen.SetNextScreen from Lua.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_BOOL(skip_intro, true, "BBR", "Fast-forward the start-up logo and legal screens");

REX_EXTERN(__imp__sub_820A3FB0);
REX_EXTERN(__imp__sub_820A3DA8);

namespace {
constexpr float kSpeed = 16.0f;       // UI clock multiplier while skipping
constexpr float kMaxStep = 0.5f;      // never hand the UI more than this per frame
constexpr double kGiveUpSeconds = 45; // safety net if the title screen is never requested

std::atomic<bool> g_intro{true};  // true from boot until the title screen is requested
std::chrono::steady_clock::time_point g_start;
bool g_started = false;
}  // namespace

REX_HOOK_RAW(sub_820A3FB0) {
  const uint32_t timer = ctx.r3.u32;
  __imp__sub_820A3FB0(ctx, base);
  if (!g_intro.load(std::memory_order_relaxed)) return;
  const auto now = std::chrono::steady_clock::now();
  if (!g_started) {
    g_started = true;
    g_start = now;
  }
  if (std::chrono::duration<double>(now - g_start).count() > kGiveUpSeconds) {
    g_intro = false;
    return;
  }
  if (!REXCVAR_GET(skip_intro)) return;
  uint32_t bits;
  std::memcpy(&bits, base + timer + 8, 4);
  bits = __builtin_bswap32(bits);
  float dt;
  std::memcpy(&dt, &bits, 4);
  dt *= kSpeed;
  if (dt > kMaxStep) dt = kMaxStep;
  std::memcpy(&bits, &dt, 4);
  bits = __builtin_bswap32(bits);
  std::memcpy(base + timer + 8, &bits, 4);
}

REX_HOOK_RAW(sub_820A3DA8) {
  if (g_intro.load(std::memory_order_relaxed)) {
    const char* name = reinterpret_cast<const char*>(base + ctx.r4.u32);
    if (ctx.r4.u32 && std::strstr(name, "TitleScreen")) {
      g_intro = false;
      REXLOG_INFO("[intro] title screen reached{}", REXCVAR_GET(skip_intro) ? " (intro fast-forwarded)" : "");
    }
  }
  __imp__sub_820A3DA8(ctx, base);
}
