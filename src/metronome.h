#pragma once
#include <string>

namespace bbr {
// Calibration metronome, mixed into the game's own audio output (see metronome.cpp).
// Commands from the calibration screen:
//   "bbrr.metro.start" key = beat length in ms, value = "audible,count_in,beats_per_bar,lead"
//                      e.g. "1,4,4,2": audible, four count-in beats, 4/4 time, two silent
//                      beats before the count-in starts
//   "bbrr.metro.pos"   -> position in beats ("-100" when stopped); beat 0 comes one beat after start
//   "bbrr.metro.stop"
// Plays one short click `seconds_from_now` from now in mixed (audio stream) time, i.e.
// placed in the outgoing sound the same way the music is. Used by the assist tick
// (calibration_sounds/tick.wav if present, otherwise a built-in click).
void ScheduleTick(double seconds_from_now, double volume);
std::string MetronomeCommand(const std::string& command, const std::string& key, const std::string& value);
}  // namespace bbr
