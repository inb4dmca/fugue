// s2c 0x0062: input() prompt request (blocking).
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Layout [title\0][default\0][message\0][typeMask u32][filter u8][filterKind u8][id u32]. typeMask is passed through
//   uninterpreted.
canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    std::string title = r.cstr(), def = r.cstr(), message = r.cstr();
    uint32_t mask = r.u32();
    uint8_t filter = r.u8(), kind = r.u8();
    uint32_t id = r.u32();
    r.expectEnd();
    return canon::Object().str("title", title).str("default", def).str("message", message)
        .u("typeMask", mask).u("filter", filter).u("filterKind", kind).u("id", id).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0062, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
