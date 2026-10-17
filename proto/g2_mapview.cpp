// Wire layout: header [origin x u16][origin y u16][z u16]; a full refresh is forced (0x7fff) for 0x12, 0x61 and a
// z change; the walk is y-outer, x-inner; the RLE cell stream has N+1 run semantics with lastID=0xffff; the 0x20 trailer is [count u16] n x
// [index][appid]; ids are u32. The preserve rectangle and world-bounds test use 16-bit wraparound
// arithmetic, and view state advances only when the whole frame decodes.
#include "proto/g2_mapview.h"

namespace fugue::proto::g2 {

namespace {

using Cells = std::vector<std::pair<uint16_t, uint16_t>>;

Cells footprint(const State& st, const MapView& v, uint16_t originX, uint16_t originY, bool full) {
    const auto& b = st.mapView;
    int32_t x1, y1, x2, y2;
    if (full) {
        x1 = y1 = x2 = y2 = 0x7fff;
    } else {
        x1 = int16_t(uint16_t(v.originX - originX));
        y1 = int16_t(uint16_t(v.originY - originY));
        x2 = int16_t(uint16_t(uint16_t(x1) - 1 + b.winW));
        y2 = int16_t(uint16_t(uint16_t(y1) - 1 + b.winH));
    }
    Cells out;
    out.reserve(size_t(b.winW) * b.winH);
    for (int32_t y = 0; y < b.winH; y++) {
        for (int32_t x = 0; x < b.winW; x++) {
            if (x >= x1 && x <= x2 && y >= y1 && y <= y2) continue;   // preserved by the client
            int32_t wx = int16_t(uint16_t(uint16_t(x) + originX));
            int32_t wy = int16_t(uint16_t(uint16_t(y) + originY));
            if (wx < 0 || wy < 0 || wx >= int16_t(b.maxX) || wy >= int16_t(b.maxY)) continue;   // outside world
            out.push_back({uint16_t(wx), uint16_t(wy)});
        }
    }
    return out;
}

// The RLE cell stream for exactly n cells, wide ids.
std::vector<uint32_t> readRle(Reader& r, size_t n) {
    std::vector<uint32_t> out;
    out.reserve(n);
    uint32_t last = 0xffff, cur = 0;
    unsigned run = 0;
    for (size_t i = 0; i < n; i++) {
        if (run > 0) { run--; out.push_back(cur); continue; }
        uint32_t v = r.u32();
        if (v == 0xffffffffu) { cur = last; run = r.u8(); }
        else { cur = last = v; }
        out.push_back(cur);
    }
    return out;
}

}  // namespace

canon::Json decodeMapRefresh(State& st, uint16_t typ, ByteSpan body) {
    if (typ != 0x12 && typ != 0x14 && typ != 0x61) throw DecodeError("not a map refresh type");
    if (st.mapView.winW == 0 || st.mapView.winH == 0) throw DecodeError("map window size unknown (no 0x003e yet)");
    auto& v = st.get<MapView>();
    Reader r(body);
    uint16_t ox = r.u16(), oy = r.u16(), z = r.u16();
    bool full = typ == 0x12 || typ == 0x61 || z != v.z;
    Cells cells = footprint(st, v, ox, oy, full);
    std::vector<uint32_t> ids = readRle(r, cells.size());
    r.expectEnd();

    std::vector<canon::Json> js;
    js.reserve(cells.size());
    for (size_t i = 0; i < cells.size(); i++)
        js.push_back(canon::Object().u("x", cells[i].first).u("y", cells[i].second).u("appid", ids[i]).build());
    v.originX = ox; v.originY = oy; v.z = z;
    v.pending = std::move(cells);
    v.dirty = true;
    return canon::Object().u("originX", ox).u("originY", oy).u("z", z).boolean("full", full)
        .set("cells", canon::array(js)).build();
}

canon::Json decodeTurfGrid(State& st, ByteSpan body) {
    auto& v = st.get<MapView>();
    Reader r(body);
    std::vector<canon::Json> cells, binds;
    // The armed flag is cleared right after the grid read, before the trailer can fail.
    if (v.dirty) {
        std::vector<uint32_t> ids = readRle(r, v.pending.size());
        cells.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); i++)
            cells.push_back(canon::Object().u("x", v.pending[i].first).u("y", v.pending[i].second).u("index", ids[i]).build());
        v.dirty = false;
    }
    uint16_t n = r.u16();
    for (unsigned i = 0; i < n; i++) {
        uint32_t idx = r.u32(), appid = r.u32();
        binds.push_back(canon::Object().u("index", idx).u("appid", appid).build());
    }
    r.expectEnd();
    return canon::Object().set("cells", canon::array(cells)).set("binds", canon::array(binds)).build();
}

}  // namespace fugue::proto::g2
