// s2c 0x0007: world state = viewport/eye prologue + the same ten object arrays as 0x0008.
#include "proto/g3_inview.h"

namespace fugue::proto {
namespace {

canon::Json u16Array(g3::Cur& c, int n) {
    std::vector<canon::Json> v;
    for (int i = 0; i < n; i++) v.push_back(canon::u(c.u16()));
    return canon::array(v);
}

// The DM-value widths come from the shared helper in g3_inview.cpp. Eye floats are emitted as raw IEEE bits.
canon::Json decode(State&, ByteSpan body) {
    g3::Cur c(body);
    canon::Object o;
    uint8_t flags = c.u8();
    o.u("flags", flags);

    uint16_t mapX = 0, eyeX = 0, mapY = 0, eyeY = 0;
    if (flags & 2) { mapX = c.u16(); eyeX = c.u16(); }
    if (flags & 4) { mapY = c.u16(); eyeY = c.u16(); }
    o.boolean("hasX", (flags & 2) != 0).u("mapX", mapX).u("eyeX", eyeX);
    o.boolean("hasY", (flags & 4) != 0).u("mapY", mapY).u("eyeY", eyeY);

    if (flags & 6) {
        canon::Object e;
        uint8_t kind = c.u8();
        uint8_t eyeType = 0;
        uint32_t eyeValue = 0;
        if (kind == 0) { eyeType = c.u8(); eyeValue = c.u32(); }
        uint8_t viewMode = c.u8();
        uint32_t f32a = c.u32(), f32b = c.u32();
        uint32_t glide = c.u32(), step = c.u32();
        uint16_t vx = c.u16(), vy = c.u16();
        e.u("kind", kind).u("eyeType", eyeType).u("eyeValue", eyeValue).u("viewMode", viewMode)
         .u("flags32", f32a).u("flags32b", f32b)
         .set("glideSize", canon::f32(glide)).set("stepSize", canon::f32(step))
         .u("viewX", vx).u("viewY", vy);
        o.set("eye", e.build());
    } else {
        o.set("eye", canon::null());
    }
    if (flags & 0x08) c.take(2);
    if (flags & 0x10) c.take(2);
    uint8_t sight = 0;
    if (flags & 0x20) sight = c.u8();
    o.u("sight", sight).boolean("hasSight", (flags & 0x20) != 0);
    ByteSpan dm;
    if (flags & 0x40) dm = g3::readDmValue(c);
    o.bytes("dmValue", dm);

    if (flags & 0x80) {
        canon::Object v;
        uint8_t sub = c.u8();
        uint8_t sid = 0, sinv = 0;
        uint32_t wide = 0;
        if (sub & 1) sid = c.u8();
        if (sub & 2) sinv = c.u8();
        if (sub & 4) wide = c.u32();
        canon::Json rect = u16Array(c, (sub & 0x08) ? 4 : 0);
        canon::Json origin = u16Array(c, (sub & 0x10) ? 2 : 0);
        canon::Json screen = u16Array(c, (sub & 0x20) ? 4 : 0);
        canon::Json bounds = u16Array(c, (sub & 0x40) ? 4 : 0);
        // Absent fixed-size u16 arrays decode as all zeros, never empty.
        auto zeros = [](int n) { std::vector<canon::Json> z(n, canon::u(0)); return canon::array(z); };
        v.u("sub", sub).u("seeInDark", sid).u("seeInvisible", sinv).u("sightWide", wide)
         .set("rect0x08", (sub & 0x08) ? rect : zeros(4))
         .set("viewOrigin", (sub & 0x10) ? origin : zeros(2))
         .set("screenOffset", (sub & 0x20) ? screen : zeros(4))
         .set("mapBounds", (sub & 0x40) ? bounds : zeros(4));
        o.set("vision", v.build());
    } else {
        o.set("vision", canon::null());
    }

    canon::Object objs;
    objs.u("flags", 0).u("extra1", 0).u("extra2", 0).boolean("hasExtra1", false).boolean("hasExtra2", false);
    g3::readObjectArrays(c, objs);
    o.set("objects", objs.build());
    c.expectEnd("0x0007");
    return o.build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0007, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
