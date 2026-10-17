#include "proto/g3_inview.h"

namespace fugue::proto::g3 {

namespace {

// Every record struct embeds the InViewRec header; the canonical encoding does not flatten embedded
// structs, so it appears as a nested object under "inViewRec".
canon::Json recJson(uint32_t id, uint16_t key) { return canon::Object().u("id", id).u("key", key).build(); }

struct Head { uint32_t id; uint16_t key; };
Head head(Cur& c) { Head h; h.id = c.u32(); h.key = c.u16(); return h; }

template <class F>
canon::Json counted(Cur& c, F f) {
    std::vector<canon::Json> v;
    for (unsigned n = c.u8(); n > 0; n--) {
        Head h = head(c);
        canon::Object o;
        o.set("inViewRec", recJson(h.id, h.key));
        f(o);
        v.push_back(o.build());
    }
    return canon::array(v);
}

// The first array is a plain list of InViewRec headers (no wrapper object).
canon::Json countedPlain(Cur& c) {
    std::vector<canon::Json> v;
    for (unsigned n = c.u8(); n > 0; n--) { Head h = head(c); v.push_back(recJson(h.id, h.key)); }
    return canon::array(v);
}

// Per entry: InViewRec head, [particleID u32]; unless the id is 0xffff, a [len u16] fixed block follows, then
// generators [key u16][type u8] (type 0xff = no payload, else [len u16][len bytes]) up to key 0 or end of body.
canon::Json particles(Cur& c) {
    std::vector<canon::Json> v;
    for (unsigned n = c.u8(); n > 0; n--) {
        Head h = head(c);
        uint32_t pid = c.u32();
        ByteSpan fixed;
        std::vector<canon::Json> gens;
        if (pid != 0xffff) {
            fixed = c.take(c.u16());
            // The generator stream ends at key==0 OR at the end of the body; a body that simply stops
            // is accepted.
            while (c.more()) {
                uint16_t k = c.u16();
                if (k == 0) break;
                uint8_t t = c.u8();
                ByteSpan payload;
                if (t != 0xff) payload = c.take(c.u16());
                gens.push_back(canon::Object().u("key", k).u("type", t).bytes("payload", payload).build());
            }
        }
        v.push_back(canon::Object()
            .set("inViewRec", recJson(h.id, h.key))
            .u("particleID", pid)
            .bytes("fixed", fixed)
            .set("gens", canon::array(gens))
            .build());
    }
    return canon::array(v);
}

}  // namespace

// DMValue is a shared self-delimiting reader: kinds 1/2/3/4/6/12/15/0x2a are sized as below, anything
// else is NULL with zero payload bytes; refs are u32.
// Widths: 1: u32+ref, 2/3: ref+u16+ref, 4: ref+ref, 6: cstring, 0xc/0x2a: u32, 0xf: u16 count + nested.
ByteSpan readDmValue(Cur& c) {
    size_t start = c.pos();
    uint8_t kind = c.u8();
    switch (kind) {
    case 1: c.u32(); c.u32(); break;
    case 2: case 3: c.u32(); c.u16(); c.u32(); break;
    case 4: c.u32(); c.u32(); break;
    case 6: while (c.u8() != 0) {} break;
    case 0xc: case 0x2a: c.u32(); break;
    case 0xf: for (unsigned n = c.u16(); n > 0; n--) readDmValue(c); break;
    default: break;
    }
    return c.from(start);
}

// Array order, record shapes and u8 counts follow the 0x0008 layout. A bytes-left guard precedes the optional arrays
// and the tail.
void readObjectArrays(Cur& c, canon::Object& o) {
    o.set("new", countedPlain(c));
    o.set("pos", counted(c, [&](canon::Object& r) {
        uint16_t x = c.u16(), y = c.u16(), z = c.u16();
        r.u("x", x).u("y", y).u("z", z);
    }));
    o.set("bind", counted(c, [&](canon::Object& r) { r.u("other", c.u32()); }));
    o.set("dir", counted(c, [&](canon::Object& r) { r.u("dir", c.u8()); }));
    o.set("appearance", counted(c, [&](canon::Object& r) { r.u("appearance", c.u32()); }));

    auto s16 = [&]() { return int64_t(int16_t(c.u16())); };
    std::vector<canon::Json> none;
    canon::Json empty = canon::array(none);

    o.set("pixel", c.more() ? counted(c, [&](canon::Object& r) {
            int64_t px = s16(), py = s16(), pw = s16(), pz = s16();
            r.i("pixelX", px).i("pixelY", py).i("pixelW", pw).i("pixelZ", pz);
        }) : empty);
    o.set("bounds", c.more() ? counted(c, [&](canon::Object& r) {
            int64_t bx = s16(), by = s16(), bw = s16(), bh = s16();
            r.i("boundX", bx).i("boundY", by).i("boundW", bw).i("boundH", bh);
        }) : empty);
    o.set("step", c.more() ? counted(c, [&](canon::Object& r) {
            int64_t sx = s16(), sy = s16();
            uint32_t ex = c.u32();
            r.i("stepX", sx).i("stepY", sy).u("extra", ex);
        }) : empty);
    o.set("visContents", c.more() ? counted(c, [&](canon::Object& r) {
            std::vector<canon::Json> items;
            for (unsigned k = c.u16(); k > 0; k--) {
                uint8_t t = c.u8();
                uint32_t v = c.u32();
                items.push_back(canon::Object().u("type", t).u("value", v).build());
            }
            r.set("items", canon::array(items));
        }) : empty);
    o.set("particles", c.more() ? particles(c) : empty);

    if (c.more()) o.u("tail", c.u8()).boolean("hasTail", true);
    else o.u("tail", 0).boolean("hasTail", false);
}

}  // namespace fugue::proto::g3
