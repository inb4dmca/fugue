// Scene assembly (see scene.h), organised as:
//   buildGrid()      cells, LOS cull, movables in id order, vis_contents, client.images
//   buildApp()       appearance build (+ inheritOverlayIcon/Color, applyOverlayDir, stateFor)
//   placeApp()       viewport-pixel placement, plane/layer resolution
//   placeHud()       client.screen objects (tile-clipped HUD passes, or screen-space layers when a plane master exists)
//   run()            plane grouping and the layer sort (the sort lives HERE)
#include "render/scene.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <functional>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include "model/gomath.h"
#include "model/json.h"
#include "model/statpanel.h"
#include "net/resource.h"
#include "render/ddmi.h"
#include "render/easing.h"
#include "render/maptextpaint.h"
#include "render/screenloc.h"
#include "render/xform.h"

namespace fugue::render {

using model::JVal;

namespace {

int g_sceneMutation = 0;

constexpr uint32_t kRegistryA = 0x80000000u;
constexpr uint32_t kNoIcon = 0xFFFF;
constexpr int kDefaultViewRange = 7;
constexpr int kMinAbsolutePlane = -16384;
constexpr int kFloatPlane = -32767;        // FLOAT_PLANE
constexpr uint32_t kSightMobs = 1u << 2, kSightObjs = 1u << 3, kSightTurfs = 1u << 4;
constexpr int kDirEast = 4, kDirWest = 8;

// Undo DD's int16 truncation of a float-relative plane.
double truePlane(int wire) {
    if (wire > -kMinAbsolutePlane - 1) return double(int32_t(wire) - 65536);
    return double(wire);
}
double resolvePlane(double plane, double base) {
    if (plane < kMinAbsolutePlane) return base + (plane - kFloatPlane);
    return plane;
}

// The subset of an appearance the renderer reads, parsed from the model's canonical JSON.
struct Ap {
    uint32_t iconResID = kNoIcon;
    std::string iconState;
    int dir = 0;
    uint32_t flags = 0;
    double layer = 0;
    bool hasPlane = false;
    int plane = 0;
    bool hasColor = false;
    int cr = 0, cg = 0, cb = 0, ca = 0;
    int pixelX = 0, pixelY = 0, pixelW = 0, pixelZ = 0;
    std::string screenLoc, renderSource, maptext, name;
    int mtW = 0, mtH = 0, mtX = 0, mtY = 0;   // maptext_width / _height / _x / _y
    std::vector<uint32_t> overlays, underlays;
    bool hasTransform = false;
    std::array<float, 6> transform{1, 0, 0, 0, 1, 0};   // the six float32 cells, DM matrix(a,b,c,d,e,f) order
    bool hasPixelOffsets = false;
    int invisibility = 0;
    bool hasBlend = false;
    int blend = 0;
    bool hasColorMatrix = false;
    ColorMatrixPtr cmat;               // 20 float32 -> float64
    std::string renderTarget;          // the SECOND 0x1000 cstr (RenderTarget)
    struct Flt { int type = 0; uint32_t mask = 0; Bytes params; };
    std::vector<Flt> filters;          // the 0x0800 chain, params raw in ascending param-bit order

    // MouseOpacity: a 2-bit field at bits 12-13, rotated so the default (1) encodes as 0.
    int mouseOpacity() const { return int(((flags >> 12) + 1) & 3); }
    bool opaque() const { return flags & 1; }                       // Opaque
    bool dirValid() const { return flags & 0x200; }                 // DirValid (ApprDirValid)
    bool resetColor() const { return (flags >> 21) & 2; }           // DM appearance flag: reset colour
    bool resetAlpha() const { return (flags >> 21) & 4; }           // DM appearance flag: reset alpha
    bool planeMaster() const { return ((flags >> 21) & 128) != 0; }
    // image.override: bit 0x40000 of the low (client-internal) flags word, set on images with override = 1.
    bool overrides() const { return (flags & 0x40000) != 0; }
};

// An appearance as the assembler works with it.
struct RApp {
    uint32_t appid = 0;
    const Icon* icon = nullptr;
    std::string state;
    int dir = 0;
    bool dirValid = false;   // dir is its OWN, not the parent's (applyOverlayDir)
    bool underlay = false;   // placed BEFORE its parent's own icon (placeApp, flatten)
    int frame = 0;
    bool hasTint = false;
    uint8_t tr = 255, tg = 255, tb = 255;
    int alpha = 255;
    double layer = 0, plane = 0;
    int px = 0, py = 0;
    bool hasXform = false;
    std::array<float, 6> xf{1, 0, 0, 0, 1, 0};
    std::vector<RApp> overlays;
    uint8_t blend = 0;                                   // a BlendMode
    ColorMatrixPtr cmat;
    std::string relay;                                   // render_source: a relay draws a slate, not an icon
    std::string maptext;                                 // drawn by placeApp in a box of mtW x mtH at offset mtX, mtY
    int mtW = 0, mtH = 0, mtX = 0, mtY = 0;
    int hitAtom = -1;   // HitScene::atoms index of the TOP-LEVEL atom this appearance belongs to (hit collection only)
    bool borrowsHit = false;   // a client.image: hitAtom is its PARENT's, whose position/facing it must not overwrite
};

using Tile = std::vector<RApp>;

struct Placed {
    DrawLayer layer;
    double plane = 0;
    double sortLayer = 0;
    int seq = 0;
    int atom = -1;
    bool hitVisible = true;   // false for layers the hit sidecar never saw (blackness, group overlays)
    bool screen = false;   // client.screen layers draw above every map layer of their plane
    HitFrame frame;           // the owning atom's icon frame (hittest.h), for icon-x / vis-x
    bool own = false;         // the atom's own icon, not an overlay/underlay (HitLayer::own)
    bool maptext = false;     // a maptext box (HitLayer::maptext)
};

// A collected PLANE_MASTER: its plane, how it composites, and the overlays that join its group.
struct MasterRec {
    double plane = 0;
    std::shared_ptr<PlaneMasterSpec> spec;
    std::vector<RApp> overlays;
    // The master's mouse_opacity == 0: everything drawn on its plane is mouse-transparent, whatever each atom says.
    // Stock DreamSeeker 516 gives the click to whatever lies under an overlay that sits on such a plane: the click goes to
    // the turf where only that overlay paints. With the master at 1, or no master, the overlay answers for its atom.
    bool hideFromMouse = false;
};

// The colour/blend half of a DrawLayer, from the appearance it draws.
void setFx(DrawLayer& l, const RApp& a) {
    l.hasTint = a.hasTint; l.tintR = a.tr; l.tintG = a.tg; l.tintB = a.tb;
    l.alpha = a.alpha;
    l.blend = a.blend;
    l.cmat = a.cmat;
}

class Assembler {
public:
    Assembler(const model::Session& s, ResStore& res, int64_t clockNs, SceneStats& st, bool withHit = false, std::string_view mapStyle = {})
        : s_(s), w_(s.world()), res_(res), clockNs_(clockNs), st_(st), mapStyle_(mapStyle) {
        if (withHit) hit_ = std::make_shared<HitScene>();
    }
    std::shared_ptr<HitScene> takeHit() { return std::move(hit_); }

    bool run(DrawList& out, std::string* err);
    // A SECONDARY map control (assembleSecondaryMap): only the client.screen objects whose screen_loc names `mapName`.
    bool runSecondary(std::string_view mapName, DrawList& out, std::string* err);

private:
    std::string mapName_;             // the map control being drawn: "" = the default map
    const model::Session& s_;
    const model::World& w_;
    ResStore& res_;
    int64_t clockNs_;
    int scrollX_ = 0, scrollY_ = 0;   // eye scroll offset at clockNs_, set by buildGrid
    SceneStats& st_;
    std::string mapStyle_;            // the default map control's `style`: maptext's sheet (render/maptextpaint.h)
    std::string err_;
    bool failed_ = false;
    std::shared_ptr<HitScene> hit_;   // non-null => collect the hit-test sidecar
    int curAtom_ = -1;                // placeApp: the top-level atom being placed
    HitFrame curFrame_;               // placeApp: that atom's icon frame, shared by all its layers
    int newHitAtom(uint32_t appid, uint8_t kind, uint32_t ref, bool screen);

    std::unordered_map<uint32_t, std::unique_ptr<Ap>> apCache_;
    struct SpriteKey {
        const Icon* ic; int state, slot, frame;
        bool operator<(const SpriteKey& o) const { return std::tie(ic, state, slot, frame) < std::tie(o.ic, o.state, o.slot, o.frame); }
    };
    std::map<SpriteKey, SpritePtr> spriteCache_;

    // grid
    int maxX_ = 0, maxY_ = 0;
    // Only the cell window (gridX0_/gridY0_ = its bottom-left world coords, gridW_ x gridH_) is stored; tiles outside it
    // are not allocated.
    int gridX0_ = 0, gridY0_ = 0, gridW_ = 0, gridH_ = 0;
    std::vector<Tile> grid_;
    const Tile* tileIn(int x, int y) const {
        if (x < 1 || y < 1 || x > maxX_ || y > maxY_) return nullptr;
        int lx = x - gridX0_, ly = y - gridY0_;
        if (lx < 0 || ly < 0 || lx >= gridW_ || ly >= gridH_) return nullptr;
        return &grid_[size_t(ly) * size_t(gridW_) + size_t(lx)];
    }
    // Hidden cells: the viewer cannot see them (no line of sight), but BYOND paints its own opaque blackness
    // over them on plane 0. Non-empty only when a visibility pass ran (haveVisible); see appendBlackness.
    std::set<std::pair<int, int>> hidden_;
    std::vector<MasterRec> masters_;

    void fail(const std::string& e) { if (!failed_) { failed_ = true; err_ = e; } }

    const Ap* ap(uint32_t appid);
    const Icon* iconFor(const Ap& a);
    std::string stateFor(const Ap& a, const Icon* ic) const;
    bool hiddenFromClient(uint32_t appid);
    bool buildApp(uint32_t appid, std::set<uint32_t>& visited, RApp& out);
    void inheritOverlayColor(RApp& ov, bool pHas, uint8_t pr, uint8_t pg, uint8_t pb, int pAlpha);
    void inheritOverlayIcon(RApp& ov, const Icon* parent);
    static void applyOverlayDir(RApp& ov, int dir);
    int frameFor(const Icon* ic, const std::string& state) const;
    void applyIconAnimation(RApp& a) const;
    void countUnmodeled(const Ap& a);

    SpritePtr sprite(const RApp& a, bool& drawable);
    bool drawableState(const RApp& a, int& stateIdx);

    void visChildren(const std::vector<uint32_t>& children, std::set<uint32_t>& visited, std::vector<RApp>& out, const RApp* holder);

    // client.images (per-movable images, turf images, image vis children), read once per frame.
    struct ImgRec { uint32_t id = 0, appid = 0, locRef = 0, cell = 0; uint8_t locKind = 0; std::vector<uint32_t> visKids; };
    std::vector<ImgRec> imgs_;
    std::unordered_map<uint32_t, std::vector<size_t>> imgsOn_;   // movable key -> imgs_ indices, ascending image id
    void collectImages();
    bool imageApp(const ImgRec& im, RApp& out);
    bool imagesOn(uint32_t key, const RApp& parent, std::vector<RApp>& out);
    void visContents(uint32_t id, std::set<uint32_t>& visited, std::vector<RApp>& out, const RApp* holder);

