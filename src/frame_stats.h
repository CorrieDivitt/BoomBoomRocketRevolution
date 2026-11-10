#pragma once
#include <rex/ui/overlay/debug_overlay.h>

namespace bbr {
// Called once per game frame (from the sub_82077100 hook); returns host frame time in seconds.
double OnGameFrame();
// Provider for ReXApp::SetGuestFrameStats -> F3 debug overlay.
rex::ui::FrameStats GetFrameStats();
}  // namespace bbr
