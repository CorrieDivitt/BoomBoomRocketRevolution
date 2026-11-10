// Frame counter for the F3 debug overlay.
//
// sub_82077100 is the game's per-frame time update (see docs/TIMING.md): it runs once
// per game frame and returns the frame's delta time. We wrap it to timestamp each call
// on the host clock, which gives the real game frame rate shown as "Guest: N FPS".
//
// It also fixes the one frame-locked piece of the timing code: sub_82077100 advances a
// fade timer by the float at 0x821D4330, which the game initialises once to 1/60
// (assuming 60 fps), so fades ran 2-4x too fast at 120-240 fps. Before each call we
// overwrite that value with the real frame time, keeping fade speed constant.

#include "frame_stats.h"

#include <rex/hook.h>

#include <bit>
#include <chrono>
#include <cstring>
#include <mutex>

REX_EXTERN(__imp__sub_82077100);

namespace bbr {
namespace {
using Clock = std::chrono::steady_clock;
std::mutex g_mutex;
Clock::time_point g_last_frame;
Clock::time_point g_window_start;
uint64_t g_frames = 0;
uint64_t g_window_frames = 0;
double g_fps = 0.0;
double g_frame_ms = 0.0;
}  // namespace

double OnGameFrame() {
  const auto now = Clock::now();
  std::lock_guard<std::mutex> lock(g_mutex);
  double dt_s = 1.0 / 60.0;
  if (g_frames == 0) {
    g_window_start = now;
  } else {
    const double ms = std::chrono::duration<double, std::milli>(now - g_last_frame).count();
    dt_s = ms / 1000.0;
    g_frame_ms = (g_frame_ms == 0.0) ? ms : g_frame_ms * 0.9 + ms * 0.1;  // smoothed
  }
  g_last_frame = now;
  ++g_frames;
  ++g_window_frames;
  const double window_s = std::chrono::duration<double>(now - g_window_start).count();
  if (window_s >= 0.5) {  // FPS averaged over half-second windows
    g_fps = double(g_window_frames) / window_s;
    g_window_frames = 0;
    g_window_start = now;
  }
  return dt_s;
}

rex::ui::FrameStats GetFrameStats() {
  std::lock_guard<std::mutex> lock(g_mutex);
  rex::ui::FrameStats stats;
  stats.frame_time_ms = g_frame_ms;
  stats.fps = g_fps;
  stats.frame_count = g_frames;
  return stats;
}
}  // namespace bbr

namespace {
// Guest address of the fade step (seconds per frame), see docs/TIMING.md.
constexpr uint32_t kFadeStepAddr = 0x821D4330;
}

REX_HOOK_RAW(sub_82077100) {
  double dt = bbr::OnGameFrame();
  if (dt < 0.0) dt = 0.0;
  if (dt > 0.1) dt = 0.1;  // stalls (loading, window drag) shouldn't jump fades
  const float step = float(dt);
  uint32_t bits;
  std::memcpy(&bits, &step, sizeof(bits));
  *reinterpret_cast<volatile uint32_t*>(base + kFadeStepAddr) = __builtin_bswap32(bits);
  __imp__sub_82077100(ctx, base);
}
