// Particle quality (--particle_quality, default 1): more or fewer particles per firework.
//
// Each firework type has a config file (Fireworks/*.config) with four sections - Rubbish,
// Good, Awesome, Visualizer - and each section's "Count" is how many particles one burst
// emits. sub_8206C0F8 reads a section into a 0x34-byte block of the type's config object
// (Count is the first word, stored at 0x8206C7A4). A mid-asm hook there notes the block
// and the file's own count, and stores the scaled count instead; when the setting changes
// later, the noted blocks are rewritten, so it takes effect without a restart.
//
// Limits: the game stops making sparks at 70000 alive (see TrackPoolUse below). 10x gets
// there within seconds and has crashed the game; 5x stays under half, so that is the top.

#include "particles.h"
#include "city_lighting.h"
#include "settings.h"
#include "midasm_hooks.h"
#include "widescreen.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

REXCVAR_DEFINE_DOUBLE(particle_quality, 1.0, "BBR",
                      "Particles per firework, as a multiple of the original (0.25 to 5)");

REXCVAR_DEFINE_DOUBLE(particle_limit, 68000.0, "BBR",
                      "Most sparks alive at once before fireworks stop adding more (1000 to 70000). "
                      "The game's own limit is 70000, and it has crashed after running at that limit");

REX_EXTERN(__imp__sub_820E5D10);
REX_EXTERN(__imp__sub_820E5FB8);

namespace {
struct Block {
  uint32_t address;   // guest address of the section's Count
  uint32_t original;  // the count in the config file
  uint32_t written;   // what we last stored there
};
std::mutex g_mutex;
std::vector<Block> g_blocks;
double g_applied = -1.0;

// 0 = no sparks at all (see SparkBudgetLeft); otherwise 0.25x to 5x.
double Quality() {
  const double q = REXCVAR_GET(particle_quality);
  return q <= 0.0 ? 0.0 : std::clamp(q, 0.25, 5.0);
}

uint32_t Scaled(uint32_t original, double quality) {
  if (original == 0) return 0;
  if (quality <= 0.0) return original;  // counts stay as in the file; no sparks are added at all
  return uint32_t(std::clamp(std::lround(double(original) * quality), 1l, 20000l));
}

uint32_t Load32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}
void Store32(uint8_t* base, uint32_t address, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + address, &v, 4);
}
}  // namespace

// The two functions that add an emitter's sparks for this frame (r3 = spark system,
// sparks alive at +0x2814). The game only stops at 70000, the exact size of its vertex
// buffers, and at 10x density it ran at that limit for under a minute before the GPU was
// fed garbage and the game died. So stop adding sparks earlier (--particle_limit). One
// call can still add a whole burst (the game's own 70000 check still applies to each
// spark). 68000 was chosen in play: a full song at 5x held there without a crash.
namespace {
bool SparkBudgetLeft(PPCContext& ctx, uint8_t* base) {
  if (Quality() <= 0.0) return false;  // Particle Density 0x: the fireworks make no sparks
  const double limit = std::clamp(REXCVAR_GET(particle_limit), 1000.0, 70000.0);
  return ctx.r3.u32 < 0x10000 || Load32(base, ctx.r3.u32 + 0x2814) < uint32_t(limit);
}
}  // namespace

REX_HOOK_RAW(sub_820E5D10) {
  const uint32_t emitter = ctx.r4.u32;
  if (SparkBudgetLeft(ctx, base)) {
    __imp__sub_820E5D10(ctx, base);
    bbr::CityLightProbeEmit(base, emitter);
  }
}

REX_HOOK_RAW(sub_820E5FB8) {
  const uint32_t emitter = ctx.r4.u32;
  if (SparkBudgetLeft(ctx, base)) {
    __imp__sub_820E5FB8(ctx, base);
    bbr::CityLightProbeEmit(base, emitter);
  }
}

