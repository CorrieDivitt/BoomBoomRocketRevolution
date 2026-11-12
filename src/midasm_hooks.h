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

// Arrow icon quad (sub_8206A600) at 0x8206A874, after the icon's position has been
// transformed: r31 = icon ([r31] = its rocket), r1 = stack (clip x/y/z/w at +0x50..+0x5C).
// Implemented in src/input_offset.cpp.
void BbrArrowIconHook(PPCRegister& r31, PPCRegister& r1);

// Firework config loader (sub_8206C0F8) at 0x8206C7A4: r3 = a section's particle Count
// about to be stored at 0(r25). Implemented in src/particles.cpp.
void BbrParticleCountHook(PPCRegister& r3, PPCRegister& r25);

// Audio block about to go to the driver (sub_8213E448 at 0x8213E490): r4 = 6 x 256 floats.
// Implemented in src/metronome.cpp.
void BbrAudioFrameHook(PPCRegister& r4);

// Arrow placement (sub_8206A328) at 0x8206A418, where the eased and linear branches meet:
// f30 = flown / flight, f31 = fraction across, f11 = fraction upwards.
// Implemented in src/modifiers.cpp (Rocket Acceleration).
void BbrArrowEaseHook(PPCRegister& f30, PPCRegister& f31, PPCRegister& f11);