    // world
    bool buildGrid(int& originX, int& originY, int& winW, int& winH, int& extentW, int& extentH, int& vx0, int& vy0);
    void tileSize(int& tw, int& th) const;
    void placeApp(const RApp& a, int baseX, int baseY, int tileH, double parentPlane, double parentLayer, bool hasParent, std::vector<Placed>& out,
                  const std::array<float, 6>* inheritedXf = nullptr);
    bool animatedApp(uint32_t id, const model::Atom& m, RApp& out);
    void flickOverride(uint32_t id, RApp& out);
    bool tweenChain(const model::Atom::Chain& ch, uint32_t settled, RApp& out);
    void glideOffset(const model::Atom& m, int tileW, int tileH, int& px, int& py) const;
    bool animEndpointCarriesPixels(const model::Atom& m) const;
    void placeHud(int cols, int rows, int tw, int th, DrawList& dl, bool slateMode, std::vector<Placed>& placed);
    void collectMasters(uint32_t appid, std::set<uint32_t>& seen);
    void appendBlackness(std::vector<Placed>& placed, int originX, int originY, int extentW, int extentH, int tw, int th);
    bool compositePlanes(std::vector<Placed>& placed, DrawList& dl, int th);
    bool mastersFor(uint32_t appid);
    struct FlatLayer { DrawLayer l; double plane, layer; };
    void flatten(const RApp& a, int w, int h, std::vector<FlatLayer>& out);
public:
    bool appearanceIcon(uint32_t appid, int dir, int& w, int& h, std::vector<uint8_t>& rgba);
private:
    void placeWarpedScreenObj(const RApp& ra, double anchorX, double anchorBottomY, int canvasW, int canvasH, std::vector<Placed>& placed);
};

// ---- appearances -----------------------------------------------------------------------------

const Ap* Assembler::ap(uint32_t appid) {
    auto c = apCache_.find(appid);
    if (c != apCache_.end()) return c->second.get();
    auto it = w_.appearances.find(appid);
    if (it == w_.appearances.end()) { apCache_[appid] = nullptr; return nullptr; }
    JVal v;
    try { v = model::parseJson(it->second.fieldsJson); } catch (const std::exception&) { apCache_[appid] = nullptr; return nullptr; }
    auto a = std::make_unique<Ap>();
    a->iconResID = v["iconResID"].u32();
    a->iconState = v["iconState"].bytesOrStr();
    a->dir = int(v["dir"].i());
    a->flags = v["appearanceFlags"].u32();
    uint32_t lb = v["layer"].f32bits();
    float lf;
    std::memcpy(&lf, &lb, 4);
    a->layer = double(lf);
    a->hasPlane = v["hasPlane"].b;
    a->plane = int(v["plane"].i());
    a->hasColor = v["hasColor"].b;
    a->cr = int(v["colorR"].i()); a->cg = int(v["colorG"].i()); a->cb = int(v["colorB"].i()); a->ca = int(v["colorA"].i());
    a->pixelX = int(v["pixelX"].i()); a->pixelY = int(v["pixelY"].i());
    a->pixelW = int(v["pixelW"].i()); a->pixelZ = int(v["pixelZ"].i());
    a->screenLoc = v["screenLoc"].bytesOrStr();
    a->renderSource = v["renderSource"].bytesOrStr();
    a->maptext = v["maptext"].bytesOrStr();
    a->mtW = int(v["maptextW"].i()); a->mtH = int(v["maptextH"].i());
    a->mtX = int(v["maptextX"].i()); a->mtY = int(v["maptextY"].i());
    // Stripped of BYOND's embedded text-macro markers here, once, so every reader of HitAtom::name (status bar, verb menu,
    // local menu) gets the plain name. A name such as "\improper Scrubbers pipe" arrives with a marker prefix from stock
    // DreamDaemon, and stock DreamSeeker shows the plain name in its status bar.
    a->name = model::plainAtomName(v["name"].bytesOrStr());
    for (const JVal& o : v["overlays"].a) a->overlays.push_back(o.u32());
    for (const JVal& o : v["underlays"].a) a->underlays.push_back(o.u32());
    a->hasTransform = v["hasTransform"].b;
    a->hasPixelOffsets = v["hasPixelOffsets"].b;
    {
        const auto& tj = v["transform"].a;
        for (size_t k = 0; k < 6 && k < tj.size(); k++) {
            uint32_t b = tj[k].f32bits();
            std::memcpy(&a->transform[k], &b, 4);
        }
    }
    a->renderTarget = v["renderTarget"].bytesOrStr();
    for (const JVal& f : v["filters"].a) {
        Ap::Flt ft;
        ft.type = int(f["type"].i());
        ft.mask = f["paramMask"].u32();
        if (auto d = hexDecode(f["params"].bytesOrStr())) ft.params = *d;
        a->filters.push_back(std::move(ft));
    }
    for (const JVal& var : v["vars"].a) {
        int bit = int(var["bit"].i());
        auto data = hexDecode(var["data"].bytesOrStr());
        if (bit == 0x0004 && data && !data->empty()) a->invisibility = (*data)[0];   // VarInvisibility
        if (bit == 0x0100 && data && data->size() == 1) { a->hasBlend = true; a->blend = (*data)[0]; }   // VarBlendMode
        if (bit == 0x0400 && data && data->size() == 80) {   // VarColorMatrix
            a->hasColorMatrix = true;
            auto cm = std::make_shared<ColorMatrix>();
            for (size_t k = 0; k < 20; k++) {
                uint32_t b = uint32_t((*data)[k * 4]) | uint32_t((*data)[k * 4 + 1]) << 8 | uint32_t((*data)[k * 4 + 2]) << 16 | uint32_t((*data)[k * 4 + 3]) << 24;
                float f;
                std::memcpy(&f, &b, 4);
                (*cm)[k] = double(f);
            }
            a->cmat = std::move(cm);
        }
    }
    const Ap* p = a.get();
    apCache_[appid] = std::move(a);
    return p;
}

const Icon* Assembler::iconFor(const Ap& a) {
    if (a.iconResID == kNoIcon) return nullptr;
    if (a.iconResID >= w_.resources.size()) return nullptr;
    return res_.iconResolved(w_.resources[a.iconResID].first, w_.resources);   // byRes: plain icons AND evaluated DDMI recipes
}

// stateFor: the appearance's own icon_state wins whenever the sheet has it (including ""); a miss
// degrades to the unnamed default state.
std::string Assembler::stateFor(const Ap& a, const Icon* ic) const {
    if (!ic) return "";
    if (ic->stateIndex(a.iconState) >= 0) return a.iconState;
    return "";
}

bool Assembler::hiddenFromClient(uint32_t appid) {
    if (!w_.haveSeeInvisible) return false;
    const Ap* a = ap(appid);
    if (!a) return false;
    return a->invisibility > int(w_.seeInvisible);
}

void Assembler::countUnmodeled(const Ap& a) {
    if (!a.maptext.empty()) st_.maptext++;
    // render_source relays, colour matrices and blend modes are modelled (placeApp, executor.cpp). The atom.transform on the
    // map path is modelled too (xform.cpp); the HUD path draws it differently (placeWarpedScreenObj) and counts it in
    // placeHud.
}

int Assembler::newHitAtom(uint32_t appid, uint8_t kind, uint32_t ref, bool screen) {
    if (!hit_) return -1;
    HitAtom h;
    if (const Ap* a = ap(appid)) { h.name = a->name; h.mouseOpacity = a->mouseOpacity(); }
    h.appid = appid;
    h.kind = kind; h.ref = ref; h.screen = screen;
    hit_->atoms.push_back(std::move(h));
    return int(hit_->atoms.size()) - 1;
}

bool Assembler::buildApp(uint32_t appid, std::set<uint32_t>& visited, RApp& out) {
    if (visited.count(appid)) return false;
    const Ap* a = ap(appid);
    if (!a) return false;
    visited.insert(appid);

    const Icon* ic = iconFor(*a);
    RApp ra;
    ra.appid = appid;
    ra.icon = ic;
    ra.state = stateFor(*a, ic);
    ra.dir = a->dir;
    ra.dirValid = a->dirValid();
    ra.alpha = 255;
    ra.layer = a->layer;
    ra.px = a->pixelX + a->pixelW;
    ra.py = a->pixelY + a->pixelZ;
    if (a->hasPlane) ra.plane = truePlane(a->plane);
    if (a->hasColor) {
        ra.hasTint = true;
        ra.tr = uint8_t(a->cr); ra.tg = uint8_t(a->cg); ra.tb = uint8_t(a->cb);
        ra.alpha = a->ca;
    }
    if (a->hasTransform) { ra.hasXform = true; ra.xf = a->transform; }
    // atom.color's LIST form and atom.blend_mode ride on the appearance (NOT inherited by overlays; only the scalar
    // colour is, inheritOverlayColor).
    if (a->cmat) ra.cmat = a->cmat;
    if (a->hasBlend) ra.blend = blendModeOfWire(a->blend);
    ra.relay = a->renderSource;
    ra.maptext = a->maptext;
    ra.mtW = a->mtW; ra.mtH = a->mtH; ra.mtX = a->mtX; ra.mtY = a->mtY;
    countUnmodeled(*a);

    for (uint32_t uid : a->underlays) {
        RApp ov;
        if (buildApp(uid, visited, ov)) {
            inheritOverlayIcon(ov, ic);
            applyOverlayDir(ov, ra.dir);   // underlays follow the parent's dir exactly like overlays
            if (a->hasColor) inheritOverlayColor(ov, ra.hasTint, ra.tr, ra.tg, ra.tb, ra.alpha);
            ov.underlay = true;
            ra.overlays.push_back(std::move(ov));
        }
    }
    for (uint32_t oid : a->overlays) {
        RApp ov;
        if (buildApp(oid, visited, ov)) {
            inheritOverlayIcon(ov, ic);
            applyOverlayDir(ov, ra.dir);
            if (a->hasColor) inheritOverlayColor(ov, ra.hasTint, ra.tr, ra.tg, ra.tb, ra.alpha);
            ra.overlays.push_back(std::move(ov));
        }
    }
    visited.erase(appid);
    out = std::move(ra);
    return true;
}

// The parent's colour and alpha MULTIPLY into the overlay's own (white / 255 when it has none), over the overlay's whole
// subtree, and RESET_COLOR / RESET_ALPHA cut the chain at that node. Rounding is (x*y + 127) / 255.
void Assembler::inheritOverlayColor(RApp& ov, bool pHas, uint8_t pr, uint8_t pg, uint8_t pb, int pAlpha) {
    bool resetColor = false, resetAlpha = false;
    if (const Ap* ca = ap(ov.appid)) { resetColor = ca->resetColor(); resetAlpha = ca->resetAlpha(); }
    if (resetColor && resetAlpha) return;
    auto mul = [](uint8_t x, uint8_t y) { return uint8_t((int(x) * int(y) + 127) / 255); };
    if (!resetColor && pHas) {
        if (!ov.hasTint) { ov.hasTint = true; ov.tr = pr; ov.tg = pg; ov.tb = pb; }
        else { ov.tr = mul(ov.tr, pr); ov.tg = mul(ov.tg, pg); ov.tb = mul(ov.tb, pb); }
    }
    if (!resetAlpha) ov.alpha = (ov.alpha * pAlpha + 127) / 255;
    for (auto& o : ov.overlays) inheritOverlayColor(o, pHas, pr, pg, pb, pAlpha);
}

void Assembler::inheritOverlayIcon(RApp& ov, const Icon* parent) {
    if (!parent) return;
    if (!ov.icon) {
        const Ap* ca = ap(ov.appid);
        if (ca && ca->iconResID == kNoIcon) {
            ov.icon = parent;
            ov.state = stateFor(*ca, parent);
        }
    }
    const Icon* inner = ov.icon ? ov.icon : parent;
    for (auto& o : ov.overlays) inheritOverlayIcon(o, inner);
}

// applyOverlayDir: an overlay/underlay without ApprDirValid (0x200) faces its parent's dir. One with it keeps its own,
// SOUTH included (a pipe manifold's arm sets dir = SOUTH explicitly).
void Assembler::applyOverlayDir(RApp& ov, int dir) {
    if (!ov.dirValid) ov.dir = dir;
    for (auto& o : ov.overlays) applyOverlayDir(o, dir);
}

// Icon-state frame selection (deciseconds since the Unix epoch, pinned clock).
int Assembler::frameFor(const Icon* ic, const std::string& state) const {
    if (!ic) return 0;
    int si = ic->stateIndex(state);
    if (si < 0) return 0;
    return frameAtTime(ic->states[size_t(si)], double(clockNs_) / 1e8);
}

// Icon animation, applied to movables, HUD atoms, turf cells and every screen object.
void Assembler::applyIconAnimation(RApp& a) const {
    a.frame = frameFor(a.icon, a.state);
    for (auto& o : a.overlays) applyIconAnimation(o);
}

// No icon draws nothing; a missing NAMED state is an error, a missing unnamed one draws nothing.
bool Assembler::drawableState(const RApp& a, int& stateIdx) {
    stateIdx = -1;
    if (!a.icon) return false;
    int si = a.icon->stateIndex(a.state);
    if (si < 0) {
        if (!a.state.empty()) fail("render: icon has no state \"" + a.state + "\"");
        return false;
    }
    stateIdx = si;
    return true;
}

// The frame pixels of one appearance (state, dir, frame). nullptr + fail() on error.
SpritePtr Assembler::sprite(const RApp& a, bool& drawable) {
    drawable = false;
    int si;
    if (!drawableState(a, si)) return nullptr;
    const IconState& st = a.icon->states[size_t(si)];
    int dir = a.dir;
    if (g_sceneMutation == 3) { if (dir == kDirEast) dir = kDirWest; else if (dir == kDirWest) dir = kDirEast; }
    int slot = dirIndex(int(st.dirs), dir);
    if (slot < 0) { fail("dmi: unsupported dir count " + std::to_string(st.dirs) + " (want 1, 4 or 8)"); return nullptr; }
    SpriteKey key{a.icon, si, slot, a.frame};
    auto c = spriteCache_.find(key);
    if (c != spriteCache_.end()) { drawable = true; return c->second; }
    auto sp = std::make_shared<Sprite>();
    std::string e;
    if (!a.icon->frame(st, slot, a.frame, sp->px, &e)) { fail("dmi: " + e); return nullptr; }
    sp->w = a.icon->width;
    sp->h = a.icon->height;
    spriteCache_[key] = sp;
    drawable = true;
    return sp;
}

// ---- vis_contents ------------------------------------------------------------------------------

// The holder's atom.transform reaches its vis_contents. Stock DreamSeeker 516 draws a child of a scale(1.5)
// holder 1.5x about its own centre, with its pixel_x offset unscaled (it spans -8..+40 px of the holder's centre, not
// 0..48), and a click on it names the child with vis-x/vis-y and icon-x/y through that transform in the child's own frame.
// So the child is placed as if the transform were its own. A child with a transform of its own gets holder x child (child
// first). A test-only hook makes a child keep only its own transform.
static std::array<float, 6> composeDm(const std::array<float, 6>& h, const std::array<float, 6>& c) {
    // DM matrix cells a,b,c,d,e,f: x' = a x + b y + c, y' = d x + e y + f. Apply c first, then h.
    return {h[0] * c[0] + h[1] * c[3], h[0] * c[1] + h[1] * c[4], h[0] * c[2] + h[1] * c[5] + h[2],
            h[3] * c[0] + h[4] * c[3], h[3] * c[1] + h[4] * c[4], h[3] * c[2] + h[4] * c[5] + h[5]};
}

void Assembler::visChildren(const std::vector<uint32_t>& children, std::set<uint32_t>& visited, std::vector<RApp>& out, const RApp* holder) {
    for (uint32_t child : children) {
        auto cs = w_.atoms.find(child);
        if (cs == w_.atoms.end()) continue;
        if (hiddenFromClient(cs->second.appid)) continue;
        std::set<uint32_t> v2;
        RApp ca;
        if (!buildApp(cs->second.appid, v2, ca)) continue;
        if (cs->second.dir != 0) ca.dir = cs->second.dir;
        if (hit_) ca.hitAtom = newHitAtom(cs->second.appid, (child & kRegistryA) ? kAtomMob : kAtomObj, child & ~kRegistryA, false);
        ca.px += cs->second.pixelX;
        ca.py += cs->second.pixelY;
        if (holder && holder->hasXform && g_sceneMutation != 21) {
            ca.xf = ca.hasXform ? composeDm(holder->xf, ca.xf) : holder->xf;
            ca.hasXform = true;
        }
        out.push_back(std::move(ca));
        const RApp held = out.back();   // a grandchild takes this child's (now inherited) transform
        visContents(child, visited, out, &held);
    }
}

void Assembler::visContents(uint32_t id, std::set<uint32_t>& visited, std::vector<RApp>& out, const RApp* holder) {
    if (visited.count(id)) return;
    visited.insert(id);
    auto it = w_.visContents.find(id);
    if (it != w_.visContents.end()) visChildren(it->second, visited, out, holder);
    visited.erase(id);
}

// ---- client.images -------------------------------------------------------------------------------
// Stock DreamDaemon sends a speech bubble and a runechat as image frames (0x0033 with an image id, appid and a
// mob location, then 0x0035 and 0x00f0). Stock DreamSeeker 516 draws the bubble over the speaker and the runechat maptext
// above it.

// The decoder's image records: kind 0x01 = obj slot, 0x02 = mob slot (the RegistryA space), 0x07 = a
// turf's flat cell; visContents Type 2 cells name obj slots (any other Type is skipped).
void Assembler::collectImages() {
    imgs_.clear();
    imgsOn_.clear();
    for (const auto& [id, js] : w_.images) {   // ascending id: the stable draw order across runs
        JVal v;
        try { v = model::parseJson(js); } catch (const std::exception&) { continue; }
        ImgRec im;
        im.id = id;
        im.appid = v["appid"].u32();
        im.locKind = uint8_t(v["locKind"].u32());
        im.locRef = v["locRef"].u32();
        im.cell = v["cell"].u32();
        for (const JVal& c : v["visContents"].a)
            if (c["type"].u32() == 2) im.visKids.push_back(c["value"].u32());
        // An obj slot keys bare, a mob slot with kRegistryA (separate id spaces: the KIND matters)
        if (im.locKind == 0x01) imgsOn_[im.locRef].push_back(imgs_.size());
        else if (im.locKind == 0x02) imgsOn_[kRegistryA | im.locRef].push_back(imgs_.size());
        imgs_.push_back(std::move(im));
    }
}

// The image's appearance at the render clock, tweening its animate() chain (a runechat fades in from
// alpha 0) exactly as a movable's is. Its icon state animates like a movable's, as DreamSeeker animates other map atoms.
bool Assembler::imageApp(const ImgRec& im, RApp& out) {
    auto ch = w_.imageChains.find(im.id);
    bool ok;
    if (ch == w_.imageChains.end() || ch->second.steps.empty()) {
        std::set<uint32_t> v;
        ok = buildApp(im.appid, v, out);
    } else {
        ok = tweenChain(ch->second, im.appid, out);
    }
    if (ok) applyIconAnimation(out);
    return ok;
}

// The images attached to one movable (its key: an obj slot bare, a mob slot with kRegistryA;
// the loc kind matters, as the two registries are separate id spaces). An image is not an atom for the mouse: a hit on it
// names its parent (the DM Reference on image mouse_opacity). Its vis_contents draw whether or not the holder itself
// resolved (a radial menu is a blank holder whose buttons are all children).
// For the mouse, an image's pixels are its parent's in every respect but shape (stock DreamSeeker 516):
//   * a click on an image's icon names the parent, with icon-x/icon-y in the image's own frame, at the parent's location;
//   * the image's own mouse_opacity and alpha are ignored: the parent's mouse_opacity governs (a parent at 0 takes nothing,
//     image pixels included);
//   * an image's maptext takes no click at all (hittest.h "maptext").
// An image's FLOAT_PLANE and FLOAT_LAYER (an /image's defaults) are its parent's, as an overlay's are: a default image() is
// drawn over the parent's floor turf, and one at layer 1 is hidden beneath that floor. Resolving them against plane 0 would
// hide every such image under the turfs.
// Returns true when one of the drawn images is an OVERRIDE (Ap::overrides): the caller then draws the parent as nothing.
bool Assembler::imagesOn(uint32_t key, const RApp& parent, std::vector<RApp>& out) {
    auto it = imgsOn_.find(key);
    if (it == imgsOn_.end()) return false;
    bool overridden = false;
    for (size_t idx : it->second) {
        const ImgRec& im = imgs_[idx];
        if (hiddenFromClient(im.appid)) continue;
        RApp ra;
        if (imageApp(im, ra)) {
            ra.hitAtom = parent.hitAtom;
            ra.borrowsHit = true;
            ra.plane = resolvePlane(ra.plane, resolvePlane(parent.plane, 0));
            if (ra.layer < 0) ra.layer = parent.layer;
            out.push_back(std::move(ra));
            const Ap* a = ap(im.appid);
            if (a && a->overrides() && g_sceneMutation != 24) overridden = true;
        }
        std::set<uint32_t> visited;
        visChildren(im.visKids, visited, out, nullptr);
    }
    return overridden;
}

// Animation sampling: the animate() chains and the glide offset. The chain and glide start instants are stamped by
// the model at ingest; here they are only
// sampled at clockNs_. The policy: a hard switch of discrete fields at the raw midpoint, numeric fields eased, rigid
// matrix interpolation, the overlay stack not animated.

namespace {

double lerpF(double a, double b, double t) { return a + (b - a) * t; }
int lerpInt(int a, int b, double t) { return int(gomath::round(lerpF(double(a), double(b), t))); }

float bitsToFloat(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }

int64_t stepDurNs(const model::Atom::AnimStep& st, int64_t tickNs) { return int64_t(double(bitsToFloat(st.timeBits)) * double(tickNs)); }

enum class ChainState { NotStarted, InFlight, Done };
struct ChainPos { ChainState state = ChainState::Done; size_t step = 0; int64_t elapsed = 0, dur = 0; };

// Which step of the chain is in flight at `now`.
ChainPos chainPos(const model::Atom::Chain& c, int64_t now) {
    ChainPos r;
    int64_t rem = now - c.startNs;
    if (rem < 0) { r.state = ChainState::NotStarted; return r; }
    for (size_t i = 0; i < c.steps.size(); i++) {
        int64_t d = stepDurNs(c.steps[i], c.tickNs);
        if (d <= 0) continue;   // instantaneous step: settle past it
        if (rem < d) { r.state = ChainState::InFlight; r.step = i; r.elapsed = rem; r.dur = d; return r; }
        rem -= d;
    }
    return r;   // fully played out
}

// The appearance to show when no step is in flight: the first step's From before the chain starts, else the settled one.
uint32_t chainAppidAt(const model::Atom::Chain& c, uint32_t settled, int64_t now) {
    int64_t elapsed = now - c.startNs;
    if (elapsed < 0) return c.steps[0].from;
    for (const auto& s : c.steps) {
        int64_t d = stepDurNs(s, c.tickNs);
        if (d <= 0) continue;
        if (elapsed < d) return elapsed < d / 2 ? s.from : s.to;
        elapsed -= d;
    }
    return settled;
}

}  // namespace

bool Assembler::tweenChain(const model::Atom::Chain& ch, uint32_t settled, RApp& out) {
    ChainPos pos = chainPos(ch, clockNs_);
    if (pos.state != ChainState::InFlight) {
        std::set<uint32_t> v;
        return buildApp(chainAppidAt(ch, settled, clockNs_), v, out);
    }
    const model::Atom::AnimStep& step = ch.steps[pos.step];
    RApp from, to;
    std::set<uint32_t> vf, vt;
    bool fromOK = buildApp(step.from, vf, from);
    bool toOK = buildApp(step.to, vt, to);
    if (!fromOK) { if (toOK) out = std::move(to); return toOK; }
    if (!toOK) { out = std::move(from); return true; }
    double raw = double(pos.elapsed) / double(pos.dur);
    double frac = ease(raw, step.easing);

    RApp result = raw >= 0.5 ? to : from;   // discrete fields switch at the RAW midpoint
    result.px = lerpInt(from.px, to.px, frac);
    result.py = lerpInt(from.py, to.py, frac);
    result.alpha = lerpInt(from.alpha, to.alpha, frac);
    // Two flat tints lerp per channel. Any other shape mismatch switches at the eased fraction (t >= 0.5 goes to `to`).
    if (from.hasTint && to.hasTint) {
        result.hasTint = true;
        result.tr = uint8_t(lerpInt(from.tr, to.tr, frac));
        result.tg = uint8_t(lerpInt(from.tg, to.tg, frac));
        result.tb = uint8_t(lerpInt(from.tb, to.tb, frac));
    } else {
        const RApp& side = frac >= 0.5 ? to : from;
        result.hasTint = side.hasTint; result.tr = side.tr; result.tg = side.tg; result.tb = side.tb;
    }
    // A colour matrix and a blend mode are not interpolated; they follow the discrete switch at the midpoint.
    // transform: a side without one tweens from/to IDENTITY; rigid interpolation via lerpMatrix.
    if (from.hasXform || to.hasXform) {
        Mat fm, tm;
        if (from.hasXform) fm = Mat{double(from.xf[0]), double(from.xf[1]), double(from.xf[2]), double(from.xf[3]), double(from.xf[4]), double(from.xf[5])};
        if (to.hasXform) tm = Mat{double(to.xf[0]), double(to.xf[1]), double(to.xf[2]), double(to.xf[3]), double(to.xf[4]), double(to.xf[5])};
        Mat m;
        if (lerpMatrix(fm, tm, frac, m)) {
            result.xf = {float(m.a), float(m.b), float(m.c), float(m.d), float(m.e), float(m.f)};
            result.hasXform = true;
        }
    }
    // The overlay/underlay stack is not animated: the LIVE appearance's overlays draw for the whole tween.
    if (settled != step.from && settled != step.to) {
        std::set<uint32_t> vc;
        RApp cur;
        if (buildApp(settled, vc, cur)) result.overlays = std::move(cur.overlays);
    }
    out = std::move(result);
    return true;
}

// Tween + icon animation, then the flick() overlay.
bool Assembler::animatedApp(uint32_t id, const model::Atom& m, RApp& out) {
    bool ok;
    if (!m.animating || m.chain.steps.empty()) {
        std::set<uint32_t> v;
        ok = buildApp(m.appid, v, out);
    } else {
        ok = tweenChain(m.chain, m.appid, out);
    }
    if (ok) {
        applyIconAnimation(out);
        flickOverride(id, out);
    }
    return ok;
}

// While a flick() plays, the atom shows the flicked icon_state's own frame sequence once from the
// flick's arrival, then reverts to its real appearance (BYOND: "temporarily replaced ... for the duration of the animation").
// Only icon, state and frame change; position, colour, plane and overlays keep the live appearance. Without it a door jumps
// straight from closed to open, since its opening state never plays.
void Assembler::flickOverride(uint32_t id, RApp& out) {
    auto it = w_.flicks.find(id);
    if (it == w_.flicks.end()) return;
    const model::World::Flick& f = it->second;
    if (!f.haveRes || f.iconState.empty() || f.startNs == 0 || f.iconResIndex >= w_.resources.size()) return;
    const Icon* ic = res_.iconResolved(w_.resources[f.iconResIndex].first, w_.resources);
    if (!ic) return;
    int si = ic->stateIndex(f.iconState);
    if (si < 0) return;
    const IconState& st = ic->states[size_t(si)];
    std::vector<std::pair<int, double>> steps;   // frame, delay (ds); the same sequence frameFor plays, ONCE
    auto delay = [&](int64_t i) { return (i >= 0 && size_t(i) < st.delays.size()) ? st.delays[size_t(i)] : 1.0; };
    for (int64_t i = 0; i < std::max<int64_t>(1, st.frames); i++) steps.push_back({int(i), delay(i)});
    if (st.rewind && st.frames > 2)
        for (int64_t i = st.frames - 2; i >= 1; i--) steps.push_back({int(i), delay(i)});
    double total = 0;
    for (auto& s : steps) total += s.second;
    double elapsed = double(clockNs_ - f.startNs) / 1e8;
    if (total <= 0 || elapsed < 0 || elapsed >= total) return;   // not started (clock skew) or already played out
    double acc = 0;
    int frame = steps.back().first;
    for (auto& s : steps) {
        if (elapsed < acc + s.second) { frame = s.first; break; }
        acc += s.second;
    }
    out.icon = ic;
    out.state = f.iconState;
    out.frame = frame;
}

// Glide progress and offset: (from - to) * iconSize scaled by the remaining fraction, rounded; 0 once played out.
void Assembler::glideOffset(const model::Atom& m, int tileW, int tileH, int& px, int& py) const {
    px = py = 0;
    if (!m.gliding) return;
    const model::Atom::Glide& g = m.glide;
    double progress;
    if (g.durNs <= 0) progress = 1;
    else {
        int64_t el = clockNs_ - g.startNs;
        if (el <= 0) progress = 0;
        else if (el >= g.durNs) progress = 1;
        else progress = double(el) / double(g.durNs);
    }
    double rem = 1 - progress;
    if (rem <= 0) return;
    if (g_sceneMutation == 6) {   // test-only: truncate instead of round
        px = int(double((g.fromX - m.x) * tileW) * rem);
        py = int(double((g.fromY - m.y) * tileH) * rem);
        return;
    }
    px = int(gomath::round(double((g.fromX - m.x) * tileW) * rem));
    py = int(gomath::round(double((g.fromY - m.y) * tileH) * rem));
}

// A chain's From/To appearances carry pixel_x/y as ABSOLUTE values, while the atom's own pixel offsets repeat the settled
// ones, so those must not be added again.
bool Assembler::animEndpointCarriesPixels(const model::Atom& m) const {
    if (g_sceneMutation == 7) return false;   // test-only: add the A6 copy on top of the endpoint's
    if (!m.animating || m.chain.steps.empty()) return false;
    auto has = [&](uint32_t appid) {
        auto it = w_.appearances.find(appid);
        return it != w_.appearances.end() && it->second.hasPixelOffsets;
    };
    ChainPos pos = chainPos(m.chain, clockNs_);
    if (pos.state == ChainState::InFlight) {
        const auto& st = m.chain.steps[pos.step];
        return has(st.from) || has(st.to);
    }
    const auto& last = m.chain.steps.back();
    return m.appid == last.to && has(last.to);
}

// ---- grid --------------------------------------------------------------------------------------

static bool opaqueAppearance(const Ap* a) { return a && a->opaque(); }

// Outputs the view rect (origin/extent); vx0/vy0 are unused padding for symmetry.
bool Assembler::buildGrid(int& originX, int& originY, int& winW, int& winH, int& extentW, int& extentH, int& vx0, int& vy0) {
    (void)vx0; (void)vy0;
    // The cell grid comes from the model (model::Session::cells).
    model::CellGrid cg = s_.cells();
    if (cg.err) { fail("no bounds (0x3e) yet / no map grid yet"); return false; }
    winW = cg.w; winH = cg.h;
    originX = cg.originX; originY = cg.originY;
    const std::vector<uint32_t>& cells = cg.appids;
    if (size_t(winW) * size_t(winH) != cells.size()) { fail("cells section: w*h != len(appids)"); return false; }

    maxX_ = w_.bounds.maxX; maxY_ = w_.bounds.maxY;
    if (maxX_ <= 0 || maxY_ <= 0) { fail("render: empty grid"); return false; }
    gridX0_ = originX; gridY0_ = originY; gridW_ = winW; gridH_ = winH;
    grid_.assign(size_t(winW) * size_t(winH), Tile{});
    auto tileAt = [&](int x, int y) -> Tile* { return const_cast<Tile*>(tileIn(x, y)); };

    // Visible rect: the eye, or the map centre when there is none.
    int eyeX = w_.haveEye ? w_.eyeX : (maxX_ + 1) / 2;
    int eyeY = w_.haveEye ? w_.eyeY : (maxY_ + 1) / 2;
    if (w_.bounds.viewW > 0 && w_.bounds.viewH > 0) { extentW = w_.bounds.viewW; extentH = w_.bounds.viewH; }
    else if (w_.bounds.view > 0) { extentW = extentH = 2 * w_.bounds.view + 1; }
    else { extentW = extentH = 2 * kDefaultViewRange + 1; }
    if (maxX_ > 0 && extentW > maxX_) extentW = maxX_;
    if (maxY_ > 0 && extentH > maxY_) extentH = maxY_;
    int viewOriginX = eyeX - extentW / 2, viewOriginY = eyeY - extentH / 2;
    if (maxX_ > 0 && extentW >= maxX_) viewOriginX = 1;
    if (maxY_ > 0 && extentH >= maxY_) viewOriginY = 1;

    // sight: skipLOS when the mob sees turfs, mobs AND objs; turfs always visible when SEE_TURFS.
    const uint32_t all = kSightMobs | kSightObjs | kSightTurfs;
    bool skipLOS = w_.haveSight && (w_.sight & all) == all;
    bool turfsAlwaysVisible = skipLOS || (w_.haveSight && (w_.sight & kSightTurfs) != 0);

    // A frame inside an eye scroll keeps one extra LOS ring alive, the
    // tile the scroll partially reveals beyond the strict view edge.
    model::eyeScrollOffset(w_, clockNs_, scrollX_, scrollY_);
    const int losMargin = (scrollX_ != 0 || scrollY_ != 0) ? 1 : 0;
    bool haveVisible = false;
    CellSet visible;
    if (!skipLOS && w_.haveEye && winW > 0 && winH > 0) {
        int ex = eyeX, ey = eyeY;
        if (!(ex < originX || ex >= originX + winW || ey < originY || ey >= originY + winH)) {
            int rng = std::max(extentW / 2, extentH / 2) + losMargin;
            bool allKnown = true;
            for (int y = ey - rng; y <= ey + rng && allKnown; y++)
                for (int x = ex - rng; x <= ex + rng; x++) {
                    int lx = x - originX, ly = y - originY;
                    if (lx < 0 || ly < 0 || lx >= winW || ly >= winH) continue;
                    if (!w_.appearances.count(cells[size_t(ly) * size_t(winW) + size_t(lx)])) { allKnown = false; break; }
                }
            if (allKnown) {
                std::set<std::pair<int, int>> blockers;
                for (auto& [id, m] : w_.atoms)
                    if (opaqueAppearance(ap(m.appid))) blockers.insert({m.x, m.y});
                auto opaque = [&](int x, int y) {
                    int lx = x - originX, ly = y - originY;
                    if (lx < 0 || ly < 0 || lx >= winW || ly >= winH) return true;   // an edge is an occluder
                    if (blockers.count({x, y})) return true;
                    const Ap* a = ap(cells[size_t(ly) * size_t(winW) + size_t(lx)]);
                    return a ? a->opaque() : false;
                };
                visible = visibleCells(ex, ey, rng, opaque);
                haveVisible = true;
            }
        }
    }
    auto isVisible = [&](int x, int y) { return visible.count({x, y}) != 0; };

    for (int ly = 0; ly < winH; ly++)
        for (int lx = 0; lx < winW; lx++) {
            uint32_t appid = cells[size_t(ly) * size_t(winW) + size_t(lx)];
            if (hiddenFromClient(appid)) continue;
            std::set<uint32_t> visited;
            RApp ra;
            if (!buildApp(appid, visited, ra)) {
                // an unbuildable cell that is also out of sight is remembered as hidden (blackness covers it)
                if (haveVisible && !isVisible(originX + lx, originY + ly)) hidden_.insert({originX + lx, originY + ly});
                continue;
            }
            // A turf's multi-frame icon animates in stock DreamSeeker 516, including an animated GIF on a turf.
            if (g_sceneMutation != 22) applyIconAnimation(ra);
            ra.hitAtom = newHitAtom(appid, kAtomTurf, 0, false);
            if (Tile* t = tileAt(originX + lx, originY + ly)) *t = Tile{std::move(ra)};
        }
    if (haveVisible && !turfsAlwaysVisible) {
        for (int ly = 0; ly < winH; ly++)
            for (int lx = 0; lx < winW; lx++) {
                int x = originX + lx, y = originY + ly;
                if (isVisible(x, y)) continue;
                hidden_.insert({x, y});
                if (Tile* t = tileAt(x, y)) t->clear();
            }
    }

    // Movables in ascending id order. The glide offset uses the tile size as it stands here, before any movable is
    // appended (the most common icon size over the turf tiles), not the final size.
    int glideTW = 32, glideTH = 32;
    tileSize(glideTW, glideTH);
    collectImages();
    for (auto& [id, m] : w_.atoms) {
        if (!m.hasPos) continue;
        if (m.x < originX || m.x >= originX + winW || m.y < originY || m.y >= originY + winH) continue;
        if (haveVisible && !isVisible(m.x, m.y)) continue;
        if (hiddenFromClient(m.appid)) continue;
        RApp ra;
        if (!animatedApp(id, m, ra)) continue;   // no appearance flyweight for this atom (or its chain's From) yet
        ra.hitAtom = newHitAtom(m.appid, (id & kRegistryA) ? kAtomMob : kAtomObj, id & ~kRegistryA, false);
        ra.dir = m.dir;
        for (auto& o : ra.overlays) applyOverlayDir(o, m.dir);
        // gx,gy = glide offset + the atom's own A6 pixel offsets; folded in so vis_contents children
        // drawn at this atom's tile travel with it. An animate() endpoint already carries the pixel offset
        // absolutely, so the A6 copy is taken back out of the atom's own sprite (not of its children).
        int glx, gly;
        glideOffset(m, glideTW, glideTH, glx, gly);
        int gx = glx + int(m.pixelX) + int(m.pixelW), gy = gly + int(m.pixelY) + int(m.pixelZ);
        ra.px += gx;
        ra.py += gy;
        if (animEndpointCarriesPixels(m)) {
            ra.px -= int(m.pixelX) + int(m.pixelW);
            ra.py -= int(m.pixelY) + int(m.pixelZ);
        }
        Tile* t = tileAt(m.x, m.y);
        if (!t) continue;
        std::vector<RApp> attached;
        std::set<uint32_t> visitedV;
        visContents(id, visitedV, attached, &ra);
        // client.images attached to this atom draw at its tile, with it. An OVERRIDE image replaces the atom's own appearance
        // for this viewer. Stock DreamSeeker 516: the parent's icon, its overlays and its maptext are neither drawn nor clicked
        // (the parent's opaque half answers "floor"), and the image is drawn and clicked in their place, naming the PARENT in
        // the image's frame. An override image that draws nothing (state "blank", or alpha 0, which still takes clicks)
        // leaves the atom invisible. The atom's vis_contents still draw and still answer as themselves. The atom stays in the
        // scene, drawing nothing, so the hit test still knows where it is.
        // An override image on a TURF is drawn in addition to the turf.
        if (imagesOn(id, ra, attached)) {
            ra.icon = nullptr;
            ra.state.clear();
            ra.overlays.clear();
            ra.maptext.clear();
            ra.relay.clear();
        }
        t->push_back(std::move(ra));
        for (auto& a : attached) { a.px += gx; a.py += gy; t->push_back(std::move(a)); }
    }
    // client.images on a TURF (loc kind 0x07) draw after every movable of their tile, only on the
    // viewport's own z. A cell the viewer cannot see draws nothing (in DreamSeeker a
    // LOS-hidden cell is blackness). A hit on one falls through to the turf.
    for (const ImgRec& im : imgs_) {
        if (im.locKind != 0x07) continue;
        if (maxX_ <= 0 || maxY_ <= 0) break;
        int perZ = maxX_ * maxY_, n = int(im.cell);
        int z = n / perZ + 1, rem = n % perZ;
        int x = rem % maxX_ + 1, y = rem / maxX_ + 1;
        if (z != cg.viewZ || hidden_.count({x, y})) continue;
        Tile* t = tileAt(x, y);
        if (!t) continue;
        int turfHit = t->empty() ? -1 : t->front().hitAtom;
        RApp ra;
        if (imageApp(im, ra)) { ra.hitAtom = turfHit; ra.borrowsHit = true; t->push_back(std::move(ra)); }
        std::vector<RApp> kids;
        std::set<uint32_t> visited;
        visChildren(im.visKids, visited, kids, nullptr);
        for (auto& k : kids) t->push_back(std::move(k));
    }
    st_.images += int(w_.images.size());

    originX = viewOriginX;   // from here on the caller wants the VIEW origin
    originY = viewOriginY;
    return !failed_;
}

// The size of the first icon found in an appearance or its overlays.
static bool firstIconSize(const std::vector<RApp>& as, int& w, int& h) {
    for (const RApp& a : as) {
        if (a.icon) { w = a.icon->width; h = a.icon->height; return true; }
        if (firstIconSize(a.overlays, w, h)) return true;
    }
    return false;
}

// The most common icon size over the grid tiles, ties broken toward the first found; 32x32 when there is none.
void Assembler::tileSize(int& tw, int& th) const {
    auto from = firstIconSize;
    std::map<std::pair<int, int>, int> counts;
    std::pair<int, int> first{0, 0};
    for (const Tile& t : grid_) {  // the whole-world order, restricted to the window
        if (t.empty()) continue;
        int w, h;
        if (from(t, w, h)) {
            if (first == std::pair<int, int>{0, 0}) first = {w, h};
            counts[{w, h}]++;
        }
    }
    auto best = first;
    int bestN = 0;
    for (auto& [sz, n] : counts)
        if (n > bestN || (n == bestN && sz == first)) { best = sz; bestN = n; }
    if (bestN == 0) { tw = th = 32; return; }
    tw = best.first; th = best.second;
}

// ---- placement -----------------------------------------------------------------------------------

// Places an appearance (relays and maptext included). atom.transform is applied to the icon's own pixels about its own
// centre BEFORE the tile-anchor placement; the anchor is computed from the native (pre-transform) size and the grown or
// shrunk image is re-centred on it (as stock DreamSeeker does). `inheritedXf` is the parent's transform, copied over
// every overlay's own, identity included.
void Assembler::placeApp(const RApp& a, int baseX, int baseY, int tileH, double parentPlane, double parentLayer, bool hasParent, std::vector<Placed>& out,
                         const std::array<float, 6>* inheritedXf) {
    double plane = a.plane, layer = a.layer;
    double base = hasParent ? parentPlane : 0.0;
    plane = resolvePlane(plane, base);
    if (hasParent && layer < 0) layer = parentLayer;   // a FLOAT_LAYER overlay sorts with its parent
    // Test-only hook: an overlay keeps its own transform instead of taking its parent's.
    const bool hasX = (inheritedXf && g_sceneMutation != 8) ? true : a.hasXform;
    const std::array<float, 6> xf = (inheritedXf && g_sceneMutation != 8) ? *inheritedXf : a.xf;
    static const std::array<float, 6> kIdentity{1, 0, 0, 0, 1, 0};
    if (!hasParent) {
        // The atom's icon frame (hittest.h HitFrame): its own icon, anchored exactly as the untransformed sprite below, plus
        // its transform. Its overlays and underlays are placed with this same frame, since stock DreamSeeker names a click on
        // any of them in the atom's frame.
        int si = 0;
        bool own = drawableState(a, si);
        HitFrame fr;
        fr.w = own ? a.icon->width : tileH;
        fr.h = own ? a.icon->height : tileH;
        fr.x = baseX + a.px;
        fr.y = baseY + (tileH - fr.h) - a.py;
        if (hasX && xf != kIdentity) { fr.hasXf = true; fr.m = dmMatrixToImage(xf); }
        curFrame_ = fr;
    }

    // UNDERLAYS are placed ahead of this appearance's own icon and the overlays after it, so at an
    // equal (or FLOAT_) layer the stable sort keeps an underlay beneath its parent.
    for (const RApp& ov : a.overlays) {
        if (!ov.underlay) continue;
        placeApp(ov, baseX + a.px, baseY - a.py, tileH, plane, layer, true, out, hasX ? &xf : nullptr);
        if (failed_) return;
    }
    // A RELAY draws a slate, not an icon, so it is placed even though it has no icon at all.
    // Its anchor is the tile's BOTTOM-left with the pixel offsets applied; the slate's bottom edge lands on bottomY.
    if (!a.relay.empty()) {
        Placed p;
        p.layer.relay = a.relay;
        p.layer.x = baseX + a.px;
        p.layer.relayBottomY = baseY + tileH - a.py;
        setFx(p.layer, a);
        p.plane = plane;
        p.sortLayer = layer;
        p.seq = int(out.size());
        p.atom = curAtom_;
        p.hitVisible = false;
        out.push_back(std::move(p));
    }
    bool drawable;
    SpritePtr sp = sprite(a, drawable);
    if (failed_) return;
    if (drawable) {
        SpritePtr img = sp;
        if (hasX && xf != kIdentity) img = applyTransform(*sp, dmMatrixToImage(xf), Sampling::Bilinear);
        int dxNative = baseX + a.px;
        int dyNative = baseY + (tileH - sp->h) - a.py;   // bottom-anchored; +pixel_y is north = -Y
        Placed p;
        p.layer.sprite = img;
        if (g_sceneMutation == 4) {   // test-only: anchor the transformed image by its own corner, not its centre
            p.layer.x = dxNative;
            p.layer.y = dyNative;
        } else {
            p.layer.x = dxNative + int(gomath::round(double(sp->w - img->w) / 2));
            p.layer.y = dyNative + int(gomath::round(double(sp->h - img->h) / 2));
        }
        setFx(p.layer, a);
        p.plane = plane;
        p.sortLayer = layer;
        p.seq = int(out.size());
        p.atom = curAtom_;
        p.frame = curFrame_;
        p.own = !hasParent;
        out.push_back(std::move(p));
    }
    // MAPTEXT: in front of this appearance's own icon at the same plane and layer (it sorts with the atom,
    // and its higher seq puts it on top). The box is anchored bottom-left like a sprite, moved +maptext_x right and +maptext_y
    // up; a 0 size is the tile. Only the appearance's alpha reaches it (a runechat fades by animating
    // alpha). The text inside the box: render/maptextpaint.h. For the mouse the box is Placed::maptext: its ink never answers,
    // only mouse_opacity 2's rectangle does.
    if (!a.maptext.empty()) {
        int bw = a.mtW > 0 ? a.mtW : tileH, bh = a.mtH > 0 ? a.mtH : tileH;
        if (SpritePtr mt = paintMaptext(a.maptext, mapStyle_, bw, bh)) {
            Placed p;
            p.layer.sprite = mt;
            p.layer.x = baseX + a.px + a.mtX;
            p.layer.y = baseY + tileH - bh - a.py - a.mtY;
            p.layer.alpha = a.alpha;
            p.plane = plane;
            p.sortLayer = layer;
            p.seq = int(out.size());
            p.atom = curAtom_;
            p.frame = curFrame_;
            p.maptext = g_sceneMutation != 25;
            out.push_back(std::move(p));
        }
    }
    for (const RApp& ov : a.overlays) {
        if (ov.underlay) continue;
        placeApp(ov, baseX + a.px, baseY - a.py, tileH, plane, layer, true, out, hasX ? &xf : nullptr);
        if (failed_) return;
    }
}

// Flatten (the per-tile path used for HUD objects). Overlays keep their own raw
// plane/layer (no float resolution on this path) and inherit the parent's pixel offset.
void Assembler::flatten(const RApp& a, int w, int h, std::vector<FlatLayer>& out) {
    bool drawable;
    SpritePtr sp = sprite(a, drawable);
    if (failed_) return;
    // Underlays ahead of this appearance's own icon, the overlays after it.
    for (RApp ov : a.overlays) {
        if (!ov.underlay) continue;
        ov.px += a.px;
        ov.py += a.py;
        flatten(ov, w, h, out);
        if (failed_) return;
    }
    if (drawable) {
        FlatLayer f;
        f.l.sprite = sp;
        f.l.x = a.px;       // +px east, +py north (= -Y)
        f.l.y = -a.py;
        setFx(f.l, a);
        if (a.cmat) {
            // The sprite is drawn onto a fresh w x h TRANSPARENT canvas (copy, clipped), and the layer is that
            // canvas. It only matters once a colour matrix runs: the matrix maps a blank pixel to its constant row, so the
            // blank margin around the sprite must exist as pixels for the matrix to act on.
            auto cv = std::make_shared<Sprite>();
            cv->w = w; cv->h = h;
            cv->px.assign(size_t(w) * size_t(h) * 4, 0);
            for (int sy = 0; sy < sp->h; sy++) {
                int dy = sy - a.py;
                if (dy < 0 || dy >= h) continue;
                for (int sx = 0; sx < sp->w; sx++) {
                    int dx = sx + a.px;
                    if (dx < 0 || dx >= w) continue;
                    std::memcpy(&cv->px[(size_t(dy) * size_t(w) + size_t(dx)) * 4], &sp->px[(size_t(sy) * size_t(sp->w) + size_t(sx)) * 4], 4);
                }
            }
            f.l.sprite = cv;
            f.l.x = 0;
            f.l.y = 0;
        }
        f.plane = a.plane;
        f.layer = a.layer;
        out.push_back(std::move(f));
    }
    for (RApp ov : a.overlays) {
        if (ov.underlay) continue;
        ov.px += a.px;
        ov.py += a.py;
        flatten(ov, w, h, out);
        if (failed_) return;
    }
}

// The right-click menu's icon for one atom: its appearance drawn on its own, the raw icon state plus its overlays and
// underlays, all at the canvas origin, on a canvas the size of its icon. The rules are DreamSeeker's (see scene.h); the
// shape (build appearance -> flatten -> composite sort) is the map's own.
bool Assembler::appearanceIcon(uint32_t appid, int dir, int& w, int& h, std::vector<uint8_t>& rgba) {
    std::set<uint32_t> visited;
    RApp ra;
    if (!buildApp(appid, visited, ra) || failed_) return false;
    applyIconAnimation(ra);  // the frame at clockNs_ (stat panel icons animate; at clock 0, the first frame)
    if (dir >= 0) {  // the facing the atom is drawn with on the map (a movable's own dir overrides its appearance's)
        ra.dir = dir;
        for (auto& o : ra.overlays) applyOverlayDir(o, dir);
    }
    // Stock DreamSeeker: no colour (scalar, matrix, inherited or an overlay's own), no alpha, no transform (flatten never
    // applies one), and no pixel offset, neither the atom's own nor an overlay's (an image() overlay with pixel_x = 8 sits at 0).
    std::function<void(RApp&)> raw = [&](RApp& r) {
        r.hasTint = false; r.tr = r.tg = r.tb = 255; r.alpha = 255; r.cmat = nullptr;
        r.px = 0; r.py = 0;
        for (auto& o : r.overlays) raw(o);
    };
    if (g_sceneMutation != 19) raw(ra);   // test-only: draw it the way the MAP does - colour, alpha and offsets kept
    // FLOAT_PLANE / FLOAT_LAYER resolve against the parent exactly as the map does it (placeApp). flatten keeps an overlay's
    // raw layer, which sorts an image() overlay (layer FLOAT_LAYER = -1) beneath its parent. Stock DreamSeeker draws such an
    // overlay over the atom's own sprite in the menu icon.
    std::function<void(RApp&)> resolveFloat = [&](RApp& p) {
        for (auto& o : p.overlays) {
            o.plane = resolvePlane(o.plane, p.plane);
            if (o.layer < 0) o.layer = p.layer;
            resolveFloat(o);
        }
    };
    ra.plane = resolvePlane(ra.plane, 0);
    if (g_sceneMutation != 20) resolveFloat(ra);   // test-only: flatten's raw overlay layers
    // The canvas is the atom's own icon; an iconless atom still shows its overlays (as stock DreamSeeker does), on the first icon found.
    std::function<const Icon*(const RApp&)> firstIcon = [&](const RApp& r) -> const Icon* {
        if (r.icon) return r.icon;
        for (const RApp& o : r.overlays)
            if (const Icon* ic = firstIcon(o)) return ic;
        return nullptr;
    };
    const Icon* canvas = firstIcon(ra);
    if (!canvas || canvas->width <= 0 || canvas->height <= 0) return false;
    w = canvas->width;
    h = canvas->height;
    std::vector<FlatLayer> fl;
    flatten(ra, w, h, fl);
    if (failed_ || fl.empty()) return false;
    std::vector<size_t> ord(fl.size());
    for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
    // Stable by (Plane, Layer), ADD after non-ADD at a tie (the same sort as the HUD tile path).
    std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
        if (fl[a].plane != fl[b].plane) return fl[a].plane < fl[b].plane;
        if (fl[a].layer != fl[b].layer) return fl[a].layer < fl[b].layer;
        return (fl[a].l.blend == kBlendAdd ? 1 : 0) < (fl[b].l.blend == kBlendAdd ? 1 : 0);
    });
    rgba.assign(size_t(w) * size_t(h) * 4, 0);
    for (size_t i : ord) blendLayerOver(rgba.data(), w, h, fl[i].l);
    return true;
}

