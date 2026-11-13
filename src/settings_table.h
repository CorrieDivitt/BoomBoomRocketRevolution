// The port's settings: every setting the in-game BBRR Settings screen offers (cvar, choices,
// default) and how the screen's pages are laid out. Shared by the game (src/settings.cpp)
// and the launcher (launcher/), which offers the Video Settings page and a few others, so
// both always offer the same choices. Plain C++: no SDK headers.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace bbr::settings_table {

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

inline const std::vector<Setting>& Settings() {
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
    s.push_back({"hit_timing", "hit_timing", "Show Accuracy", {{"true", "On"}, {"false", "Off"}}, 0, "modifiers"});
    s.push_back({"assist_tick", "assist_tick", "Assist Tick", {{"true", "On"}, {"false", "Off"}}, 1, "modifiers"});
    s.push_back({"skip_intro", "skip_intro", "Skip Intro", {{"true", "On"}, {"false", "Off"}}, 0});
    s.push_back({"layout", "ui_anchor", "Menu Layout", {{"edges", "Screen Edges"}, {"center", "Centred"}}, 0});
    s.push_back({"aspect", "aspect_ratio", "Aspect Ratio",
                 {{"auto", "Match Window"}, {"16:9", "16:9"}, {"21:9", "21:9"}, {"32:9", "32:9"}}, 0, "video"});
    // Sets --fullscreen and --window_borderless (SDK patch 0021) in src/settings.cpp.
    s.push_back({"window", "window_mode", "Window Mode",
                 {{"fullscreen", "Fullscreen"}, {"windowed", "Windowed"}, {"borderless", "Borderless Window"}}, 0,
                 "video"});
    s.push_back({"antialiasing", "swap_post_effect", "Anti-Aliasing",
                 {{"none", "Off"}, {"fxaa", "FXAA"}, {"fxaa_extreme", "FXAA High"}}, 0, "video"});
    // The SDK's anisotropic_override (0 off, 2 = 4x ... 5 = 16x); the SDK default is 4x.
    // Read whenever a sampler is set up, so a change applies at once.
    s.push_back({"aniso", "anisotropic_override", "Texture Filtering",
                 {{"0", "Off"}, {"2", "2x"}, {"3", "4x"}, {"4", "8x"}, {"5", "16x"}}, 2, "video"});
    s.push_back({"glow", "output_glow", "Glow",
                 {{"0", "Off"}, {"0.3", "Low"}, {"0.6", "Medium"}, {"1", "High"}}, 0, "video"});
    // City lighting (src/city_lighting.cpp, SDK patch 0011): the value is a strength multiplier.
    s.push_back({"citylight", "city_lighting", "City Lighting",
                 {{"0", "Off"}, {"1", "Low"}, {"2", "Medium"}, {"4", "High"}, {"8", "Very High"}, {"32", "Acid Trip"}}, 2,
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
                 {{"0.5", "Low"}, {"1", "Normal"}, {"2", "High"}, {"4", "Very High"}}, 1, "modifiers"});
    // Every rocket hit at 0 ms; songs that used it save no score (src/autoplay.cpp).
    // How fast the song's camera travels (src/camera_speed.cpp); applies from the next song.
    s.push_back({"camera_speed", "camera_speed", "Camera Speed",
                 {{"0", "0"}, {"10", "10"}, {"20", "20"}, {"30", "30"}, {"40", "40"}, {"50", "50"}, {"60", "60"},
                  {"70", "70"}, {"80", "80"}, {"90", "90"}, {"100", "100"}},
                 10, "modifiers"});
    s.push_back({"autoplay", "autoplay", "Autoplay", {{"false", "Off"}, {"true", "On"}}, 0, "modifiers"});
    s.push_back({"no_background", "no_background", "Background", {{"false", "Default"}, {"true", "Off"}}, 0,
                 "modifiers"});
    // Fireworks and the water (src/city_lighting.cpp, SDK patch 0019).
    s.push_back({"waterglow", "water_glow", "Water Glow",
                 {{"0", "Off"}, {"0.5", "Low"}, {"1", "Medium"}, {"2", "High"}}, 3, "video"});
    // Water Reflections (R-417) is parked and kept out of the menus and settings.ini while it is a
    // test; developers can still turn it on with --water_reflections=true.
    s.push_back({"hdr", "output_hdr", "HDR", {{"false", "Off"}, {"true", "On"}}, 0, "video"});
    // Frame Rate (frame_rate): Monitor runs one game frame per display refresh; a number runs the
    // game at that rate from a timer; Unlocked as fast as the PC can (src/settings.cpp ApplyFrameRate).
    s.push_back({"framerate", "frame_rate", "Frame Rate",
                 {{"60", "60"}, {"120", "120"}, {"144", "144"}, {"240", "240"}, {"unlocked", "Unlocked"},
                  {"display", "Monitor"}},
                 5, "video", true});
    s.push_back({"resolution", "resolution_scale", "Resolution",
                 {{"1", "720p"}, {"2", "1440p"}, {"3", "2160p"}, {"4", "2880p"}}, 0, "video", true});
    return s;
  }();
  return settings;
}

// On a page, settings that need a restart go last (the footnote says "the last N settings").
inline const std::vector<Page>& Pages() {
  static const std::vector<Page> pages = {
      {"main", "BBRR Settings", {"page:profiles", "page:timing", "page:video", "page:modifiers", "page:keyboard", "skip_intro", "layout"}},
      // Rows other than "keyboard" handled by src/keyboard.cpp.
      {"keyboard", "Keyboard", {"keyboard", "keyboard_dpad", "action:bind_a", "action:bind_b", "action:bind_x",
                                "action:bind_y", "action:bind_lb", "action:bind_rb", "action:bind_triggers",
                                "action:bind_start", "action:keys_reset"}},
      // Rows handled by src/profiles.cpp.
      {"profiles", "Profiles", {"profile_p1", "profile_p2", "controller_p1", "action:rename_p1", "action:rename_p2", "action:new_profile"}},
      {"modifiers", "Modifiers", {"scroll_speed", "hit_timing", "assist_tick", "autoplay", "no_background", "camera_speed",
                                  "rocket_acceleration", "horizontal_drift", "ddr_mode", "visualizer"}},
      {"video", "Video Settings", {"resolution", "window", "aspect", "framerate", "hdr", "antialiasing", "aniso", "particles",
                                   "citylight", "waterglow", "glow"}},
      // Its own screen (ui_scripts/Options/BBRR_calibrate.lua), listed here for its title.
      {"calibrate", "Auto Calibration", {}},
      {"timing", "Calibration", {"page:calibrate", "audio_offset", "input_offset"}},
  };
  return pages;
}

}  // namespace bbr::settings_table
