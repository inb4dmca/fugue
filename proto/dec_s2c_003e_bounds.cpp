// s2c 0x003e: map / view bounds. 24 bytes, twelve u16 LE.
//   u16[0] maxX   u16[1] maxY      (world size in tiles)
//   u16[2] winW   u16[3] winH      (map window, tiles; can be larger than the view)
//   u16[4..5]                      (unnamed)
//   u16[6] viewW  u16[7] viewH     (displayed view extent per axis)
//   u16[8..11]                     (unnamed)
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// The minimum body is 14 bytes. viewH defaults to viewW when the body is < 16 bytes. view = (viewW-1)/2 in 16-bit
//   arithmetic, so viewW == 0 wraps to 32767, kept as-is. rest = body[8:24] only when the body is >= 24 bytes.
//   Offsets 4/6 are the window size, not the map size.
canon::Json decode(State& st, ByteSpan b) {
    if (b.size() < 14) throw DecodeError("short bounds");
    uint16_t maxX = le16(&b[0]), maxY = le16(&b[2]);
    uint16_t winW = le16(&b[4]), winH = le16(&b[6]);
    uint16_t viewW = le16(&b[12]);
    uint16_t viewH = b.size() >= 16 ? le16(&b[14]) : viewW;
    uint16_t view = uint16_t(uint16_t(viewW - 1) / 2);
    ByteSpan rest = b.size() >= 24 ? b.subspan(8, 16) : ByteSpan{};

    // Record the map bounds and window size on every 0x003e; the map refresh decoders depend on them.
    st.mapView.haveBounds = true;
    st.mapView.maxX = maxX; st.mapView.maxY = maxY; st.mapView.winW = winW; st.mapView.winH = winH;

    return canon::Object()
        .u("maxX", maxX).u("maxY", maxY).u("winW", winW).u("winH", winH)
        .u("view", view).u("viewW", viewW).u("viewH", viewH)
        .bytes("rest", rest)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x003e, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