// Screen objects: EITHER the map-then-HUD path (tile-clipped HUD passes drawn after every plane, only when
// no plane master exists) OR, with any master (slateMode), the HUD joins the plane pipeline as unclipped
// screen-space layers marked `screen`, drawn above the map within their plane.
void Assembler::placeHud(int cols, int rows, int tw, int th, DrawList& dl, bool slateMode, std::vector<Placed>& placed) {
    for (uint32_t id : w_.hudOrder) {
        auto slotIt = w_.hud.find(id);
        if (slotIt == w_.hud.end()) continue;
        uint32_t slotAppid;
        try { slotAppid = model::parseJson(slotIt->second)["appid"].u32(); } catch (const std::exception&) { continue; }
        const Ap* app = ap(slotAppid);
        if (!app || app->screenLoc.empty()) continue;
        if (w_.haveSeeInvisible && app->invisibility > int(w_.seeInvisible)) continue;
        if (app->planeMaster()) continue;   // A master's icon is never drawn; the plane pass consumes the master instead.
        // An iconless screen atom must not become a grey square.
        std::function<bool(uint32_t, std::set<uint32_t>&)> resolved = [&](uint32_t aid, std::set<uint32_t>& vis) {
            if (vis.count(aid)) return false;
            const Ap* a = ap(aid);
            if (!a) return false;
            vis.insert(aid);
            bool r = false;
            if (a->iconResID != kNoIcon && a->iconResID < w_.resources.size() && res_.iconResolved(w_.resources[a->iconResID].first, w_.resources)) r = true;
            for (uint32_t u : a->underlays) if (!r && resolved(u, vis)) r = true;
            for (uint32_t o : a->overlays) if (!r && resolved(o, vis)) r = true;
            vis.erase(aid);
            return r;
        };
        std::set<uint32_t> vis0;
        if (app->renderSource.empty() && !resolved(slotAppid, vis0)) continue;   // a relay is iconless on purpose

        // The HUD ref's own atom (its appid, plus icon animation) when it is also a movable, else the slot's appearance.
        RApp ra;
        std::set<uint32_t> visited;
        auto atom = w_.atoms.find(id);
        if (atom != w_.atoms.end()) {
            if (!buildApp(atom->second.appid, visited, ra)) continue;
            applyIconAnimation(ra);
            ra.px += int(atom->second.pixelX);
            ra.py += int(atom->second.pixelY);
        } else {
            if (!buildApp(slotAppid, visited, ra)) continue;
            applyIconAnimation(ra);  // screen objects animate too
        }

        // In slate mode a transformed screen object takes the warp branch below; otherwise it is counted as unmodelled.
        bool skipSlateXf = false;
        if (slateMode) {
            static const std::array<float, 6> kIdentity{1, 0, 0, 0, 1, 0};
            if (ra.hasXform && ra.xf != kIdentity) skipSlateXf = true;   // the warp branch (placeWarpedScreenObj below)
            if (atom != w_.atoms.end() && atom->second.animating) st_.animatedAtoms++;
        } else {
            static const std::array<float, 6> kIdentity{1, 0, 0, 0, 1, 0};
            std::function<bool(const RApp&)> anyXf = [&](const RApp& r) {
                if (r.hasXform && r.xf != kIdentity) return true;
                for (const RApp& o : r.overlays) if (anyXf(o)) return true;
                return false;
            };
            if (anyXf(ra)) st_.transform++;
            if (atom != w_.atoms.end() && atom->second.animating) st_.animatedAtoms++;
        }

        ParsedScreenLoc p;
        if (!parseScreenLoc(app->screenLoc, cols, rows, tw, th, p) || !p.mapCtrl.empty()) continue;
        int spanC = std::max(p.spanCols, 1), spanR = std::max(p.spanRows, 1);
        if (slateMode && skipSlateXf) {
            // A transformed screen object is not placed as an ordinary appearance; it is rendered
            // untransformed at its icon's native size, inverse-warped about the icon's own centre onto the
            // viewport canvas (placeWarpedScreenObj) and placed as ONE layer at the canvas's own origin.
            for (int cx = 0; cx < spanC; cx++)
                for (int cy = 0; cy < spanR; cy++) {
                    int baseX = p.x + cx * tw;
                    int baseY = rows * th - (p.y + cy * th) - th;
                    size_t n0 = placed.size();
                    placeWarpedScreenObj(ra, double(baseX), double(baseY + th), cols * tw, rows * th, placed);
                    if (failed_) return;
                    for (size_t k = n0; k < placed.size(); k++) placed[k].screen = true;
                }
        }
        if (slateMode && !skipSlateXf) {
            // baseY is the cell's TOP edge (screen_loc's origin is the bottom-left).
            for (int cx = 0; cx < spanC; cx++)
                for (int cy = 0; cy < spanR; cy++) {
                    int baseX = p.x + cx * tw;
                    int baseY = rows * th - (p.y + cy * th) - th;
                    size_t n0 = placed.size();
                    curAtom_ = -1;
                    placeApp(ra, baseX, baseY, th, 0, 0, false, placed);
                    if (failed_) return;
                    for (size_t k = n0; k < placed.size(); k++) { placed[k].hitVisible = false; placed[k].screen = true; }
                }
        }
        bool built = false;
        HudPass pass;
        for (int cx = 0; cx < spanC; cx++)
            for (int cy = 0; cy < spanR; cy++) {
                int destX = p.x + cx * tw;
                int bottomY = rows * th - (p.y + cy * th);
                int destY = bottomY - th;
                if (!(destX < cols * tw && destX + tw > 0 && destY < rows * th && destY + th > 0)) continue;
                if (!built) {
                    std::vector<FlatLayer> fl;
                    flatten(ra, tw, th, fl);
                    if (failed_) return;
                    std::vector<size_t> ord(fl.size());
                    for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
                    // Stable by (Plane, Layer); here Plane/Layer are the RAW values.
                    std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
                        if (fl[a].plane != fl[b].plane) return fl[a].plane < fl[b].plane;
                        if (fl[a].layer != fl[b].layer) return fl[a].layer < fl[b].layer;
                        return (fl[a].l.blend == kBlendAdd ? 1 : 0) < (fl[b].l.blend == kBlendAdd ? 1 : 0);   // ADD after non-ADD at a tie
                    });
                    pass.tileW = tw; pass.tileH = th;
                    for (size_t i : ord) pass.layers.push_back(fl[i].l);
                    built = true;
                }
                pass.dests.push_back({destX, destY});
            }
        if (built) {
            if (hit_) {
                // The HUD ref is named by its id; the appearance drawn answers for name/opacity.
                HitHud hh;
                hh.atom = newHitAtom(ra.appid, (id & kRegistryA) ? kAtomMob : kAtomObj, id & ~kRegistryA, true);
                hit_->atoms[size_t(hh.atom)].dir = ra.dir;
                hh.plane = resolvePlane(ra.plane, 0);   // the screen atom's own plane/layer order it in the hit test
                hh.layer = ra.layer;
                hh.tileW = pass.tileW; hh.tileH = pass.tileH;
                for (const DrawLayer& l : pass.layers) hh.layers.push_back(HitLayer{l.sprite, l.x, l.y, hh.atom, {}, false});
                for (const auto& d : pass.dests) hh.dests.push_back({d.x, d.y});
                hit_->hud.push_back(std::move(hh));
            }
            if (!slateMode) dl.hud.push_back(std::move(pass));
        }
    }
}

