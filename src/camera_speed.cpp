// Camera Speed (BBRR Settings > Modifiers > Camera Speed, --camera_speed, 0 to 100 %).
//
// A song's camera follows a looping path through the city, and so do the points the rockets
// launch from and burst at: three spline files per song, <song>_SplCamera.spl,
// <song>_SplLaunch.spl and <song>_SplTarget.spl (Game Files\Content). sub_82086948 loads a
// spline (r3 = the spline object, r4 = the file name, f1 = its length in seconds) and
// sub_82085F08 gives a point on it (r3 = out, r4 = the spline, f1 = song time; the caller
// takes the song's offset off first, sub_82077630).
//
// Slowing only the camera would leave the rockets launching from and bursting at points
// meant for where the camera should have been, off screen. So all three paths are slowed
// together: on these three splines the song time is multiplied by the camera speed. The
// rockets are built from the same paths, so they still rise to the target line on time;
// the whole scene just travels less far. 0 % keeps everything at the start of the path.
//
// The speed is taken when a song's paths are loaded, so a change applies from the next
// song. Known side effect: below 100 % the camera has not gone round its whole loop when
// the song ends, so the closing camera move (a separate _Out spline that starts where the
// loop starts) can jump.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <algorithm>
#include <cctype>
#include <atomic>
#include <cstring>
#include <string>

REXCVAR_DEFINE_INT32(camera_speed, 100, "BBR",
                     "Camera Speed: how fast a song's camera (and the launch and burst points with it) travels "
                     "along its path, in percent (0 to 100)");

REX_EXTERN(__imp__sub_82086948);
REX_EXTERN(__imp__sub_82085F08);

namespace {
// The three paths of the song being played and the speed they were loaded with.
std::atomic<uint32_t> g_paths[3] = {0, 0, 0};
std::atomic<float> g_factor{1.0f};

std::string GuestText(uint8_t* base, uint32_t address) {
  std::string text;
  if (address < 0x10000) return text;
  for (uint32_t i = 0; i < 260; ++i) {
    const char c = static_cast<char>(base[address + i]);
    if (!c) break;
    text.push_back(c);
  }
  return text;
}

bool EndsWith(const std::string& text, const char* tail) {
  const size_t n = std::strlen(tail);
  if (text.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(text[text.size() - n + i])) !=
        std::tolower(static_cast<unsigned char>(tail[i]))) {
      return false;
    }
  }
  return true;
}
}  // namespace

// Spline load: note the song's three paths (and the speed to use for this song).
REX_HOOK_RAW(sub_82086948) {
  const uint32_t spline = ctx.r3.u32;
  const std::string path = GuestText(base, ctx.r4.u32);
  __imp__sub_82086948(ctx, base);
  int which = -1;
  if (EndsWith(path, "_SplCamera.spl")) which = 0;
  else if (EndsWith(path, "_SplLaunch.spl")) which = 1;
  else if (EndsWith(path, "_SplTarget.spl")) which = 2;
  if (which < 0) return;
  g_paths[which].store(spline);
  if (which == 0) {
    const int percent = std::clamp(int(REXCVAR_GET(camera_speed)), 0, 100);
    g_factor.store(float(percent) / 100.0f);
    REXLOG_INFO("[camera] song path {} at {:08X}: camera speed {}%", path, spline, percent);
  }
}

// A point on a spline: on the song's three paths, at the slowed time.
REX_HOOK_RAW(sub_82085F08) {
  const uint32_t spline = ctx.r4.u32;
  if (spline && (spline == g_paths[0].load() || spline == g_paths[1].load() || spline == g_paths[2].load())) {
    const float factor = g_factor.load();
    if (factor != 1.0f) ctx.f1.f64 = double(float(ctx.f1.f64 * double(factor)));
  }
  __imp__sub_82085F08(ctx, base);
}
