// s2c 0x003f: verb-expansion reply (the current choices list).
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Layout [count u16][cstring] x count. Nothing may trail the last string.
canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    uint16_t n = r.u16();
    std::vector<canon::Json> choices;
    for (unsigned i = 0; i < n; i++) choices.push_back(canon::str(r.cstr()));
    r.expectEnd();
    return canon::Object().set("choices", canon::array(choices)).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x003f, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
