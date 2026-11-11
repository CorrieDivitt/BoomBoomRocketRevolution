#pragma once

namespace rex { class Runtime; }

namespace bbr {
// Finds Xbox 360 DLC packages (the original LIVE/PIRS container files, as downloaded) for
// this game and installs any that are not installed yet, so the game can mount them.
// Looked up in: --dlc_path (a file or a folder) and a "DLC" folder next to the exe or in
// any parent folder. No DLC content is shipped with this project.
void InstallDlcPackages(rex::Runtime* runtime);
}  // namespace bbr
