// s2c 0x006a: statpanel (DreamSeeker's info control). flags is a BITMASK of independent sections.
#include <cstring>

#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Frame layout: [flags u8][token u8] {&1: DMValue} {&2: u16 n, n cstrings} {&4: u16} {&8: DMValue} [rows u16] x
//   {cstring, DMValue, u16 ordinal} [clears u16] x u16. DMValue kinds 1/2/3/4/6/12/15/0x2a are sized as below; any other
//   kind is NULL with zero payload bytes. Refs are u32.
//   Payload shapes: 0x01/0x04 [u32 head][u32 cid]; 0x02/0x03 [u32 head][u16][u32 cid]; 0x06 cstring; 0x0c u32;
//   0x0f u16 count + recursive values, depth cap 8; 0x2a float32 bits. A row surfaces head, cid, the 0x06 text and the
//   0x2a number; the 0x02/0x03 u16, the 0x0c word and 0x0f's values are skipped. The flags&1 / flags&8 DMValues are
//   read and discarded.
struct Value { uint8_t kind = 0; std::string text; double num = 0; uint32_t appid = 0; bool hasAppid = false; uint32_t head = 0; };

Value value(Reader& r, int depth) {
    Value v;
    v.kind = r.u8();
    switch (v.kind) {
    case 0x01: v.head = r.u32(); v.appid = r.u32(); v.hasAppid = true; break;
    case 0x04: v.head = r.u32(); v.appid = r.u32(); v.hasAppid = true; break;
    case 0x02: case 0x03: v.head = r.u32(); r.u16(); v.appid = r.u32(); v.hasAppid = true; break;
    case 0x06: v.text = r.cstr(); break;
    case 0x0c: r.u32(); break;
    case 0x0f: {
        if (depth > 8) throw DecodeError("statpanel DMValue list nested too deep");
        uint16_t n = r.u16();
        for (unsigned i = 0; i < n; i++) value(r, depth + 1);
        break;
    }
    case 0x2a: {
        uint32_t bits = r.u32();
        float f; std::memcpy(&f, &bits, 4);
        v.num = double(f);
        break;
    }
    default: break;   // NULL arm: zero payload bytes
    }
    return v;
}

canon::Json valueJson(const Value& v) {
    return canon::Object().u("kind", v.kind).str("text", v.text).set("num", canon::f64FromDouble(v.num))
        .u("appid", v.appid).boolean("hasAppid", v.hasAppid).u("head", v.head).build();
}

canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    uint8_t flags = r.u8(), token = r.u8();
    if (flags & 0x01) value(r, 0);
    bool hasTabs = false, hasSelected = false;
    std::vector<canon::Json> tabs;
    uint16_t selected = 0;
    if (flags & 0x02) {
        uint16_t n = r.u16();
        hasTabs = true;
        for (unsigned i = 0; i < n; i++) tabs.push_back(canon::str(r.cstr()));
    }
    if (flags & 0x04) { selected = r.u16(); hasSelected = true; }
    if (flags & 0x08) value(r, 0);
    uint16_t nrows = r.u16();
    std::vector<canon::Json> rows;
    for (unsigned i = 0; i < nrows; i++) {
        std::string name = r.cstr();
        Value v = value(r, 0);
        uint16_t ord = r.u16();
        rows.push_back(canon::Object().str("name", name).set("value", valueJson(v)).u("ordinal", ord).build());
    }
    uint16_t nclear = r.u16();
    std::vector<canon::Json> clears;
    for (unsigned i = 0; i < nclear; i++) clears.push_back(canon::u(r.u16()));
    r.expectEnd();
    return canon::Object().u("flags", flags).u("token", token).boolean("hasTabs", hasTabs)
        .set("tabs", canon::array(tabs)).boolean("hasSelected", hasSelected).u("selected", selected)
        .set("rows", canon::array(rows)).set("clears", canon::array(clears)).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x006a, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
