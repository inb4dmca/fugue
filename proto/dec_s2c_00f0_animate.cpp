// s2c 0x00f0: animate() chain. 14-byte header + `count` mask-driven steps.
#include "proto/g3_inview.h"

namespace fugue::proto {
namespace {

// Header [kind u8][ref u32][ver u16][delay f32][chain u8][count u16]. Each step: [from u32][to u32][time f32][loop i32]
//   [easing u8][flags u8, plus a u16 as bits 16-31 when bit 0x80 is set], then StepDelay f32 (when flags has 0x40000000
//   or 0x4, or the previous step had ext), [tag u16], TimeBase f32 (same flags, or the first step of a chain), a NUL-
//   terminated string (0x10000000) and an ext f32 (0x8).
canon::Json decode(State&, ByteSpan body) {
    g3::Cur c(body);
    uint8_t kind = c.u8();
    uint32_t ref = c.u32();
    uint16_t ver = c.u16();
    uint32_t delay = c.u32();
    uint8_t chain = c.u8();
    unsigned n = c.u16();
    std::vector<canon::Json> steps;
    bool prevExt = false;
    bool wantBase = chain != 0;
    for (unsigned i = 0; i < n; i++) {
        uint32_t from = c.u32(), to = c.u32(), tm = c.u32();
        int32_t loop = int32_t(c.u32());
        uint8_t easing = c.u8();
        uint8_t lo = c.u8();
        uint32_t flags = lo;
        if (lo & 0x80) flags |= uint32_t(c.u16()) << 16;
        bool head = (flags & (0x40000000u | 0x4u)) != 0;
        bool hasDelay = false, hasBase = false, hasExt = false;
        uint32_t sd = 0, tb = 0, ext = 0;
        std::string str;
        if (head || prevExt) { hasDelay = true; sd = c.u32(); }
        uint16_t tag = c.u16();
        if (head) wantBase = true;
        if (wantBase) { hasBase = true; tb = c.u32(); wantBase = false; }
        if (flags & 0x10000000u) {
            // NUL-terminated string
            std::string s;
            for (uint8_t ch; (ch = c.u8()) != 0;) s.push_back(char(ch));
            str = s;
        }
        if (flags & 0x8u) { hasExt = true; ext = c.u32(); }
        prevExt = (flags & 0x8u) != 0;
        steps.push_back(canon::Object()
            .u("from", from).u("to", to).set("time", canon::f32(tm)).i("loop", loop)
            .u("easing", easing).u("flags", flags).u("tag", tag)
            .boolean("hasDelay", hasDelay).set("delay", canon::f32(sd))
            .boolean("hasBase", hasBase).set("timeBase", canon::f32(tb))
            .str("str", str)
            .boolean("hasExt", hasExt).set("ext", canon::f32(ext))
            .build());
    }
    c.expectEnd("0x00f0");
    return canon::Object().u("kind", kind).u("ref", ref).u("ver", ver).set("delay", canon::f32(delay))
        .u("chain", chain).set("steps", canon::array(steps)).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x00f0, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
