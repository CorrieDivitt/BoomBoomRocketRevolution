// Profiles: several local players, each with a gamertag of their own, their own save data
// (the SDK keeps each profile's title data under its XUID) and their own achievements.
//
// Kept in <user folder>/boomboomrocket/profiles.ini:
//   profile = <XUID, 16 hex digits> <gamertag>
//   player1 = <XUID>
//   player2 = <XUID> | none
// The first profile is the one the game always had (XUID B13EBABEBABEBABE, "User"), so the
// data from before profiles existed stays with it.
//
// The settings screen (BBRR Settings > Profiles) picks who is Player 1 and Player 2 and can
// rename and create profiles. Choices take effect when the page is saved (A): the profiles are
// signed in to controller slots 0 and 1 and the game is told (XN_SYS_SIGNINCHANGED), so it
// reads the players again. Names in the menus come from stored strings STR108-STR115 (STR116 is a blank).

#include "profiles.h"

#include "widescreen.h"

#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/kernel/xam/text_entry.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace bbr {
namespace {

constexpr uint64_t kOriginalXuid = 0xB13EBABEBABEBABEull;
constexpr size_t kMaxProfiles = 8;
constexpr size_t kNameMax = 15;
constexpr uint32_t kTextManager = 0x821D48C8;  // the game's stored strings, 256 bytes each
constexpr uint32_t kFirstNameSlot = 108;       // STR108-STR115: profile names, in list order
constexpr uint32_t kBlankSlot = 116;           // STR116: a space (the rename rows show no value)
constexpr uint32_t kFirstPadSlot = 100;        // STR100-STR103: connected controllers' names
constexpr size_t kMaxPads = 4;
// XN_SYS_SIGNINCHANGED: the game reads who is signed in again (sub_820C5650, case 0x0A).
// XN_SYS_PROFILESETTINGCHANGED: it reads every signed-in player's name again (case 0x0E).
constexpr uint32_t kSigninChanged = 0x0000000A;
constexpr uint32_t kProfileSettingChanged = 0x0000000E;

struct Profile {
  uint64_t xuid;
  std::string name;
};

std::mutex g_mutex;
std::vector<Profile> g_profiles;
uint64_t g_player1 = kOriginalXuid, g_player2 = 0;  // chosen (saved with the page)
uint64_t g_signed1 = 0, g_signed2 = 0;              // what the kernel has now
std::string g_signed1_name, g_signed2_name;
std::atomic<bool> g_list_changed{false};            // a profile was added: rebuild the page
// Player 1's controller (rex::input::DeviceKey), "" = the first one connected. The others
// follow in connection order.
std::string g_pad1, g_applied_pad1 = "\x01";
std::vector<std::string> g_pad_keys;               // controllers listed on the page, in order

void WriteSlot(uint8_t* base, uint32_t slot, const std::string& text);

rex::input::InputSystem* Input() {
  auto* ks = REX_KERNEL_STATE();
  auto* runtime = ks ? ks->emulator() : nullptr;
  return runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
}

// Short name for the settings page.
std::string PadName(std::string name) {
  for (const char* tail : {" Wireless Controller", " Controller"}) {
    const size_t n = std::strlen(tail);
    if (name.size() > n && name.compare(name.size() - n, n, tail) == 0) name.resize(name.size() - n);
  }
  if (name.size() > 24) name.resize(24);
  return name.empty() ? std::string("Controller") : name;
}

// Lists the connected controllers into g_pad_keys and STR100-103 (call with g_mutex held).
void ListPadsLocked() {
  g_pad_keys.clear();
  auto* input = Input();
  const auto pads = input ? input->ListControllers() : std::vector<rex::input::DeviceInfo>();
  uint8_t* base = GuestBase();
  for (const auto& pad : pads) {
    if (g_pad_keys.size() >= kMaxPads) break;
    if (base) WriteSlot(base, kFirstPadSlot + uint32_t(g_pad_keys.size()), PadName(pad.name));
    g_pad_keys.push_back(rex::input::DeviceKey(pads, pad));
  }
}

void ApplyPadLocked() {
  if (g_pad1 == g_applied_pad1) return;
  auto* input = Input();
  if (!input) return;
  input->SetPlayerOrder(g_pad1.empty() ? std::vector<std::string>() : std::vector<std::string>{g_pad1});
  g_applied_pad1 = g_pad1;
  REXLOG_INFO("[profiles] Player 1 controller: {}", g_pad1.empty() ? std::string("first connected") : g_pad1);
}

std::filesystem::path FilePath() { return rex::filesystem::GetUserFolder() / "boomboomrocket" / "profiles.ini"; }

std::string Clean(std::string text) {
  for (char& c : text) if (static_cast<unsigned char>(c) < 0x20) c = ' ';
  while (!text.empty() && text.back() == ' ') text.pop_back();
  while (!text.empty() && text.front() == ' ') text.erase(text.begin());
  if (text.size() > kNameMax) text.resize(kNameMax);
  return text;
}

int IndexOf(uint64_t xuid) {  // call with g_mutex held
  for (size_t i = 0; i < g_profiles.size(); ++i) if (g_profiles[i].xuid == xuid) return int(i);
  return -1;
}

std::string NameOf(uint64_t xuid) {  // call with g_mutex held
  const int i = IndexOf(xuid);
  return i >= 0 ? g_profiles[size_t(i)].name : std::string();
}

void SaveLocked() {
  std::error_code ec;
  std::filesystem::create_directories(FilePath().parent_path(), ec);
  std::ofstream out(FilePath(), std::ios::trunc);
  out << "# Boom Boom Rocket Revolution profiles: profile = <XUID> <gamertag>; player1 / player2 = <XUID> or none\n";
  char xuid[32];
  for (const Profile& p : g_profiles) {
    std::snprintf(xuid, sizeof(xuid), "%016llX", static_cast<unsigned long long>(p.xuid));
    out << "profile = " << xuid << ' ' << p.name << "\n";
  }
  std::snprintf(xuid, sizeof(xuid), "%016llX", static_cast<unsigned long long>(g_player1));
  out << "player1 = " << xuid << "\n";
  if (g_player2) {
    std::snprintf(xuid, sizeof(xuid), "%016llX", static_cast<unsigned long long>(g_player2));
    out << "player2 = " << xuid << "\n";
  } else {
    out << "player2 = none\n";
  }
  out << "player1_controller = " << (g_pad1.empty() ? std::string("first") : g_pad1) << "\n";
}

void WriteSlot(uint8_t* base, uint32_t slot, const std::string& text) {
  if (!base) return;
  std::memcpy(base + kTextManager + slot * 256, text.c_str(), std::min<size_t>(text.size(), 255) + 1);
}

// The names the settings page shows (call with g_mutex held).
void ShowNamesLocked() {
  uint8_t* base = GuestBase();
  if (!base) return;
  for (size_t i = 0; i < kMaxProfiles; ++i) {
    WriteSlot(base, kFirstNameSlot + uint32_t(i), i < g_profiles.size() ? g_profiles[i].name : std::string());
  }
  WriteSlot(base, kBlankSlot, " ");
}

uint64_t NewXuid() {  // call with g_mutex held
  // Offline-style XUIDs (E0...). The game refuses XUIDs with bits 54-55 set; these have none.
  std::random_device rd;
  std::mt19937_64 rng((uint64_t(rd()) << 32) ^ rd() ^
                      uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));
  for (;;) {
    const uint64_t xuid = 0xE000000000000000ull | (rng() & 0x000000FFFFFFFFFFull);
    if (IndexOf(xuid) < 0) return xuid;
  }
}

