// zlib/DEFLATE decoder (RFC 1950 / RFC 1951), Qt-free, no zlib dependency.
// Spec: RFC 1951 (block types, fixed/dynamic Huffman, length/distance tables), RFC 1950 (2-byte header, Adler-32 trailer).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fugue::render {

enum class ZStatus { Ok, BadHeader, BadData, Truncated, Checksum, Limit };

// Inflates a zlib stream. Appends to `out`. Stops with Limit as soon as out.size() > limit.
// `used` (optional) receives how many input bytes the stream (header..Adler-32) occupied.
ZStatus zlibInflate(const uint8_t* p, size_t n, std::vector<uint8_t>& out, size_t limit, size_t* used = nullptr);

uint32_t crc32Ieee(const uint8_t* p, size_t n, uint32_t crc = 0);

}  // namespace fugue::render
