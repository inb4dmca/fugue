#include "net/cipher.h"

namespace fugue::net {

// Stream recurrence, shared with the stock client and server, both directions:
//   keystream byte = byte(key >> (sum & 31)), sum = running sum (mod 256) of plaintext bytes so far.
//   cipher[i] = plain[i] + sum + keystream; checksum byte = final sum.
Bytes encryptBody(ByteSpan plain, uint32_t key) {
    Bytes out;
    out.reserve(plain.size() + 1);
    uint8_t sum = 0;
    for (uint8_t p : plain) {
        out.push_back(uint8_t(p + sum + uint8_t(key >> (sum & 31))));
        sum = uint8_t(sum + p);
    }
    out.push_back(sum);
    return out;
}

Bytes encryptBodyNoChecksum(ByteSpan plain, uint32_t key) {
    Bytes out = encryptBody(plain, key);
    out.pop_back();
    return out;
}

bool decryptBody(ByteSpan cipher, uint32_t key, Bytes& plainOut) {
    plainOut.clear();
    if (cipher.empty()) return false;
    size_t n = cipher.size() - 1;
    plainOut.reserve(n);
    uint8_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t p = uint8_t(cipher[i] - sum - uint8_t(key >> (sum & 31)));
        plainOut.push_back(p);
        sum = uint8_t(sum + p);
    }
    return cipher[n] == sum;
}

Bytes decryptBodyNoChecksum(ByteSpan cipher, uint32_t key) {
    Bytes out;
    out.reserve(cipher.size());
    uint8_t sum = 0;
    for (uint8_t c : cipher) {
        uint8_t p = uint8_t(c - sum - uint8_t(key >> (sum & 31)));
        out.push_back(p);
        sum = uint8_t(sum + p);
    }
    return out;
}

}  // namespace fugue::net
