// s2c 0x006c: flick() icon_state payload.
//   [variant u8 = 0][icon_state \0][kind u8 = 2][ref u32][iconResIndex u32][aux u16]
#include "proto/g3_inview.h"

namespace fugue::proto {
namespace {

// The AtomLoc is [kind u8][ref u32][iconResIndex u32][aux u16]. Only variant 0 and AtomLoc kind 2 are decoded; other
//   values are refused rather than guessed. The first byte is a variant discriminator: 0 string only, 1 index only, 2 both.
canon::Json decode(State&, ByteSpan body) {
    g3::Cur c(body);
    uint8_t variant = c.u8();
    if (variant != 0) throw DecodeError("0x6c variant " + std::to_string(variant) + " not decoded");
    std::string s;
    for (;;) {
        if (!c.more()) throw DecodeError("0x6c: unterminated icon_state");
        uint8_t ch = c.u8();
        if (ch == 0) break;
        s.push_back(char(ch));
    }
    if (body.size() - c.pos() != 11) throw DecodeError("0x6c tail is not 11 bytes");
    uint8_t kind = c.u8();
    if (kind != 2) throw DecodeError("0x6c AtomLoc kind " + std::to_string(kind) + " not decoded");
    uint32_t ref = c.u32();
    uint32_t idx = c.u32();
    uint16_t aux = c.u16();
    return canon::Object().u("variant", variant).str("iconState", s)
        .set("loc", canon::Object().u("kind", kind).u("ref", ref).u("aux", aux).build())
        .u("iconResIndex", idx).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x006c, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
