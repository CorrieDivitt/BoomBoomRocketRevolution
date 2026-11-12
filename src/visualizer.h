#pragma once
// Visualizer mode fed by the music playing on the PC (src/visualizer.cpp).

#include <string>

namespace bbr {

// "off", "starting", "listening (game left out)" or "listening (game included)".
std::string VisualizerStatus();

}  // namespace bbr
