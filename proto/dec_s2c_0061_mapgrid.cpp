// s2c 0x61: initial viewport grid. The record is the MapGrid (repeat-previous grammar sized by the body);
// as a side effect it ALSO drives the footprint machine (errors there are swallowed).
#include "proto/g2_mapview.h"

namespace fugue::proto {
namespace {

// Header x0,y0,z as three u16; then literal u32 cell ids, or a 0xFFFFFFFF run token followed by a u8 count.
//   The implicit initial previous id is 0, and a leading run gets one bonus cell.
canon::Json decode(State& st, ByteSpan b) {
    if (b.size() < 6) throw DecodeError("short map grid");
    Reader r(b);
    uint16_t x0 = r.u16(), y0 = r.u16(), z = r.u16();
    std::vector<canon::Json> cells;
    uint32_t cur = 0;
    bool first = true;
    while (r.remaining()) {
        uint32_t v = r.u32();   // throws on a truncated token
        if (v == 0xffffffffu) {
            unsigned run = unsigned(r.u8()) + 1;   // throws on a missing count
            if (first) run++;
            for (unsigned i = 0; i < run; i++) cells.push_back(canon::u(cur));
        } else {
            cur = v;
            cells.push_back(canon::u(v));
        }
        first = false;
    }
    try { (void)g2::decodeMapRefresh(st, 0x61, b); } catch (const std::exception&) {}
    return canon::Object().u("x0", x0).u("y0", y0).u("z", z).set("cells", canon::array(cells)).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0061, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
