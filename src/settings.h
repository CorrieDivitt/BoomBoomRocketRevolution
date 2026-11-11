#pragma once
#include <string>

namespace bbr {
// Loads <user folder>/boomboomrocket/settings.ini and applies it. Values given on the
// command line win over the file. Call before the runtime is set up.
void LoadSettings();

// The settings screen's side of the bridge (see src/ui_mod.cpp, ui_scripts/Options/BBRR.lua).
// command: "bbrr.get" / "bbrr.default" -> option number (1-based) for a setting key,
//          "bbrr.set" -> choose option `value`, "bbrr.save" -> write the file.
std::string SettingsCommand(const std::string& command, const std::string& key, const std::string& value);

// UTF-8 "KEY = text" lines for the settings screen (added to the game's string table).
std::string SettingsStrings();
// Call when the window is asked to close: if the normal shutdown has not ended the
// process a moment later (it can hang while stopping the game's threads), end it.
void ExitSoon();
}  // namespace bbr