// Mid-asm hook at 0x8206C7A4 (see config/default.toml): r3 = Count just read from the
// file, r25 = the block it is about to be stored in.
void BbrParticleCountHook(PPCRegister& r3, PPCRegister& r25) {
  const uint32_t original = r3.u32;
  if (original == 0 || original > 100000 || r25.u32 < 0x10000) return;
  const double quality = Quality();
  const uint32_t scaled = Scaled(original, quality);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    bool known = false;
    for (Block& b : g_blocks) {
      if (b.address == r25.u32) {
        b.original = original;
        b.written = scaled;
        known = true;
      }
    }
    if (!known && g_blocks.size() < 1024) g_blocks.push_back({r25.u32, original, scaled});
  }
  r3.u64 = scaled;
}

namespace {
// How full the spark system gets. A firework's Count ends up as a float in its emitter
// (+0x1A8); sub_820E5D10 / sub_820E5FB8 turn it into sparks in the system at [0x821C051C]:
//   +0x2814  sparks alive; the game stops making new ones at 70000 (0x11170), which is
//            also what its three vertex buffers are sized for (70000 x 112 bytes)
//   +0x2004  blocks of 128 sparks in use, out of 2048 (sub_820E5010 returns null when out)
// (The pools at [0x821C0518] and [game + 0x74] hold rockets and trail segments, a few
// hundred at most, and do not grow with Count.)
// Logged every 10 seconds, and every second once either number passes half its limit, so
// the last line before a crash shows how full things were. KEEP THIS LOGGING (asked for
// 2026-11-11): a crash from too many sparks may turn up long after anyone is thinking
// about particles, and these lines plus a "ShaderTranslator ... Unsupported write to
// export" error at the end of the log are how to recognise it.
constexpr uint32_t kSparkLimit = 70000;
constexpr uint32_t kBlockLimit = 2048;

void TrackPoolUse(uint8_t* base, double quality) {
  using Clock = std::chrono::steady_clock;
  static Clock::time_point window_start = Clock::now();
  static uint32_t spark_peak = 0, block_peak = 0, sparks_full = 0, blocks_full = 0, frames = 0;
  const uint32_t system = Load32(base, 0x821C051C);
  if (system < 0x10000) return;
  const uint32_t sparks = Load32(base, system + 0x2814);
  const uint32_t blocks = Load32(base, system + 0x2004);
  if (sparks > 0x1000000 || blocks > 0x1000000) return;
  spark_peak = std::max(spark_peak, sparks);
  block_peak = std::max(block_peak, blocks);
  if (sparks >= kSparkLimit) ++sparks_full;
  if (blocks >= kBlockLimit) ++blocks_full;
  ++frames;
  const bool busy = spark_peak >= kSparkLimit / 2 || block_peak >= kBlockLimit / 2;
  const auto window = busy ? std::chrono::seconds(1) : std::chrono::seconds(10);
  if (Clock::now() - window_start < window) return;
  if (spark_peak > 0) {
    REXLOG_INFO("[particles] last {} s: peak {} of {} sparks ({:.0f}%), at the limit on {} of {} frames; "
                "peak {} of {} blocks ({:.0f}%), all in use on {} frames; density {:.2f}x",
                busy ? 1 : 10, spark_peak, kSparkLimit, 100.0 * spark_peak / kSparkLimit, sparks_full,
                frames, block_peak, kBlockLimit, 100.0 * block_peak / kBlockLimit, blocks_full, quality);
  }
  window_start = Clock::now();
  spark_peak = block_peak = sparks_full = blocks_full = frames = 0;
}
}  // namespace

void bbr::ApplyParticleQuality(uint8_t* base) {
  bbr::BootCompleted();  // frames are being drawn, so start-up got through
  const double quality = Quality();
  TrackPoolUse(base, quality);
  if (quality == g_applied) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  size_t changed = 0;
  for (auto it = g_blocks.begin(); it != g_blocks.end();) {
    // Only touch a block that still holds what we put there; anything else means the
    // config object is gone and the memory belongs to something else now.
    if (Load32(base, it->address) != it->written) {
      it = g_blocks.erase(it);
      continue;
    }
    const uint32_t scaled = Scaled(it->original, quality);
    if (scaled != it->written) {
      Store32(base, it->address, scaled);
      it->written = scaled;
      ++changed;
    }
    ++it;
  }
  if (g_applied >= 0.0 || quality != 1.0) {
    REXLOG_INFO("[particles] quality {:.2f}x: {} of {} firework settings changed", quality, changed,
                g_blocks.size());
  }
  g_applied = quality;
}
