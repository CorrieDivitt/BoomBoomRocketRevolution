#pragma once
#include <string>

namespace bbr {
// Calibration metronome, mixed into the game's own audio output (see metronome.cpp).
// Commands from the calibration screen:
//   "bbrr.metro.start" key = beat length in ms, value = "audible,count_in,beats_per_bar"
//                      e.g. "1,4,4": audible, four count-in beats, 4/4 time
//   "bbrr.metro.pos"   -> position in beats ("-100" when stopped); beat 0 comes one beat after start
//   "bbrr.metro.stop"
std::string MetronomeCommand(const std::string& command, const std::string& key, const std::string& value);
}  // namespace bbr
