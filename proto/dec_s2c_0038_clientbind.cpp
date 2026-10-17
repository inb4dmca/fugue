// s2c 0x0038: client flyweight rebind. [appid u32][u16 x 1..4]; 12 bytes in the usual case.
#include "proto/registry.h"

namespace fugue::proto::g1 {
namespace {

// Layout [appid u32][u16 x 1..4]. The appid equals the preceding 0x0026's appid.
//   The client reads an id, then four i16 values, length-guarded after the first u16, so 6/8/10/12-byte bodies are legal.
//   "fields" is always a 4-element array; absent trailing u16s are reported as 0, with nFields = count present.
canon::Json decode(State&, ByteSpan b) {
    if (b.size() < 6 || b.size() > 12 || (b.size() & 1))
        throw DecodeError("0x38 body is " + std::to_string(b.size()) + " bytes, want 6, 8, 10 or 12");
    size_t n = (b.size() - 4) / 2;
    std::vector<canon::Json> f;
    for (size_t k = 0; k < 4; k++) f.push_back(canon::u(k < n ? le16(&b[4 + 2 * k]) : 0));
    return canon::Object().u("appid", le32(&b[0])).set("fields", canon::array(f)).u("nFields", n).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0038, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto::g1
