// DEFLATE (RFC 1951) and gzip (RFC 1952) decompression. Self-contained: zlib is not linked, so the net/ object
// library needs no -lz. Used for the gzipped resource bundles that load_rsc offers can carry.
#pragma once
#include "net/bytes.h"

namespace fugue::net {
// Raw DEFLATE stream. false on any malformed/truncated input or if output would exceed maxOut.
bool inflateRaw(ByteSpan in, Bytes& out, size_t maxOut = size_t(1) << 28);
// gzip member (header + deflate + CRC32/ISIZE trailer, both verified).
bool gunzip(ByteSpan in, Bytes& out, size_t maxOut = size_t(1) << 28);
// RFC 1952 CRC-32 (reflected, poly 0xEDB88320). NOT the BYOND resID CRC.
uint32_t crc32Gzip(ByteSpan data);
}  // namespace fugue::net
