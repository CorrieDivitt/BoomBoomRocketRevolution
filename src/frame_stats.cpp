// Frame counter for the F3 debug overlay.
//
// sub_82077100 is the game's per-frame time update (see docs/TIMING.md): it runs once
// per game frame and returns the frame's delta time. We wrap it to timestamp each call
// on the host clock, which gives the real game frame rate shown as "Guest: N FPS".

#include "frame_stats.h"

#include <rex/hook.h>

#include <chrono>
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

void OnGameFrame() {
  const auto now = Clock::now();
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_frames == 0) {
    g_window_start = now;
  } else {
    const double ms = std::chrono::duration<double, std::milli>(now - g_last_frame).count();
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

REX_HOOK_RAW(sub_82077100) {
  bbr::OnGameFrame();
  __imp__sub_82077100(ctx, base);
}
