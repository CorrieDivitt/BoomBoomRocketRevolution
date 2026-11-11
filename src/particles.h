#pragma once
#include <cstdint>

namespace bbr {
// Called once per game frame: brings the loaded firework configs in line with the
// current --particle_quality (see particles.cpp).
void ApplyParticleQuality(uint8_t* base);
}  // namespace bbr
