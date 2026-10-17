// s2c 0x00a7: targeted output(value, control).
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Layout [value\n\0][control\0], raw bytes, NUL terminated. The BYOND-appended newline is removed from value (one only).
//   The text is NOT url-decoded: the DM code url-encodes it before it reaches the wire.
canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    std::string value = r.cstr(), control = r.cstr();
    r.expectEnd();
    if (!value.empty() && value.back() == '\n') value.pop_back();
    return canon::Object().str("value", value).str("control", control).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x00a7, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
