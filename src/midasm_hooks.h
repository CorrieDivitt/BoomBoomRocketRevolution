// Declarations for [[midasm_hook]] functions listed in config/default.toml.
// The codegen emits matching `extern` declarations in the generated sources; this
// header keeps our definitions in sync with them.
#pragma once
#include <rex/ppc.h>

// UI renderer (sub_820A2D10) at 0x820A3054: f31 = horizontal scale, f30 = x offset,
// r9 = widescreen flag. Implemented in src/widescreen.cpp.
void BbrUiTransformHook(PPCRegister& f31, PPCRegister& f30, PPCRegister& r9);

// Same renderer: f0 = vertex-shader vScale.x at 0x820A2DF4 and vOffset.x after 0x820A2E44.
void BbrUiScaleXHook(PPCRegister& f0);
void BbrUiOffsetXHook(PPCRegister& f0);

// Widget list walk (sub_8208B648) at 0x8208B6B4: r3 = widget about to render its quads.
void BbrUiWidgetHook(PPCRegister& r3);

// Arrow icon quad (sub_8206A600) at 0x8206A844: f0 = width/height factor (9/16).
void BbrArrowAspectHook(PPCRegister& f0);
