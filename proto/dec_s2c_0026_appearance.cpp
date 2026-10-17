// s2c 0x0026: atom appearance, interned by appid.
// Wire layout (516, wide appearance ids u32, narrow verb ids u16):
//   [appid u32][name cstr][suffix cstr][icon_res u32][icon_state cstr][text cstr][maptext cstr]
//   [maptext_w u16][maptext_h u16][maptext_x s16][maptext_y s16][dir u8][appearance_flags u32]
//   3 x [tag u8 (+ id u32 iff tag == 2)]  [icon_ref u32]
//   [nverbs u16][verb u16 x n] [noverlays u16][u32 x n] [nunderlays u16][u32 x n]
//   [layer f32][screen_loc cstr] [vars_mask: u8, or u32 re-read from the same offset iff first byte & 0x80]
//   payloads for each set mask bit in ascending bit order (see kVars below).
#include <cstring>

#include "proto/registry.h"

namespace fugue::proto::g1 {
namespace {

// Layout follows the client's appearance handler. Absent
//   convenience values (hasColor/colorR.., plane, pixelX.., glideSize, transform[6], iconRef, overlayCount, widths)
//   are zero.
//   A filter record with type 0 ends the chain early; the record is dropped.
//   Anything not fully accounted for (short body, unterminated cstr, mask bits above 0xffff, bytes left over) throws
//   rather than guessing a framing.
struct Cur {
    ByteSpan b;
    size_t p = 0;
    void need(size_t n) const {
        if (b.size() - p < n) throw DecodeError("0x26 truncated at " + std::to_string(p) + " (want " + std::to_string(n) + ")");
    }
    uint8_t u8() { need(1); return b[p++]; }
    uint16_t u16() { need(2); auto v = le16(&b[p]); p += 2; return v; }
    uint32_t u32() { need(4); auto v = le32(&b[p]); p += 4; return v; }
    ByteSpan take(size_t n) { need(n); auto s = b.subspan(p, n); p += n; return s; }
    std::string cstr() {
        for (size_t i = p; i < b.size(); i++)
            if (b[i] == 0) { std::string s(reinterpret_cast<const char*>(&b[p]), i - p); p = i + 1; return s; }
        throw DecodeError("0x26 unterminated string at " + std::to_string(p));
    }
    // one byte; if its high bit is set, re-read a u32 from the same offset
    uint32_t varint() {
        need(1);
        if (b[p] & 0x80) return u32();
        return u8();
    }
};

canon::Json F(uint32_t bits) { return canon::f32(bits); }

// Filter param width per bit; -1 = cstr. 0 = not a param bit.
int filterParamWidth(uint32_t bit) {
    switch (bit) {
        case 0x1: case 0x2: case 0x4: case 0x8: case 0x10: return 4;
        case 0x20: return 2;
        case 0x40: case 0x100: return 4;
        case 0x200: return -1;
        case 0x400: return 80;
        case 0x800: case 0x1000: return 4;
        case 0x2000: case 0x4000: return 1;
    }
    return 0;
}

// Var payload width per mask bit; 0 = no payload (0x80), -1 = variable (0x800, 0x1000).
int varWidth(uint32_t bit) {
    switch (bit) {
        case 0x1: case 0x2: case 0x4: case 0x100: return 1;
        case 0x8: return 8;
        case 0x10: case 0x40: case 0x4000: return 4;
        case 0x20: return 24;
        case 0x80: return 0;
        case 0x200: case 0x2000: return 2;
        case 0x400: return 80;
        case 0x8000: return 4;
        case 0x800: case 0x1000: return -1;
    }
    throw DecodeError("0x26 unknown vars_mask bit");
}

canon::Json readFilters(Cur& c) {
    std::vector<canon::Json> out;
    unsigned n = c.u8();
    for (unsigned i = 0; i < n; i++) {
        uint8_t type = c.u8();
        if (type == 0) break;
        uint8_t flags = c.u8();
        uint32_t id = c.u32();
        uint32_t mask = c.varint() & ~0x80u;
        if (mask & ~0x7fffu) throw DecodeError("0x26 filter param mask has unknown high bits");
        size_t start = c.p;
        std::string str;
        for (uint32_t bit = 1; bit <= 0x4000; bit <<= 1) {
            if (!(mask & bit)) continue;
            int w = filterParamWidth(bit);
            if (w == 0) throw DecodeError("0x26 filter param bit unknown");
            if (w < 0) str = c.cstr(); else c.take(size_t(w));
        }
        out.push_back(canon::Object().u("flags", flags).u("id", id).u("paramMask", mask)
                          .bytes("params", c.b.subspan(start, c.p - start)).str("str", str).u("type", type).build());
    }
    return canon::array(out);
}

canon::Json decode(State&, ByteSpan body) {
    Cur c{body};
    canon::Object o;
    uint32_t appid = c.u32();
    std::string name = c.cstr();
    std::string suffix = c.cstr();
    uint32_t iconRes = c.u32();
    std::string iconState = c.cstr();
    std::string text = c.cstr();
    std::string maptext = c.cstr();
    uint16_t mtW = c.u16(), mtH = c.u16();
    int16_t mtX = int16_t(c.u16()), mtY = int16_t(c.u16());
    uint8_t dir = c.u8();
    uint32_t aflags = c.u32();
    std::vector<canon::Json> slots;
    for (int k = 0; k < 3; k++) {
        uint8_t tag = c.u8();
        bool has = tag == 2;
        uint32_t id = has ? c.u32() : 0;
        slots.push_back(canon::Object().boolean("has", has).u("id", id).u("tag", tag).build());
    }
    uint32_t iconRef = c.u32();
    std::vector<canon::Json> verbs;
    for (unsigned n = c.u16(), i = 0; i < n; i++) verbs.push_back(canon::u(c.u16()));
    std::vector<canon::Json> overlays, underlays;
    for (unsigned n = c.u16(), i = 0; i < n; i++) overlays.push_back(canon::u(c.u32()));
    for (unsigned n = c.u16(), i = 0; i < n; i++) underlays.push_back(canon::u(c.u32()));
    uint32_t layer = c.u32();
    std::string screenLoc = c.cstr();
    uint32_t mask = c.varint();
    if (mask & ~0xffffu) throw DecodeError("0x26 vars_mask has unmodelled high bits");

    // convenience values, zero when the var is absent
    bool hasColor = false, hasPlane = false, hasPix = false, hasGlide = false, hasXform = false;
    uint8_t col[4] = {0, 0, 0, 0};
    int16_t plane = 0, pix[4] = {0, 0, 0, 0};
    uint32_t glide = 0, xform[6] = {0, 0, 0, 0, 0, 0};
    std::string rSource, rTarget;
    canon::Json filters = canon::array({});
    std::vector<canon::Json> vars;

    for (uint32_t bit = 1; bit <= 0x8000; bit <<= 1) {
        if (!(mask & bit)) continue;
        int w = varWidth(bit);
        size_t start = c.p;
        if (bit == 0x800) filters = readFilters(c);
        else if (bit == 0x1000) { rSource = c.cstr(); rTarget = c.cstr(); }   // wire order: source then target
        else if (w > 0) {
            ByteSpan d = c.take(size_t(w));
            if (bit == 0x40) { hasColor = true; std::memcpy(col, d.data(), 4); }
            else if (bit == 0x200) { hasPlane = true; plane = int16_t(le16(&d[0])); }
            else if (bit == 0x10) { hasGlide = true; glide = le32(&d[0]); }
            else if (bit == 0x8) { hasPix = true; for (int k = 0; k < 4; k++) pix[k] = int16_t(le16(&d[2 * k])); }
            else if (bit == 0x20) { hasXform = true; for (int k = 0; k < 6; k++) xform[k] = le32(&d[4 * k]); }
        }
        if (w != 0)   // bit 0x80 is a flag only: no payload, no vars entry
            vars.push_back(canon::Object().u("bit", bit).bytes("data", c.b.subspan(start, c.p - start)).build());
    }
    if (c.p != body.size()) throw DecodeError("0x26 consumed " + std::to_string(c.p) + " of " + std::to_string(body.size()));

    std::vector<canon::Json> xf;
    for (int k = 0; k < 6; k++) xf.push_back(F(xform[k]));
    return o.u("appid", appid).str("name", name).str("suffix", suffix).u("iconResID", iconRes)
        .str("iconState", iconState).str("text", text).str("maptext", maptext)
        .u("maptextW", mtW).u("maptextH", mtH).i("maptextX", mtX).i("maptextY", mtY)
        .u("dir", dir).u("appearanceFlags", aflags).u("flags", uint8_t(aflags))
        .set("slots", canon::array(slots)).u("iconRef32", iconRef).u("iconRef", uint16_t(iconRef))
        .set("verbs", canon::array(verbs))
        .set("overlays", canon::array(overlays)).set("underlays", canon::array(underlays))
        .u("overlayCount", overlays.size()).u("underlayCount", underlays.size())
        .set("layer", F(layer)).str("screenLoc", screenLoc).u("varsMask", mask)
        .set("vars", canon::array(vars)).set("filters", filters)
        .str("renderSource", rSource).str("renderTarget", rTarget)
        .set("widths", canon::Object().u("id", 4).u("verb", 2).build())
        .boolean("hasColor", hasColor).u("colorR", col[0]).u("colorG", col[1]).u("colorB", col[2]).u("colorA", col[3])
        .i("plane", plane).boolean("hasPlane", hasPlane)
        .i("pixelX", pix[0]).i("pixelY", pix[1]).i("pixelW", pix[2]).i("pixelZ", pix[3])
        .boolean("hasPixelOffsets", hasPix)
        .set("glideSize", F(glide)).boolean("hasGlideSize", hasGlide)
        .set("transform", canon::array(xf)).boolean("hasTransform", hasXform)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0026, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto::g1
