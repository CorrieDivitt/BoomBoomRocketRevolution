# SDL_GameControllerDB

`gamecontrollerdb.txt` from https://github.com/mdqinc/SDL_GameControllerDB (master, downloaded
2026-11-13), under the zlib license in `LICENSE`. Community button mappings for controllers SDL does
not know by itself. CMake copies it next to `boomboomrocket.exe`, `scripts\package.ps1` puts it in
the release, and the game loads it from its own folder (`src/boomboomrocket_app.h`;
`--hid_mappings_file=<path>` uses another file). To update, download the file again.