// Signs the chosen players in (call with g_mutex held). Returns the notification to send: 0
// none, kSigninChanged when who is signed in changed, kProfileSettingChanged for a new name.
uint32_t ApplyLocked() {
  auto* ks = REX_KERNEL_STATE();
  if (!ks) return 0;
  if (g_player2 == g_player1) g_player2 = 0;
  const std::string name1 = NameOf(g_player1), name2 = g_player2 ? NameOf(g_player2) : std::string();
  uint32_t note = 0;
  if (g_player1 != g_signed1 || name1 != g_signed1_name) {
    ks->SignInUser(0, g_player1, name1);
    note = g_player1 != g_signed1 ? kSigninChanged : kProfileSettingChanged;
    g_signed1 = g_player1;
    g_signed1_name = name1;
  }
  if (g_player2 != g_signed2 || name2 != g_signed2_name) {
    if (g_player2) {
      ks->SignInUser(1, g_player2, name2);
    } else {
      ks->SignOutUser(1);
    }
    if (g_player2 != g_signed2) note = kSigninChanged;
    else if (!note) note = kProfileSettingChanged;
    g_signed2 = g_player2;
    g_signed2_name = name2;
  }
  return note;
}

void Notify(uint32_t note) {
  if (!note) return;
  auto* ks = REX_KERNEL_STATE();
  if (!ks) return;
  ks->BroadcastNotification(note, note == kSigninChanged ? ks->signed_in_mask() : 1u);
}

