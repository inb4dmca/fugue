// s2c 0x0024: wide atom -> appearance rebind. Exactly 8 bytes: [ref u32 LE][appid u32 LE].
#include "proto/registry.h"

namespace fugue::proto::g1 {
namespace {

// Two wide (u32) ids with no slack; a body of any other size throws rather than guessing a width.
canon::Json decode(State&, ByteSpan body) {
    if (body.size() != 8) throw DecodeError("0x24 body is " + std::to_string(body.size()) + " bytes, want 8");
    return canon::Object().u("ref", le32(&body[0])).u("appid", le32(&body[4])).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0024, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto::g1
