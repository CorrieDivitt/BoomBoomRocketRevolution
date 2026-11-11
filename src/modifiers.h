#pragma once
#include <string>

namespace bbr {
// For the in-game HUD script (ui_scripts/Options/BBRR_hit.lua):
//   "bbrr.speed.poll" -> "1" when the scroll speed was just changed with LB / RB; the text
//                        to show ("Speed 1.5x") is in stored string STR127
// For the song select screen (ui_scripts/Options/BBRR_speed.lua):
//   "bbrr.speed.step" -> applies any LB / RB presses, writes the speed ("1.5x") into
//                        stored string STR119 and returns "1" when that text changed
std::string SpeedCommand(const std::string& command);
}  // namespace bbr
