#pragma once
// Keyboard page of the settings screen (src/keyboard.cpp): which key does each button.

#include <string>

namespace bbr {

// Rows handled here: keyboard_dpad and action:bind_* / action:keys_reset.
bool IsKeyboardKey(const std::string& key);
// bbrr.action names handled here (bind_*, keys_reset).
bool IsKeyboardAction(const std::string& name);
// bbrr.choices / get / set / default / label for those rows, bbrr.action, bbrr.keyboard.show.
std::string KeyboardCommand(const std::string& command, const std::string& key, const std::string& value);

}  // namespace bbr
