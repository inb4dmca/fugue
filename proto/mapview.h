// Shared map-view state. The map decoders (0x0012 0x0014 0x0020 0x0061) are stateful: they need the
// map bounds from 0x003e and the grid from 0x0061, and dropping a 0x0061 silently desyncs every later
// 0x0014/0x0020. Ids are wide (u32). The footprint state machine lives in g2_mapview.h.
#pragma once
#include <cstdint>

namespace fugue::proto {

struct MapView {
    // Set by the 0x003e decoder (the only writer).
    bool haveBounds = false;
    uint16_t maxX = 0, maxY = 0, winW = 0, winH = 0;
};

}  // namespace fugue::proto
