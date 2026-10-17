// s2c 0x0000: session / version control ("session end").
//   body = [reason u16 LE] ++ optional [requiredVersion u32 LE] (read when the body is at least 6 bytes)
//   reason: 4/5 "Version %u is required" (requiredVersion follows), 6 reset, 9 virtual disconnect,
//   0x0b lock, >= 0x0c "Server closing connection".
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Example: `05 00 | ea 01 00 00` is reason 5, requiredVersion 490. A 6-byte body with version 0 and the 2-byte form
//   both emit requiredVersion 0.
canon::Json decode(State&, ByteSpan body) {
    if (body.size() < 2) throw DecodeError("short session");
    Reader r(body);
    uint16_t reason = r.u16();
    uint32_t required = 0;
    if (body.size() >= 6) required = r.u32();
    return canon::Object().u("reason", reason).u("requiredVersion", required).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0000, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
