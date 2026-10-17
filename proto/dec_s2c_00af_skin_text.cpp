// s2c 0x00af: the world's .dmf interface as literal source text.
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Layout [skin cstring][hub cstring]? [flags u16]?; the last two are length-guarded, and an empty body
//   means no skin.
canon::Json decode(State&, ByteSpan body) {
    std::string dmf, hub;
    uint16_t flags = 0;
    bool hasHub = false, hasFlags = false;
    if (!body.empty()) {
        Reader r(body);
        dmf = r.cstr();
        if (r.remaining()) { hub = r.cstr(); hasHub = true; }
        if (r.remaining()) { flags = r.u16(); hasFlags = true; }
        r.expectEnd();
    }
    return canon::Object().str("dmf", dmf).str("hub", hub).boolean("hasHub", hasHub)
        .u("flags", flags).boolean("hasFlags", hasFlags).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x00af, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
