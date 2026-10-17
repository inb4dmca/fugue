#include "net/resource.h"

namespace fugue::net {

// Payload cipher: Thomas Wang 32-bit hash, as the stock client's payload cipher uses it.
uint32_t wangHash32(uint32_t k) {
    k = k + 0x7ed55d16u + (k << 12);
    k = (k ^ 0xc761c23cu) ^ (k >> 19);
    k = k + 0x165667b1u + (k << 5);
    k = (k + 0xd3a2646cu) ^ (k << 9);
    k = k + 0xfd7046c5u + (k << 3);
    k = (k ^ 0xb55a4f09u) ^ (k >> 16);
    return k;
}

Bytes encodeResource(ByteSpan src, uint32_t id) {
    Bytes out(src.size());
    uint32_t s = id;
    for (size_t i = 0; i < src.size(); i++) {
        uint8_t c = uint8_t(src[i] ^ uint8_t(s));
        out[i] = c;
        s = wangHash32(uint32_t(c) + s);
    }
    return out;
}

Bytes decodeResource(ByteSpan wire, uint32_t id) {
    Bytes out(wire.size());
    uint32_t s = id;
    for (size_t i = 0; i < wire.size(); i++) {
        uint8_t c = wire[i];
        out[i] = uint8_t(c ^ uint8_t(s));
        s = wangHash32(uint32_t(c) + s);
    }
    return out;
}

// resID: CRC-32/XFER, as the stock DreamMaker stamps resource ids.
uint32_t resId(ByteSpan data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t b : data) {
        crc ^= uint32_t(b) << 24;
        for (int i = 0; i < 8; i++) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0xAFu : crc << 1;
    }
    return crc;
}

}  // namespace fugue::net
