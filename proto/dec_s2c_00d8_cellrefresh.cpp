// s2c 0xd8: single-cell map refresh: [N u16] then records until the body ends; N is cross-checked.
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Header [N u16], then per record [tag u8]: tag 1 = [x u16][y u16][z u16][appid u32][n u16]; other tags =
//   [ref u32][n u16]; then n x [type u8][value u32]. The
//   decoder loops until the body ends, then requires the header count to equal the records parsed.
canon::Json decode(State&, ByteSpan b) {
    if (b.size() < 2) throw DecodeError("short 0xd8");
    Reader r(b);
    unsigned n = r.u16();
    std::vector<canon::Json> recs;
    while (r.remaining()) {
        uint8_t tag = r.u8();
        uint16_t x = 0, y = 0, z = 0;
        uint32_t ref = 0, appid = 0;
        unsigned cnt;
        if (tag == 0x01) {
            x = r.u16(); y = r.u16(); z = r.u16(); appid = r.u32(); cnt = r.u16();
        } else {
            ref = r.u32(); cnt = r.u16();
        }
        std::vector<canon::Json> contents;
        for (unsigned i = 0; i < cnt; i++) {
            uint8_t t = r.u8();
            uint32_t v = r.u32();
            contents.push_back(canon::Object().u("type", t).u("value", v).build());
        }
        recs.push_back(canon::Object().u("tag", tag).u("x", x).u("y", y).u("z", z).u("ref", ref)
                           .u("appid", appid).set("contents", canon::array(contents)).build());
    }
    if (recs.size() != n) throw DecodeError("0xd8 header count != records parsed");
    return canon::Object().set("records", canon::array(recs)).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x00d8, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
