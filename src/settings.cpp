// Revolution settings: what the in-game settings screen can change, and the file they
// are saved in (<user folder>/boomboomrocket/settings.ini, one "cvar = value" per line).
//
// Each setting is a list of choices for one cvar; the screen works with option numbers.

#include "settings.h"
#include "modifiers.h"
#include "widescreen.h"
#include <atomic>
#include <thread>
#include "metronome.h"
#include "input_offset.h"
#include "autoplay.h"
#include "profiles.h"
#include "keyboard.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <windows.h>
#endif
#include <cstring>

#include <cctype>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

REXCVAR_DEFINE_STRING(frame_rate, "display", "BBR",
                      "Frames per second: display (match the main display's refresh rate) or a number. "
                      "--video_mode_refresh_rate on the command line overrides it");

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
    Setting input{"input_offset", "input_offset_ms", "Input Offset", {}, 20};
    for (int i = 0; i < 41; ++i) {
      const int ms = -100 + i * 5;
      input.choices.push_back({std::to_string(ms), std::to_string(ms) + " ms"});
    }
    s.push_back(input);
    s.push_back({"hit_timing", "hit_timing", "Show Accuracy", {{"true", "On"}, {"false", "Off"}}, 0});
    s.push_back({"assist_tick", "assist_tick", "Assist Tick", {{"true", "On"}, {"false", "Off"}}, 1});
    s.push_back({"skip_intro", "skip_intro", "Skip Intro", {{"true", "On"}, {"false", "Off"}}, 0});
    s.push_back({"layout", "ui_anchor", "Menu Layout", {{"edges", "Screen Edges"}, {"center", "Centred"}}, 0});
    s.push_back({"aspect", "aspect_ratio", "Aspect Ratio",
                 {{"auto", "Match Window"}, {"16:9", "16:9"}, {"21:9", "21:9"}, {"32:9", "32:9"}}, 0, "video"});
    s.push_back({"window", "fullscreen", "Window Mode", {{"true", "Fullscreen"}, {"false", "Windowed"}}, 0, "video"});
    s.push_back({"antialiasing", "swap_post_effect", "Anti-Aliasing",
                 {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA High"}}, 0, "video"});
    s.push_back({"glow", "output_glow", "Glow",
                 {{"0", "Off"}, {"0.3", "Low"}, {"0.6", "Medium"}, {"1", "High"}}, 0, "video"});
    // City lighting (src/city_lighting.cpp, SDK patch 0011): the value is a strength multiplier.
    s.push_back({"citylight", "city_lighting", "City Lighting",
                 {{"0", "Off"}, {"1", "Low"}, {"2", "Medium"}, {"4", "High"}, {"8", "Very High"}, {"32", "Extreme"}}, 0,
                 "video"});
    // A multiplier on the original particle count: 0.25x steps below 1x, whole steps up to 5x.
    // (10x fills the game's spark system within seconds and has crashed it; 5x stays under half.)
    Setting particles{"particles", "particle_quality", "Particle Density",
                      {{"0", "0x"}, {"0.25", "0.25x"}, {"0.5", "0.5x"}, {"0.75", "0.75x"}}, 4, "video"};
    for (int i = 1; i <= 5; ++i) {
      particles.choices.push_back({std::to_string(i), std::to_string(i) + "x"});
    }
    s.push_back(particles);
    // Scroll speed, as in StepMania: arrows travel faster and sit further apart, timing unchanged.
    s.push_back({"scroll_speed", "scroll_speed", "Scroll Speed",
                 {{"0.25", "0.25x"}, {"0.5", "0.5x"}, {"0.75", "0.75x"}, {"1", "1x"}, {"1.25", "1.25x"}, {"1.5", "1.5x"}, {"1.75", "1.75x"}, {"2", "2x"}, {"2.25", "2.25x"}, {"2.5", "2.5x"}, {"2.75", "2.75x"}, {"3", "3x"}, {"3.25", "3.25x"}, {"3.5", "3.5x"}, {"3.75", "3.75x"}, {"4", "4x"}},
                 3, "modifiers"});
    // Arrows speed up as they climb (original) or move at an even pace (src/modifiers.cpp).
    s.push_back({"rocket_acceleration", "rocket_acceleration", "Rocket Acceleration",
                 {{"false", "Default"}, {"true", "Static"}}, 0, "modifiers"});
    // The city is not drawn during a song (src/modifiers.cpp).
    // Arrows go straight up instead of drifting towards their firework (src/modifiers.cpp).
    s.push_back({"horizontal_drift", "horizontal_drift", "Horizontal Drift",
                 {{"true", "Default"}, {"false", "Off"}}, 0, "modifiers"});
    // Arrows in four lanes by button, like DDR (src/modifiers.cpp).
    s.push_back({"ddr_mode", "ddr_mode", "DDR Mode", {{"false", "Off"}, {"true", "On"}}, 0, "modifiers"});
    // Keyboard as a controller (W A S D face buttons, arrow-key d-pad; see KeyboardDefaults).
    s.push_back({"keyboard", "mnk_mode", "Use Keyboard", {{"false", "Off"}, {"true", "On"}}, 1, "keyboard"});
    // How strongly the Visualizer reacts to the PC's music (src/visualizer.cpp).
    s.push_back({"visualizer", "visualizer_sensitivity", "Visualizer Sensitivity",
                 {{"0.5", "Low"}, {"1", "Normal"}, {"2", "High"}, {"4", "Very High"}}, 1, "main"});
    // Every rocket hit at 0 ms; songs that used it save no score (src/autoplay.cpp).
    s.push_back({"autoplay", "autoplay", "Autoplay", {{"false", "Off"}, {"true", "On"}}, 0, "modifiers"});
    s.push_back({"no_background", "no_background", "Background", {{"false", "Default"}, {"true", "Off"}}, 0,
                 "modifiers"});
    s.push_back({"hdr", "output_hdr", "HDR", {{"false", "Off"}, {"true", "On"}}, 0, "video"});
    s.push_back({"resolution", "resolution_scale", "Resolution",
                 {{"1", "720p"}, {"2", "1440p"}, {"3", "2160p"}, {"4", "2880p"}}, 1, "video", true});
    return s;
  }();
  return settings;
}

// On a page, settings that need a restart go last (the footnote says "the last N settings").
const std::vector<Page>& Pages() {
  static const std::vector<Page> pages = {
      {"main", "BBRR Settings", {"page:profiles", "page:video", "page:timing", "page:modifiers", "page:keyboard", "visualizer", "skip_intro", "layout"}},
      // Rows other than "keyboard" handled by src/keyboard.cpp.
      {"keyboard", "Keyboard", {"keyboard", "keyboard_dpad", "action:bind_a", "action:bind_b", "action:bind_x",
                                "action:bind_y", "action:bind_lb", "action:bind_rb", "action:bind_triggers",
                                "action:bind_start", "action:keys_reset"}},
      // Rows handled by src/profiles.cpp.
      {"profiles", "Profiles", {"profile_p1", "profile_p2", "controller_p1", "action:rename_p1", "action:rename_p2", "action:new_profile"}},
      {"modifiers", "Modifiers", {"scroll_speed", "rocket_acceleration", "horizontal_drift", "ddr_mode", "no_background", "autoplay"}},
      {"video", "Video Settings", {"aspect", "window", "antialiasing", "glow", "citylight", "particles", "hdr", "resolution"}},
      // Its own screen (ui_scripts/Options/BBRR_calibrate.lua), listed here for its title.
      {"calibrate", "Auto Calibration", {}},
      {"timing", "Calibration", {"page:calibrate", "audio_offset", "input_offset", "hit_timing", "assist_tick"}},
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

// Keyboard (the SDK's keyboard-as-controller, --mnk_mode): the port's own default keys,
// replacing the SDK's. Face buttons on W A S D in the same places as on the pad (W = Y, A = X,
// S = A, D = B), d-pad on the arrow keys. Enter also confirms (A), Esc pauses (Start),
// Backspace also goes back (B), Q / E are LB / RB (scroll speed), Space pulls both triggers
// (Bonus Run). Sticks are not used by the game, so they get no keys. Saved in settings.ini
// with the other settings, so they can be changed there (and later on a settings page).
const std::vector<std::pair<std::string, std::string>>& KeyboardDefaults() {
  static const std::vector<std::pair<std::string, std::string>> keys = {
      {"keybind_a", "S,Return"},        {"keybind_b", "D,Backspace"},
      {"keybind_x", "A"},               {"keybind_y", "W"},
      {"keybind_dpad_up", "Up"},        {"keybind_dpad_down", "Down"},
      {"keybind_dpad_left", "Left"},    {"keybind_dpad_right", "Right"},
      {"keybind_left_shoulder", "Q"},   {"keybind_right_shoulder", "E"},
      {"keybind_left_trigger", "Space"}, {"keybind_right_trigger", "Space"},
      {"keybind_start", "Escape"},      {"keybind_back", "Tab"},
      {"keybind_guide", ""},
      {"keybind_lstick_up", ""},        {"keybind_lstick_down", ""},
      {"keybind_lstick_left", ""},      {"keybind_lstick_right", ""},
      {"keybind_lstick_press", ""},     {"keybind_rstick_up", ""},
      {"keybind_rstick_down", ""},      {"keybind_rstick_left", ""},
      {"keybind_rstick_right", ""},     {"keybind_rstick_press", ""},
  };
  return keys;
}
bool IsKeybind(const std::string& cvar) {
  for (const auto& [name, keys] : KeyboardDefaults()) if (name == cvar) return true;
  return false;
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

// Sets the runtime's guest refresh rate. There is no menu setting for it: the game runs
// at the main display's refresh rate, and (with the SDK patch in patches/rexglue-sdk)
// each game frame is started on a real display refresh, so motion does not judder.
// --frame_rate=<number> or --video_mode_refresh_rate on the command line override it;
// --vsync=false removes the limit, --vblank_display_sync=false goes back to a timer.
void ApplyFrameRate();

// Refresh rate of the main display in Hz (0 if unknown).
double DisplayRefreshRate() {
#if defined(_WIN32)
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
    return double(mode.dmDisplayFrequency);
  }
#endif
  return 0.0;
}

void ApplyFrameRate() {
  if (rex::cvar::GetFlagSource("video_mode_refresh_rate") == rex::cvar::Source::kCommandLine) return;
  const std::string wanted = rex::cvar::GetFlagByName("frame_rate");
  double hz = std::strtod(wanted.c_str(), nullptr);
  if (wanted == "display" || hz < 24.0) {
    hz = DisplayRefreshRate();
    if (hz < 24.0) hz = 60.0;
  }
  hz = std::min(hz, 240.0);  // the runtime's upper limit
  rex::cvar::SetFlagByName("video_mode_refresh_rate", std::to_string(hz));
  REXLOG_INFO("[settings] frame rate {} -> {:.0f} fps", wanted, hz);
}

namespace {
std::filesystem::path BootMarker() { return FilePath().parent_path() / "cache" / "starting.txt"; }

void GuardShaderCache() {
  std::error_code ec;
  const std::filesystem::path marker = BootMarker();
  if (std::filesystem::exists(marker, ec)) {
    const std::filesystem::path shaders = marker.parent_path() / "shaders";
    const auto removed = std::filesystem::remove_all(shaders, ec);
    REXLOG_WARN("[settings] the last start did not finish; cleared the saved shaders ({} files) in {}",
                ec ? 0 : removed, shaders.string());
  }
  std::filesystem::create_directories(marker.parent_path(), ec);
  std::ofstream(marker, std::ios::trunc) << "The game is starting. Removed once it is running.\n";
}
}  // namespace

void BootCompleted() {
  static bool done = false;
  if (done) return;
  done = true;
  std::error_code ec;
  std::filesystem::remove(BootMarker(), ec);
}

void LoadSettings() {
  GuardShaderCache();
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
  for (const auto& [cvar, keys] : KeyboardDefaults()) {
    if (!locked(cvar)) SetAtBoot(cvar, keys);
  }
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
    ApplyFrameRate();
    return;
  }
  std::string line;
  int applied = 0;
  while (std::getline(in, line)) {
    line = Trim(line);
    const size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    const std::string cvar = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
    bool known = IsKeybind(cvar);
    for (const Setting& s : Settings()) known |= (s.cvar == cvar);
    if (!known || locked(cvar)) continue;
    SetAtBoot(cvar, value);
    intended[cvar] = value;
    ++applied;
  }
  snapshot();
  REXLOG_INFO("[settings] loaded {} setting(s) from {}", applied, FilePath().string());
  ApplyFrameRate();
}

void ExitSoon() {
#if defined(_WIN32)
  static std::atomic<bool> started{false};
  if (started.exchange(true)) return;
  std::thread([] {
    Sleep(1500);
    TerminateProcess(GetCurrentProcess(), 0);
  }).detach();
#endif
}

int g_pending_keyboard = -1;  // Use Keyboard choice waiting for the page to be saved (0-based)

std::string SettingsCommand(const std::string& command, const std::string& key, const std::string& value) {
  if (command == "bbrr.save") {
    if (g_pending_keyboard >= 0) {
      if (const Setting* k = Find("keyboard")) Apply(*k, size_t(g_pending_keyboard));
      g_pending_keyboard = -1;
    }
    std::error_code ec;
    std::filesystem::create_directories(FilePath().parent_path(), ec);
    std::ofstream out(FilePath(), std::ios::trunc);
    out << "# Boom Boom Rocket Revolution settings (written by the in-game settings screen)\n";
    for (const Setting& s : Settings()) out << s.cvar << " = " << rex::cvar::GetFlagByName(s.cvar) << "\n";
    out << "# Keyboard keys (names as in the SDK's keybinds, several separated by commas, empty = none)\n";
    for (const auto& [cvar, keys] : KeyboardDefaults()) out << cvar << " = " << rex::cvar::GetFlagByName(cvar) << "\n";
    REXLOG_INFO("[settings] saved to {}", FilePath().string());
    ProfilesCommand("bbrr.profiles.save", "", "");
    return "ok";
  }
  if (command == "bbrr.count" || command == "bbrr.key" || command == "bbrr.note") {
    if (command == "bbrr.count" && key == "profiles") ProfilesCommand("bbrr.profiles.show", "", "");
    if (command == "bbrr.count" && key == "keyboard") KeyboardCommand("bbrr.keyboard.show", "", "");
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
  if (command.rfind("bbrr.hit.", 0) == 0) return HitCommand(command);
  if (command.rfind("bbrr.speed.", 0) == 0) return SpeedCommand(command);
  if (command.rfind("bbrr.autoplay.", 0) == 0) return AutoplayCommand(command);
  if (command == "bbrr.ui.keep") {
    // key = "x0,y0,x1,y1": a rectangle whose contents must not follow a window edge.
    float r[4];
    if (std::sscanf(key.c_str(), "%f,%f,%f,%f", &r[0], &r[1], &r[2], &r[3]) == 4) KeepUiCentred(r[0], r[1], r[2], r[3]);
    return "";
  }
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
  if (command == "bbrr.calibrate_input") {
    // key = how late the taps were in the picture test (ms). With the sound lined up to
    // the picture by Audio Offset, that is the delay between seeing and the press arriving.
    const Setting* offset = Find("input_offset");
    if (!offset) return "";
    const double wanted = std::strtod(key.c_str(), nullptr);
    size_t best = offset->default_choice;
    double best_error = 1e18;
    for (size_t i = 0; i < offset->choices.size(); ++i) {
      const double error = std::fabs(std::strtod(offset->choices[i].value.c_str(), nullptr) - wanted);
      if (error < best_error) { best_error = error; best = i; }
    }
    REXLOG_INFO("[settings] calibration: picture taps {:.1f} ms late -> input offset {} ms", wanted,
                offset->choices[best].value);
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
  if (command == "bbrr.keys.reset") {
    for (const auto& [cvar, keys] : KeyboardDefaults()) rex::cvar::SetFlagByName(cvar, keys);
    SettingsCommand("bbrr.save", "", "");
    REXLOG_INFO("[keyboard] default keys restored");
    return "ok";
  }
  if ((command == "bbrr.action" && IsKeyboardAction(key)) ||
      (IsKeyboardKey(key) && (command == "bbrr.choices" || command == "bbrr.get" || command == "bbrr.set" ||
                              command == "bbrr.default" || command == "bbrr.label"))) {
    return KeyboardCommand(command, key, value);
  }
  if (command.rfind("bbrr.profiles.", 0) == 0 || command == "bbrr.action" ||
      (IsProfileKey(key) && (command == "bbrr.choices" || command == "bbrr.get" || command == "bbrr.set" ||
                             command == "bbrr.default" || command == "bbrr.label"))) {
    return ProfilesCommand(command, key, value);
  }
  if (command == "bbrr.label") return "";
  const Setting* s = Find(key);
  if (!s) return "";
  if (command == "bbrr.choices") return std::to_string(s->choices.size());
  // Use Keyboard only changes when the page is saved (A): turned off at once, the keyboard
  // driving the menu would stop before it could be turned back on. (Switching it also adds or
  // removes the keyboard as a device, which made the pad's held d-pad press count twice.)
  if (s->cvar == "mnk_mode") {
    if (command == "bbrr.get") return std::to_string(g_pending_keyboard >= 0 ? g_pending_keyboard + 1 : int(CurrentChoice(*s)) + 1);
    if (command == "bbrr.set") {
      const long n = std::strtol(value.c_str(), nullptr, 10);
      if (n >= 1 && size_t(n) <= s->choices.size()) g_pending_keyboard = int(n - 1);
      return "ok";
    }
  }
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
                    "BBRR_T_profile_p1 = Player 1\n"
                    "BBRR_T_profile_p2 = Player 2\n"
                    "BBRR_T_controller_p1 = Player 1 Controller\n"
                    "BBRR_T_action_rename_p1 = Rename Player 1\n"
                    "BBRR_T_action_rename_p2 = Rename Player 2\n"
                    "BBRR_T_action_new_profile = New Profile\n"
                    "BBRR_CREATE = Create\n"
                    "BBRR_NONE = None\n"
                    "BBRR_RESET = Reset\n"
                    "BBRR_DPAD_ARROWS = Arrow Keys\n"
                    "BBRR_DPAD_NUMPAD = Numpad 8 4 2 6\n"
                    "BBRR_DPAD_IJKL = I J K L\n"
                    "BBRR_T_keyboard_dpad = D-pad\n"
                    "BBRR_T_action_bind_a = A (green)\n"
                    "BBRR_T_action_bind_b = B (red)\n"
                    "BBRR_T_action_bind_x = X (blue)\n"
                    "BBRR_T_action_bind_y = Y (yellow)\n"
                    "BBRR_T_action_bind_lb = LB (speed down)\n"
                    "BBRR_T_action_bind_rb = RB (speed up)\n"
                    "BBRR_T_action_bind_triggers = Triggers (Bonus Run)\n"
                    "BBRR_T_action_bind_start = Start (pause)\n"
                    "BBRR_T_action_keys_reset = Reset Keys\n"
                    "BBRR_SPEED_LABEL = Scroll Speed\n"
                    "BBRR_SPEED_HINT = ~~{LB}~~ Change ~~{RB}~~\n"
                    "BBRR_BPM = BPM\n"
                    "BBRR_LENGTH = Length\n"
                    "BBRR_RESTART_NOTE = A new Resolution applies after a restart\n"
                    "BBRR_RESTART_TITLE = Restart needed\n"
                    "BBRR_RESTART_INFO = Your settings are saved. Some of the changes only take effect after a restart. Restart the game now?\n"
                    "BBRR_CAL_SOUND_INTRO = Sound test. Press ~~{A}~~ on every beat\n"
                    "BBRR_CAL_SOUND_RUN = Press ~~{A}~~ on every beat\n"
                    "BBRR_CAL_PICTURE_INTRO = Picture test. No sound this time\n"
                    "BBRR_CAL_PICTURE_RUN = Press ~~{A}~~ every time a bar reaches the line\n"
                    "BBRR_CAL_BEGIN = Press ~~{A}~~ to begin\n"
                    "BBRR_CAL_RESULT = Suggested offsets. Press ~~{A}~~ to save\n"
                    "BBRR_CAL_FAILED = Those taps were too uneven. Let us try again\n"
                    "BBRR_CAL_READY = Get ready\n"
                    "BBRR_CAL_TAP_M3 = -3 / 16\n"
                    "BBRR_CAL_TAP_M2 = -2 / 16\n"
                    "BBRR_CAL_TAP_M1 = -1 / 16\n"
                    "BBRR_CAL_RING = O\n"
                    "BBRR_EXIT_GAME = Exit Game\n"
                    "BBRR_RESTART_SONG = Restart Song\n"
                    "BBRR_REPLAY_SONG = Replay Song\n"
                    "BBRR_RESTART_SONG_INFO = Are you sure you want to restart this song?  Doing so will mean your current score will not be saved.\n"
                    "BBRR_EXIT_GAME_INFO = Close Boom Boom Rocket and return to Windows.\n"
                    "BBRR_X_RESTART = ~~{X}~~ Restart\n"
                    "BBRR_B_LATER = Not now ~~{B}~~\n";
  for (const Page& p : Pages()) out += "BBRR_T_page_" + p.name + " = " + p.title + "\n";
  for (int i = 0; i <= 16; ++i) out += "BBRR_CAL_TAP_" + std::to_string(i) + " = " + std::to_string(i) + " / 16\n";
  for (const Setting& s : Settings()) {
    out += "BBRR_T_" + s.key + " = " + s.title + "\n";
    for (size_t i = 0; i < s.choices.size(); ++i) {
      out += "BBRR_C_" + s.key + "_" + std::to_string(i + 1) + " = " + s.choices[i].label + "\n";
      // "Audio Offset  -15 ms": one line of the calibration result.
      if (s.key == "audio_offset" || s.key == "input_offset") {
        out += "BBRR_CAL_" + s.key + "_" + std::to_string(i + 1) + " = " + s.title + "  " + s.choices[i].label + "\n";
      }
    }
  }
  return out;
}
}  // namespace bbr
