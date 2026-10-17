// s2c 0x0076: client.screen membership. A flat list of self-framed records; decodes to an array.
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Tags: 01 remove (9 bytes), 02 and 03 add (14 bytes). The u16 after the tag is a cumulative END OFFSET from body start.
//   Fields not present in a record are 0.
canon::Json decode(State&, ByteSpan b) {
    std::vector<canon::Json> out;
    size_t p = 0;
    while (p < b.size()) {
        if (b.size() - p < 3) throw DecodeError("0x76: short record header");
        uint8_t op = b[p];
        size_t end = le16(&b[p + 1]);
        if (end <= p || end > b.size()) throw DecodeError("0x76: bad end offset " + std::to_string(end));
        ByteSpan r = b.subspan(p, end - p);
        canon::Object o;
        if (op == 0x01) {
            if (r.size() != 9) throw DecodeError("0x76 remove record is not 9 bytes");
            o.u("op", op).u("kind", 0).u("index", le32(&r[3])).u("ver", 0).u("appid", 0).u("flags", le16(&r[7]));
        } else if (op == 0x02 || op == 0x03) {
            if (r.size() != 14) throw DecodeError("0x76 add record is not 14 bytes");
            o.u("op", op).u("kind", r[3]).u("index", le32(&r[4])).u("ver", le16(&r[8]))
             .u("appid", le32(&r[10])).u("flags", 0);
        } else {
            throw DecodeError("0x76: unknown record tag");
        }
        out.push_back(o.build());
        p = end;
    }
    return canon::array(out);
}

FUGUE_REGISTER_DECODER(Dir::S2C, 0x0076, "cpp:stock-dd-wire", decode);

}  // namespace
}  // namespace fugue::proto
