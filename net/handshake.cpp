#include "net/handshake.h"

namespace fugue::net {

// Little-endian scalars, as the stock 516 client sends them.
Bytes encodeHandshake(const Handshake& h) {
    Bytes o;
    putLe32(o, h.proto);
    putLe32(o, h.field2);
    putLe32(o, h.field3);
    if (h.extended) {
        putLe32(o, h.extra1);
        putLe32(o, h.extra2);
    }
    putLe16(o, h.tail);
    return o;
}

// Base key = field3 + (field2 << 16) + proto (mod 2^32).
uint32_t sessionKey(const Handshake& h) { return h.field3 + (h.field2 << 16) + h.proto; }

// Reply plaintext: u32 major, u32 field2, three flag bytes, then the nonce (plain[11:]).
bool parseHandshakeReply(ByteSpan p, HandshakeReply& out) {
    if (p.size() < 11) return false;
    out.major = le32(&p[0]);
    out.field2 = le32(&p[4]);
    out.flags[0] = p[8];
    out.flags[1] = p[9];
    out.flags[2] = p[10];
    out.nonce.assign(p.begin() + 11, p.end());
    return true;
}

// Loop A of the stock client's nonce scan. A terminator in the last word (or none) gives K = base: the handler's
// clamp-to-0 reading. Loop B is a skip scan with no effect on the key and is not needed for K_login.
uint32_t loginKey(uint32_t base, ByteSpan nonce) {
    size_t n = nonce.size() / 4;
    size_t i = 0;
    for (; i < n; i++) {
        uint32_t w = le32(&nonce[i * 4]);
        if ((((w + 0x71bd632fu) >> 1) & 0x02004000u) == 0) break;
    }
    size_t j = i + 1;
    if (j >= n) return base;
    return base + le32(&nonce[j * 4]);
}

}  // namespace fugue::net