// A transformed screen object. anchorX/anchorBottomY is the screen_loc cell's bottom-left in viewport pixels; the pivot
// is the icon's own centre at its native size. The source is the atom without its top-level transform, flattened and
// sorted by (plane, layer, ADD-last), with each layer under its own blend onto a transparent ow x oh canvas. The
// destination is scanned only over the bounding box (the forward image of the source rect, padded 2 px) clipped to the
// canvas, sampling nearest with
// floor(half + d + 0.5); a fully transparent sample is left unwritten. The result is placed with alpha 255, the
// object's own blend mode and layer, plane resolvePlane(plane, 0).
void Assembler::placeWarpedScreenObj(const RApp& ra, double anchorX, double anchorBottomY, int canvasW, int canvasH, std::vector<Placed>& placed) {
    bool drawable;
    SpritePtr native = sprite(ra, drawable);
    if (failed_ || !drawable || !native || native->w <= 0 || native->h <= 0) return;
    const int ow = native->w, oh = native->h;
    const double centerX = anchorX + double(ow) / 2;
    const double centerY = anchorBottomY - double(oh) / 2;

    RApp un = ra;
    un.hasXform = false;
    un.xf = {1, 0, 0, 0, 1, 0};
    std::vector<FlatLayer> fl;
    flatten(un, ow, oh, fl);
    if (failed_) return;
    std::vector<size_t> ord(fl.size());
    for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
    std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
        if (fl[a].plane != fl[b].plane) return fl[a].plane < fl[b].plane;
        if (fl[a].layer != fl[b].layer) return fl[a].layer < fl[b].layer;
        return (fl[a].l.blend == kBlendAdd ? 1 : 0) < (fl[b].l.blend == kBlendAdd ? 1 : 0);
    });
    std::vector<uint8_t> src(size_t(ow) * size_t(oh) * 4, 0);
    for (size_t i : ord) blendLayerOver(src.data(), ow, oh, fl[i].l);

    const double a_ = double(ra.xf[0]), b_ = double(ra.xf[1]), c_ = double(ra.xf[2]);
    const double d_ = double(ra.xf[3]), e_ = double(ra.xf[4]), f_ = double(ra.xf[5]);
    const double det = a_ * e_ - b_ * d_;
    if (det == 0) return;   // degenerate: draws nothing
    const double inv00 = e_ / det, inv01 = -b_ / det, inv10 = -d_ / det, inv11 = a_ / det;
    const double tx = c_, ty = -f_;   // DM +y is up; image +y is down
    const double halfW = double(ow) / 2, halfH = double(oh) / 2;

    // The destination window: the forward image of the source rect, clipped to the canvas.
    int sx0 = 0, sy0 = 0, sx1 = canvasW, sy1 = canvasH;
    {
        double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
        const double corners[4][2] = {{0, 0}, {double(ow), 0}, {0, double(oh)}, {double(ow), double(oh)}};
        for (const auto& c : corners) {
            double dxDm = c[0] - halfW, dyDm = -(c[1] - halfH);
            double dxDmP = a_ * dxDm + b_ * dyDm;
            double dyDmP = d_ * dxDm + e_ * dyDm;
            double x = centerX + tx + dxDmP;
            double y = centerY + ty - dyDmP;
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
        if (!(std::isinf(minX) || std::isnan(minX) || std::isnan(minY) || std::isnan(maxX) || std::isnan(maxY))) {
            const int pad = 2;
            int bx0 = int(std::floor(minX)) - pad, by0 = int(std::floor(minY)) - pad;
            int bx1 = int(std::ceil(maxX)) + pad + 1, by1 = int(std::ceil(maxY)) + pad + 1;
            sx0 = std::max(bx0, 0); sy0 = std::max(by0, 0); sx1 = std::min(bx1, canvasW); sy1 = std::min(by1, canvasH);
        }
    }
    if (sx0 >= sx1 || sy0 >= sy1) return;   // lands entirely off-canvas

    auto out = std::make_shared<Sprite>();
    out->w = sx1 - sx0;
    out->h = sy1 - sy0;
    out->px.assign(size_t(out->w) * size_t(out->h) * 4, 0);
    for (int y = sy0; y < sy1; y++)
        for (int x = sx0; x < sx1; x++) {
            double dxImgP = double(x) - centerX - tx;
            double dyImgP = double(y) - centerY - ty;
            double dxDmP = dxImgP, dyDmP = -dyImgP;
            double dxDm = inv00 * dxDmP + inv01 * dyDmP;
            double dyDm = inv10 * dxDmP + inv11 * dyDmP;
            double dxImg = dxDm, dyImg = -dyDm;
            int lx = int(std::floor(halfW + dxImg + 0.5));
            int ly = int(std::floor(halfH + dyImg + 0.5));
            if (lx < 0 || ly < 0 || lx >= ow || ly >= oh) continue;
            const uint8_t* sp = &src[(size_t(ly) * size_t(ow) + size_t(lx)) * 4];
            if (sp[3] == 0) continue;
            std::memcpy(&out->px[(size_t(y - sy0) * size_t(out->w) + size_t(x - sx0)) * 4], sp, 4);
        }

    Placed pl;
    pl.layer.sprite = out;
    pl.layer.x = sx0;
    pl.layer.y = sy0;
    pl.layer.alpha = 255;
    pl.layer.blend = ra.blend;
    pl.plane = resolvePlane(ra.plane, 0);
    pl.sortLayer = ra.layer;
    pl.seq = int(placed.size());
    pl.atom = -1;
    pl.hitVisible = false;
    placed.push_back(std::move(pl));
}

