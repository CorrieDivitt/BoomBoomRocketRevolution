#pragma once
#include <string>

namespace bbr {
// Calibration metronome, mixed into the game's own audio output (see metronome.cpp).
// Commands from the calibration screen:
//   "bbrr.metro.start" key = beat length in ms, value = "1" audible / "0" silent
//   "bbrr.metro.pos"   -> position in beats ("-100" when stopped); beat 0 comes one beat after start
//   "bbrr.metro.stop"
std::string MetronomeCommand(const std::string& command, const std::string& key, const std::string& value);
}  // namespace bbr
