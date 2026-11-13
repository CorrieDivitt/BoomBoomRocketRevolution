#pragma once
#include <string>

namespace bbr {
// Applies Game Volume (--game_volume) when it has changed; called once a frame.
void UpdateGameVolume();
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

// The SDK keeps the game's shaders in <user folder>/boomboomrocket/cache/shaders and reads
// them back at start-up. If the game crashes while feeding the GPU garbage, the garbage is
// kept too and the next start crashes on it. So LoadSettings leaves a marker file while a
// start-up is under way and throws that folder away when the last start-up never finished;
// BootCompleted (called once frames are being drawn) removes the marker.
void BootCompleted();
}  // namespace bbr
