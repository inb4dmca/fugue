// Decoder registry: decoders self-register from their own .cpp, keyed by (dir, type).
//
// HOW TO ADD A DECODER (no shared file is edited): create proto/dec_<dir>_<type>.cpp, write the
// function, and register it with FUGUE_REGISTER_DECODER at namespace scope.
#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <unordered_map>

#include "net/bytes.h"
#include "proto/mapview.h"
#include "tools/canon_json/canon_json.h"

namespace fugue::proto {

enum class Dir : uint8_t { C2S = 0, S2C = 1 };   // wire direction: client-to-server 0, server-to-client 1
inline const char* dirName(Dir d) { return d == Dir::C2S ? "c2s" : "s2c"; }

// Thrown by Reader and by decoders on malformed input; the caller reports the message as undecodable.
struct DecodeError : std::runtime_error { using std::runtime_error::runtime_error; };

// Cross-frame state for one decode run, threaded through the decoders in order.
class State {
public:
    MapView mapView;   // see proto/mapview.h

    // Decoder-private state, no header change needed: any default-constructible type, created on
    // first use and kept until this State is destroyed. e.g.  auto& s = st.get<MyDecoderState>();
    template <class T>
    T& get() {
        auto& slot = ext_[std::type_index(typeid(T))];
        if (!slot) slot = std::shared_ptr<void>(new T(), [](void* p) { delete static_cast<T*>(p); });
        return *static_cast<T*>(slot.get());
    }
private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> ext_;
};

// A decoder returns the canonical JSON of the "fields" value (build it with fugue::canon), or throws
// DecodeError / any std::exception for a malformed body. It must NOT print.
using DecodeFn = canon::Json (*)(State&, ByteSpan body);

struct DecoderInfo {
    Dir dir;
    uint16_t type;
    const char* dec;   // name identifying the decoder that produced the entry
    DecodeFn fn;
};

class Registry {
public:
    static Registry& instance();
    void add(const DecoderInfo& d);   // duplicate (dir,type) aborts at startup: two decoders for one slot is a bug
    const DecoderInfo* find(Dir dir, uint16_t type) const;
    size_t size() const { return map_.size(); }
private:
    std::unordered_map<uint32_t, DecoderInfo> map_;
};

struct Registrar {
    Registrar(Dir d, uint16_t t, const char* dec, DecodeFn fn) { Registry::instance().add({d, t, dec, fn}); }
};

#define FUGUE_CONCAT2_(a, b) a##b
#define FUGUE_CONCAT_(a, b) FUGUE_CONCAT2_(a, b)
// FUGUE_REGISTER_DECODER(Dir::S2C, 0x0071, "dec_s2c_0071_selfref", decode71);
#define FUGUE_REGISTER_DECODER(dir, type, decName, fn) \
    static ::fugue::proto::Registrar FUGUE_CONCAT_(fugue_registrar_, __LINE__)((dir), (type), (decName), (fn))

// Bounds-checked little-endian reader over a body.
class Reader {
public:
    explicit Reader(ByteSpan b) : b_(b) {}
    size_t remaining() const { return b_.size() - p_; }
    size_t pos() const { return p_; }
    size_t size() const { return b_.size(); }
    uint8_t u8() { need(1); return b_[p_++]; }
    uint16_t u16() { need(2); auto v = le16(&b_[p_]); p_ += 2; return v; }
    uint32_t u32() { need(4); auto v = le32(&b_[p_]); p_ += 4; return v; }
    ByteSpan take(size_t n) { need(n); auto s = b_.subspan(p_, n); p_ += n; return s; }
    ByteSpan rest() { return take(remaining()); }
    // NUL-terminated string (terminator consumed, not returned). Throws if there is no NUL.
    std::string cstr();
    void expectEnd() const { if (remaining()) throw DecodeError("trailing " + std::to_string(remaining()) + " bytes"); }
private:
    void need(size_t n) const { if (remaining() < n) throw DecodeError("short body: need " + std::to_string(n) + ", have " + std::to_string(remaining())); }
    ByteSpan b_;
    size_t p_ = 0;
};

}  // namespace fugue::proto
