// s2c 0x14: map-window refresh (appearance grid) - stateful, see proto/g2_mapview.h.
#include "proto/g2_mapview.h"

namespace fugue::proto {
namespace {

// Machine: proto/g2_mapview.cpp.
canon::Json decode(State& st, ByteSpan body) { return g2::decodeMapRefresh(st, 0x14, body); }

FUGUE_REGISTER_DECODER(Dir::S2C, 0x14, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
