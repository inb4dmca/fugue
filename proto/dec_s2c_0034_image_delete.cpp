// s2c 0x0034: client.images -= ... One frame carries EVERY image removed in the same tick: n x u32 ids.
#include "proto/g4_imageop.h"

namespace fugue::proto {
namespace {

// The body is n x u32 ids. The first fills imageID and the rest go to moreIDs; the other ImageOp fields stay zero/empty.
canon::Json decode(State&, ByteSpan body) {
    if (body.size() < 4 || body.size() % 4 != 0)
        throw DecodeError("0x34 body of " + std::to_string(body.size()) + " bytes is not a list of u32 image ids");
    Reader r(body);
    g4::ImageOpFields f;
    f.op = 0x0034;
    f.imageID = r.u32();
    while (r.remaining()) f.moreIDs.push_back(r.u32());
    return g4::imageOpJson(f);
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0034, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
