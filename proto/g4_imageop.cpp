#include "proto/g4_imageop.h"

namespace fugue::proto::g4 {

canon::Json imageOpJson(const ImageOpFields& f) {
    std::vector<canon::Json> vis, more;
    for (auto& [t, v] : f.vis) vis.push_back(canon::Object().u("type", t).u("value", v).build());
    for (uint32_t id : f.moreIDs) more.push_back(canon::u(id));
    return canon::Object()
        .u("appid", f.appid).u("cell", f.cell).u("imageID", f.imageID).u("locKind", f.locKind)
        .u("locRef", f.locRef).set("moreIDs", canon::array(more)).u("op", f.op)
        .set("visContents", canon::array(vis))
        .build();
}

// Layout: [image-id u32][appid u32][loc-kind u8][loc]; a turf loc is a flat u32 cell, a mob/obj loc is
// a ref. The two bytes after the loc are the vis_contents count m, then m x {Type u8, Value u32}.
// A turf (loc-kind 0x07) fills cell, otherwise locRef.
// The body must be exactly 15 + 5m bytes; anything else throws rather than guessing a framing.
ImageOpFields parseImageCreateModify(uint16_t typ, ByteSpan body) {
    Reader r(body);
    ImageOpFields f;
    f.op = typ;
    f.imageID = r.u32();
    f.appid = r.u32();
    f.locKind = r.u8();
    uint32_t loc = r.u32();
    if (f.locKind == 0x07) f.cell = loc; else f.locRef = loc;
    uint16_t m = r.u16();
    for (uint16_t i = 0; i < m; i++) {
        uint8_t t = r.u8();
        uint32_t v = r.u32();
        f.vis.emplace_back(t, v);
    }
    r.expectEnd();
    return f;
}

}  // namespace fugue::proto::g4