// --- settings screen rows ------------------------------------------------------------------
// profile_p1: one choice per profile. profile_p2: "None", then one per profile.
size_t ChoiceCount(const std::string& key) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_profiles.size() + (key == "profile_p2" ? 1 : 0);
}
size_t CurrentChoice(const std::string& key) {  // 1-based
  std::lock_guard<std::mutex> lock(g_mutex);
  if (key == "profile_p1") return size_t(std::max(IndexOf(g_player1), 0)) + 1;
  return g_player2 ? size_t(std::max(IndexOf(g_player2), 0)) + 2 : 1;
}
void SetChoice(const std::string& key, size_t n) {
  std::lock_guard<std::mutex> lock(g_mutex);
  // One profile cannot be both players: a choice of the other player's profile is ignored
  // (the settings page skips it as well).
  if (key == "profile_p1") {
    if (n >= 1 && n <= g_profiles.size() && g_profiles[n - 1].xuid != g_player2) g_player1 = g_profiles[n - 1].xuid;
  } else if (n == 1) {
    g_player2 = 0;
  } else if (n >= 2 && n - 1 <= g_profiles.size() && g_profiles[n - 2].xuid != g_player1) {
    g_player2 = g_profiles[n - 2].xuid;
  }
}

void Rename(uint64_t xuid) {
  std::string current;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    current = NameOf(xuid);
  }
  if (current.empty()) return;
  rex::kernel::xam::ShowTextEntryDialog(
      "Rename profile", "New gamertag (up to 15 characters).", current, kNameMax,
      [xuid](bool ok, std::string text) {
        text = Clean(text);
        if (!ok || text.empty()) return;
        uint32_t note;
        {
          std::lock_guard<std::mutex> lock(g_mutex);
          const int i = IndexOf(xuid);
          if (i < 0) return;
          REXLOG_INFO("[profiles] renamed {} to {}", g_profiles[size_t(i)].name, text);
          g_profiles[size_t(i)].name = text;
          SaveLocked();
          ShowNamesLocked();
          note = ApplyLocked();
        }
        Notify(note);
      });
}

void Create() {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_profiles.size() >= kMaxProfiles) {
      REXLOG_INFO("[profiles] already {} profiles, the most there can be", kMaxProfiles);
      return;
    }
  }
  rex::kernel::xam::ShowTextEntryDialog(
      "New profile", "Gamertag for the new profile (up to 15 characters).", "", kNameMax,
      [](bool ok, std::string text) {
        text = Clean(text);
        if (!ok || text.empty()) return;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_profiles.size() >= kMaxProfiles) return;
        const Profile p{NewXuid(), text};
        g_profiles.push_back(p);
        REXLOG_INFO("[profiles] created {} ({:016X})", p.name, p.xuid);
        SaveLocked();
        ShowNamesLocked();
        g_list_changed.store(true);
      });
}

}  // namespace

void LoadProfiles() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_profiles.clear();
  g_player1 = kOriginalXuid;
  g_player2 = 0;
  std::ifstream in(FilePath());
  std::string line;
  while (in && std::getline(in, line)) {
    const size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    key.erase(std::remove(key.begin(), key.end(), ' '), key.end());
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
    while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
    if (key == "profile") {
      const size_t space = value.find(' ');
      if (space == std::string::npos) continue;
      const uint64_t xuid = std::strtoull(value.substr(0, space).c_str(), nullptr, 16);
      const std::string name = Clean(value.substr(space + 1));
      if (xuid && !name.empty() && IndexOf(xuid) < 0 && g_profiles.size() < kMaxProfiles)
        g_profiles.push_back({xuid, name});
    } else if (key == "player1") {
      g_player1 = std::strtoull(value.c_str(), nullptr, 16);
    } else if (key == "player2") {
      g_player2 = value == "none" ? 0 : std::strtoull(value.c_str(), nullptr, 16);
    } else if (key == "player1_controller") {
      g_pad1 = value == "first" ? std::string() : value;
    }
  }
  if (IndexOf(kOriginalXuid) < 0 && g_profiles.size() < kMaxProfiles) {
    g_profiles.insert(g_profiles.begin(), Profile{kOriginalXuid, "User"});
  }
  if (IndexOf(g_player1) < 0) g_player1 = g_profiles.front().xuid;
  if (g_player2 && (IndexOf(g_player2) < 0 || g_player2 == g_player1)) g_player2 = 0;
  SaveLocked();
  REXLOG_INFO("[profiles] {} profile(s); Player 1 {}, Player 2 {}", g_profiles.size(), NameOf(g_player1),
              g_player2 ? NameOf(g_player2) : std::string("none"));
}

