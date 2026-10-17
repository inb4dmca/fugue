// s2c 0x20: turf-index grid + bind trailer; consumes the footprint armed by the preceding refresh.
#include "proto/g2_mapview.h"

namespace fugue::proto {
namespace {

// Consumes the footprint armed by the preceding refresh (see g2_mapview.cpp).
canon::Json decode(State& st, ByteSpan body) { return g2::decodeTurfGrid(st, body); }

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0020, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
