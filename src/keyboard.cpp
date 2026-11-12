// Keyboard page of the settings screen (BBRR Settings > Keyboard): the keys for each button.
//
// The keyboard drives a pad through the SDK's keyboard emulation (--mnk_mode, keybind_* cvars;
// the port's default keys are in src/settings.cpp, KeyboardDefaults). Here each button row
// shows its keys (stored strings STR92-STR99); A on a row waits for the next key pressed and
// makes it that button's key (Esc cancels, except on the Start row). A key taken by another
// button is taken off it. The D-pad row chooses arrow keys, numpad or I J K L. Reset Keys puts
// the defaults back. While waiting for a key the game's input is held, so the key does not
// also act in the menu.

#include "keyboard.h"

#include "settings.h"
#include "widescreen.h"

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace bbr {
namespace {

constexpr uint32_t kTextManager = 0x821D48C8;  // the game's stored strings, 256 bytes each
constexpr uint32_t kFirstSlot = 92;            // STR92-STR99: the rows' keys

struct Button {
  const char* row;                 // action:<row>
  std::vector<const char*> cvars;  // the keybind cvars it sets
};
const std::vector<Button>& Buttons() {
  static const std::vector<Button> buttons = {
      {"bind_a", {"keybind_a"}},
      {"bind_b", {"keybind_b"}},
      {"bind_x", {"keybind_x"}},
      {"bind_y", {"keybind_y"}},
      {"bind_lb", {"keybind_left_shoulder"}},
      {"bind_rb", {"keybind_right_shoulder"}},
      {"bind_triggers", {"keybind_left_trigger", "keybind_right_trigger"}},
      {"bind_start", {"keybind_start"}},
  };
  return buttons;
}

// Every keybind cvar a key could be taken from.
const std::vector<const char*>& AllCvars() {
  static const std::vector<const char*> cvars = {
      "keybind_a", "keybind_b", "keybind_x", "keybind_y", "keybind_left_shoulder", "keybind_right_shoulder",
      "keybind_left_trigger", "keybind_right_trigger", "keybind_start", "keybind_back", "keybind_guide",
      "keybind_dpad_up", "keybind_dpad_down", "keybind_dpad_left", "keybind_dpad_right"};
  return cvars;
}

struct DpadChoice {
  const char* label;  // text-table name
  const char* up;
  const char* down;
  const char* left;
  const char* right;
};
const DpadChoice kDpad[] = {
    {"BBRR_DPAD_ARROWS", "Up", "Down", "Left", "Right"},
    {"BBRR_DPAD_NUMPAD", "Numpad8", "Numpad2", "Numpad4", "Numpad6"},
    {"BBRR_DPAD_IJKL", "I", "K", "J", "L"},
};
constexpr size_t kDpadCount = sizeof(kDpad) / sizeof(kDpad[0]);

std::atomic<bool> g_capturing{false};

std::vector<std::string> Split(const std::string& list) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= list.size()) {
    const size_t comma = list.find(',', start);
    std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    while (!item.empty() && item.front() == ' ') item.erase(item.begin());
    while (!item.empty() && item.back() == ' ') item.pop_back();
    if (!item.empty()) out.push_back(item);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

std::string Join(const std::vector<std::string>& items) {
  std::string out;
  for (const auto& item : items) out += (out.empty() ? "" : ",") + item;
  return out;
}

std::string Friendly(const std::string& name) {
  if (name == "Return") return "Enter";
  if (name == "Escape") return "Esc";
  if (name.rfind("Numpad", 0) == 0 && name.size() == 7) return "Num " + name.substr(6);
  return name;
}

void WriteSlot(uint32_t slot, const std::string& text) {
  uint8_t* base = GuestBase();
  if (!base) return;
  const std::string t = text.substr(0, 255);
  std::memcpy(base + kTextManager + slot * 256, t.c_str(), t.size() + 1);
}

void ShowKeys() {
  const auto& buttons = Buttons();
  for (size_t i = 0; i < buttons.size(); ++i) {
    std::string text;
    for (const auto& key : Split(rex::cvar::GetFlagByName(buttons[i].cvars[0]))) {
      text += (text.empty() ? "" : " / ") + Friendly(key);
    }
    WriteSlot(kFirstSlot + uint32_t(i), text.empty() ? std::string("-") : text);
  }
}

int ButtonIndex(const std::string& row) {
  const auto& buttons = Buttons();
  for (size_t i = 0; i < buttons.size(); ++i) if (row == buttons[i].row) return int(i);
  return -1;
}

// Makes `key` the only key of a button, taking it off any other button.
void Assign(const Button& button, const std::string& key) {
  for (const char* cvar : AllCvars()) {
    auto keys = Split(rex::cvar::GetFlagByName(cvar));
    const auto end = std::remove(keys.begin(), keys.end(), key);
    if (end != keys.end()) {
      keys.erase(end, keys.end());
      rex::cvar::SetFlagByName(cvar, Join(keys));
    }
  }
  for (const char* cvar : button.cvars) rex::cvar::SetFlagByName(cvar, key);
  SettingsCommand("bbrr.save", "", "");
  REXLOG_INFO("[keyboard] {} = {}", button.row, key);
}

rex::input::InputSystem* Input() {
  auto* ks = REX_KERNEL_STATE();
  auto* runtime = ks ? ks->emulator() : nullptr;
  return runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
}

#if defined(_WIN32)
bool OurWindowHasFocus() {
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
}

bool Skipped(int vk) {
  return vk < 0x08 || (vk >= 0x10 && vk <= 0x12) || (vk >= 0xA0 && vk <= 0xA5) || vk == 0x5B || vk == 0x5C ||
         vk == 0x5D;
}
#endif

// Waits (on a thread of its own) for the next key and gives it to the button.
void Capture(int index) {
  if (index < 0 || g_capturing.exchange(true)) return;
  const Button button = Buttons()[size_t(index)];
  auto* input = Input();
  if (input) input->AddUIInputBlocker();
  WriteSlot(kFirstSlot + uint32_t(index), "Press a key...");
  std::thread([button, index, input]() {
#if defined(_WIN32)
    bool down[256] = {};
    for (int vk = 0; vk < 256; ++vk) down[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const auto start = std::chrono::steady_clock::now();
    int got = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(10) && !got) {
      Sleep(5);
      const bool focused = OurWindowHasFocus();
      for (int vk = 0; vk < 256 && !got; ++vk) {
        const bool d = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (focused && d && !down[vk] && !Skipped(vk)) got = vk;
        down[vk] = d;
      }
    }
    const bool cancel = got == VK_ESCAPE && std::strcmp(button.row, "bind_start") != 0;
    if (got && !cancel) {
      const std::string name = rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(got));
      if (!name.empty()) {
        Assign(button, name);
      } else {
        REXLOG_INFO("[keyboard] key {:02X} cannot be used", got);
      }
    }
    // Let go of the key before the game sees input again.
    const auto release = std::chrono::steady_clock::now();
    while (got && (GetAsyncKeyState(got) & 0x8000) &&
           std::chrono::steady_clock::now() - release < std::chrono::seconds(2)) {
      Sleep(5);
    }
#endif
    (void)index;
    ShowKeys();
    if (input) input->RemoveUIInputBlocker();
    g_capturing.store(false);
  }).detach();
}

size_t CurrentDpad() {
  const std::string up = rex::cvar::GetFlagByName("keybind_dpad_up");
  for (size_t i = 0; i < kDpadCount; ++i) {
    const auto keys = Split(up);
    if (std::find(keys.begin(), keys.end(), kDpad[i].up) != keys.end()) return i + 1;
  }
  return 1;
}

void SetDpad(size_t n) {
  if (n < 1 || n > kDpadCount || n == CurrentDpad()) return;
  const DpadChoice& c = kDpad[n - 1];
  const char* keys[] = {c.up, c.down, c.left, c.right};
  const char* cvars[] = {"keybind_dpad_up", "keybind_dpad_down", "keybind_dpad_left", "keybind_dpad_right"};
  // Take the keys off the buttons first, as for a single key.
  for (const char* key : keys) {
    for (const char* cvar : AllCvars()) {
      auto list = Split(rex::cvar::GetFlagByName(cvar));
      const auto end = std::remove(list.begin(), list.end(), std::string(key));
      if (end != list.end()) {
        list.erase(end, list.end());
        rex::cvar::SetFlagByName(cvar, Join(list));
      }
    }
  }
  for (int i = 0; i < 4; ++i) rex::cvar::SetFlagByName(cvars[i], keys[i]);
  ShowKeys();
  REXLOG_INFO("[keyboard] d-pad on {}", c.label);
}

}  // namespace

