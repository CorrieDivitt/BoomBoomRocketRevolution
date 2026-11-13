// City lighting (R-410). See city_lighting.h.
//
// How it works (found with the probe below, 2026-11-12): the game keeps up to six firework
// lights and gives all of them to its smoke shader, but the city's shaders - which do light
// per pixel, falling off with the square of the distance - only ever get one. SDK patch 0011
// (thirdparty GPU code, patches/rexglue-sdk/0011-city-lighting.patch) gives every city draw a
// light of its own that stands in for all six (and the game's own flash) as seen from that
// piece; flat faces are brightened when they face the light (patch 0012). This file only passes
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

REXCVAR_DEFINE_DOUBLE(city_lighting, 2.0, "BBR",
                      "City Lighting: 0 = off (the game's own look); above 0, each piece of the city is lit by "
                      "the fireworks near it, times this strength (1, 2, 4, 8, 32 in the menu; 1 = a firework at "
                      "full strength lights the city as brightly as the game's own flash at its peak)");

// Read by the GPU side (SDK patch 0016) once a frame.
REXCVAR_DEFINE_DOUBLE(city_light_ceiling, 0.35, "BBR",
                      "City Lighting: the most light a surface is given (1 = shown at the full brightness of its "
                      "texture; the unlit city is about 0.125). Not applied at the top setting");
REXCVAR_DEFINE_DOUBLE(city_light_standoff, 350.0, "BBR",
                      "City Lighting: the nearest the light is allowed to a piece it lights, in world units");

REXCVAR_DEFINE_DOUBLE(water_glow, 2.0, "BBR",
                      "Water Glow: the water's highlight takes the fireworks' colour and moves to where they are; "
                      "0 = off, 0.5 / 1 / 2 = Low / Medium / High");
// Water Reflections (test, R-417): the burst draw is issued again inside the emulated GPU with
// the mirror camera (SDK patch 0019). The bursts have no depth test, so the mirrored copy also
// paints over things that should hide it; offered again 2026-11-13 as a test option.
REXCVAR_DEFINE_BOOL(water_reflections, false, "BBR",
                    "Water Reflections (test): the firework bursts are drawn again with the mirror camera, so "
                    "they show in the water (they also show over things that should hide them)");
// Read by the GPU side (SDK patch 0020) once a frame.
REXCVAR_DEFINE_DOUBLE(water_glow_spread, 20.0, "BBR",
                      "Water Glow: how tight the fireworks' highlight on the water is (shininess; the moon's is 50, "
                      "lower spreads wider)");
REXCVAR_DEFINE_DOUBLE(water_glow_wash, 0.06, "BBR",
                      "Water Glow: how much of the fireworks' colour washes over the whole water (0 to 1)");

REXCVAR_DEFINE_BOOL(city_light_probe, false, "BBR",
                    "Development: log city draws, firework bursts and spark emitters once per run "
                    "(for the city lighting work); nothing on screen changes");

REX_EXTERN(__imp__sub_82075E18);

#if defined(_WIN32)
namespace rex::graphics {
void RequestDrawDump(uint32_t frames);  // SDK patch 0005
void SetConstantWatch(uint64_t pixel_hash);  // SDK patch 0010
void SetCityLighting(float strength);  // SDK patch 0011
void SetWaterGlow(float strength);     // SDK patch 0019
void SetWaterReflections(bool on);     // SDK patch 0019
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
    const double want = std::clamp(REXCVAR_GET(city_lighting), 0.0, 64.0);
    if (want != sent) {
      sent = want;
      rex::graphics::SetCityLighting(float(want));
      REXLOG_INFO("[city] City Lighting strength {}", want);
    }
  }
  {
    // Water Glow and Water Reflections (SDK patch 0019).
    static double glow_sent = -1.0;
    static int reflections_sent = -1;
    const double glow = std::clamp(REXCVAR_GET(water_glow), 0.0, 8.0);
    // Water Reflections (test): see the cvar above.
    const int reflections = REXCVAR_GET(water_reflections) ? 1 : 0;
    if (glow != glow_sent) {
      glow_sent = glow;
      rex::graphics::SetWaterGlow(float(glow));
      REXLOG_INFO("[city] Water Glow strength {}", glow);
    }
    if (reflections != reflections_sent) {
      reflections_sent = reflections;
      rex::graphics::SetWaterReflections(reflections != 0);
      REXLOG_INFO("[city] Water Reflections {}", reflections ? "on" : "off");
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