// ---- plane masters -----------------------------------------------------------------------------------

namespace {
// The raw payload of one param bit, walking the ascending-bit layout. false when the bit
// is absent, or when an EARLIER bit has no fixed width (the 0x200 cstr) - nothing after it is addressable.
bool filterParam(const Ap::Flt& f, uint32_t bit, const uint8_t*& data, size_t& len) {
    size_t off = 0;
    for (uint32_t b = 1; b <= 0x4000; b <<= 1) {
        if (!(f.mask & b)) continue;
        int w;
        switch (b) {
            case 0x1: case 0x2: case 0x4: case 0x8: case 0x10: case 0x40: case 0x100: case 0x800: case 0x1000: w = 4; break;
            case 0x20: w = 2; break;
            case 0x400: w = 80; break;
            case 0x2000: case 0x4000: w = 1; break;
            default: return false;   // 0x200 (cstr) or a bit the table does not know
        }
        if (b == bit) {
            if (off + size_t(w) > f.params.size()) return false;
            data = f.params.data() + off;
            len = size_t(w);
            return true;
        }
        off += size_t(w);
    }
    return false;
}
bool filterFloat(const Ap::Flt& f, uint32_t bit, double& out) {
    const uint8_t* d; size_t n;
    if (!filterParam(f, bit, d, n) || n != 4) return false;
    uint32_t b = uint32_t(d[0]) | uint32_t(d[1]) << 8 | uint32_t(d[2]) << 16 | uint32_t(d[3]) << 24;
    float fl;
    std::memcpy(&fl, &b, 4);
    out = double(fl);
    return true;
}
bool filterRGBA(const Ap::Flt& f, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    const uint8_t* d; size_t n;
    if (!filterParam(f, 0x2, d, n) || n != 4) return false;
    r = d[0]; g = d[1]; b = d[2]; a = d[3];
    return true;
}
}  // namespace

