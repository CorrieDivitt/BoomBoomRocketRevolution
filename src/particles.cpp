// Particle quality (--particle_quality, default 1): more or fewer particles per firework.
//
// Each firework type has a config file (Fireworks/*.config) with four sections - Rubbish,
// Good, Awesome, Visualizer - and each section's "Count" is how many particles one burst
// emits. sub_8206C0F8 reads a section into a 0x34-byte block of the type's config object
// (Count is the first word, stored at 0x8206C7A4). A mid-asm hook there notes the block
// and the file's own count, and stores the scaled count instead; when the setting changes
// later, the noted blocks are rewritten, so it takes effect without a restart.
//
// Limits: all particles share one pool of 32768 (sub_82081B30 is created with 0x8000);
// when it is full, new particles are simply not made (sub_820735D0 checks), so a high
// setting thins out the busiest moments rather than breaking anything.

#include "particles.h"
#include "midasm_hooks.h"
#include "widescreen.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

REXCVAR_DEFINE_DOUBLE(particle_quality, 1.0, "BBR",
                      "Particles per firework, as a multiple of the original (0.25 to 4)");

namespace {
struct Block {
  uint32_t address;   // guest address of the section's Count
  uint32_t original;  // the count in the config file
  uint32_t written;   // what we last stored there
};
std::mutex g_mutex;
std::vector<Block> g_blocks;
double g_applied = -1.0;

double Quality() { return std::clamp(REXCVAR_GET(particle_quality), 0.25, 4.0); }

uint32_t Scaled(uint32_t original, double quality) {
  if (original == 0) return 0;
  return uint32_t(std::clamp(std::lround(double(original) * quality), 1l, 4000l));
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

void bbr::ApplyParticleQuality(uint8_t* base) {
  const double quality = Quality();
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
