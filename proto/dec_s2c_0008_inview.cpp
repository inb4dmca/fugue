// s2c 0x0008: in-view object update. [flags u8][u16 if flags&2][u16 if flags&4] then the ten counted
// object arrays and an optional tail byte (shared grammar in g3_inview.cpp).
#include "proto/g3_inview.h"

namespace fugue::proto {
namespace {

// Header and array grammar: see g3_inview.cpp.
canon::Json decode(State&, ByteSpan body) {
    g3::Cur c(body);
    canon::Object o;
    uint8_t flags = c.u8();
    o.u("flags", flags);
    uint16_t e1 = 0, e2 = 0;
    if (flags & 2) e1 = c.u16();
    if (flags & 4) e2 = c.u16();
    o.u("extra1", e1).u("extra2", e2).boolean("hasExtra1", (flags & 2) != 0).boolean("hasExtra2", (flags & 4) != 0);
    g3::readObjectArrays(c, o);
    c.expectEnd("0x0008");
    return o.build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0008, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
