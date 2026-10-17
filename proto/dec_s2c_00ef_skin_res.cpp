// s2c 0x00ef: the world's skin by resource id (0xffffffff = no skin).
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Layout [resid u32][hub cstring][flags u16], with no length guards.
canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    uint32_t resID = r.u32();
    std::string hub = r.cstr();
    uint16_t flags = r.u16();
    r.expectEnd();
    return canon::Object().u("resID", resID).str("hub", hub).u("flags", flags).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x00ef, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
