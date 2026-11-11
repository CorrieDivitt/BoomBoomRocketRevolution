// Revolution settings: what the in-game settings screen can change, and the file they
// are saved in (<user folder>/boomboomrocket/settings.ini, one "cvar = value" per line).
//
// Each setting is a list of choices for one cvar; the screen works with option numbers.

#include "settings.h"

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace bbr {
namespace {
struct Choice {
  std::string value;  // cvar value
  std::string label;  // text shown on the settings screen ("" = the screen supplies it)
  std::string text_id;
};
struct Setting {
  std::string key;    // name used by the settings screen
  std::string cvar;
  std::string title_id, title;
  std::vector<Choice> choices;
  size_t default_choice;
};

const std::vector<Setting>& Settings() {
  static const std::vector<Setting> settings = [] {
    std::vector<Setting> s;
    s.push_back({"aspect", "aspect_ratio", "BBRR_ASPECT", "Aspect Ratio",
                 {{"auto", "Match Window", "BBRR_AR_AUTO"},
                  {"16:9", "16:9", "BBRR_AR_169"},
                  {"21:9", "21:9", "BBRR_AR_219"},
                  {"32:9", "32:9", "BBRR_AR_329"}},
                 0});
    s.push_back({"layout", "ui_anchor", "BBRR_LAYOUT", "Menu Layout",
                 {{"edges", "Screen Edges", "BBRR_LAYOUT_EDGES"}, {"center", "Centred", "BBRR_LAYOUT_CENTRE"}},
                 0});
    Setting offset{"audio_offset", "audio_offset_ms", "BBRR_AUDIO_OFFSET", "Audio Offset", {}, 16};
    for (int i = 0; i < 41; ++i) {
      const int ms = -100 + i * 5;
      offset.choices.push_back({std::to_string(ms), std::to_string(ms) + " ms", "BBRR_MS_" + std::to_string(i + 1)});
    }
    s.push_back(offset);
    s.push_back({"skip_intro", "skip_intro", "BBRR_SKIP_INTRO", "Skip Intro", {{"true", "", ""}, {"false", "", ""}}, 0});
    return s;
  }();
  return settings;
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
}  // namespace

void LoadSettings() {
  // Settings the port wants by default when there is no file and no command-line value.
  auto from_command_line = [](const std::string& cvar) {
    return rex::cvar::GetFlagSource(cvar) == rex::cvar::Source::kCommandLine;
  };
  for (const Setting& s : Settings()) {
    if (!from_command_line(s.cvar)) rex::cvar::SetFlagByName(s.cvar, s.choices[s.default_choice].value);
  }
  std::ifstream in(FilePath());
  if (!in) return;
  std::string line;
  int applied = 0;
  while (std::getline(in, line)) {
    line = Trim(line);
    const size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    const std::string cvar = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
    bool known = false;
    for (const Setting& s : Settings()) known |= (s.cvar == cvar);
    if (!known || from_command_line(cvar)) continue;
    if (rex::cvar::SetFlagByName(cvar, value)) ++applied;
  }
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
  const Setting* s = Find(key);
  if (!s) return "";
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
  std::string out = "BBRR_SETTINGS = BBRR Settings\n";
  for (const Setting& s : Settings()) {
    out += s.title_id + " = " + s.title + "\n";
    for (const Choice& c : s.choices) {
      if (!c.text_id.empty()) out += c.text_id + " = " + c.label + "\n";
    }
  }
  return out;
}
}  // namespace bbr
