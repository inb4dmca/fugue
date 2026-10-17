// s2c 0x006d: sound play. Wide ids (u32 ResID) as on 516.
#include "proto/registry.h"

namespace fugue::proto {
namespace {


// Head is [ResID u32][Repeat][Wait][Channel u8][Volume u16], then an optional length-guarded Marker byte:
//   marker 0x01 -> Pan u16, Frequency f32, Priority u8, Status u8, Environment i8; 0x02 -> X,Y,Z,Falloff f32;
//   0x10 -> Channel u16 override.
//   A body whose marker bits are not 01/02/10, or whose block is short or has trailing slack, is emitted as
//   hasMarker/marker/head plus `unresolved` = body[9:] verbatim, with every other tail field zero. The framing is known;
//   the remainder is carried verbatim rather than guessed.
canon::Json decode(State&, ByteSpan b) {
    if (b.size() < 9) throw DecodeError("short sound");
    Reader r(b);
    uint32_t resID = r.u32();
    uint8_t repeat = r.u8(), wait = r.u8(), channel = r.u8();
    uint16_t volume = r.u16();
    bool hasMarker = false;
    uint8_t marker = 0;
    uint16_t pan = 0, channelWide = 0;
    uint32_t freq = 0, x = 0, y = 0, z = 0, fall = 0;
    uint8_t prio = 0, status = 0;
    int8_t env = 0;
    ByteSpan unresolved;
    if (b.size() >= 10) {
        hasMarker = true;
        marker = r.u8();
        bool ok = (marker & ~0x13) == 0;
        if (ok && (marker & 0x01)) {
            if (r.remaining() < 9) ok = false;
            else { pan = r.u16(); freq = r.u32(); prio = r.u8(); status = r.u8(); env = int8_t(r.u8()); }
        }
        if (ok && (marker & 0x02)) {
            if (r.remaining() < 16) ok = false;
            else { x = r.u32(); y = r.u32(); z = r.u32(); fall = r.u32(); }
        }
        if (ok && (marker & 0x10)) {
            if (r.remaining() < 2) ok = false;
            else channelWide = r.u16();
        }
        if (ok && r.remaining() != 0) ok = false;
        if (!ok) {
            pan = channelWide = 0; freq = x = y = z = fall = 0; prio = status = 0; env = 0;
            unresolved = b.subspan(9);
        }
    }
    return canon::Object()
        .u("resID", resID).u("repeat", repeat).u("wait", wait).u("channel", channel).u("volume", volume)
        .boolean("hasMarker", hasMarker).u("marker", marker)
        .u("pan", pan).set("frequency", canon::f32(freq)).u("priority", prio).u("status", status)
        .i("environment", env)
        .set("x", canon::f32(x)).set("y", canon::f32(y)).set("z", canon::f32(z)).set("falloff", canon::f32(fall))
        .u("channelWide", channelWide).bytes("unresolved", unresolved)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x006d, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