void ApplyProfiles() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ApplyLocked();  // at start: the game has not read anyone yet, so nothing to tell it
  ApplyPadLocked();
}

std::string ProfileName(uint64_t xuid) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return NameOf(xuid);
}

bool IsProfileKey(const std::string& key) {
  return key == "profile_p1" || key == "profile_p2" || key == "controller_p1" || key.rfind("action:", 0) == 0;
}

std::string ProfilesCommand(const std::string& command, const std::string& key, const std::string& value) {
  if (command == "bbrr.profiles.changed") return g_list_changed.exchange(false) ? "1" : "0";
  if (command == "bbrr.profiles.show") {
    std::lock_guard<std::mutex> lock(g_mutex);
    ShowNamesLocked();
    ListPadsLocked();
    return "ok";
  }
  if (key == "controller_p1") {
    // Player 1 Controller: one option per connected controller (none: one "None" option).
    std::lock_guard<std::mutex> lock(g_mutex);
    const size_t count = std::max<size_t>(g_pad_keys.size(), 1);
    if (command == "bbrr.choices") return std::to_string(count);
    if (command == "bbrr.label") {
      const long n = std::strtol(value.c_str(), nullptr, 10);
      if (g_pad_keys.empty()) return "BBRR_NONE";
      return "STR" + std::to_string(kFirstPadSlot + uint32_t(std::clamp<long>(n, 1, long(kMaxPads)) - 1));
    }
    if (command == "bbrr.get") {
      for (size_t i = 0; i < g_pad_keys.size(); ++i) if (g_pad_keys[i] == g_pad1) return std::to_string(i + 1);
      return "1";  // the first connected plays as Player 1 unless another is chosen
    }
    if (command == "bbrr.default") return "1";
    if (command == "bbrr.set") {
      const long n = std::strtol(value.c_str(), nullptr, 10);
      if (n >= 1 && size_t(n) <= g_pad_keys.size()) {
        const bool unchanged_default = g_pad1.empty() && n == 1;
        if (!unchanged_default) g_pad1 = g_pad_keys[size_t(n - 1)];
      }
      return "ok";
    }
    return "";
  }
  if (command == "bbrr.profiles.save") {
    // The settings page was saved: keep the choices and sign the players in.
    uint32_t note;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_player2 == g_player1) g_player2 = 0;
      SaveLocked();
      ShowNamesLocked();
      note = ApplyLocked();
      ApplyPadLocked();
    }
    Notify(note);
    return "ok";
  }
  if (command == "bbrr.action") {
    uint64_t p1, p2;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      p1 = g_player1;
      p2 = g_player2;
    }
    if (key == "rename_p1") Rename(p1);
    else if (key == "rename_p2" && p2) Rename(p2);
    else if (key == "new_profile") Create();
    return "ok";
  }
  if (command == "bbrr.label") {
    // What a row's option n shows: a stored string, or a text-table name.
    const long n = std::strtol(value.c_str(), nullptr, 10);
    // The rename rows show only their title ("Rename Player 1"), no name next to it.
    if (key == "action:rename_p1" || key == "action:rename_p2") return "STR" + std::to_string(kBlankSlot);
    if (key == "action:new_profile") return "BBRR_CREATE";
    if (key == "profile_p2" && n == 1) return "BBRR_NONE";
    const long index = key == "profile_p2" ? n - 2 : n - 1;
    if (index >= 0 && index < long(kMaxProfiles)) return "STR" + std::to_string(kFirstNameSlot + uint32_t(index));
    return "";
  }
  if (command == "bbrr.choices") return std::to_string(ChoiceCount(key));
  if (command == "bbrr.get") return std::to_string(CurrentChoice(key));
  if (command == "bbrr.default") return "1";
  if (command == "bbrr.set") {
    SetChoice(key, size_t(std::max(0L, std::strtol(value.c_str(), nullptr, 10))));
    return "ok";
  }
  return "";
}

}  // namespace bbr
