// s2c 0x0011: verb-list advertisement, ONE verb entry per frame.
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Layout: [id u16][name\0][desc\0][cat\0][head u8][src u8][flags u8]([flagsExt u32][extByte u8] iff flags&0x80)
//   [argc u8] argc x [typemask u32][filter u8][filterKind u8]. Head, src, filter and filterKind are not decoded further.
canon::Json decode(State&, ByteSpan body) {
    Reader r(body);
    uint16_t id = r.u16();
    std::string name = r.cstr(), desc = r.cstr(), cat = r.cstr();
    uint8_t head = r.u8(), src = r.u8(), flags = r.u8();
    uint32_t flagsExt = 0;
    uint8_t extByte = 0;
    if (flags & 0x80) { flagsExt = r.u32(); extByte = r.u8(); }
    uint8_t argc = r.u8();
    std::vector<canon::Json> args;
    for (unsigned i = 0; i < argc; i++) {
        uint32_t mask = r.u32();
        uint8_t filter = r.u8(), kind = r.u8();
        args.push_back(canon::Object().u("typeMask", mask).u("filter", filter).u("filterKind", kind).build());
    }
    r.expectEnd();
    return canon::Object()
        .u("id", id).str("name", name).str("desc", desc).str("category", cat)
        .u("head", head).u("src", src).u("flags", flags).u("flagsExt", flagsExt).u("extByte", extByte)
        .set("args", canon::array(args))
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0011, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
