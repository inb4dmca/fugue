// s2c 0x00fe: browse_rsc batch - a REPEATING [ordinal u32][cstring] loop.
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// A repeating [ordinal u32][cstring] loop, run to the end of the body. An empty body means no records.
canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    std::vector<canon::Json> recs;
    while (r.remaining()) {
        uint32_t id = r.u32();
        std::string name = r.cstr();
        recs.push_back(canon::Object().u("resID", id).str("name", name).build());
    }
    return canon::Object().set("records", canon::array(recs)).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x00fe, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
