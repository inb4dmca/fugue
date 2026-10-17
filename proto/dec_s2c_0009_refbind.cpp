// s2c 0x0009: ref -> appearance bind. 9 bytes: [ref u32 LE][appid u32 LE][pad 00].
#include "proto/registry.h"

namespace fugue::proto::g1 {
namespace {

// Framing: the client reads exactly two u32 ids (8 bytes). The 9th byte is pad and is ignored. The appid is a 0x0026
//   appearance id.
canon::Json decode(State&, ByteSpan body) {
    if (body.size() < 8) throw DecodeError("0x09 body is " + std::to_string(body.size()) + " bytes, want >= 8");
    return canon::Object().u("ref", le32(&body[0])).u("appid", le32(&body[4])).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0009, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto::g1
