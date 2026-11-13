# boomboomrocket - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(boomboomrocket LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(BOOMBOOMROCKET_SOURCES
    src/main.cpp
    src/frame_stats.cpp
    src/leaderboards.cpp
    src/input_offset.cpp
    src/particles.cpp
    src/song_clock.cpp
    src/song_length.cpp
    src/widescreen.cpp
    src/dlc.cpp
    src/game_log.cpp
    src/intro_skip.cpp
    src/metronome.cpp
    src/autoplay.cpp
    src/modifiers.cpp
    src/city_lighting.cpp
    src/camera_speed.cpp
    src/settings.cpp
    src/crash_report.cpp
    src/profiles.cpp
    src/visualizer.cpp
    src/visualizer_zoom.cpp
    src/keyboard.cpp
    src/ui_mod.cpp
)

if(WIN32)
    add_executable(boomboomrocket WIN32 ${BOOMBOOMROCKET_SOURCES})
else()
    add_executable(boomboomrocket ${BOOMBOOMROCKET_SOURCES})
endif()

rexglue_setup_target(boomboomrocket GPU_PLUGINS xenos)

if(WIN32)
    # src/visualizer.cpp: WASAPI loopback capture (COM).
    target_link_libraries(boomboomrocket PRIVATE ole32)
endif()


# Release version, shown by the launcher and at the top of each log (scripts\package.ps1 -Version overrides it).
set(BBRR_VERSION "1.0.0-dev" CACHE STRING "Version of Boom Boom Rocket Revolution")
target_compile_definitions(boomboomrocket PRIVATE BBRR_VERSION_STRING="${BBRR_VERSION}")
if(WIN32)
    # src/crash_report.cpp: crash dumps.
    target_link_libraries(boomboomrocket PRIVATE dbghelp)
endif()
# Controller mappings (thirdparty/SDL_GameControllerDB), loaded from the game's own folder.
add_custom_command(TARGET boomboomrocket POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/SDL_GameControllerDB/gamecontrollerdb.txt"
        "$<TARGET_FILE_DIR:boomboomrocket>/gamecontrollerdb.txt")

# The launcher (launcher/): sets up the player's own game files and DLC next to itself,
# offers the video settings, and starts the game. Dear ImGui on Direct3D 11, built from the
# copy of Dear ImGui in the SDK's thirdparty folder. Output: BoomBoomRocketRevolution.exe.
if(WIN32)
    set(BBRR_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/thirdparty/imgui")
    add_executable(bbrr_launcher WIN32
        launcher/main.cpp
        launcher/launcher_core.cpp
        ${BBRR_IMGUI_DIR}/imgui.cpp
        ${BBRR_IMGUI_DIR}/imgui_draw.cpp
        ${BBRR_IMGUI_DIR}/imgui_tables.cpp
        ${BBRR_IMGUI_DIR}/imgui_widgets.cpp
        ${BBRR_IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${BBRR_IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    set_target_properties(bbrr_launcher PROPERTIES OUTPUT_NAME "BoomBoomRocketRevolution")
    target_include_directories(bbrr_launcher PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/launcher
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${BBRR_IMGUI_DIR}
        ${BBRR_IMGUI_DIR}/backends
    )
    target_compile_definitions(bbrr_launcher PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        LAUNCHER_VERSION="${BBRR_VERSION}")
    target_link_libraries(bbrr_launcher PRIVATE d3d11 dxgi d3dcompiler dwmapi ole32 uuid shell32)
    # The game is what the launcher starts: build both together.
    add_dependencies(boomboomrocket bbrr_launcher)
endif()
