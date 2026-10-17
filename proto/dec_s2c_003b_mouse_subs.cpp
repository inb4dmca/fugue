// s2c 0x003b: global config - the FormatFlags word (mouse subscriptions).
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Body [12:16) is the FormatFlags word verbatim; a second word at [16:20) is present iff flags bit 31 is set.
//   Bytes [0:12) (per-flag bytes, resource count) are not decoded. A body shorter than 16 (or 20 with bit 31) throws.
canon::Json decode(State&, ByteSpan body) {
    if (body.size() < 16) throw DecodeError("0x3b shorter than 16 bytes");
    uint32_t flags = le32(&body[12]), extra = 0;
    if (flags & 0x80000000u) {
        if (body.size() < 20) throw DecodeError("0x3b bit31 set but no second word");
        extra = le32(&body[16]);
    }
    return canon::Object().u("flags", flags).u("extra", extra).boolean("known", true).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x003b, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
