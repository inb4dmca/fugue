// Helpers for the map-window refresh family (0x0012 0x0014 0x0061 0x0020). The footprint state
// machine lives here, in State::get<g2::MapView>(), beside the shared MapView struct. World bounds and
// window size are read from State::mapView, which the 0x003e decoder already fills.
#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "proto/registry.h"

namespace fugue::proto::g2 {

struct MapView {
    uint16_t originX = 0, originY = 0, z = 0;           // window origin after the last successful refresh
    std::vector<std::pair<uint16_t, uint16_t>> pending; // footprint the next 0x20 must re-read
    bool dirty = false;                                  // a refresh armed a grid read for the next 0x20
};

// 0x0012 / 0x0014 / 0x0061 body -> canonical fields of a MapRefresh; advances the view on success only.
canon::Json decodeMapRefresh(State& st, uint16_t typ, ByteSpan body);
// 0x0020 body -> canonical fields of a TurfGrid; consumes the armed footprint.
canon::Json decodeTurfGrid(State& st, ByteSpan body);

}  // namespace fugue::proto::g2
