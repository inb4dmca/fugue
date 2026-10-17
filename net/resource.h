// Resource payload cipher (Wang-hash feedback XOR seeded by resID) and resID (CRC-32/XFER, poly 0xAF).
#pragma once
#include "net/bytes.h"

namespace fugue::net {

uint32_t wangHash32(uint32_t k);
// Length-preserving. Both directions feed the CIPHERTEXT byte back, so they differ only in which
// buffer the keystream is XORed against.
Bytes encodeResource(ByteSpan src, uint32_t resId);
Bytes decodeResource(ByteSpan wire, uint32_t resId);
// CRC-32 MSB-first, poly 0xAF, init 0xFFFFFFFF, no reflection, no final XOR, over the file bytes.
uint32_t resId(ByteSpan data);

}  // namespace fugue::net
