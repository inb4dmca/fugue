// Shared by the client.images decoders 0x0033 / 0x0034 / 0x0035: the canonical field object.
// All three types decode to the same ImageOp record, so the same key set is emitted.
#pragma once
#include <cstdint>
#include <vector>

#include "proto/registry.h"

namespace fugue::proto::g4 {

struct ImageOpFields {
    uint16_t op = 0;
    uint32_t imageID = 0, appid = 0, cell = 0, locRef = 0;
    uint8_t locKind = 0;
    std::vector<std::pair<uint8_t, uint32_t>> vis;   // {Type u8, Value u32}
    std::vector<uint32_t> moreIDs;
};

canon::Json imageOpJson(const ImageOpFields& f);

// 0x0033 / 0x0035 body: [imageID u32][appid u32][locKind u8][loc u32][m u16] m x {type u8, value u32}.
ImageOpFields parseImageCreateModify(uint16_t typ, ByteSpan body);

}  // namespace fugue::proto::g4