// A plane master is a client.screen object (or an overlay of one) whose
// appearance carries PLANE_MASTER; masters NEST (a master's overlay may itself be a master for its own plane), and any
// other overlay joins the enclosing master's group. Children are collected before their parent (post-order), and the
// LAST master naming a plane wins.
void Assembler::collectMasters(uint32_t appid, std::set<uint32_t>& seen) {
    if (seen.count(appid)) return;
    const Ap* a = ap(appid);
    if (!a || !a->planeMaster()) return;
    seen.insert(appid);

    MasterRec m;
    m.spec = std::make_shared<PlaneMasterSpec>();
    PlaneMasterSpec& sp = *m.spec;
    sp.renderTarget = a->renderTarget;
    if (a->hasPlane) m.plane = truePlane(a->plane);
    m.hideFromMouse = a->mouseOpacity() == kMouseOpacityNone && g_sceneMutation != 23;
    if (a->hasColor) { sp.hasColor = true; sp.r = uint8_t(a->cr); sp.g = uint8_t(a->cg); sp.b = uint8_t(a->cb); sp.alpha = a->ca; }
    sp.cmat = a->cmat;
    if (a->hasTransform) { sp.hasXform = true; sp.xf = a->transform; }
    if (a->hasBlend) sp.blend = blendModeOfWire(a->blend);
    for (const Ap::Flt& f : a->filters) {
        double v;
        switch (f.type) {
            case 1: {   // blur
                if (!filterFloat(f, 0x1, v)) { sp.unappliedFilters++; break; }
                FilterSpec fs; fs.kind = FilterSpec::Blur; fs.size = v;
                sp.filters.push_back(fs);
                break;
            }
            case 2: {   // outline: DM's defaults size 1, colour black
                FilterSpec fs; fs.kind = FilterSpec::Outline; fs.size = 1; fs.alpha = 255;
                if (filterFloat(f, 0x1, v)) fs.size = v;
                uint8_t r, g, b, al;
                if (filterRGBA(f, r, g, b, al)) { fs.r = r; fs.g = g; fs.b = b; fs.alpha = al; }
                const uint8_t* d; size_t n;
                if (filterParam(f, 0x20, d, n) && n == 2) fs.flags = uint16_t(d[0] | d[1] << 8);
                if (fs.size <= 0 || fs.alpha == 0) { sp.unappliedFilters++; break; }
                sp.filters.push_back(fs);
                break;
            }
            case 3: {   // drop_shadow: DM's defaults x=1, y=-1, size=1, offset=0, colour black
                FilterSpec fs; fs.kind = FilterSpec::DropShadow; fs.x = 1; fs.y = -1; fs.size = 1; fs.alpha = 255;
                if (filterFloat(f, 0x4, v)) fs.x = int(v);
                if (filterFloat(f, 0x8, v)) fs.y = -int(v);   // DM's +y is NORTH, image space's +y is DOWN
                if (filterFloat(f, 0x1, v)) fs.size = v;
                if (filterFloat(f, 0x10, v)) fs.offset = v;
                uint8_t r, g, b, al;
                if (filterRGBA(f, r, g, b, al)) { fs.r = r; fs.g = g; fs.b = b; fs.alpha = al; }
                sp.filters.push_back(fs);
                break;
            }
            default: sp.unappliedFilters++;
        }
    }
    st_.unappliedFilters += sp.unappliedFilters;
    std::vector<uint32_t> kids = a->underlays;
    kids.insert(kids.end(), a->overlays.begin(), a->overlays.end());
    for (uint32_t oid : kids) {
        const Ap* o = ap(oid);
        if (o && o->planeMaster()) { collectMasters(oid, seen); continue; }
        std::set<uint32_t> vis;
        RApp ra;
        if (buildApp(oid, vis, ra)) m.overlays.push_back(std::move(ra));
    }
    masters_.push_back(std::move(m));
}

