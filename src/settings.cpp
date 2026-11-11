// Revolution settings: what the in-game settings screen can change, and the file they
// are saved in (<user folder>/boomboomrocket/settings.ini, one "cvar = value" per line).
//
// Each setting is a list of choices for one cvar; the screen works with option numbers.

#include "settings.h"
#include "metronome.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace bbr {
namespace {
struct Choice {
  std::string value;  // cvar value
  std::string label;  // text shown on the settings screen
};
struct Setting {
  std::string key;    // name used by the settings screen (letters, digits, underscore)
  std::string cvar;
  std::string title;
  std::vector<Choice> choices;
  size_t default_choice;
  std::string page = "main";  // which settings page shows it
  bool restart = false;       // only takes effect the next time the game starts
};

// Pages of the settings screen. A row is a setting key or "page:<name>" (opens that page).
struct Page {
  std::string name, title;
  std::vector<std::string> rows;
};

const std::vector<Setting>& Settings() {
  static const std::vector<Setting> settings = [] {
    std::vector<Setting> s;
    Setting offset{"audio_offset", "audio_offset_ms", "Audio Offset", {}, 16};
    for (int i = 0; i < 41; ++i) {
      const int ms = -100 + i * 5;
      offset.choices.push_back({std::to_string(ms), std::to_string(ms) + " ms"});
    }
    s.push_back(offset);
    s.push_back({"skip_intro", "skip_intro", "Skip Intro", {{"true", "On"}, {"false", "Off"}}, 0});
    s.push_back({"layout", "ui_anchor", "Menu Layout", {{"edges", "Screen Edges"}, {"center", "Centred"}}, 0});
    s.push_back({"aspect", "aspect_ratio", "Aspect Ratio",
                 {{"auto", "Match Window"}, {"16:9", "16:9"}, {"21:9", "21:9"}, {"32:9", "32:9"}}, 0, "video"});
    s.push_back({"vsync", "vsync", "VSync", {{"true", "On"}, {"false", "Off"}}, 0, "video"});
    s.push_back({"window", "fullscreen", "Window Mode", {{"true", "Fullscreen"}, {"false", "Windowed"}}, 0, "video", true});
    s.push_back({"fps", "video_mode_refresh_rate", "Frame Rate",
                 {{"60", "60"}, {"90", "90"}, {"120", "120"}, {"144", "144"}, {"165", "165"}, {"240", "240"}},
                 0, "video", true});
    s.push_back({"resolution", "resolution_scale", "Resolution",
                 {{"1", "720p"}, {"2", "1440p"}, {"3", "2160p"}, {"4", "2880p"}}, 1, "video", true});
    s.push_back({"antialiasing", "swap_post_effect", "Anti-Aliasing",
                 {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA High"}}, 0, "video", true});
    return s;
  }();
  return settings;
}

// On a page, settings that need a restart go last (the footnote says "the last N settings").
const std::vector<Page>& Pages() {
  static const std::vector<Page> pages = {
      {"main", "BBRR Settings", {"audio_offset", "page:calibrate", "skip_intro", "layout", "page:video"}},
      {"video", "Video Settings", {"aspect", "vsync", "window", "fps", "resolution", "antialiasing"}},
      // Its own screen (ui_scripts/Options/BBRR_calibrate.lua), listed here for its title.
      {"calibrate", "Calibration", {}},
  };
  return pages;
}

const Page* FindPage(const std::string& name) {
  for (const Page& p : Pages()) if (p.name == name) return &p;
  return nullptr;
}

// Values the restart-only settings had when the game started (= what is really in use).
std::vector<std::pair<std::string, std::string>> g_boot_values;

// Sets a cvar during start-up. Some cvars belong to the GPU plugin, which is not loaded
// yet at that point, so SetFlagByName has nothing to set. For those we set the matching
// REX_<NAME> environment variable: the cvar picks it up when the plugin registers it
// (a command-line value still wins). Returns the value now in force as far as we know.
void SetAtBoot(const std::string& cvar, const std::string& value) {
  if (rex::cvar::SetFlagByName(cvar, value)) return;
  std::string env = "REX_";
  for (char c : cvar) env.push_back(char(std::toupper(static_cast<unsigned char>(c))));
#if defined(_WIN32)
  SetEnvironmentVariableA(env.c_str(), value.c_str());
#else
  setenv(env.c_str(), value.c_str(), 1);
#endif
}

const Setting* Find(const std::string& key) {
  for (const Setting& s : Settings()) if (s.key == key) return &s;
  return nullptr;
}

std::filesystem::path FilePath() {
  return rex::filesystem::GetUserFolder() / "boomboomrocket" / "settings.ini";
}

std::string Trim(std::string v) {
  while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r')) v.pop_back();
  size_t i = 0;
  while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) ++i;
  return v.substr(i);
}

// Which choice matches the cvar's current value (numbers compare by value).
size_t CurrentChoice(const Setting& s) {
  const std::string now = rex::cvar::GetFlagByName(s.cvar);
  for (size_t i = 0; i < s.choices.size(); ++i) if (s.choices[i].value == now) return i;
  char* end = nullptr;
  const double number = std::strtod(now.c_str(), &end);
  if (end != now.c_str()) {
    size_t best = s.default_choice;
    double best_error = 1e18;
    for (size_t i = 0; i < s.choices.size(); ++i) {
      char* e2 = nullptr;
      const double v = std::strtod(s.choices[i].value.c_str(), &e2);
      if (e2 == s.choices[i].value.c_str()) continue;
      if (std::fabs(v - number) < best_error) { best_error = std::fabs(v - number); best = i; }
    }
    if (best_error < 1e17) return best;
  }
  return s.default_choice;
}

void Apply(const Setting& s, size_t choice) {
  const std::string& value = s.choices[choice].value;
  if (rex::cvar::GetFlagByName(s.cvar) == value) return;
  rex::cvar::SetFlagByName(s.cvar, value);
  if (s.cvar == "aspect_ratio") {
    // Other shapes than 16:9 stretch the game's image over the whole window.
    rex::cvar::SetFlagByName("present_letterbox", value == "16:9" ? "true" : "false");
    if (value != "16:9") {
      rex::cvar::SetFlagByName("present_safe_area_x", "100");
      rex::cvar::SetFlagByName("present_safe_area_y", "100");
    }
  }
  REXLOG_INFO("[settings] {} = {}", s.cvar, value);
}

// Starts a fresh copy of the game with the same command line, then ends this one.
void RestartGame() {
  REXLOG_INFO("[settings] restarting the game to apply settings");
#if defined(_WIN32)
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::wstring command_line = GetCommandLineW();
  if (CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    TerminateProcess(GetCurrentProcess(), 0);
  } else {
    REXLOG_ERROR("[settings] could not start a new copy of the game (error {})", GetLastError());
  }
#endif
}
}  // namespace

void LoadSettings() {
  // Settings the port wants by default when there is no file and no command-line value.
  auto from_command_line = [](const std::string& cvar) {
    return rex::cvar::GetFlagSource(cvar) == rex::cvar::Source::kCommandLine;
  };
  // --draw_resolution_scale_x/_y on the command line also count as choosing the resolution.
  const bool custom_resolution = from_command_line("draw_resolution_scale_x") || from_command_line("draw_resolution_scale_y");
  auto locked = [&](const std::string& cvar) {
    return from_command_line(cvar) || (cvar == "resolution_scale" && custom_resolution);
  };
  std::map<std::string, std::string> intended;  // what we asked for, per cvar
  for (const Setting& s : Settings()) {
    if (!locked(s.cvar)) {
      SetAtBoot(s.cvar, s.choices[s.default_choice].value);
      intended[s.cvar] = s.choices[s.default_choice].value;
    }
  }
  // Remember what the restart-only settings start with. A cvar that is not registered
  // yet reads back empty, so prefer what we asked for.
  auto snapshot = [&] {
    g_boot_values.clear();
    for (const Setting& s : Settings()) {
      if (!s.restart) continue;
      const auto it = intended.find(s.cvar);
      g_boot_values.push_back({s.cvar, it != intended.end() ? it->second : rex::cvar::GetFlagByName(s.cvar)});
    }
  };
  std::ifstream in(FilePath());
  if (!in) {
    snapshot();
    return;
  }
  std::string line;
  int applied = 0;
  while (std::getline(in, line)) {
    line = Trim(line);
    const size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    const std::string cvar = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
    bool known = false;
    for (const Setting& s : Settings()) known |= (s.cvar == cvar);
    if (!known || locked(cvar)) continue;
    SetAtBoot(cvar, value);
    intended[cvar] = value;
    ++applied;
  }
  snapshot();
  REXLOG_INFO("[settings] loaded {} setting(s) from {}", applied, FilePath().string());
}

std::string SettingsCommand(const std::string& command, const std::string& key, const std::string& value) {
  if (command == "bbrr.save") {
    std::error_code ec;
    std::filesystem::create_directories(FilePath().parent_path(), ec);
    std::ofstream out(FilePath(), std::ios::trunc);
    out << "# Boom Boom Rocket Revolution settings (written by the in-game settings screen)\n";
    for (const Setting& s : Settings()) out << s.cvar << " = " << rex::cvar::GetFlagByName(s.cvar) << "\n";
    REXLOG_INFO("[settings] saved to {}", FilePath().string());
    return "ok";
  }
  if (command == "bbrr.count" || command == "bbrr.key" || command == "bbrr.note") {
    const Page* page = FindPage(key);
    if (!page) return command == "bbrr.key" ? "" : "0";
    if (command == "bbrr.count") return std::to_string(page->rows.size());
    if (command == "bbrr.key") {
      const long n = std::strtol(value.c_str(), nullptr, 10);
      return (n >= 1 && size_t(n) <= page->rows.size()) ? page->rows[size_t(n - 1)] : "";
    }
    for (const std::string& row : page->rows) {
      const Setting* s = Find(row);
      if (s && s->restart) return "1";
    }
    return "0";
  }
  if (command.rfind("bbrr.metro.", 0) == 0) return MetronomeCommand(command, key, value);
  if (command == "bbrr.time") {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", duration<double, std::milli>(steady_clock::now() - start).count());
    return text;
  }
  if (command == "bbrr.calibrate") {
    // key = how late the taps were in the sound test, value = in the picture test (ms).
    // Both include the player's input delay, so their difference is how far the sound
    // trails the picture. The song clock is already shifted by the host audio queue
    // (see src/song_clock.cpp), so only the rest goes into Audio Offset.
    const Setting* offset = Find("audio_offset");
    if (!offset) return "";
    const double sound = std::strtod(key.c_str(), nullptr), picture = std::strtod(value.c_str(), nullptr);
    double queue_ms = 0.0;
    if (rex::cvar::GetFlagByName("audio_sync_compensation") != "false") {
      int frames = std::atoi(rex::cvar::GetFlagByName("audio_maxqframes").c_str());
      if (frames <= 0) frames = 8;
      queue_ms = frames * 256.0 * 1000.0 / 48000.0;
    }
    const double wanted = (sound - picture) - queue_ms;
    size_t best = offset->default_choice;
    double best_error = 1e18;
    for (size_t i = 0; i < offset->choices.size(); ++i) {
      const double error = std::fabs(std::strtod(offset->choices[i].value.c_str(), nullptr) - wanted);
      if (error < best_error) { best_error = error; best = i; }
    }
    REXLOG_INFO("[settings] calibration: sound taps {:.1f} ms late, picture taps {:.1f} ms late, audio queue {:.1f} ms "
                "-> audio offset {:.1f} ms, using {} ms",
                sound, picture, queue_ms, wanted, offset->choices[best].value);
    return std::to_string(best + 1);
  }
  if (command == "bbrr.restart_needed") {
    for (const auto& [cvar, value_at_boot] : g_boot_values) {
      const std::string now = rex::cvar::GetFlagByName(cvar);
      if (value_at_boot.empty() || now.empty() || now == value_at_boot) continue;
      // "120" and "120.000000" are the same setting.
      char *e1 = nullptr, *e2 = nullptr;
      const double a = std::strtod(now.c_str(), &e1), b = std::strtod(value_at_boot.c_str(), &e2);
      if (e1 != now.c_str() && e2 != value_at_boot.c_str() && *e1 == 0 && *e2 == 0 && std::fabs(a - b) < 1e-6) continue;
      return "1";
    }
    return "0";
  }
  if (command == "bbrr.restart") {
    RestartGame();
    return "ok";
  }
  const Setting* s = Find(key);
  if (!s) return "";
  if (command == "bbrr.choices") return std::to_string(s->choices.size());
  if (command == "bbrr.get") return std::to_string(CurrentChoice(*s) + 1);
  if (command == "bbrr.default") return std::to_string(s->default_choice + 1);
  if (command == "bbrr.set") {
    const long n = std::strtol(value.c_str(), nullptr, 10);
    if (n >= 1 && size_t(n) <= s->choices.size()) Apply(*s, size_t(n - 1));
    return "ok";
  }
  return "";
}

std::string SettingsStrings() {
  std::string out = "BBRR_SETTINGS = BBRR Settings\n"
                    "BBRR_OPEN = Open\n"
                    "BBRR_RESTART_NOTE = The last four settings apply after a restart\n"
                    "BBRR_RESTART_TITLE = Restart needed\n"
                    "BBRR_RESTART_INFO = Your settings are saved. Some of the changes only take effect after a restart. Restart the game now?\n"
                    "BBRR_CAL_SOUND_INTRO = Sound test. Press ~~{A}~~ on every beat\n"
                    "BBRR_CAL_SOUND_RUN = Press ~~{A}~~ on every beat\n"
                    "BBRR_CAL_PICTURE_INTRO = Picture test. No sound this time\n"
                    "BBRR_CAL_PICTURE_RUN = Press ~~{A}~~ every time a bar reaches the line\n"
                    "BBRR_CAL_BEGIN = Press ~~{A}~~ to begin\n"
                    "BBRR_CAL_RESULT = Suggested audio offset. Press ~~{A}~~ to save\n"
                    "BBRR_CAL_FAILED = Those taps were too uneven. Let us try again\n"
                    "BBRR_CAL_READY = Get ready\n"
                    "BBRR_CAL_TAP_M3 = -3 / 16\n"
                    "BBRR_CAL_TAP_M2 = -2 / 16\n"
                    "BBRR_CAL_TAP_M1 = -1 / 16\n"
                    "BBRR_CAL_RING = O\n"
                    "BBRR_X_RESTART = ~~{X}~~ Restart\n"
                    "BBRR_B_LATER = Not now ~~{B}~~\n";
  for (const Page& p : Pages()) out += "BBRR_T_page_" + p.name + " = " + p.title + "\n";
  for (int i = 0; i <= 16; ++i) out += "BBRR_CAL_TAP_" + std::to_string(i) + " = " + std::to_string(i) + " / 16\n";
  for (const Setting& s : Settings()) {
    out += "BBRR_T_" + s.key + " = " + s.title + "\n";
    for (size_t i = 0; i < s.choices.size(); ++i) {
      out += "BBRR_C_" + s.key + "_" + std::to_string(i + 1) + " = " + s.choices[i].label + "\n";
    }
  }
  return out;
}
}  // namespace bbr
