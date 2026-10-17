// DDMI recipes: parse and evaluate. See ddmi.h.
#include "render/ddmi.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "render/pixops.h"

namespace fugue::render {

namespace {

int g_ddmiMut = 0;

constexpr uint8_t kOpIconRef = 0x01, kOpColor = 0x03, kOpState = 0x05, kOpDir = 0x40, kOpBlend = 0x42, kOpUnk48 = 0x48, kOpDrawBox = 0x49,
                  kOpMapColors = 0x4a;

bool payloadLen(uint8_t tag, const uint8_t* d, size_t n, size_t i, size_t& out, std::string* err) {
    size_t avail = n - (i + 1);
    auto fixed = [&](size_t w) {
        if (avail < w) { if (err) *err = "DDMI op wants more bytes than remain"; return false; }
        out = w;
        return true;
    };
    switch (tag) {
        case kOpIconRef: return fixed(5);
        case kOpState: {
            const uint8_t* nul = static_cast<const uint8_t*>(std::memchr(d + i + 1, 0, avail));
            if (!nul) { if (err) *err = "DDMI state name unterminated"; return false; }
            out = size_t(nul - (d + i + 1)) + 1;
            return true;
        }
        case kOpDir: return fixed(3);
        case kOpColor: return fixed(4);
        case kOpBlend: return fixed(1);
        case kOpMapColors:
            if (avail < 1) { if (err) *err = "DDMI MapColors op has no count byte"; return false; }
            return fixed(1 + 4 * size_t(d[i + 1]));
        case kOpUnk48:
        case kOpDrawBox: {
            // 0x48 takes its width from the end of the body. Both 0x48 and 0x49 are accepted only as the final op.
            size_t want = tag == kOpUnk48 ? 1 : 8;
            if (avail != want) { if (err) *err = "DDMI op 0x48/0x49 is not final: nothing has been seen after one"; return false; }
            out = want;
            return true;
        }
        default:
            if (err) *err = "DDMI unrecognised op tag";
            return false;
    }
}

// The 12-byte footer of an overlay (blend type 7) followed by the colour-key SwapColor: two COLOR ops (0,255,0,255),
// (0,0,0,0) and an op 0x48.
const uint8_t kSwapFooter[12] = {0x03, 0x00, 0xff, 0x00, 0xff, 0x03, 0x00, 0x00, 0x00, 0x00, 0x48, 0x01};

// Straight-alpha overlay of `top` on `bottom`.
Rgba8 overComposite(Rgba8 bottom, Rgba8 top) {
    if (bottom.a == 0) return top;
    double sa = double(top.a) / 255;
    double da = double(bottom.a) / 255;
    double oa = sa + da * (1 - sa);
    auto round8 = [](double v) -> uint8_t { if (v <= 0) return 0; if (v >= 255) return 255; return uint8_t(std::round(v)); };
    auto ch = [&](uint8_t topC, uint8_t bottomC) {
        uint8_t premul = round8(double(topC) * sa);
        return clamp8(double(premul) + double(bottomC) * (1 - sa));
    };
    return Rgba8{ch(top.r, bottom.r), ch(top.g, bottom.g), ch(top.b, bottom.b), clamp8(oa * 255)};
}
enum { kIconAdd = 0, kIconSubtract = 1, kIconMultiply = 2, kIconOverlay = 3, kIconAnd = 4, kIconOr = 5, kIconUnderlay = 6 };
// R/G/B independently clamped, alpha the PRODUCT 255*sa*da
template <class Op>
Rgba8 channelBlend(Rgba8 self, Rgba8 other, Op op) {
    double sa = double(self.a) / 255;
    double da = double(other.a) / 255;
    return Rgba8{clamp8(op(self.r, other.r)), clamp8(op(self.g, other.g)), clamp8(op(self.b, other.b)), clamp8(255 * sa * da)};
}
// The clamped colour add with the UNION alpha
Rgba8 orBlend(Rgba8 self, Rgba8 other) {
    double sa = double(self.a) / 255;
    double da = double(other.a) / 255;
    double oa = sa + da * (1 - sa);
    auto add = [](uint8_t a, uint8_t b) { return clamp8(double(a) + double(b)); };
    return Rgba8{add(self.r, other.r), add(self.g, other.g), add(self.b, other.b), clamp8(oa * 255)};
}
// One pixel under every IconBlendMode, in DM's own numbering (ADD 0 SUBTRACT 1 MULTIPLY 2 OVERLAY 3 AND 4 OR 5 UNDERLAY 6).
Rgba8 blendPixelIcon(int mode, Rgba8 self, Rgba8 other) {
    if (self.a == 0) { self.r = self.g = self.b = 0; }
    if (other.a == 0) { other.r = other.g = other.b = 0; }
    switch (mode) {
        case kIconOverlay: return overComposite(self, other);
        case kIconUnderlay: return overComposite(other, self);
        case kIconOr: return orBlend(self, other);
        case kIconAdd:
        case kIconAnd:   // DD's ICON_AND is byte-for-byte ICON_ADD
            return channelBlend(self, other, [](uint8_t a, uint8_t b) { return double(a) + double(b); });
        case kIconSubtract: return channelBlend(self, other, [](uint8_t a, uint8_t b) { return double(a) - double(b); });
        case kIconMultiply: return channelBlend(self, other, [](uint8_t a, uint8_t b) { return double(a) * double(b) / 255; });
        default: return self;
    }
}

// The wire blend byte DD 516 writes -> the DM IconBlendMode it means (mapped, never cast):
// ADD 0, SUBTRACT 1, OR 2, AND 3, MULTIPLY 4, OVERLAY 7, UNDERLAY 8.
bool wireBlend(uint8_t b, int& mode) {
    if (g_ddmiMut == 17) { mode = int(b); return true; }
    switch (b) {
        case 0: mode = kIconAdd; return true;
        case 1: mode = kIconSubtract; return true;
        case 2: mode = kIconOr; return true;
        case 3: mode = kIconAnd; return true;
        case 4: mode = kIconMultiply; return true;
        case 7: mode = kIconOverlay; return true;
        case 8: mode = kIconUnderlay; return true;
    }
    return false;
}

Rgba8 pixelAt(const RgbaImage& im, int x, int y) {
    const uint8_t* p = &im.px[(size_t(y) * size_t(im.w) + size_t(x)) * 4];
    return Rgba8{p[0], p[1], p[2], p[3]};
}
void setPixel(RgbaImage& im, int x, int y, Rgba8 c) {
    uint8_t* p = &im.px[(size_t(y) * size_t(im.w) + size_t(x)) * 4];
    p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
}
const IconState* stateByName(const Icon& ic, const std::string& name) {
    int i = ic.stateIndex(name);
    return i < 0 ? nullptr : &ic.states[size_t(i)];
}
bool cellRect(const Icon& ic, int64_t cell, int& x0, int& y0) {
    x0 = int((cell % ic.sheetCols) * ic.width);
    y0 = int((cell / ic.sheetCols) * ic.height);
    return x0 >= 0 && y0 >= 0 && x0 + ic.width <= ic.sheet.w && y0 + ic.height <= ic.sheet.h;
}

// One state (optionally one dir-frame of it) into a standalone icon, laid out as one row of cells. The state is unnamed
// on purpose: stateFor falls back to it, so the caller's icon_state still resolves.
bool stateIcon(const Icon& ic, const std::string& name, int dirIdx, std::unique_ptr<Icon>& out, std::string& err) {
    if (ic.sheet.px.empty()) { err = "StateIcon on an icon with no pixels"; return false; }
    const IconState* st = stateByName(ic, name);
    if (!st) { err = "icon has no state \"" + name + "\""; return false; }
    if (st->dirs <= 0 || st->frames <= 0) { err = "state has no dirs/frames"; return false; }
    std::vector<int> dirs;
    if (dirIdx >= 0) {
        if (dirIdx >= st->dirs) { err = "state has fewer dirs than the extracted dir index"; return false; }
        dirs.push_back(dirIdx);
    } else {
        for (int d = 0; d < st->dirs; d++) dirs.push_back(d);
    }
    int cols = int(dirs.size()) * int(st->frames);
    auto o = std::make_unique<Icon>();
    o->version = ic.version;
    o->width = ic.width; o->height = ic.height;
    o->sheet.w = cols * ic.width; o->sheet.h = ic.height;
    o->sheet.px.assign(size_t(o->sheet.w) * size_t(o->sheet.h) * 4, 0);
    o->sheetCols = cols;
    std::vector<uint8_t> cell;
    for (size_t di = 0; di < dirs.size(); di++)
        for (int f = 0; f < st->frames; f++) {
            std::string e;
            if (!ic.frame(*st, dirs[di], f, cell, &e)) { err = e; return false; }
            int x0 = (f * int(dirs.size()) + int(di)) * ic.width;
            for (int y = 0; y < ic.height; y++)
                std::memcpy(&o->sheet.px[(size_t(y) * size_t(o->sheet.w) + size_t(x0)) * 4], &cell[size_t(y) * size_t(ic.width) * 4], size_t(ic.width) * 4);
        }
    IconState ns;
    ns.name = "";
    ns.dirs = int64_t(dirs.size());
    ns.frames = st->frames;
    ns.hasDelays = st->hasDelays;
    ns.delays = st->delays;
    ns.movement = st->movement;
    ns.rewind = st->rewind;
    ns.loop = st->loop;
    ns.hasHotspot = st->hasHotspot;
    ns.hotspot = st->hotspot;
    ns.firstCell = 0;
    o->states.push_back(std::move(ns));
    out = std::move(o);
    return true;
}

// Every pixel of the whole sheet blended with a flat colour.
std::unique_ptr<Icon> blendedColor(const Icon& ic, int mode, Rgba8 col) {
    auto o = std::make_unique<Icon>(ic);
    for (int y = 0; y < o->sheet.h; y++)
        for (int x = 0; x < o->sheet.w; x++) setPixel(o->sheet, x, y, blendPixelIcon(mode, pixelAt(ic.sheet, x, y), col));
    return o;
}

// ic blended cell by cell with `other`; each of ic's states pairs with other's SAME-NAMED state, else other's
// FIRST. The result keeps ic's state list and layout.
std::unique_ptr<Icon> blendedWith(const Icon& ic, int mode, const Icon& other) {
    auto o = std::make_unique<Icon>(ic);
    if (other.states.empty()) return o;
    for (const IconState& st : o->states) {
        const IconState* ost = stateByName(other, st.name);
        if (!ost) ost = &other.states[0];
        for (int64_t dir = 0; dir < st.dirs; dir++)
            for (int64_t frame = 0; frame < st.frames; frame++) {
                int64_t cell = st.cellIndex(dir, frame);
                if (cell < 0) continue;
                int rx, ry;
                if (!cellRect(ic, cell, rx, ry)) continue;
                int64_t od = dir, of = frame;
                if (od >= ost->dirs || of >= ost->frames) od = of = 0;
                int64_t ocell = ost->cellIndex(od, of);
                if (ocell < 0) continue;
                int ox, oy;
                if (!cellRect(other, ocell, ox, oy)) continue;
                for (int y = 0; y < ic.height; y++)
                    for (int x = 0; x < ic.width; x++) {
                        if (x >= other.width || y >= other.height) continue;
                        Rgba8 op = pixelAt(other.sheet, ox + x, oy + y);
                        setPixel(o->sheet, rx + x, ry + y, blendPixelIcon(mode, pixelAt(o->sheet, rx + x, ry + y), op));
                    }
            }
    }
    return o;
}

// SwapColor(rgb(0,255,0,255), rgb(0,0,0,0)) with matchAlpha and writeAlpha both true.
std::unique_ptr<Icon> swapColorCells(const Icon& ic, Rgba8 oldc, Rgba8 newc) {
    auto o = std::make_unique<Icon>(ic);
    for (const IconState& st : o->states)
        for (int64_t dir = 0; dir < st.dirs; dir++)
            for (int64_t f = 0; f < st.frames; f++) {
                int64_t cell = st.cellIndex(dir, f);
                if (cell < 0) continue;
                int rx, ry;
                if (!cellRect(ic, cell, rx, ry)) continue;
                for (int y = ry; y < ry + ic.height; y++)
                    for (int x = rx; x < rx + ic.width; x++) {
                        Rgba8 p = pixelAt(o->sheet, x, y);
                        if (p.r != oldc.r || p.g != oldc.g || p.b != oldc.b) continue;
                        if (p.a != oldc.a) continue;
                        setPixel(o->sheet, x, y, newc);
                    }
            }
    return o;
}

// DM's icon.MapColors() over every cell, in float32 arithmetic.
bool mapColorCells(const Icon& ic, const std::vector<float>& coeff, std::unique_ptr<Icon>& out, std::string& err) {
    if (coeff.size() != 20) { err = "unsupported DDMI recipe shape: MapColors with " + std::to_string(coeff.size()) + " coefficients (only the 20-value form is modelled)"; return false; }
    auto o = std::make_unique<Icon>(ic);
    auto ch = [&](float r, float g, float b, float a, int i) -> uint8_t {
        float v = r * coeff[size_t(i)] + g * coeff[size_t(4 + i)] + b * coeff[size_t(8 + i)] + a * coeff[size_t(12 + i)] + coeff[size_t(16 + i)];
        if (v <= 0) return 0;
        if (v >= 1) return 255;
        return uint8_t(v * 255 + 0.5f);
    };
    for (const IconState& st : o->states)
        for (int64_t dir = 0; dir < st.dirs; dir++)
            for (int64_t f = 0; f < st.frames; f++) {
                int64_t cell = st.cellIndex(dir, f);
                if (cell < 0) continue;
                int rx, ry;
                if (!cellRect(ic, cell, rx, ry)) continue;
                for (int y = ry; y < ry + ic.height; y++)
                    for (int x = rx; x < rx + ic.width; x++) {
                        Rgba8 p = pixelAt(o->sheet, x, y);
                        float r = float(p.r) / 255, g = float(p.g) / 255, b = float(p.b) / 255, a = float(p.a) / 255;
                        setPixel(o->sheet, x, y, Rgba8{ch(r, g, b, a, 0), ch(r, g, b, a, 1), ch(r, g, b, a, 2), ch(r, g, b, a, 3)});
                    }
            }
    out = std::move(o);
    return true;
}

// DM's icon.DrawBox over every cell. The colour replaces the pixel, alpha included. Both corners are inclusive, y counts
// up from the bottom row, reversed corners are normalised and out-of-range ones are clipped to the icon.
std::unique_ptr<Icon> drawBoxCells(const Icon& ic, const DdmiRecipe& rec) {
    auto o = std::make_unique<Icon>(ic);
    int x1 = std::max(std::min(rec.bx1, rec.bx2), 1), x2 = std::min(std::max(rec.bx1, rec.bx2), int(ic.width));
    int y1 = std::max(std::min(rec.by1, rec.by2), 1), y2 = std::min(std::max(rec.by1, rec.by2), int(ic.height));
    const Rgba8 c{rec.br, rec.bg, rec.bb, rec.ba};
    for (const IconState& st : o->states)
        for (int64_t dir = 0; dir < st.dirs; dir++)
            for (int64_t f = 0; f < st.frames; f++) {
                int64_t cell = st.cellIndex(dir, f);
                if (cell < 0) continue;
                int rx, ry;
                if (!cellRect(ic, cell, rx, ry)) continue;
                for (int y = y1; y <= y2; y++)
                    for (int x = x1; x <= x2; x++) setPixel(o->sheet, rx + x - 1, ry + int(ic.height) - y, c);
            }
    return o;
}

}  // namespace

bool isDdmi(const uint8_t* d, size_t n) { return n >= 8 && std::memcmp(d, "DDMI", 4) == 0; }

bool parseDdmi(const uint8_t* d, size_t n, DdmiRecipe& rec, std::string* err) {
    if (!isDdmi(d, n)) { if (err) *err = "not a DDMI recipe (bad magic)"; return false; }
    uint32_t bodyLen = uint32_t(d[4]) | uint32_t(d[5]) << 8 | uint32_t(d[6]) << 16 | uint32_t(d[7]) << 24;
    if (uint64_t(8) + uint64_t(bodyLen) != uint64_t(n)) { if (err) *err = "DDMI declared body length does not match the payload size"; return false; }
    size_t blendEnd = size_t(-1);
    size_t i = 8;
    uint8_t prevTag = 0;
    while (i < n) {
        uint8_t tag = d[i];
        size_t len;
        if (!payloadLen(tag, d, n, i, len, err)) return false;
        const uint8_t* payload = d + i + 1;
        switch (tag) {
            case kOpIconRef: {
                DdmiRef r;
                r.ordinal = uint32_t(payload[0]) | uint32_t(payload[1]) << 8 | uint32_t(payload[2]) << 16 | uint32_t(payload[3]) << 24;
                r.kind = payload[4];
                rec.refs.push_back(std::move(r));
                break;
            }
            case kOpState:
                if (rec.refs.empty()) { if (err) *err = "DDMI state op has no icon reference before it"; return false; }
                rec.refs.back().name.assign(reinterpret_cast<const char*>(payload), len - 1);
                rec.refs.back().hasName = true;
                break;
            case kOpDir:
                if (rec.refs.empty()) { if (err) *err = "DDMI dir op has no icon reference before it"; return false; }
                rec.refs.back().hasDir = true;
                std::memcpy(rec.refs.back().trailer, payload, 3);
                break;
            case kOpColor:
                if (!rec.hasBlend) {
                    if (rec.hasColor) { if (err) *err = "DDMI recipe carries more than one colour op before the blend (unmodelled)"; return false; }
                    rec.hasColor = true;
                    rec.cr = payload[0]; rec.cg = payload[1]; rec.cb = payload[2]; rec.ca = payload[3];
                }
                break;
            case kOpBlend:
                if (rec.hasBlend) { if (err) *err = "DDMI recipe carries a second blend op (unmodelled)"; return false; }
                rec.hasBlend = true;
                rec.blendType = payload[0];
                blendEnd = i + 1 + len;
                break;
            case kOpMapColors: {
                if (rec.hasMapColors) { if (err) *err = "DDMI recipe carries a second MapColors op (unmodelled)"; return false; }
                rec.hasMapColors = true;
                rec.mapColors.resize(payload[0]);
                for (size_t k = 0; k < rec.mapColors.size(); k++) {
                    uint32_t b = uint32_t(payload[1 + 4 * k]) | uint32_t(payload[2 + 4 * k]) << 8 | uint32_t(payload[3 + 4 * k]) << 16 | uint32_t(payload[4 + 4 * k]) << 24;
                    std::memcpy(&rec.mapColors[k], &b, 4);
                }
                break;
            }
            case kOpDrawBox:
                // The colour is the COLOR op immediately before it; without one it stays unsettled.
                if (rec.hasBlend) break;
                if (prevTag == kOpColor && rec.hasColor) {
                    auto u16 = [&](int k) { return int(payload[2 * k]) | int(payload[2 * k + 1]) << 8; };
                    rec.hasDrawBox = true;
                    rec.br = rec.cr; rec.bg = rec.cg; rec.bb = rec.cb; rec.ba = rec.ca;
                    rec.bx1 = u16(0); rec.by1 = u16(1); rec.bx2 = u16(2); rec.by2 = u16(3);
                    rec.hasColor = false;
                    rec.cr = rec.cg = rec.cb = rec.ca = 0;
                } else {
                    rec.unsettled.push_back(tag);
                }
                break;
            case kOpUnk48:
                if (!rec.hasBlend) rec.unsettled.push_back(tag);
                break;
            default: break;
        }
        prevTag = tag;
        i += 1 + len;
    }
    if (blendEnd != size_t(-1) && blendEnd < n) rec.footer.assign(d + blendEnd, d + n);
    return true;
}

DdmiResult evalDdmi(const DdmiRecipe& rec, const std::vector<const Icon*>& ops) {
    DdmiResult res;
    auto refuse = [&](const std::string& why) { res.err = why; return std::move(res); };
    if (ops.size() != rec.refs.size()) return refuse("EvalDDMI got the wrong number of operands");
    std::vector<std::unique_ptr<Icon>> ownedSubs(rec.refs.size());
    std::vector<const Icon*> subs(rec.refs.size(), nullptr);
    for (size_t i = 0; i < rec.refs.size(); i++) {
        const DdmiRef& ref = rec.refs[i];
        if (!ops[i]) return refuse("DDMI operand is nil");
        if (!ref.hasName) {
            // No state op means the whole icon, every state and dir, not the empty-named state.
            if (ref.hasDir && ref.trailer[0] != 0xFF) return refuse("unsupported DDMI recipe shape: a dir qualifier but no icon_state");
            subs[i] = ops[i];
            continue;
        }
        int dirIdx = int(ref.trailer[0]);
        if (!ref.hasDir || ref.trailer[0] == 0xFF) dirIdx = -1;
        else if (!(dirIdx >= 0 && dirIdx <= 3)) return refuse("unsupported DDMI recipe shape: dir-frame index outside 0..3 and 0xff");
        std::string e;
        if (!stateIcon(*ops[i], ref.name, dirIdx, ownedSubs[i], e)) return refuse(e);
        subs[i] = ownedSubs[i].get();
    }
    // test-only hook: draw the first operand for a refused shape instead of nothing
    auto refuseShape = [&](const std::string& why) {
        if (g_ddmiMut == 18 && !subs.empty() && subs[0]) {
            if (ownedSubs[0]) res.owned = std::move(ownedSubs[0]); else res.alias = subs[0];
            return std::move(res);
        }
        return refuse(why);
    };
    // An op whose meaning is unsettled poisons the whole recipe.
    if (!rec.unsettled.empty()) return refuseShape("unsupported DDMI recipe shape: carries unsettled op(s)");

    const size_t nrefs = subs.size();
    if (rec.hasDrawBox) {
        // A transparent cell (MapColors with -1 alpha) with a near-transparent white box drawn on it. Only a single unblended
        // operand is modelled.
        if (rec.hasBlend || nrefs != 1 || rec.hasColor)
            return refuseShape("unsupported DDMI recipe shape: DrawBox over anything but a single unblended operand");
        const Icon* base = subs[0];
        std::unique_ptr<Icon> mapped;
        if (rec.hasMapColors) {
            std::string e;
            if (!mapColorCells(*base, rec.mapColors, mapped, e)) return refuse(e);
            base = mapped.get();
        }
        res.owned = drawBoxCells(*base, rec);
        return res;
    }
    if (!rec.hasBlend && nrefs == 1 && !rec.hasColor && rec.hasMapColors) {  // MapColors over the whole icon
        std::string e;
        if (!mapColorCells(*subs[0], rec.mapColors, res.owned, e)) return refuse(e);
        return res;
    }
    if (!rec.hasBlend && nrefs == 1 && !rec.hasColor) {  // the extracted operand, verbatim
        if (ownedSubs[0]) res.owned = std::move(ownedSubs[0]); else res.alias = subs[0];
        return res;
    }
    int mode = 0;
    if (rec.hasBlend && nrefs == 1 && rec.hasColor && rec.footer.empty() && wireBlend(rec.blendType, mode)) {
        // a literal colour operand: icon + / - / * "#rrggbb" and Blend(colour, ICON_*), the mode from the wire byte
        res.owned = blendedColor(*subs[0], mode, Rgba8{rec.cr, rec.cg, rec.cb, rec.ca});
        return res;
    }
    if (rec.hasBlend && nrefs == 2 && !rec.hasColor && rec.footer.empty() && wireBlend(rec.blendType, mode)) {   // left.Blend(right, mode)
        res.owned = blendedWith(*subs[0], mode, *subs[1]);
        return res;
    }
    if (rec.hasBlend && rec.blendType == 7 && nrefs == 2 && !rec.hasColor) {  // the overlay, then the colour-key SwapColor
        if (rec.footer.size() != sizeof kSwapFooter || std::memcmp(rec.footer.data(), kSwapFooter, sizeof kSwapFooter) != 0)
            return refuseShape("unsupported DDMI recipe shape: blend type 7 with a footer other than the one observed");
        std::unique_ptr<Icon> blended = blendedWith(*subs[0], kIconOverlay, *subs[1]);
        res.owned = swapColorCells(*blended, Rgba8{0, 255, 0, 255}, Rgba8{0, 0, 0, 0});
        return res;
    }
    return refuseShape("unsupported DDMI recipe shape: " + std::to_string(nrefs) + " refs, blend type " + std::to_string(rec.blendType));
}

void setDdmiMutation(int m) { g_ddmiMut = m; }

}  // namespace fugue::render