bool IsKeyboardKey(const std::string& key) {
  return key == "keyboard_dpad" || key.rfind("action:bind_", 0) == 0 || key == "action:keys_reset";
}

bool IsKeyboardAction(const std::string& name) { return name.rfind("bind_", 0) == 0 || name == "keys_reset"; }

std::string KeyboardCommand(const std::string& command, const std::string& key, const std::string& value) {
  if (command == "bbrr.keyboard.show") {
    ShowKeys();
    return "ok";
  }
  if (command == "bbrr.action") {
    if (key == "keys_reset") {
      SettingsCommand("bbrr.keys.reset", "", "");
      ShowKeys();
    } else {
      Capture(ButtonIndex(key));
    }
    return "ok";
  }
  if (key == "keyboard_dpad") {
    if (command == "bbrr.choices") return std::to_string(kDpadCount);
    if (command == "bbrr.get") return std::to_string(CurrentDpad());
    if (command == "bbrr.default") return "1";
    if (command == "bbrr.set") {
      SetDpad(size_t(std::max(0L, std::strtol(value.c_str(), nullptr, 10))));
      return "ok";
    }
    if (command == "bbrr.label") {
      const long n = std::strtol(value.c_str(), nullptr, 10);
      return n >= 1 && size_t(n) <= kDpadCount ? kDpad[n - 1].label : "";
    }
    return "";
  }
  if (command == "bbrr.label") {
    if (key == "action:keys_reset") return "BBRR_RESET";
    const int i = ButtonIndex(key.substr(std::strlen("action:")));
    return i >= 0 ? "STR" + std::to_string(kFirstSlot + uint32_t(i)) : "";
  }
  return "";
}

}  // namespace bbr
