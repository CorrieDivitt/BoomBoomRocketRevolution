#pragma once
// Local profiles (src/profiles.cpp): who Player 1 and Player 2 are, with their own gamertags,
// save data and achievements. Kept in <user folder>/boomboomrocket/profiles.ini.

#include <cstdint>
#include <string>

namespace bbr {

// Reads profiles.ini (creating it with the original "User" profile when missing).
void LoadProfiles();
// Signs the chosen profiles in to controller slots 0 and 1 (at start, after the game loads).
void ApplyProfiles();
// Gamertag of a profile, or "" when no profile has that XUID.
std::string ProfileName(uint64_t xuid);
// Settings screen rows handled here: profile_p1, profile_p2 and action:<name> rows.
bool IsProfileKey(const std::string& key);
// bbrr.choices / get / set / default / label for those rows, bbrr.action, and
// bbrr.profiles.save / show / changed.
std::string ProfilesCommand(const std::string& command, const std::string& key, const std::string& value);

}  // namespace bbr
