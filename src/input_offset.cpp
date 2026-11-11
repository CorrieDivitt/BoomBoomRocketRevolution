// Input offset (--input_offset_ms, default 0).
//
// How the game judges a press (see docs/TIMING.md):
//   sub_82064978(obj, time) keeps the last three song times once per frame:
//     0x821BE9C8 = this frame, 0x821BE9CC = previous frame, 0x821BE9D0 = the one before.
//   sub_82066700 judges a press against a rocket: with T = the rocket's target time and
//     W = [0x821BE9D0, 0x821BE9CC] (the previous frame's time span),
//     accuracy = 1 - |T - clamp(T, W)| / (TargetWidth / 2).
// So a press counts as made during the previous frame. The input offset moves that span:
// a positive value counts presses as made that many milliseconds earlier (for input or
// display delay), a negative value later. Only judging is affected; arrows and music
// are not.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

REXCVAR_DEFINE_DOUBLE(input_offset_ms, 0.0, "BBR",
                      "Input offset in milliseconds: positive = presses are judged as made earlier "
                      "(compensates input/display delay). Range -300..300");

REX_EXTERN(__imp__sub_82066700);

namespace {
constexpr uint32_t kPressSpanEnd = 0x821BE9CC;    // song time of the previous frame
constexpr uint32_t kPressSpanStart = 0x821BE9D0;  // song time of the frame before that

float LoadFloat(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
void StoreFloat(uint8_t* base, uint32_t addr, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(base + addr, &v, 4);
}
}  // namespace

REX_HOOK_RAW(sub_82066700) {
  const float shift = float(std::clamp(REXCVAR_GET(input_offset_ms), -300.0, 300.0) / 1000.0);
  if (shift == 0.0f) {
    __imp__sub_82066700(ctx, base);
    return;
  }
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXLOG_INFO("[input] presses are judged with an input offset of {:.0f} ms", shift * 1000.0f);
  }
  const float end = LoadFloat(base, kPressSpanEnd), start = LoadFloat(base, kPressSpanStart);
  StoreFloat(base, kPressSpanEnd, end - shift);
  StoreFloat(base, kPressSpanStart, start - shift);
  __imp__sub_82066700(ctx, base);
  StoreFloat(base, kPressSpanEnd, end);
  StoreFloat(base, kPressSpanStart, start);
}
