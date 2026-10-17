// s2c 0x0071: a bare [value u32 LE]: the client's own mob ref.
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Exactly 4 bytes.
canon::Json decode(State&, ByteSpan body) {
    if (body.size() != 4) throw DecodeError("0x71 body is " + std::to_string(body.size()) + " bytes, want 4");
    return canon::Object().u("value", le32(body.data())).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0071, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
