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
    src/settings.cpp
    src/ui_mod.cpp
)

if(WIN32)
    add_executable(boomboomrocket WIN32 ${BOOMBOOMROCKET_SOURCES})
else()
    add_executable(boomboomrocket ${BOOMBOOMROCKET_SOURCES})
endif()

rexglue_setup_target(boomboomrocket GPU_PLUGINS xenos)

