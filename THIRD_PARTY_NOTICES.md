# Third-party notices

Boom Boom Rocket Revolution's own code is under the BSD 3-Clause License (see `LICENSE`). It is
built on, and ships with, the software below, each under its own license. The full license texts
are in each project's folder under `thirdparty/rexglue-sdk/` (and its `thirdparty/` folder).

| Software | Used for | License |
|---|---|---|
| [ReXGlue SDK](https://github.com/rexglue) - Copyright (c) 2026 Tom Clay | Static recompilation runtime, kernel, GPU and input | BSD 3-Clause |
| [Xenia](https://xenia.jp) - Copyright (c) 2022 Ben Vanik and Xenia project contributors | Parts of the SDK are derived from Xenia; the files in `patches/rexglue-sdk/` change some of them | BSD 3-Clause |
| [FFmpeg](https://ffmpeg.org) | Audio (XMA) decoding, inside the SDK | LGPL 2.1 or later. Its source is at https://ffmpeg.org and in `thirdparty/rexglue-sdk/thirdparty/FFmpeg` |
| [SDL 3](https://libsdl.org) | Controllers, windowing | zlib |
| [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) | Button mappings for controllers SDL does not know by itself (`gamecontrollerdb.txt`, in `thirdparty/SDL_GameControllerDB/`) | zlib |
| Microsoft Visual C++ runtime (`msvcp140*.dll`, `vcruntime140*.dll`, `concrt140.dll`) | C++ runtime, shipped next to the programs | Microsoft Visual Studio redistributable files, distributed under the Visual Studio license terms |
| Calibration sounds (`calibration_sounds/beat.wav`, `subbeat.wav`) | Metronome clicks for Auto Calibration | Royalty-free samples |
| [Dear ImGui](https://github.com/ocornut/imgui) | Overlays and dialogs | MIT |
| [{fmt}](https://github.com/fmtlib/fmt) | Text formatting | MIT |
| [spdlog](https://github.com/gabime/spdlog) | Logging | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) | Settings files | MIT |
| [xxHash](https://github.com/Cyan4973/xxHash) | Hashing | BSD 2-Clause |
| [DirectX Shader Compiler](https://github.com/microsoft/DirectXShaderCompiler) | Shader translation | University of Illinois / NCSA |
| Other libraries in `thirdparty/rexglue-sdk/thirdparty/` (stb, libmspack, utfcpp, CLI11, simde, glslang, SPIRV-Tools, Vulkan headers and loader, VMA, ...) | Parts of the SDK | Each under its own license, in its folder |

Boom Boom Rocket is (c) Electronic Arts. This project is unofficial and is not affiliated with or
endorsed by Electronic Arts, Bizarre Creations or Microsoft. No game files are included.
