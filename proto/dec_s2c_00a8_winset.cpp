// s2c 0x00a8: winset(control, "prop=val;...").
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Layout [control\0][props\0]; a null control is an empty string. Props are stored raw, not url-decoded.
canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    std::string control = r.cstr(), props = r.cstr();
    r.expectEnd();
    return canon::Object().str("control", control).str("props", props).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x00a8, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
