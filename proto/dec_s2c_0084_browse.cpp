// s2c 0x0084: browse(html, params).
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Layout [html\0][params\0][flag u8]; flag 0 = html present, 1 = html null / close. The flag is accepted only as 0 or 1,
//   and nothing may trail it.
canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    std::string html = r.cstr(), params = r.cstr();
    uint8_t flag = r.u8();
    if (flag > 1) throw DecodeError("0x84 flag is not 0/1");
    r.expectEnd();
    return canon::Object().str("html", html).str("params", params).boolean("close", flag == 1).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0084, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
