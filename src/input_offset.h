#pragma once
#include <string>

namespace bbr {
// Hit timing readout, for the in-game HUD script (ui_scripts/Options/BBRR_hit.lua):
//   "bbrr.hit.enabled" -> "1" when --hit_timing is on
//   "bbrr.hit.summary" -> "1" after writing the finished song's timing line (average, spread,
//                         early and late counts) into stored string STR126; "0" if none
//   "bbrr.hit.poll"    -> "" or "slot,x,grade" for the next hit not yet shown: the text is
//                         in stored string STR<slot>, x is a 640x480 layout coordinate,
//                         grade is the judgement (6 Perfect, 5 Awesome, 3 Good, 1 OK)
std::string HitCommand(const std::string& command);
}  // namespace bbr
