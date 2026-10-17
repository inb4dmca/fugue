// s2c 0x00fc: client.MeasureText() query.
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Layout [text\0][style\0][width u16][id u32].
canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    std::string text = r.cstr(), style = r.cstr();
    uint16_t width = r.u16();
    uint32_t id = r.u32();
    r.expectEnd();
    return canon::Object().str("text", text).str("style", style).u("width", width).u("id", id).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x00fc, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
