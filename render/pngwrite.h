// Minimal PNG writer (RGBA8, filter 0, zlib STORED blocks). A convenience for inspecting output, not a compressor. Qt-free.
// Spec: PNG 1.2 chunk layout + CRC-32, RFC 1950 (zlib header/Adler-32), RFC 1951 stored blocks.
#pragma once
#include <cstdint>
#include <vector>

#include "render/png.h"

namespace fugue::render {

std::vector<uint8_t> encodePng(const RgbaImage& img);

}  // namespace fugue::render
