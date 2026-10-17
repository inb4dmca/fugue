// s2c 0x0033: client.images += /image  (create). See proto/g4_imageop.cpp for the layout.
#include "proto/g4_imageop.h"

namespace fugue::proto {
namespace {

// Layout: see g4_imageop.cpp.
canon::Json decode(State&, ByteSpan body) { return g4::imageOpJson(g4::parseImageCreateModify(0x0033, body)); }

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0033, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
