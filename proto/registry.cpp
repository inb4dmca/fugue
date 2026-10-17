#include "proto/registry.h"

#include <cstdio>
#include <cstdlib>

namespace fugue::proto {

static uint32_t key(Dir d, uint16_t t) { return uint32_t(d) << 16 | t; }

Registry& Registry::instance() {
    static Registry r;   // function-local: safe against static-init order across decoder .cpp files
    return r;
}

void Registry::add(const DecoderInfo& d) {
    if (!map_.emplace(key(d.dir, d.type), d).second) {
        std::fprintf(stderr, "fugue: duplicate decoder registration for dir=%s type=0x%04x\n", dirName(d.dir), d.type);
        std::abort();
    }
}

const DecoderInfo* Registry::find(Dir dir, uint16_t type) const {
    auto it = map_.find(key(dir, type));
    return it == map_.end() ? nullptr : &it->second;
}

std::string Reader::cstr() {
    size_t k = p_;
    while (k < b_.size() && b_[k] != 0) k++;
    if (k == b_.size()) throw DecodeError("unterminated string");
    std::string s(reinterpret_cast<const char*>(b_.data()) + p_, k - p_);
    p_ = k + 1;
    return s;
}

}  // namespace fugue::proto