// BYOND's blackness: one opaque black tile per hidden cell on plane 0, and only when a plane-0 master
// exists. `sight = SEE_PIXELS` is not modelled: visibility is per tile here, where stock DreamSeeker computes it per pixel.
void Assembler::appendBlackness(std::vector<Placed>& placed, int originX, int originY, int extentW, int extentH, int tw, int th) {
    if (hidden_.empty()) return;
    bool hasPlane0 = false;
    for (const MasterRec& m : masters_)
        if (resolvePlane(m.plane, 0) == 0) { hasPlane0 = true; break; }
    if (!hasPlane0) return;
    auto black = std::make_shared<Sprite>();
    black->w = tw; black->h = th;
    black->px.assign(size_t(tw) * size_t(th) * 4, 0);
    for (size_t i = 3; i < black->px.size(); i += 4) black->px[i] = 255;
    // The margin ring matches run()'s scrolled placement: a scrolled frame reveals a tile beyond the region, and
    // blackness has to cover it too or the glide exposes a bright gutter.
    int x0 = originX, y0 = originY, ew = extentW, eh = extentH, dx = 0, dy = 0;
    if (scrollX_ != 0 || scrollY_ != 0) { x0 = originX - 1; y0 = originY - 1; ew = extentW + 2; eh = extentH + 2; dx = scrollX_ - tw; dy = scrollY_ - th; }
    for (int vr = 0; vr < eh; vr++)
        for (int vc = 0; vc < ew; vc++) {
            if (!hidden_.count({x0 + vc, y0 + vr})) continue;
            Placed p;
            p.layer.sprite = black;
            p.layer.x = vc * tw + dx;
            p.layer.y = (eh - 1 - vr) * th + dy;   // y-flip, as run() does
            p.plane = 0;
            p.sortLayer = 0;
            p.seq = int(placed.size());
            p.hitVisible = false;
            placed.push_back(std::move(p));
        }
}

bool Assembler::run(DrawList& dl, std::string* err) {
    int originX = 0, originY = 0, winW = 0, winH = 0, extentW = 0, extentH = 0, a = 0, b = 0;
    if (!buildGrid(originX, originY, winW, winH, extentW, extentH, a, b)) { if (err) *err = err_; return false; }
    int tw, th;
    tileSize(tw, th);
    dl.outW = extentW * tw;
    dl.outH = extentH * th;
    dl.groundBlack = true;

    // Row-major over the view, y flipped (top of image = highest world y). Inside an eye scroll the region is one tile
    // wider on every side, and everything placed is moved back by that tile and on by the scroll.
    std::vector<Placed> placed;
    const bool scrolled = scrollX_ != 0 || scrollY_ != 0;
    const int rx0 = scrolled ? originX - 1 : originX, ry0 = scrolled ? originY - 1 : originY;
    const int rw = scrolled ? extentW + 2 : extentW, rh = scrolled ? extentH + 2 : extentH;
    for (int vr = 0; vr < rh; vr++)
        for (int vc = 0; vc < rw; vc++) {
            int x = rx0 + vc, y = ry0 + vr;
            const Tile* tp = tileIn(x, y);
            if (!tp || tp->empty()) continue;
            const Tile& t = *tp;
            int baseX = vc * tw, baseY = (rh - 1 - vr) * th;
            for (const RApp& ra : t) {
                curAtom_ = ra.hitAtom;
                if (hit_ && ra.hitAtom >= 0 && !ra.borrowsHit) {
                    HitAtom& ha = hit_->atoms[size_t(ra.hitAtom)];
                    ha.worldX = x; ha.worldY = y;
                    ha.dir = ra.dir;   // the facing it is DRAWN with (a movable's own dir, not its appearance's)
                }
                placeApp(ra, baseX, baseY, th, 0, 0, false, placed);
                if (failed_) { if (err) *err = err_; return false; }
            }
        }
    st_.eyeScroll = scrolled;
    if (scrolled) {
        const int dx = scrollX_ - tw, dy = scrollY_ - th;
        for (Placed& p : placed) { p.layer.x += dx; p.layer.y += dy; p.layer.relayBottomY += dy; p.frame.x += dx; p.frame.y += dy; }
    }
    st_.layers = int(placed.size());

    // Masters are collected before anything is composited; slateMode decides the HUD's path.
    {
        std::set<uint32_t> seen;
        for (uint32_t id : w_.hudOrder) {
            auto slotIt = w_.hud.find(id);
            if (slotIt == w_.hud.end()) continue;
            uint32_t appid;
            try { appid = model::parseJson(slotIt->second)["appid"].u32(); } catch (const std::exception&) { continue; }
            if (!mastersFor(appid)) continue;
            collectMasters(appid, seen);
            if (failed_) { if (err) *err = err_; return false; }
        }
    }
    // Any plane master puts the HUD into the plane pipeline, as stock DreamSeeker does.
    const bool slateMode = !masters_.empty();
    appendBlackness(placed, originX, originY, extentW, extentH, tw, th);
    placeHud(extentW, extentH, tw, th, dl, slateMode, placed);
    if (failed_) { if (err) *err = err_; return false; }

    if (!compositePlanes(placed, dl, th)) { if (err) *err = err_; return false; }

    if (hit_) {
        hit_->tileW = tw; hit_->tileH = th;
        hit_->cols = extentW; hit_->rows = extentH;
        hit_->originX = originX; hit_->originY = originY;
        dl.hit = hit_;
    }
    return true;
}

bool Assembler::compositePlanes(std::vector<Placed>& placed, DrawList& dl, int th) {
    // Group by plane (ascending), then a STABLE sort within the plane by (map before HUD, layer, ADD after non-ADD).
    // A master that carries overlays or a render_target gets a plane even when no
    // sprite sits on it, and its group overlays join the plane's layers BEFORE the sort.
    std::map<double, std::vector<Placed>> byPlane;
    for (auto& p : placed) byPlane[p.plane].push_back(std::move(p));
    std::map<double, const MasterRec*> masterOf;
    for (const MasterRec& m : masters_) {
        double mp = resolvePlane(m.plane, 0);   // a FLOAT_PLANE-relative master plane must be resolved to match its layers
        masterOf[mp] = &m;
        if (!byPlane.count(mp) && (!m.overlays.empty() || !m.spec->renderTarget.empty())) byPlane[mp];
    }
    for (auto& [plane, ls] : byPlane) {
        auto mit = masterOf.find(plane);
        if (mit != masterOf.end()) {
            // The master's overlays are placed with the plane as their parent.
            for (const RApp& ov : mit->second->overlays) {
                std::vector<Placed> grp;
                curAtom_ = -1;
                placeApp(ov, 0, 0, th, plane, 0, true, grp);
                if (failed_) return false;
                for (Placed& g : grp) { g.seq = int(ls.size()); g.hitVisible = false; ls.push_back(std::move(g)); }
            }
        }
        auto addRank = [](const Placed& p) { return p.layer.blend == kBlendAdd ? 1 : 0; };
        if (g_sceneMutation == 2) {
            // test-only: break the arrival-order tie rule (equal layers drawn in reverse arrival)
            std::stable_sort(ls.begin(), ls.end(), [&](const Placed& a, const Placed& b) {
                if (a.screen != b.screen) return b.screen;
                if (a.sortLayer != b.sortLayer) return a.sortLayer < b.sortLayer;
                return a.seq > b.seq;
            });
        } else {
            std::stable_sort(ls.begin(), ls.end(), [&](const Placed& a, const Placed& b) {
                if (a.screen != b.screen) return b.screen;   // The map sub-plane, then the HUD's
                if (a.sortLayer != b.sortLayer) return a.sortLayer < b.sortLayer;
                return (g_sceneMutation == 15 ? 0 : addRank(a)) < (g_sceneMutation == 15 ? 0 : addRank(b));
            });
        }
        PlanePass pp;
        pp.plane = plane;
        if (mit != masterOf.end()) pp.master = mit->second->spec;
        // A master at mouse_opacity 0 hides its whole plane from the hit test: per placed layer, not per atom. The master
        // governs the plane a pixel is drawn on, so an atom's overlay on such a plane drops out while the atom's own icon on
        // another plane still answers (MasterRec::hideFromMouse).
        const bool mouseHidden = mit != masterOf.end() && mit->second->hideFromMouse;
        for (auto& p : ls) {
            if (hit_ && p.hitVisible && p.layer.sprite && !mouseHidden) hit_->world.push_back(HitLayer{p.layer.sprite, p.layer.x, p.layer.y, p.atom, p.frame, p.own, plane, p.maptext});
            pp.layers.push_back(std::move(p.layer));
        }
        dl.planes.push_back(std::move(pp));
    }
    st_.masters = int(masters_.size());
    return true;
}

static bool ieqAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

// Does the client.screen master `appid` govern the map being drawn? Its screen_loc's map prefix must name it: the main
// map takes only masters that name no map, a secondary map only those naming it (vg's camera console copies every
// plane master this way).
// The appearance is resolved here (ap()), not read from what the line-of-sight pass happened to cache: the masters loop
// can run before a client.screen appearance is cached (LOS skipped for SEE_TURFS|SEE_MOBS|SEE_OBJS sight, or the
// appearance not yet arrived), and an uncached answer of "yes" let the camera map's LIGHTING_PLANE master black out the
// main map. An appearance that is not here governs nothing (collectMasters refuses it too).
bool Assembler::mastersFor(uint32_t appid) {
    const Ap* a = ap(appid);
    return a && ieqAscii(screenLocMap(a->screenLoc), mapName_);
}

