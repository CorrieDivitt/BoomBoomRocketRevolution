// Modifiers (BBRR Settings > Modifiers): changes to how a chart is presented or played.
//
// Scroll speed (--scroll_speed, default 1)
// ----------------------------------------
// Every note is built by sub_82065E70 when a song is loaded:
//   r4 -> where its arrow starts (3 floats), r5 -> where it ends up (the target line),
//   f1 = flight time in seconds, f2 = the time in the song it must arrive.
// The note is launched at f2 - f1 (stored at note +0x1C) and its arrow's position each
// frame is start + (end - start) * (time flown / flight time) (sub_8206A328), so flight
// time is the only thing that sets how fast arrows travel. Dividing it by the multiplier
// makes arrows launch later and travel faster along the same path, arriving at exactly
// the same moment - the speed modifier of StepMania and Dance Dance Revolution: the
// arrows on screen are further apart and the timing against the music does not change.
//
// A note may not be launched before the song starts ([0x821BE9C8]); the game handles
// that for its own flight times by starting such an arrow part of the way along its
// path, and the same is done here when a multiplier below 1 lengthens the flight.
//
// Read when a song is loaded, so a change applies from the next song.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_DOUBLE(scroll_speed, 1.0, "BBR",
                      "Scroll speed multiplier: arrows travel this many times faster and are spaced "
                      "further apart; timing against the music is unchanged (0.25 to 8)");

REX_EXTERN(__imp__sub_82065E70);

namespace {
constexpr uint32_t kSongStartTime = 0x821BE9C8;  // float: earliest time a note may launch

float LoadFloat(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
void StoreFloat(uint8_t* base, uint32_t address, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(base + address, &v, 4);
}

double g_logged_speed = -1.0;
}  // namespace

REX_HOOK_RAW(sub_82065E70) {
  const double speed = std::clamp(REXCVAR_GET(scroll_speed), 0.25, 8.0);
  const double flight = ctx.f1.f64;
  const double arrival = ctx.f2.f64;
  const uint32_t start = ctx.r4.u32, end = ctx.r5.u32;
  if (std::fabs(speed - 1.0) < 1e-6 || !(flight > 0.001) || !std::isfinite(arrival) || start < 0x10000 ||
      end < 0x10000) {
    __imp__sub_82065E70(ctx, base);
    return;
  }
  if (speed != g_logged_speed) {
    g_logged_speed = speed;
    REXLOG_INFO("[modifiers] scroll speed {:.2f}x: flight time {:.3f} s -> {:.3f} s", speed, flight,
                flight / speed);
  }

  double wanted = flight / speed;
  double used = wanted;
  const double room = arrival - double(LoadFloat(base, kSongStartTime));
  if (used > room && room > 0.001) used = room;  // would launch before the song starts

  // The start point is the caller's local variable and is used again for other notes, so
  // it is put back after the call.
  float saved[3];
  for (int i = 0; i < 3; ++i) saved[i] = LoadFloat(base, start + 4 * i);
  if (used < wanted) {
    const float part = float(used / wanted);  // how much of the path is left to fly
    for (int i = 0; i < 3; ++i) {
      const float target = LoadFloat(base, end + 4 * i);
      StoreFloat(base, start + 4 * i, target - (target - saved[i]) * part);
    }
  }
  ctx.f1.f64 = double(float(used));
  __imp__sub_82065E70(ctx, base);
  for (int i = 0; i < 3; ++i) StoreFloat(base, start + 4 * i, saved[i]);
}
