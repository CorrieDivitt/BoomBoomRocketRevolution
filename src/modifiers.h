#pragma once
#include <cstdint>
#include <string>

namespace bbr {
// For the in-game HUD script (ui_scripts/Options/BBRR_hit.lua):
//   "bbrr.speed.poll" -> "1" when the scroll speed was just changed with LB / RB; the text
//                        to show ("Speed 1.5x") is in stored string STR127
// For the song select screen (ui_scripts/Options/BBRR_speed.lua):
//   "bbrr.speed.step" -> applies any LB / RB presses, writes the speed ("1.5x") into
//                        stored string STR119 and returns "1" when that text changed
std::string SpeedCommand(const std::string& command);

// How long an arrow of the song being played takes from its launch point to the target line
// at the current scroll speed, in seconds (the usual flight of the song's notes divided by
// the multiplier); 0 when no song has been built yet. Used by the measure lines.
double TypicalFlight();

// Called once per UI update: hides or shows the city for the No Background modifier.
void UpdateNoBackground(uint8_t* base);
// Called with the name of every UI script the game loads (see src/widescreen.cpp): tells No
// Background when a song's screens (countdown, HUD) start and end.
void NoteScreenForNoBackground(const char* name);

// Called from the arrow icon hook with the icon's world position and the game's own clip
// coordinates for it, to tell which way round the game applies its view matrix (needed by
// Horizontal Drift).
void NoteArrowProjection(const float position[3], const float clip[4]);
}  // namespace bbr
