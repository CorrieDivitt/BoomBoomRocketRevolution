// Declarations for [[midasm_hook]] functions listed in config/default.toml.
// Added to the recompiled code's precompiled header (see CMakeLists.txt) so the
// generated sources can call them.
#pragma once
#include <rex/ppc.h>

// UI renderer (sub_820A2D10) at 0x820A3054: f31 = horizontal scale, f30 = x offset,
// r9 = widescreen flag. Implemented in src/widescreen.cpp.
void BbrUiTransformHook(PPCRegister& f31, PPCRegister& f30, PPCRegister& r9);
