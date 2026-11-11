#pragma once
#include <string>

namespace bbr {
// Hit timing readout, for the in-game HUD script (ui_scripts/Options/BBRR_hit.lua):
//   "bbrr.hit.enabled" -> "1" when --hit_timing is on
//   "bbrr.hit.poll"    -> "" or "slot,x,y" for the next hit not yet shown: the text is in
//                         stored string STR<slot>, x/y are 640x480 layout coordinates
std::string HitCommand(const std::string& command);
}  // namespace bbr
