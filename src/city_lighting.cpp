// City lighting (R-410). See city_lighting.h.
//
// How it works (found with the probe below, 2026-11-12): the game keeps up to six firework
// lights and gives all of them to its smoke shader, but the city's shaders - which do light
// per pixel, falling off with the square of the distance - only ever get one. SDK patch 0011
// (thirdparty GPU code, patches/rexglue-sdk/0011-city-lighting.patch) gives every city draw a
// light of its own that stands in for all six as seen from that piece. This file only passes
// the setting on (--city_lighting, BBRR Settings > Video Settings > City Lighting) and keeps
// the probe that was used to find all this out (--city_light_probe, off by default).
//
// What is known so far:
//   - the city is about 1,000 draws a frame (pairs listed in src/modifiers.cpp, No Background);
//   - sub_82075E18 starts a firework when a press is judged (called from sub_82066700);
//   - sparks: sub_820E5D10 / sub_820E5FB8 (system r3, emitter r4, f1 = time step) emit into
//     64-byte slots, 128 to a block; emitter +36 -> the firework type's settings, whose +464
//     is the block being filled and +424 the rate.
// The probe logs raw values so the next step can pick out positions and colours.

#include "city_lighting.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

REXCVAR_DEFINE_DOUBLE(city_lighting, 0.0, "BBR",
                      "City Lighting: 0 = off (the game's own look); above 0, each piece of the city is lit by "
                      "the fireworks near it, times this strength (1, 2, 4, 8 in the menu)");

REXCVAR_DEFINE_BOOL(city_light_probe, false, "BBR",
                    "Development: log city draws, firework bursts and spark emitters once per run "
                    "(for the city lighting work); nothing on screen changes");

REX_EXTERN(__imp__sub_82075E18);

#if defined(_WIN32)
namespace rex::graphics {
void RequestDrawDump(uint32_t frames);  // SDK patch 0005
void SetConstantWatch(uint64_t pixel_hash);  // SDK patch 0010
void SetCityLighting(float strength);  // SDK patch 0011
}  // namespace rex::graphics
#endif

namespace {
uint32_t Load32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

// "+00 3F800000(1) ..." for `words` 32-bit words from `address`.
std::string Words(uint8_t* base, uint32_t address, uint32_t words) {
  std::string out;
  char item[48];
  for (uint32_t i = 0; i < words; ++i) {
    const uint32_t raw = Load32(base, address + i * 4);
    float f;
    std::memcpy(&f, &raw, 4);
    std::snprintf(item, sizeof(item), " +%02X %08X(%.4g)", i * 4, raw, double(f));
    out += item;
  }
  return out;
}

int g_bursts_logged = 0;
}  // namespace

// A firework starts. Arguments as the caller (the press-judging function) sets them.
REX_HOOK_RAW(sub_82075E18) {
  if (REXCVAR_GET(city_light_probe) && g_bursts_logged < 16) {
    ++g_bursts_logged;
    REXLOG_INFO("[city] burst {}: r3 {:08X} r4 {:08X} r5 {:08X} r6 {:08X} r7 {:08X} r8 {:08X} r9 {:08X} r10 {:08X} "
                "f1 {:.4f} f2 {:.4f} f3 {:.4f} f4 {:.4f}",
                g_bursts_logged, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32,
                ctx.r10.u32, ctx.f1.f64, ctx.f2.f64, ctx.f3.f64, ctx.f4.f64);
  }
  __imp__sub_82075E18(ctx, base);
}

void bbr::CityLightProbeEmit(uint8_t* base, uint32_t emitter) {
  static int logged = 0;
  static std::unordered_set<uint32_t> seen;
  if (logged >= 6 || !REXCVAR_GET(city_light_probe) || emitter < 0x10000) return;
  const uint32_t settings = Load32(base, emitter + 36);
  if (settings < 0x10000) return;
  const uint32_t block = Load32(base, settings + 464);
  if (block < 0x10000) return;
  const uint32_t count = Load32(base, block + 8192);
  if (count == 0 || count > 128) return;
  if (!seen.insert(emitter).second) return;
  ++logged;
  REXLOG_INFO("[city] emitter {} at {:08X}:{}", logged, emitter, Words(base, emitter, 24));
  REXLOG_INFO("[city] emitter {} settings {:08X} +180:{}", logged, settings, Words(base, settings + 0x180, 22));
  REXLOG_INFO("[city] emitter {} newest spark (slot {} of block {:08X}):{}", logged, count - 1, block,
              Words(base, block + (count - 1) * 64, 16));
}

void bbr::CityLightProbeUpdate(uint8_t* base, bool song_on) {
  (void)base;
#if defined(_WIN32)
  using Clock = std::chrono::steady_clock;
  static bool was_on = false;
  static Clock::time_point since;
  static int dumps = 0;
  {
    // City Lighting: the GPU side does the work (SDK patch 0011); it only needs the strength.
    static double sent = -1.0;
    const double want = std::clamp(REXCVAR_GET(city_lighting), 0.0, 16.0);
    if (want != sent) {
      sent = want;
      rex::graphics::SetCityLighting(float(want));
      REXLOG_INFO("[city] City Lighting strength {}", want);
    }
  }
  static bool watching = false;
  if (REXCVAR_GET(city_light_probe) && !watching) {
    // The city's most used pixel shader: log its lighting constants whenever they change.
    watching = true;
    rex::graphics::SetConstantWatch(0x238C407DFDDF25ECull);
  }
  if (!REXCVAR_GET(city_light_probe) || dumps >= 1) return;
  if (song_on != was_on) {
    was_on = song_on;
    since = Clock::now();
  }
  if (!song_on) return;
  const double seconds = std::chrono::duration<double>(Clock::now() - since).count();
  if (seconds > 12.0) {
    ++dumps;
    REXLOG_INFO("[city] probe: logging one frame of draws, {:.0f} s into the song", seconds);
    rex::graphics::RequestDrawDump(1);
  }
#else
  (void)song_on;
#endif
}
