// maptext: the SIZE half of the maptext layout (markup parse + layout) and the 0x00fc MeasureText reply.
// Colour, alignment, outline and href do not change a measured width or height, so they are not modelled.
//
// The client answers 0x00fc from dsmaptext.h, which reproduces DreamSeeker's own layout.
//
// Metric: Regular at a CSS-px to em ladder (5/6/7/8 px map to 8/9/11/13). Not DreamSeeker's own metric (see dsmaptext.h);
// the painter (maptextpaint) uses dsmaptext's layout, not this one.
//
// Fidelity notes:
//  - text is bytes. Widths are computed over rune decoding (invalid byte = U+FFFD, width 1), and the
//    mid-word break of an over-wide word can cut one byte through a multi-byte rune.
//  - Whitespace trimming uses Unicode spaces (U+00A0, U+2003, ...) so they form blank words, while tokenising
//    uses ASCII whitespace only.
//  - Float-to-int conversions match x86-64 (out-of-range = INT64_MIN, i.e. clamps to the minimum size).
//  - Known divergence: lowercasing is ASCII plus the two non-ASCII code points that fold into ASCII
//    (U+212A KELVIN SIGN -> k, U+0130 -> i); other non-ASCII case folds cannot match any keyword anyway.
//    Float parse range errors at |x| < 2.2e-308 are not modelled.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace fugue::render {

struct MaptextMeasure {
    int width = 0;    // widest line, px
    int height = 0;   // sum of line heights (ascent + descent + 1 leading, per line)
};

// Measures markup under a style sheet at the given width; width 0 = no wrap limit.
MaptextMeasure measureMaptext(std::string_view markup, std::string_view style, int width);

// MeasureText: measureMaptext reduced to the u16 pair; an overflow of the u16 is the 0xffff/0xffff
// "could not measure" encoding.
struct MeasureTextAnswer {
    uint16_t width = 0, height = 0;
};
MeasureTextAnswer measureTextAnswer(std::string_view text, std::string_view style, uint16_t width);

// Decodes a server->client 0x00fc body: [text\0][style\0][width u16][id u32].
// Quirks are kept: an unterminated second string reads as "" and the tail must be exactly 6 bytes.
// nullopt = malformed (no reply is sent).
struct MeasureTextRequest {
    std::string text, style;
    uint16_t width = 0;
    uint32_t id = 0;
};
std::optional<MeasureTextRequest> decodeMeasureTextRequest(const uint8_t* body, size_t n);

// Encodes the 8-byte reply to req, echoing req.id.
std::string measureTextReplyBody(const MeasureTextRequest& req);

}  // namespace fugue::render
