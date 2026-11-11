// Widescreen / ultrawide support, step 1: correct 3D aspect ratio (R-402).
//
// The game renders a fixed 1280x720 image. Its cameras store {fov, aspect} as the first
// two floats (see docs/ANALYSIS.md); the init code (sub_8207ECD8) picks 16:9 or 4:3
// from XGetVideoMode. For other aspect ratios we keep the 1280x720 target, tell the
// cameras the real aspect ratio, and let the presenter stretch the image to the window
// ("anamorphic"), so the 3D scene gains extra view at the sides instead of black bars.
// Use --draw_resolution_scale_x/_y to restore horizontal sharpness.
//
// Step 2: the 2D UI renderer is authored at 640x480 and, for 16:9, scales x by 0.75
// and offsets it by 106.67 to centre it. BbrUiTransformHook (a mid-function hook, see
// config/default.toml) recomputes that scale and offset for the real aspect ratio,
// which cancels the stretch and keeps the 2D layer centred at its native shape.
//
// Step 3: once per UI update (sub_820A1A98 fills the quad list) AnchorUi() walks the
// list. Quads that covered the whole 16:9 screen (menu dimmers, fades) get their side
// edges pushed out to the real screen edges, and groups of widgets that sit in the
// outer part of the 16:9 layout are moved out to the matching side of the window.
// Everything is recomputed from the current aspect ratio, so resizing the window works.
//
// Which side a group follows is worked out from where its quads are in that frame. While
// a screen is still arriving (items sliding, text flying in) the groups can look different
// from the finished screen, so a menu used to be drawn in its 16:9 place for a moment and
// then jump to the edge. To avoid that the finished layout of every screen is remembered
// (by script name and number of widgets, also across runs, in cache/ui_layout.txt) and
// used for the first two seconds after the screen's script is loaded, until the layout
// has settled.

#include "widescreen.h"
#include "midasm_hooks.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include <rex/filesystem.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

REXCVAR_DEFINE_STRING(aspect_ratio, "16:9", "BBR",
                      "3D aspect ratio: 16:9 (original), auto (match window), 21:9, 32:9, 16:10 "
                      "or a number such as 2.39");

REXCVAR_DEFINE_STRING(ui_anchor, "edges", "BBR",
                      "Ultrawide 2D layout: edges (side menus / HUD follow the window edges) or "
                      "center (keep the whole 16:9 layout centred)");
REXCVAR_DEFINE_BOOL(ui_anchor_debug, false, "BBR", "Log the 2D layout groups every few seconds");

REX_EXTERN(__imp__sub_8207ECD8);
REX_EXTERN(__imp__sub_820A1A98);
REX_EXTERN(__imp__sub_8208B648);

