// Plane-master filters: blur, outline and drop_shadow, the three types built (client.planeFilters). Each takes a
// straight-alpha RGBA8 surface and returns a new surface of the same size.
//
// Blur: gaussianKernel / convolveSep (premultiplied float64, edge clamp).
// Outline: outlinePassLadder + outlinePass + bilerpAlpha, a port of DreamSeeker's own outline technique (its HLSL shader
// and multi-pass driver).
// The blur's Gaussian weights use exp() from render/goexp.h.
//
// Not built (counted, never invented): filter types 4 and up (alpha, color, displacement, ripple, wave, motion_blur,
// bloom, angular_blur, greyscale, layer, rays).
#pragma once
#include <cstdint>

#include "render/png.h"

namespace fugue::render {

RgbaImage filterBlur(const RgbaImage& src, double size);
// flags: bit 0 OUTLINE_SHARP (step(0.75, taps) instead of saturate, its own pass ladder is NOT modelled),
//        bit 1 OUTLINE_SQUARE (the diagonal taps sit at the full offset).
RgbaImage filterOutline(const RgbaImage& src, double size, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha, uint16_t flags);
// x, y are IMAGE-space offsets (+y down); offset grows the silhouette before the blur.
RgbaImage filterDropShadow(const RgbaImage& src, int x, int y, double size, double offset, uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);

}  // namespace fugue::render
