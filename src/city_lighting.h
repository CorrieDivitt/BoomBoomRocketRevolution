#pragma once
#include <cstdint>

namespace bbr {
// City lighting (R-410), first step: finding out what there is to work with.
// With --city_light_probe on, one run logs:
//   - the details of every draw of one frame, 12 s into the first song (SDK patches 0009 and
//     0010: shader constants, vertex layout and box, and each shader's microcode as text);
//   - the city shader's lighting constants whenever they change (SDK patch 0010);
//   - the first firework bursts (sub_82075E18) and the first spark emitters with a spark each.
// Nothing on screen changes.

// Called once per UI update with whether a song is on.
void CityLightProbeUpdate(uint8_t* base, bool song_on);
// Called by the spark-emitting hooks (src/particles.cpp) after the game emitted for this emitter.
void CityLightProbeEmit(uint8_t* base, uint32_t emitter);
}  // namespace bbr
