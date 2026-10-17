// DDMI: BYOND's runtime icon-composition container (magic "DDMI"). It holds no pixels, only a recipe: icon references
// by ordinal (plus icon_state and a dir-frame qualifier), an optional literal colour, an icon.Blend-family opcode, an
// optional MapColors matrix and an optional DrawBox. The client evaluates the recipe over icons it already has. A recipe
// it cannot evaluate makes the atom invisible, not a placeholder.
//
// The wire blend byte is not an IconBlendMode: it uses its own numbering (ddmi.cpp wireBlend: 0 ADD, 1 SUBTRACT, 2 OR,
// 3 AND, 4 MULTIPLY, 7 OVERLAY, 8 UNDERLAY), so it must be mapped, never cast. A two-operand overlay may also carry a
// footer that adds a colour-key SwapColor (rgb(0,255,0) to transparent). Anything outside the supported shapes is
// refused and the icon is not drawn.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "render/dmi.h"

namespace fugue::render {

struct DdmiRef {
    uint32_t ordinal = 0;  // index into the 0x000e resource-availability list (the same space as an appearance's icon_res)
    uint8_t kind = 0;
    bool hasName = false;
    std::string name;          // the icon_state within the file
    bool hasDir = false;
    uint8_t trailer[3] = {0, 0, 0};   // byte 0 = 4-dir frame index (S N E W = 0..3) or 0xFF = keep every dir
};

struct DdmiRecipe {
    std::vector<DdmiRef> refs;
    bool hasColor = false;
    uint8_t cr = 0, cg = 0, cb = 0, ca = 0;
    bool hasBlend = false;
    uint8_t blendType = 0;
    std::vector<uint8_t> footer;       // verbatim bytes after the BLEND op
    bool hasMapColors = false;
    std::vector<float> mapColors;
    // op 0x49 = icon.DrawBox(colour, x1, y1, x2, y2): u16 LE corners, 1-based from the bottom-left. The COLOR op right
    // before it supplies the box colour and is not the recipe's colour operand (hasColor stays false).
    bool hasDrawBox = false;
    uint8_t br = 0, bg = 0, bb = 0, ba = 0;
    int bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
    std::vector<uint8_t> unsettled;    // op 0x48 (and a 0x49 with no colour before it) seen before the blend
};

bool isDdmi(const uint8_t* d, size_t n);
// false + *err on a malformed recipe (a PERMANENT refusal).
bool parseDdmi(const uint8_t* d, size_t n, DdmiRecipe& out, std::string* err);

struct DdmiResult {
    std::unique_ptr<Icon> owned;       // a new icon, or
    const Icon* alias = nullptr;       // the operand itself ("the extracted operand, verbatim" shape)
    std::string err;                   // set when neither: a refusal (unsupported shape / bad operand)
    const Icon* icon() const { return owned ? owned.get() : alias; }
};
// ops[i] is the icon refs[i] names, IN ORDER (never null). Pure; mutates nothing.
DdmiResult evalDdmi(const DdmiRecipe& rec, const std::vector<const Icon*>& ops);

// Test-only hook; 0 is the shipped behaviour.
void setDdmiMutation(int m);

}  // namespace fugue::render