// A secondary map control: vg's security camera console - a map created at runtime inside a browse() popup, fed by
// client.screen objects whose screen_loc names it. The rules, as stock DreamSeeker applies them:
//   * the view is the bounding box of the map's screen objects, at the world's icon size; the map control scales it to
//     fit and centres it on a black letterbox (MapWidget's auto-fit), so the assembler produces the box at native size;
//   * an iconless object counts toward the box; a vis_contents does not widen it; an object placed by a view keyword
//     ("name:CENTER": plane masters and vg's plane/dark) is left out of it;
//   * vis_contents turfs (World::visTurfs) are drawn at their relative places, the cell (min x, min y) of their bounding
//     box on the holder's cell - also when that cell is not one of them - each with what stands on it (respecting
//     see_invisible), clipped to the box;
//   * lighting draws in it: the camera map's own LIGHTING_PLANE master over the remote turfs' lighting_overlays;
//   * only the masters naming this map apply (mastersFor), and the default map draws none of these objects.
// Not modelled: the first map control created while its objects were already on client.screen keeps a world.view-sized
// view until an object's screen_loc next changes; a re-created control fits at once. The box's corner is the lowest
// screen_loc.
bool Assembler::runSecondary(std::string_view mapName, DrawList& dl, std::string* err) {
    mapName_ = std::string(mapName);
    maxX_ = w_.bounds.maxX; maxY_ = w_.bounds.maxY;
    const int tw = model::iconSizeOf(w_), th = tw;
    int cols = 2 * kDefaultViewRange + 1, rows = cols;   // only a keyword (CENTER, EAST...) consults them
    if (w_.bounds.viewW > 0 && w_.bounds.viewH > 0) { cols = w_.bounds.viewW; rows = w_.bounds.viewH; }
    else if (w_.bounds.view > 0) cols = rows = 2 * w_.bounds.view + 1;
    collectImages();

    struct Obj { uint32_t id, appid; ParsedScreenLoc p; bool keyword; };
    std::vector<Obj> objs;
    std::set<uint32_t> seen;
    for (uint32_t id : w_.hudOrder) {
        auto slotIt = w_.hud.find(id);
        if (slotIt == w_.hud.end()) continue;
        uint32_t appid;
        try { appid = model::parseJson(slotIt->second)["appid"].u32(); } catch (const std::exception&) { continue; }
        const Ap* app = ap(appid);
        if (!app || !ieqAscii(screenLocMap(app->screenLoc), mapName_)) continue;
        if (app->planeMaster()) { collectMasters(appid, seen); if (failed_) break; continue; }
        if (w_.haveSeeInvisible && app->invisibility > int(w_.seeInvisible)) continue;
        Obj o{id, appid, {}, false};
        if (!parseScreenLoc(app->screenLoc, cols, rows, tw, th, o.p)) continue;
        // A position that moves with the view size (CENTER, EAST, NORTH...) is relative to the box, not part of it.
        ParsedScreenLoc wider;
        o.keyword = parseScreenLoc(app->screenLoc, cols + 10, rows + 10, tw, th, wider) && (wider.x != o.p.x || wider.y != o.p.y);
        objs.push_back(o);
    }
    if (failed_) { if (err) *err = err_; return false; }
    dl.groundBlack = true;
    if (objs.empty()) { dl.outW = tw; dl.outH = th; return true; }   // a map with nothing on it: black
    // The box: every object placed by numbers. A keyword object is then placed against the box's own size. Only when
    // nothing is placed by numbers do keyword objects make the box (at the world's view size).
    bool anyNumeric = false;
    for (const Obj& o : objs) anyNumeric = anyNumeric || !o.keyword;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const Obj& o : objs) {
        if (anyNumeric && o.keyword) continue;
        x0 = std::min(x0, o.p.x); y0 = std::min(y0, o.p.y);
        x1 = std::max(x1, o.p.x + std::max(o.p.spanCols, 1) * tw);
        y1 = std::max(y1, o.p.y + std::max(o.p.spanRows, 1) * th);
    }
    const int W = x1 - x0, H = y1 - y0;
    dl.outW = W; dl.outH = H;
    if (anyNumeric)
        for (Obj& o : objs) {
            if (!o.keyword) continue;
            const Ap* app = ap(o.appid);
            ParsedScreenLoc q;
            if (app && parseScreenLoc(app->screenLoc, W / tw, H / th, tw, th, q)) { q.x += x0; q.y += y0; o.p = q; }
        }

    // What stands on each turf (only built when some holder shows turfs).
    std::map<std::array<int, 3>, std::vector<uint32_t>> onTurf;
    bool anyTurfs = false;
    for (const Obj& o : objs) anyTurfs = anyTurfs || w_.visTurfs.count(o.id);
    if (anyTurfs)
        for (const auto& [aid, m] : w_.atoms)
            if (m.hasPos) onTurf[{m.x, m.y, m.z}].push_back(aid);   // ascending id, as the default map

    std::vector<Placed> placed;
    for (const Obj& o : objs) {
        RApp ra;
        std::set<uint32_t> visited;
        auto atom = w_.atoms.find(o.id);
        if (atom != w_.atoms.end()) {
            if (!buildApp(atom->second.appid, visited, ra)) continue;
            ra.px += int(atom->second.pixelX);
            ra.py += int(atom->second.pixelY);
        } else if (!buildApp(o.appid, visited, ra)) continue;
        applyIconAnimation(ra);
        const int spanC = std::max(o.p.spanCols, 1), spanR = std::max(o.p.spanRows, 1);
        for (int cx = 0; cx < spanC; cx++)
            for (int cy = 0; cy < spanR; cy++) {
                curAtom_ = -1;
                placeApp(ra, o.p.x - x0 + cx * tw, H - (o.p.y - y0 + cy * th) - th, th, 0, 0, false, placed);
                if (failed_) { if (err) *err = err_; return false; }
            }
        const int hx = o.p.x - x0, hy = H - (o.p.y - y0) - th;   // the holder's (first) cell, top-left
        std::vector<RApp> kids;
        std::set<uint32_t> visitedV;
        visContents(o.id, visitedV, kids, &ra);
        for (const RApp& k : kids) { curAtom_ = -1; placeApp(k, hx, hy, th, 0, 0, false, placed); }
        auto vt = w_.visTurfs.find(o.id);
        if (vt == w_.visTurfs.end() || maxX_ <= 0 || maxY_ <= 0) continue;
        const uint32_t mx = uint32_t(maxX_), my = uint32_t(maxY_);
        int minX = INT32_MAX, minY = INT32_MAX;
        for (uint32_t c : vt->second) { minX = std::min(minX, int(c % mx)); minY = std::min(minY, int(c / mx % my)); }
        for (uint32_t c : vt->second) {
            const int tx = int(c % mx), ty = int(c / mx % my), tz = int(c / (mx * my));
            const int bx = hx + (tx - minX) * tw, by = hy - (ty - minY) * th;
            if (bx >= W || by >= H || bx + tw <= 0 || by + th <= 0) continue;   // outside the box: clipped
            auto t = w_.tiles.find({uint16_t(tx), uint16_t(ty), uint16_t(tz)});
            if (t != w_.tiles.end() && !hiddenFromClient(t->second)) {
                RApp tr;
                std::set<uint32_t> v;
                if (buildApp(t->second, v, tr)) {
                    applyIconAnimation(tr);
                    curAtom_ = -1;
                    placeApp(tr, bx, by, th, 0, 0, false, placed);
                }
            }
            auto on = onTurf.find({tx + 1, ty + 1, tz});
            if (on == onTurf.end()) continue;
            for (uint32_t aid : on->second) {
                const model::Atom& m = w_.atoms.at(aid);
                if (hiddenFromClient(m.appid)) continue;
                RApp ma;
                if (!animatedApp(aid, m, ma)) continue;
                ma.dir = m.dir;
                for (auto& ov : ma.overlays) applyOverlayDir(ov, m.dir);
                int gx = int(m.pixelX) + int(m.pixelW), gy = int(m.pixelY) + int(m.pixelZ);
                ma.px += gx; ma.py += gy;
                std::vector<RApp> attached;
                std::set<uint32_t> va;
                visContents(aid, va, attached, &ma);
                curAtom_ = -1;
                placeApp(ma, bx, by, th, 0, 0, false, placed);
                for (auto& a : attached) { a.px += gx; a.py += gy; placeApp(a, bx, by, th, 0, 0, false, placed); }
            }
            if (failed_) { if (err) *err = err_; return false; }
        }
    }
    for (Placed& p : placed) p.hitVisible = false;   // no hit scene for a secondary map (clicks in it are not modelled)
    st_.layers = int(placed.size());
    if (!compositePlanes(placed, dl, th)) { if (err) *err = err_; return false; }
    return true;
}

}  // namespace

// ---- ResStore ------------------------------------------------------------------------------------

bool ResStore::add(ByteSpan bytes, const uint32_t* claimedId, std::string* err) {
    uint32_t id = net::resId(bytes);
    if (claimedId && *claimedId != id) {
        char b[96];
        std::snprintf(b, sizeof b, "resource named %08x hashes to %08x: refused", *claimedId, id);
        if (err) *err = b;
        return false;
    }
    bytes_[id] = Bytes(bytes.begin(), bytes.end());
    icons_.erase(id);
    bad_.erase(id);
    return true;
}

const Icon* ResStore::icon(uint32_t id) {
    auto c = icons_.find(id);
    if (c != icons_.end()) return c->second.get();
    if (bad_.count(id)) return nullptr;
    auto b = bytes_.find(id);
    if (b == bytes_.end()) { wanted_.insert(id); return nullptr; }
    // A DDMI recipe holds no raster: it cannot be parsed as an icon and must NOT poison bad_ (iconResolved owns it).
    if (isDdmi(b->second.data(), b->second.size())) return nullptr;
    ParseResult r = parseIcon(b->second.data(), b->second.size());
    if (!r.ok()) {
        bad_[id] = true;
        std::string head;
        for (size_t i = 0; i < std::min<size_t>(8, b->second.size()); i++) { char x[3]; std::snprintf(x, sizeof x, "%02x", b->second[i]); head += x; }
        refused_[id] = "icon parse: " + r.errClass + " " + r.errDetail + " (" + std::to_string(b->second.size()) + " bytes, head " + head + ")";
        return nullptr;
    }
    const Icon* p = r.icon.get();
    icons_[id] = std::move(r.icon);
    return p;
}

// Recipe resolution. PENDING (an operand whose bytes or whose ordinal has not arrived yet) and PERMANENT (a refused
// shape, a missing icon_state, an operand that is not an icon, a cycle) are different failures and only the second may poison.
ResStore::RStat ResStore::resolveOne(uint32_t id, const std::vector<std::pair<uint32_t, uint8_t>>& ordinals, std::set<uint32_t>& inProgress,
                                     const Icon*& out) {
    out = nullptr;
    if (auto r = recipeIcon_.find(id); r != recipeIcon_.end()) { out = r->second; return RStat::Ok; }
    if (const Icon* ic = (icons_.count(id) ? icons_[id].get() : nullptr)) { out = ic; return RStat::Ok; }
    if (bad_.count(id)) return RStat::Permanent;
    auto b = bytes_.find(id);
    if (b == bytes_.end()) { wanted_.insert(id); return RStat::Pending; }
    if (!isDdmi(b->second.data(), b->second.size())) {
        // An ordinary icon reached here as somebody's operand: resolve it as the plain path would.
        const Icon* ic = icon(id);
        if (ic) { out = ic; return RStat::Ok; }
        return bad_.count(id) ? RStat::Permanent : RStat::Pending;
    }
    if (inProgress.count(id)) { bad_[id] = true; refused_[id] = "DDMI recipe cycle"; return RStat::Permanent; }   // a cycle can never be broken
    DdmiRecipe rec;
    std::string perr;
    if (!parseDdmi(b->second.data(), b->second.size(), rec, &perr)) { bad_[id] = true; refused_[id] = perr; return RStat::Permanent; }
    inProgress.insert(id);
    struct Guard { std::set<uint32_t>& s; uint32_t id; ~Guard() { s.erase(id); } } guard{inProgress, id};
    std::vector<const Icon*> ops;
    for (const DdmiRef& ref : rec.refs) {
        if (ref.ordinal >= ordinals.size()) return RStat::Pending;   // the offer list GROWS mid-session
        const Icon* op = nullptr;
        switch (resolveOne(ordinals[ref.ordinal].first, ordinals, inProgress, op)) {
            case RStat::Pending: return RStat::Pending;
            case RStat::Permanent: bad_[id] = true; refused_[id] = "a DDMI operand is permanently unresolvable"; return RStat::Permanent;
            case RStat::Ok: break;
        }
        ops.push_back(op);
    }
    DdmiResult r = evalDdmi(rec, ops);
    if (!r.icon()) {
        bad_[id] = true; refused_[id] = r.err; return RStat::Permanent;
    }
    if (r.owned) { recipeIcon_[id] = r.owned.get(); recipeOwned_[id] = std::move(r.owned); }
    else recipeIcon_[id] = r.alias;
    out = recipeIcon_[id];
    return RStat::Ok;
}

const Icon* ResStore::iconResolved(uint32_t id, const std::vector<std::pair<uint32_t, uint8_t>>& ordinals) {
    std::set<uint32_t> inProgress;
    const Icon* out = nullptr;
    if (resolveOne(id, ordinals, inProgress, out) == RStat::Ok) return out;
    return nullptr;
}

std::string SceneStats::summary() const {
    std::ostringstream o;
    o << "layers=" << layers;
    auto f = [&](const char* n, int v) { if (v) o << " unmodeled:" << n << "=" << v; };
    f("maptext", maptext); f("render_source", renderSource); f("transform", transform); f("color_matrix", colorMatrix);
    f("blend_mode", blendMode); f("plane_master", planeMasters); f("animate_chain", animatedAtoms); f("gliding", glidingAtoms);
    f("images", images);
    if (masters) o << " masters=" << masters;
    if (unappliedFilters) o << " unapplied_filters=" << unappliedFilters;
    if (eyeScroll) o << " eye_scroll=1";
    return o.str();
}

void setSceneMutation(int m) {
    g_sceneMutation = m;
    if (m == 10 || m == 11 || m == 12 || m == 13 || m == 14 || m == 16) setPipelineMutation(m);
    if (m == 17 || m == 18) setDdmiMutation(m);
}

bool assembleMapControl(const model::Session& session, ResStore& res, int64_t clockNs, DrawList& out, SceneStats& stats, std::string* err,
                        bool withHit, std::string_view mapStyle) {
    Assembler a(session, res, clockNs, stats, withHit, mapStyle);
    return a.run(out, err);
}

bool assembleSecondaryMap(const model::Session& session, ResStore& res, int64_t clockNs, std::string_view mapName, DrawList& out,
                          SceneStats& stats, std::string* err, std::string_view mapStyle) {
    Assembler a(session, res, clockNs, stats, false, mapStyle);
    return a.runSecondary(mapName, out, err);
}

bool appearanceIcon(const model::Session& session, ResStore& res, uint32_t appid, int dir, int& w, int& h, std::vector<uint8_t>& rgba,
                    int64_t clockNs) {
    SceneStats st;
    Assembler a(session, res, clockNs, st);
    return a.appearanceIcon(appid, dir, w, h, rgba);
}

bool statRowIcon(const model::Session& session, ResStore& res, uint32_t appid, int cell, int64_t clockNs, std::vector<uint8_t>& rgba) {
    int w = 0, h = 0;
    std::vector<uint8_t> src;
    if (cell <= 0 || !appearanceIcon(session, res, appid, -1, w, h, src, clockNs)) return false;
    rgba.assign(size_t(cell) * size_t(cell) * 4, 0);
    if (w <= cell && h <= cell) {  // native size, top-left
        for (int y = 0; y < h; y++) std::memcpy(&rgba[size_t(y) * size_t(cell) * 4], &src[size_t(y) * size_t(w) * 4], size_t(w) * 4);
        return true;
    }
    // Shrunk into the cell: each destination pixel is the premultiplied average of the source pixels it covers. Uniform
    // scale by the larger side.
    int big = std::max(w, h);
    int dw = std::max(1, w * cell / big), dh = std::max(1, h * cell / big);
    for (int y = 0; y < dh; y++) {
        int y0 = y * h / dh, y1 = std::max(y0 + 1, (y + 1) * h / dh);
        for (int x = 0; x < dw; x++) {
            int x0 = x * w / dw, x1 = std::max(x0 + 1, (x + 1) * w / dw);
            uint64_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++) {
                    const uint8_t* p = &src[(size_t(sy) * size_t(w) + size_t(sx)) * 4];
                    r += uint64_t(p[0]) * p[3]; g += uint64_t(p[1]) * p[3]; b += uint64_t(p[2]) * p[3]; a += p[3]; n++;
                }
            uint8_t* d = &rgba[(size_t(y) * size_t(cell) + size_t(x)) * 4];
            if (a == 0) continue;
            d[0] = uint8_t((r + a / 2) / a); d[1] = uint8_t((g + a / 2) / a); d[2] = uint8_t((b + a / 2) / a); d[3] = uint8_t((a + n / 2) / n);
        }
    }
    return true;
}

bool renderMapControl(const model::Session& session, ResStore& res, int64_t clockNs, L3Frame& out, std::string* err) {
    DrawList dl;
    out.stats = SceneStats{};
    if (!assembleMapControl(session, res, clockNs, dl, out.stats, err)) return false;
    out.img = execute(dl);
    return true;
}

}  // namespace fugue::render
