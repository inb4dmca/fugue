// s2c 0x00c7: flick() command half. [AtomLoc: kind u8, ref u32, aux u16] then an optional [appid u32].
#include "proto/g3_inview.h"

namespace fugue::proto {
namespace {

// Layout [AtomLoc][appid u32]. The 7-byte AtomLoc-only form is also accepted (the appid is length-guarded in the
//   client). Any other length is rejected.
canon::Json decode(State&, ByteSpan body) {
    if (body.size() != 11 && body.size() != 7)
        throw DecodeError("0xc7 body is " + std::to_string(body.size()) + " bytes, want 7 or 11");
    g3::Cur c(body);
    uint8_t kind = c.u8();
    uint32_t ref = c.u32();
    uint16_t aux = c.u16();
    uint32_t appid = 0;
    bool has = body.size() == 11;
    if (has) appid = c.u32();
    return canon::Object()
        .set("loc", canon::Object().u("kind", kind).u("ref", ref).u("aux", aux).build())
        .u("appid", appid).boolean("hasAppid", has).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x00c7, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
