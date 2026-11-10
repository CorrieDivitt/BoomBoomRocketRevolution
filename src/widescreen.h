#pragma once
#include <cstdint>
#include <functional>

namespace bbr {
// Returns the window's width/height ratio (0 if unknown). Set by the app.
void SetWindowAspectProvider(std::function<double()> provider);
// True when --aspect_ratio asks for anything other than the original 16:9.
bool WidescreenRequested();
// Called once per game frame: keeps the game's camera aspect ratio at the target.
void ApplyAspect(uint8_t* base);
}  // namespace bbr
