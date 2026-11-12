#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>

namespace bbr {
// Returns the window's width/height ratio (0 if unknown). Set by the app.
void SetWindowAspectProvider(std::function<double()> provider);
// True when --aspect_ratio asks for anything other than the original 16:9.
bool WidescreenRequested();
// Called once per game frame: keeps the game's camera aspect ratio at the target.
void ApplyAspect(uint8_t* base);
// Guest memory base (set once the game is running; null before that).
uint8_t* GuestBase();
// Width/height ratio the 2D layer is laid out for (16/9 unless widescreen is on).
double UiAspectRatio();
// Called whenever the game loads a UI script (src/ui_mod.cpp): a new screen is starting.
void NoteUiScriptLoaded(const char* name);
// For UI scripts: everything inside this rectangle (menu layout units) stays where it is
// instead of following a window edge. Forgotten when the next script is loaded.
void KeepUiCentred(float x0, float y0, float x1, float y1);
// The ui_scripts folder the replacement scripts come from (src/ui_mod.cpp); empty if none.
// Its ui_layout.txt holds the finished layouts shipped with the port (see AnchorUi).
std::filesystem::path UiScriptsFolder();
}  // namespace bbr
