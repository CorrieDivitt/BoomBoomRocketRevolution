#pragma once
#include <cstdint>
#include <string>

namespace bbr {
// Hit timing readout, for the in-game HUD script (ui_scripts/Options/BBRR_hit.lua):
//   "bbrr.hit.enabled" -> "1" when --hit_timing is on
//   "bbrr.hit.summary" -> "1" after writing the finished song's timing line (average, spread,
//                         early and late counts) into stored string STR126; "0" if none
//   "bbrr.hit.advice"  -> "1" after writing which way to move Input Offset into stored string STR118
//                         (song clearly early or late: 20+ hits, average 15+ ms off, 70%+
//                         on one side); "0" if no suggestion. Works with Show Accuracy off too
//   "bbrr.hit.poll"    -> "" or "slot,x,grade" for the next hit not yet shown: the text is
//                         in stored string STR<slot>, x is a 640x480 layout coordinate,
//                         grade is the judgement (6 Perfect, 5 Awesome, 3 Good, 1 OK)
std::string HitCommand(const std::string& command);

// Shoulder button presses since the last call: +1 for each RB, -1 for each LB (any pad).
// Presses older than a quarter of a second are dropped, so ones made in a menu do not
// turn up later in a song. Starts the controller thread if it is not running.
int TakeShoulderSteps();
// Buttons the game just read for a user (XINPUT bits, host order): LB / RB presses.
void NoteGuestButtons(uint32_t user, uint16_t buttons);

// The song time now (the clock the game judges and launches by), or false when no song is
// running steadily (menus, pause).
bool SongTimeNow(double* out);
}  // namespace bbr
