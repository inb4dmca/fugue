// s2c 0x0e: resource-availability list: concatenated 5-byte [content-id u32][type u8], no header.
// The resource ordinal (index across frames) is not part of the decoded value; the running total is kept in State
// so a consumer can derive it.
#include "proto/registry.h"

namespace fugue::proto {
namespace {

struct ResListState { uint64_t total = 0; };   // entries seen so far in this session (next ordinal)

// Array of [content-id u32][type u8] with no count header. Ordinals run across frame splits.
canon::Json decode(State& st, ByteSpan b) {
    if (b.size() % 5 != 0) throw DecodeError("0x0e length not a multiple of 5");
    std::vector<canon::Json> es;
    es.reserve(b.size() / 5);
    for (size_t p = 0; p < b.size(); p += 5)
        es.push_back(canon::Object().u("id", le32(&b[p])).u("type", b[p + 4]).build());
    st.get<ResListState>().total += es.size();
    return canon::array(es);
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x000e, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
