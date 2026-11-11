#pragma once
#include <string>

namespace bbr {
// For the in-game HUD script (ui_scripts/Options/BBRR_hit.lua):
//   "bbrr.speed.poll" -> "1" when the scroll speed was just changed with LB / RB; the text
//                        to show ("Speed 1.5x") is in stored string STR127
std::string SpeedCommand(const std::string& command);
}  // namespace bbr
