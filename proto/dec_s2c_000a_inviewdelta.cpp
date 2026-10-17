// s2c 0x000a: in-view add/remove delta. Four back-to-back u16-counted arrays, no header/trailer.
#include "proto/g3_inview.h"

namespace fugue::proto {
namespace {

// The add/remove delta: two id arrays and two pair arrays, each u16-counted.
canon::Json decode(State&, ByteSpan body) {
    g3::Cur c(body);
    auto ids = [&]() {
        std::vector<canon::Json> v;
        for (unsigned n = c.u16(); n > 0; n--) v.push_back(canon::u(c.u32()));
        return canon::array(v);
    };
    auto pairs = [&]() {
        std::vector<canon::Json> v;
        for (unsigned n = c.u16(); n > 0; n--) {
            uint32_t id = c.u32(), ap = c.u32();
            v.push_back(canon::Object().u("id", id).u("appearance", ap).build());
        }
        return canon::array(v);
    };
    canon::Json a = ids(), b = pairs(), r = ids(), d = pairs();
    c.expectEnd("0x000a");
    return canon::Object().set("addA", a).set("bindA", b).set("removeB", r).set("bindB", d).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x000a, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
