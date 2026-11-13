#pragma once
#include <cstdint>
#include <string>

namespace bbr {
// Autoplay (BBRR Settings > Modifiers > Autoplay, --autoplay): every rocket is hit at exactly
// its time (0 ms, Perfect). Meant for showing the game and testing: a song in which Autoplay
// was on at any moment saves no score (see src/autoplay.cpp).

// True while Autoplay is switched on.
bool AutoplayOn();
// True when the song being played (or just finished) had Autoplay on at some point: its
// score must not go to the leaderboards.
bool AutoplayUsedThisSong();
// True once Autoplay has been used in any song since the game started: the game's own
// profile save is skipped from then on (it keeps scores in memory).
bool AutoplayUsedThisRun();

// Called with every UI script the game loads (a song starts with Ingame\GetReady).
void AutoplayNoteScreen(const char* name);
// Called once per UI update with whether a song is on (countdown, playing, paused).
void AutoplayUpdate(uint8_t* base, bool song_on);
// Called for every arrow drawn: its rocket and the song time it is due.
void AutoplayNoteArrow(uint8_t* base, uint32_t rocket, double due);
// True when Autoplay pressed the button for this rocket (its press is judged at 0 ms).
bool AutoplayPressedFor(uint32_t rocket);
// Called by the press-judging hook after the game judged a press for this rocket.
void AutoplayNoteJudged(uint8_t* base, uint32_t rocket);

// For UI scripts: "bbrr.autoplay.nosave" -> "1" when the results screen must not save;
// "bbrr.autoplay.song" -> "1" when Autoplay was on at some point in this song.
std::string AutoplayCommand(const std::string& command, const std::string& key = "");
}  // namespace bbr
