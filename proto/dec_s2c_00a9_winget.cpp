// s2c 0x00a9: winget(control, prop) query (also winexists: empty prop + flag 1).
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Layout [control\0][prop\0][reqid u32][flag u8, length-guarded]; winexists rides this message with an
//   empty prop and flag 1. The flag byte is optional on the wire.
canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    std::string control = r.cstr(), prop = r.cstr();
    uint32_t reqID = r.u32();
    uint8_t flag = 0;
    bool has = false;
    if (r.remaining()) { flag = r.u8(); has = true; }
    r.expectEnd();
    return canon::Object().str("control", control).str("prop", prop).u("reqID", reqID)
        .u("flag", flag).boolean("hasFlag", has).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x00a9, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
