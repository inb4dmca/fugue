// Shared by the 0x0007 and 0x0008 decoders: the ten-array object-update grammar. Also provides a small cursor
// (used by several decoders) that, unlike proto::Reader, can hand back the bytes between two positions.
#pragma once
#include "proto/registry.h"

namespace fugue::proto::g3 {

class Cur {
public:
    explicit Cur(ByteSpan b) : b_(b) {}
    size_t pos() const { return p_; }
    size_t size() const { return b_.size(); }
    bool more() const { return p_ < b_.size(); }
    uint8_t u8() { need(1); return b_[p_++]; }
    uint16_t u16() { need(2); auto v = le16(&b_[p_]); p_ += 2; return v; }
    uint32_t u32() { need(4); auto v = le32(&b_[p_]); p_ += 4; return v; }
    ByteSpan take(size_t n) { need(n); auto s = b_.subspan(p_, n); p_ += n; return s; }
    ByteSpan from(size_t start) const { return b_.subspan(start, p_ - start); }
    void expectEnd(const char* what) const {
        if (p_ != b_.size())
            throw DecodeError(std::string(what) + " consumed " + std::to_string(p_) + " of " + std::to_string(b_.size()));
    }
private:
    void need(size_t n) const {
        if (b_.size() - p_ < n) throw DecodeError("short body: need " + std::to_string(n) + " at " + std::to_string(p_));
    }
    ByteSpan b_;
    size_t p_ = 0;
};

// Reads the ten counted object arrays + optional tail and sets, on `o`, the fields:
// new pos bind dir appearance pixel bounds step visContents particles tail hasTail.
void readObjectArrays(Cur& c, canon::Object& o);

// A self-delimiting DM value; returns the raw bytes consumed.
ByteSpan readDmValue(Cur& c);

}  // namespace fugue::proto::g3