namespace bbr {
namespace {
constexpr uint32_t kDataStart = 0x821A0000, kDataEnd = 0x822EE000;  // .data (holds the game object)
constexpr uint32_t kMainCameraAspect = 0x821C1420 + 0x104B4;        // game object camera, +4 = aspect
constexpr float kOriginalAspect = 16.0f / 9.0f;

std::function<double()> g_window_aspect;
std::mutex g_mutex;
std::vector<uint32_t> g_patched;  // guest addresses where we replaced a 16:9 aspect value
float g_written = 0.0f;
uint32_t g_frame = 0;
double g_logged = 0.0;
std::atomic<uint8_t*> g_base{nullptr};  // guest memory base, for the mid-asm hooks

float LoadF(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  v = __builtin_bswap32(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
void StoreF(uint8_t* base, uint32_t addr, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(base + addr, &v, 4);
}

double TargetAspect() {
  const std::string& s = REXCVAR_GET(aspect_ratio);
  double value = 0.0;
  if (s == "auto") {
    value = g_window_aspect ? g_window_aspect() : 0.0;
  } else if (auto colon = s.find(':'); colon != std::string::npos) {
    const double w = std::atof(s.substr(0, colon).c_str()), h = std::atof(s.substr(colon + 1).c_str());
    value = h > 0 ? w / h : 0.0;
  } else {
    value = std::atof(s.c_str());
  }
  if (!(value >= 1.0 && value <= 6.0)) return kOriginalAspect;  // unknown / minimised window
  return value;
}
}  // namespace

uint8_t* GuestBase() { return g_base.load(std::memory_order_relaxed); }

void SetWindowAspectProvider(std::function<double()> provider) { g_window_aspect = std::move(provider); }

bool WidescreenRequested() {
  const std::string& s = REXCVAR_GET(aspect_ratio);
  return !(s.empty() || s == "16:9");
}

void ApplyAspect(uint8_t* base) {
  g_base.store(base, std::memory_order_relaxed);
  if (!WidescreenRequested()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  const float target = float(TargetAspect());

  // Cameras copy / initialise their aspect at different times, so look for the exact
  // 16:9 value in the game's data every half second or so and remember where it was.
  if (g_frame++ % 30 == 0) {
    for (uint32_t addr = kDataStart; addr < kDataEnd; addr += 4) {
      if (LoadF(base, addr) == kOriginalAspect &&
          std::find(g_patched.begin(), g_patched.end(), addr) == g_patched.end()) {
        g_patched.push_back(addr);
        REXLOG_INFO("[widescreen] camera aspect found at {:08X}", addr);
      }
    }
    if (std::find(g_patched.begin(), g_patched.end(), kMainCameraAspect) == g_patched.end()) {
      const float current = LoadF(base, kMainCameraAspect);
      if (current > 1.2f && current < 4.0f) g_patched.push_back(kMainCameraAspect);
    }
  }
  for (uint32_t addr : g_patched) {
    const float current = LoadF(base, addr);
    // Only touch values that are still the original or something we wrote earlier.
    if (current == kOriginalAspect || current == g_written) StoreF(base, addr, target);
  }
  g_written = target;

  // Point sprites (arrows, firework particles) are sized in pixels of the 1280x720
  // image, which we present stretched: narrow them by the same factor. Needs the SDK
  // patch in patches/rexglue-sdk (scripts/patch-sdk.ps1). The setting lives in the GPU
  // plugin and can be re-created with its default after we set it, so read it back
  // every half second and set it again whenever it differs.
  static uint32_t sprite_frame = 0;
  static int sprite_logs = 0;
  if (sprite_frame++ % 30 == 0) {
    const double sprite_scale = double(kOriginalAspect) / target;
    const std::string current = rex::cvar::GetFlagByName("point_sprite_scale_x");
    if (current.empty()) {
      if (sprite_logs++ < 3) REXLOG_WARN("[widescreen] point_sprite_scale_x not available (SDK patch missing or GPU not ready)");
    } else if (std::fabs(std::atof(current.c_str()) - sprite_scale) > 1e-4) {
      const bool ok = rex::cvar::SetFlagByName("point_sprite_scale_x", std::to_string(sprite_scale));
      if (sprite_logs++ < 20) {
        REXLOG_INFO("[widescreen] point sprite width scale {} -> {:.4f} ({}), reads back {}", current, sprite_scale,
                    ok ? "ok" : "REJECTED", rex::cvar::GetFlagByName("point_sprite_scale_x"));
      }
    }
  }

  if (std::fabs(target - g_logged) > 0.001) {
    g_logged = target;
    REXLOG_INFO("[widescreen] 3D aspect ratio set to {:.3f} ({} locations)", target, g_patched.size());
  }
}
}  // namespace bbr

// Game/camera initialisation: apply straight away so the first frames are right too.
REX_HOOK_RAW(sub_8207ECD8) {
  __imp__sub_8207ECD8(ctx, base);
  bbr::ApplyAspect(base);
}

// Mid-asm hook in the UI renderer, see src/midasm_hooks.h.
void BbrUiTransformHook(PPCRegister& f31, PPCRegister& f30, PPCRegister& r9) {
  if (r9.u32 == 0 || !bbr::WidescreenRequested()) return;  // 4:3 path or original 16:9
  double aspect;
  {
    std::lock_guard<std::mutex> lock(bbr::g_mutex);
    aspect = bbr::TargetAspect();
  }
  f31.f64 = (f31.f64 / 0.75) * (4.0 / 3.0) / aspect;  // 640-wide design -> 480*aspect wide
  f30.f64 = (480.0 * aspect - 640.0) / 2.0;            // centre it
}

namespace {
double UiAspect() {
  std::lock_guard<std::mutex> lock(bbr::g_mutex);
  return bbr::TargetAspect();
}
bool Near(double a, double b) { return std::fabs(a - b) < 1e-5; }
}  // namespace

double bbr::UiAspectRatio() { return bbr::WidescreenRequested() ? UiAspect() : 16.0 / 9.0; }

// vScale.x: the 16:9 value is (2/640) * 0.75. Clip-space x = x * scale + offset.
void BbrUiScaleXHook(PPCRegister& f0) {
  if (!bbr::WidescreenRequested() || !Near(f0.f64, 1.5 / 640.0)) return;
  f0.f64 = double(float((2.0 / 640.0) * (4.0 / 3.0) / UiAspect()));
}

// Arrow icons (sub_8206A600): a quad built in clip space around the rocket head, with
// half-width = half-height * 9/16. f0 holds that 9/16 at 0x8206A844; use 1/aspect instead.
void BbrArrowAspectHook(PPCRegister& f0) {
  if (!bbr::WidescreenRequested() || !Near(f0.f64, 0.5625)) return;
  f0.f64 = double(float(1.0 / UiAspect()));
}

// vOffset.x: the 16:9 value is -0.75 (so x = 320 lands on the screen centre).
void BbrUiOffsetXHook(PPCRegister& f0) {
  if (!bbr::WidescreenRequested() || !Near(f0.f64, -0.75)) return;
  f0.f64 = double(float(-(4.0 / 3.0) / UiAspect()));
}

// ---------------------------------------------------------------------------------------
// 2D layout anchoring.
//
// UI renderer object at 0x822E7188: +0 = quad count, +8 = quad array (0x98 bytes each).
// A quad starts with 4 vertices of 0x20 bytes (x, y first, in 640x480 design units; the
// 16:9 screen is x = -106.67..746.67). Byte 0x80 & 3: 0 = quad, 1 = scissor rectangle
// (x/y at 0x00 and 0x20), 2 = scissor off.
// sub_820A1A98 resets the count and has every visible widget append its quads
// (sub_8208B648 walks a widget list and calls each widget's render function).
namespace {
using bbr::LoadF;
using bbr::StoreF;
constexpr uint32_t kUiRenderer = 0x822E7188;
constexpr uint32_t kElemSize = 0x98;
constexpr float kLeft16x9 = -100.0f, kRight16x9 = 740.0f;  // just inside the 16:9 edges

std::vector<uint32_t> g_marks;  // quad count at the start of each widget (and at list ends)

uint32_t LoadU32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return __builtin_bswap32(v);
}
uint32_t UiCount(uint8_t* base) { return std::min<uint32_t>(LoadU32(base, kUiRenderer), 0x200); }

struct UiItem {
  uint32_t first, last;  // quad index range [first, last)
  float x0, x1, y0, y1;
  int parent;
};

// ---- remembered layouts ----
using Sides = std::vector<std::pair<uint32_t, int>>;  // (item id, -1 left / 0 centre / +1 right)
using SteadyClock = std::chrono::steady_clock;
std::mutex g_screen_mutex;
std::string g_screen_name;                    // last UI script loaded
SteadyClock::time_point g_screen_loaded_at{};
std::map<std::string, Sides> g_layouts;       // finished layout by "<script>|<widgets>"
bool g_layouts_loaded = false;
std::string g_last_signature;
Sides g_last_sides;
int g_same_frames = 0;

std::filesystem::path LayoutFile() {
  return rex::filesystem::GetUserFolder() / "boomboomrocket" / "cache" / "ui_layout.txt";
}

void LoadLayouts() {
  if (g_layouts_loaded) return;
  g_layouts_loaded = true;
  std::ifstream in(LayoutFile());
  std::string line;
  while (std::getline(in, line)) {
    const size_t tab = line.find('\t');
    if (tab == std::string::npos) continue;
    Sides sides;
    std::istringstream parts(line.substr(tab + 1));
    std::string part;
    while (std::getline(parts, part, ',')) {
      const size_t colon = part.find(':');
      if (colon == std::string::npos) continue;
      sides.emplace_back(uint32_t(std::strtoul(part.c_str(), nullptr, 10)),
                         std::clamp(std::atoi(part.c_str() + colon + 1), -1, 1));
    }
    if (!sides.empty() && g_layouts.size() < 2000) g_layouts[line.substr(0, tab)] = sides;
  }
}

void SaveLayouts() {
  std::error_code ec;
  std::filesystem::create_directories(LayoutFile().parent_path(), ec);
  std::ofstream out(LayoutFile(), std::ios::trunc);
  for (const auto& [signature, sides] : g_layouts) {
    out << signature << '\t';
    for (size_t i = 0; i < sides.size(); ++i) out << (i ? "," : "") << sides[i].first << ':' << sides[i].second;
    out << '\n';
  }
}

int Find(std::vector<UiItem>& items, int i) {
  while (items[i].parent != i) i = items[i].parent = items[items[i].parent].parent;
  return i;
}

void AnchorUi(uint8_t* base) {
  if (!bbr::WidescreenRequested()) return;
  const uint32_t count = UiCount(base);
  const uint32_t list = LoadU32(base, kUiRenderer + 8);
  if (count == 0 || list == 0) return;
  const double aspect = UiAspect();
  const float new_left = float(320.0 - 240.0 * aspect), new_right = float(320.0 + 240.0 * aspect);
  const float shift = float(240.0 * aspect - 1280.0 / 3.0);  // 16:9 edge -> real edge
  if (std::fabs(shift) < 0.5f) return;
  const bool anchor = REXCVAR_GET(ui_anchor) != "center";

  auto elem = [&](uint32_t i) { return list + i * kElemSize; };
  auto type = [&](uint32_t i) { return base[elem(i) + 0x80] & 3; };
  auto vertices = [&](uint32_t i) { return type(i) == 0 ? 4 : type(i) == 1 ? 2 : 0; };

  // Split the quad list into widgets; quads outside any widget stand alone.
  std::vector<uint32_t> cuts;
  for (uint32_t m : g_marks) if (m <= count) cuts.push_back(m);
  const uint32_t first_mark = cuts.empty() ? count : cuts.front();
  const uint32_t last_mark = cuts.empty() ? count : cuts.back();
  std::vector<UiItem> items;
  std::vector<uint32_t> ids;  // the same from frame to frame: widget number, or quad number + 0x10000
  uint32_t next_id = 0;
  auto add = [&](uint32_t first, uint32_t last) {
    UiItem it{first, last, 1e9f, -1e9f, 1e9f, -1e9f, int(items.size())};
    for (uint32_t i = first; i < last; ++i) {
      for (int v = 0; v < vertices(i); ++v) {
        const float x = LoadF(base, elem(i) + v * 0x20), y = LoadF(base, elem(i) + v * 0x20 + 4);
        it.x0 = std::min(it.x0, x); it.x1 = std::max(it.x1, x);
        it.y0 = std::min(it.y0, y); it.y1 = std::max(it.y1, y);
      }
    }
    if (it.x1 >= it.x0) {
      items.push_back(it);
      ids.push_back(next_id);
    }
  };
  for (uint32_t i = 0; i < first_mark; ++i) { next_id = 0x10000 + i; add(i, i + 1); }
  for (size_t k = 0; k + 1 < cuts.size(); ++k) {
    next_id = uint32_t(k);
    if (cuts[k + 1] > cuts[k]) add(cuts[k], cuts[k + 1]);
  }
  for (uint32_t i = last_mark; i < count; ++i) { next_id = 0x10000 + i; add(i, i + 1); }

  // Full-screen covers: stretch to the real edges and keep them out of the grouping.
  // Other very wide items stay centred and do not take part either.
  std::vector<char> grouped(items.size(), 1);
  for (size_t k = 0; k < items.size(); ++k) {
    UiItem& it = items[k];
    if (it.x0 <= kLeft16x9 && it.x1 >= kRight16x9) {
      grouped[k] = 0;
      if (shift <= 0) continue;
      for (uint32_t i = it.first; i < it.last; ++i) {
        for (int v = 0; v < vertices(i); ++v) {
          const uint32_t a = elem(i) + v * 0x20;
          const float x = LoadF(base, a);
          if (x <= kLeft16x9) StoreF(base, a, new_left);
          else if (x >= kRight16x9) StoreF(base, a, new_right);
        }
      }
    } else if (it.x1 - it.x0 > 560.0f) {
      grouped[k] = 0;
    }
  }
  if (!anchor) return;

  // Group items that touch or sit close together (a panel and its labels, menu rows).
  constexpr float kGapX = 16.0f, kGapY = 10.0f;
  for (size_t a = 0; a < items.size(); ++a) {
    if (!grouped[a]) continue;
    for (size_t b = a + 1; b < items.size(); ++b) {
      if (!grouped[b]) continue;
      const UiItem &p = items[a], &q = items[b];
      if (p.x0 - kGapX <= q.x1 && q.x0 - kGapX <= p.x1 && p.y0 - kGapY <= q.y1 && q.y0 - kGapY <= p.y1)
        items[Find(items, int(a))].parent = Find(items, int(b));
    }
  }
  struct Box { float x0 = 1e9f, x1 = -1e9f; };
  std::vector<Box> boxes(items.size());
  for (size_t k = 0; k < items.size(); ++k) {
    if (!grouped[k]) continue;
    Box& b = boxes[Find(items, int(k))];
    b.x0 = std::min(b.x0, items[k].x0);
    b.x1 = std::max(b.x1, items[k].x1);
  }
  // A group lying wholly in one half, with its middle in the outer part of the 16:9
  // layout, follows that side of the window. Anything else stays centred.
  constexpr float kCentre = 320.0f, kOuterLeft = 213.3f, kOuterRight = 426.7f;
  static uint32_t debug_frame = 0;
  const bool debug = REXCVAR_GET(ui_anchor_debug) && (debug_frame++ % 300 == 0);
  Sides sides;  // what this frame's positions say
  std::vector<int> side_of(items.size(), 0);
  for (size_t k = 0; k < items.size(); ++k) {
    if (!grouped[k]) continue;
    const Box& b = boxes[Find(items, int(k))];
    const float mid = (b.x0 + b.x1) * 0.5f;
    if (b.x1 <= kCentre && mid <= kOuterLeft) side_of[k] = -1;
    else if (b.x0 >= kCentre && mid >= kOuterRight) side_of[k] = 1;
    sides.emplace_back(ids[k], side_of[k]);
  }

  // Remember the layout once it has stopped changing; while a screen is still arriving,
  // use what was remembered for it.
  std::string name;
  double since_load;
  {
    std::lock_guard<std::mutex> lock(g_screen_mutex);
    name = g_screen_name;
    since_load = std::chrono::duration<double>(SteadyClock::now() - g_screen_loaded_at).count();
  }
  const std::string signature = name + "|" + std::to_string(cuts.size());
  LoadLayouts();
  if (signature == g_last_signature && sides == g_last_sides) {
    ++g_same_frames;
  } else {
    g_same_frames = 0;
    g_last_signature = signature;
    g_last_sides = sides;
  }
  constexpr int kSettledFrames = 30;
  if (g_same_frames == kSettledFrames) {
    auto it = g_layouts.find(signature);
    if (it == g_layouts.end() || it->second != sides) {
      if (it != g_layouts.end() && since_load < 3.0) {
        REXLOG_INFO("[ui] layout of {} changed while it arrived; remembered for next time", signature);
      }
      if (g_layouts.size() < 2000) g_layouts[signature] = sides;
      SaveLayouts();
    }
  }
  if (g_same_frames < kSettledFrames && since_load < 2.0) {
    const auto it = g_layouts.find(signature);
    if (it != g_layouts.end()) {
      for (size_t k = 0; k < items.size(); ++k) {
        if (!grouped[k]) continue;
        for (const auto& [id, side] : it->second) {
          if (id == ids[k]) {
            side_of[k] = side;
            break;
          }
        }
      }
    }
  }

  for (size_t k = 0; k < items.size(); ++k) {
    if (!grouped[k]) continue;
    const float dx = float(side_of[k]) * shift;
    if (debug) {
      REXLOG_INFO("[ui] group {:3} quads {:3}-{:3} x {:7.1f}..{:7.1f} y {:6.1f}..{:6.1f} -> {}",
                  Find(items, int(k)), items[k].first, items[k].last, items[k].x0, items[k].x1, items[k].y0,
                  items[k].y1, dx < 0 ? "left" : dx > 0 ? "right" : "centre");
    }
    if (dx == 0.0f) continue;
    for (uint32_t i = items[k].first; i < items[k].last; ++i) {
      for (int v = 0; v < vertices(i); ++v) {
        const uint32_t a = elem(i) + v * 0x20;
        StoreF(base, a, LoadF(base, a) + dx);
      }
    }
  }
}
}  // namespace

void bbr::NoteUiScriptLoaded(const char* name) {
  if (!name) return;
  if (std::strncmp(name, "orig:", 5) == 0) name += 5;
  std::lock_guard<std::mutex> lock(g_screen_mutex);
  g_screen_name = name;
  g_screen_loaded_at = SteadyClock::now();
}

#if defined(_WIN32)
// Implemented in the SDK (patches/rexglue-sdk/0004): where the HUD is, for the glow.
namespace rex::ui::d3d12 {
void SetOutputGlowMask(const uint8_t* cells, uint32_t width, uint32_t height);
}
#endif

namespace {
// The glow (bloom) should come from the fireworks and the city, not from the HUD and
// menus. Every 2D item is a quad in the UI list, so each frame we mark the cells of a
// 256 x 144 grid that a quad covers and hand that to the output stage. Quads covering
// much of the screen (dimmers, fades, backgrounds) are not marked.
void BuildGlowMask(uint8_t* base) {
#if defined(_WIN32)
  constexpr int kWidth = 256, kHeight = 144;
  static std::vector<uint8_t> cells(size_t(kWidth) * kHeight);
  std::fill(cells.begin(), cells.end(), uint8_t(0));
  const uint32_t count = UiCount(base);
  const uint32_t list = LoadU32(base, kUiRenderer + 8);
  const double aspect = bbr::UiAspectRatio();
  for (uint32_t i = 0; list != 0 && i < count; ++i) {
    const uint32_t quad = list + i * kElemSize;
    if ((base[quad + 0x80] & 3) != 0) continue;  // scissor commands
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int v = 0; v < 4; ++v) {
      const float x = LoadF(base, quad + v * 0x20), y = LoadF(base, quad + v * 0x20 + 4);
      x0 = std::min(x0, x); x1 = std::max(x1, x);
      y0 = std::min(y0, y); y1 = std::max(y1, y);
    }
    if (!(x1 > x0) || !(y1 > y0)) continue;
    // Layout units -> 0..1 across the picture (the layout is 480 * aspect units wide).
    const double u0 = 0.5 + (x0 - 320.0) / (480.0 * aspect), u1 = 0.5 + (x1 - 320.0) / (480.0 * aspect);
    const double v0 = y0 / 480.0, v1 = y1 / 480.0;
    const double shown = (std::min(u1, 1.0) - std::max(u0, 0.0)) * (std::min(v1, 1.0) - std::max(v0, 0.0));
    if (shown <= 0.0 || shown > 0.4) continue;
    // One cell wider all round, to take in soft edges.
    const int cx0 = std::clamp(int(std::floor(u0 * kWidth)) - 1, 0, kWidth - 1);
    const int cx1 = std::clamp(int(std::ceil(u1 * kWidth)) + 1, 0, kWidth);
    const int cy0 = std::clamp(int(std::floor(v0 * kHeight)) - 1, 0, kHeight - 1);
    const int cy1 = std::clamp(int(std::ceil(v1 * kHeight)) + 1, 0, kHeight);
    for (int y = cy0; y < cy1; ++y) std::memset(&cells[size_t(y) * kWidth + cx0], 255, size_t(cx1 - cx0));
  }
  rex::ui::d3d12::SetOutputGlowMask(cells.data(), kWidth, kHeight);
#else
  (void)base;
#endif
}
}  // namespace

// Before each widget's render call in sub_8208B648 (r3 = widget): note where its quads start.
void BbrUiWidgetHook(PPCRegister& r3) {
  (void)r3;
  if (uint8_t* base = bbr::g_base.load(std::memory_order_relaxed)) g_marks.push_back(UiCount(base));
}

// Widget list walk: close the last widget's range.
REX_HOOK_RAW(sub_8208B648) {
  bbr::g_base.store(base, std::memory_order_relaxed);
  __imp__sub_8208B648(ctx, base);
  g_marks.push_back(UiCount(base));
}

// UI update: rebuilds the quad list, then we lay it out for the real aspect ratio.
REX_HOOK_RAW(sub_820A1A98) {
  bbr::g_base.store(base, std::memory_order_relaxed);
  g_marks.clear();
  __imp__sub_820A1A98(ctx, base);
  AnchorUi(base);
  BuildGlowMask(base);
  g_marks.clear();
}
