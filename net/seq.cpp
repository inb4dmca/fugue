#include "net/seq.h"
namespace fugue::net {
// Sequenced mode: next = (seq*0x43d4) mod 0xfff1, 0->1. Engaged when the peer build is > 0x19c.
uint16_t nextSeq(uint16_t seq) {
    uint32_t n = (uint32_t(seq) * 0x43D4u) % 0xFFF1u;  // max 65535*17364 < 2^32
    return uint16_t(n == 0 ? 1 : n);
}
}  // namespace fugue::net
