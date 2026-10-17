// key.txt blocks: marker scan (keyNames), per-account line-1 extraction, and the line-1 two-stage codec.
// Line 1 is stage 1 (run-length groups of base64-style characters) then stage 2 (subtractive cipher). The decoded blob
// is [00][identity: len(acct)+13 bytes][00][tail: 00 installID(le32) innerKey(le32) 3 bytes].
// Key blocks are bearer tokens; nothing here logs them.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "net/bytes.h"

namespace fugue::account {

// Account names from "[BEGIN KEY <name>]" / "BEGIN KEY <name>" marker lines, file order. Reads ONLY the
// markers, never the encoded lines under them.
std::vector<std::string> keyNames(std::string_view keytxt);

// The encoded line immediately after the marker for `acct` (ASCII case-insensitive), CR/space-trimmed.
std::optional<std::string> keyBlockLine1(std::string_view keytxt, std::string_view acct, std::string* err = nullptr);

struct KeyBlockFields {
    fugue::Bytes identity;  // len(account)+13
    uint32_t installId = 0;
    uint32_t innerKey = 0;
    fugue::Bytes tail;      // 12 bytes
};

// Stages 1+2 -> blob, checking blob[0]==0. keyStr = pwordCryptLevel2(password) (32 hex chars).
std::optional<fugue::Bytes> keyBlockBlob(std::string_view line1, std::string_view keyStr, std::string* err = nullptr);
// Full decode with the length / terminator / nonzero-credential framing checks. A wrong password fails here.
std::optional<KeyBlockFields> decodeKeyBlock(std::string_view line1, std::string_view account, std::string_view keyStr,
                                             std::string* err = nullptr);
// Synthesis (inverse of the above) for tests: blob -> line-1 text. `seed` is the 2 leading characters.
std::string encodeKeyLine1(fugue::ByteSpan blob, std::string_view keyStr, std::string_view seed = "xq");
// Builds blob = [00][identity][00][00 installID innerKey 3 bytes] for decodeKeyBlock.
fugue::Bytes buildKeyBlob(fugue::ByteSpan identity, uint32_t installId, uint32_t innerKey, fugue::ByteSpan tail3);

}  // namespace fugue::account
