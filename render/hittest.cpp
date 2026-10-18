#include "render/hittest.h"

#include <cmath>
#include <algorithm>

namespace fugue::render {
namespace {

int g_mutation = 0;
int effOpacity(int mo) {
    if (g_mutation == 1) return kMouseOpacityIcon;
    if (g_mutation == 2) return mo == kMouseOpacityNone ? mo : kMouseOpacityBBox;
    return mo;
}

// A cursor pixel in an atom's icon frame: icon-x/icon-y (the untransformed icon, 1-based, y up) and vis-x/vis-y
// (the transformed icon's bounding box). See hittest.h.
struct FrameCoords { int iconX, iconY, visX, visY; };
FrameCoords frameCoords(const HitFrame& f, int px, int py) {
    FrameCoords c{};
    // Image space is +y DOWN; the frame's centre C, the transform q = C + L(p - C) + t (render/xform.cpp applyTransform's
    // own mapping, re-centred on C). The cursor is the pixel's top-left corner in image space, and every coordinate is
    // floored in that space: icon-y and vis-y are then counted up from the bottom (h - row), not floored after the flip.
    // The transformed bounding box's half extents are floored to whole pixels about the centre. Hence Turn(90) gives
    // vis-x = icon-y + 1, vis-y = 33 - icon-x: the corner x lands on an icon row boundary, which the y-down floor assigns
    // to the row below. kEps absorbs float32 noise in the wire matrix (cos 90 = -4.4e-8) at those exact boundaries.
    constexpr double kEps = 1e-4;
    auto fl = [](double v) { return int(std::floor(v + kEps)); };
    const double qx = px, qy = py;
    const double cx = f.x + f.w / 2.0, cy = f.y + f.h / 2.0;
    double ix = qx, iy = qy, left = f.x, top = f.y, boxH = f.h;
    if (f.hasXf) {
        const Mat& m = f.m;
        double det = m.a * m.e - m.b * m.d;
        if (det != 0) {
            double dx = qx - cx - m.c, dy = qy - cy - m.f;
            ix = cx + (m.e * dx - m.b * dy) / det;
            iy = cy + (-m.d * dx + m.a * dy) / det;
        }
        // the transformed icon's bounding box: the linear part's extent, centred on C + t, half extents floored
        double hw = std::floor((std::fabs(m.a) * f.w + std::fabs(m.b) * f.h) / 2 + kEps);
        double hh = std::floor((std::fabs(m.d) * f.w + std::fabs(m.e) * f.h) / 2 + kEps);
        left = cx + m.c - hw;
        top = cy + m.f - hh;
        boxH = 2 * hh;
    }
    c.iconX = fl(ix - f.x) + 1;
    c.iconY = f.h - fl(iy - f.y);
    c.visX = fl(qx - left) + 1;
    c.visY = int(boxH) - fl(qy - top);
    if (g_mutation == 4 && f.hasXf) {
        // test-only hook: the alternative convention (pixel left edge + vertical centre, floored after the y flip,
        // unfloored box).
        const Mat& m = f.m;
        double det = m.a * m.e - m.b * m.d, dx = qx - cx - m.c, dy = qy + 0.5 - cy - m.f;
        double iy4 = det != 0 ? cy + (-m.d * dx + m.a * dy) / det : qy + 0.5;
        double hh = (std::fabs(m.d) * f.w + std::fabs(m.e) * f.h) / 2, hw = (std::fabs(m.a) * f.w + std::fabs(m.b) * f.h) / 2;
        c.iconY = int(std::floor(f.y + f.h - iy4)) + 1;
        c.visX = int(std::floor(qx - (cx + m.c - hw))) + 1;
        c.visY = int(std::floor(cy + m.f + hh - (qy + 0.5))) + 1;
    }
    return c;
}

// covers: with whole=false the SOURCE sprite's own alpha decides (any non-zero counts).
bool covers(const HitLayer& l, int px, int py, bool whole) {
    if (!l.sprite || l.sprite->w <= 0 || l.sprite->h <= 0) return false;
    int ix = px - l.x, iy = py - l.y;
    if (ix < 0 || iy < 0 || ix >= l.sprite->w || iy >= l.sprite->h) return false;
    if (whole) return true;
    if (l.maptext) return false;  // maptext never answers by its ink (see hittest.h)
    if (l.own && l.frame.hasXf && l.frame.w > 0 && g_mutation != 3) {
        // A transformed atom's OWN icon answers only where the cursor maps back INTO the icon. Under Turn(90) the
        // leftmost bounding-box column (vis-x 1, icon-y 0) names the turf beneath, though the resampled sprite paints it.
        FrameCoords c = frameCoords(l.frame, px, py);
        if (c.iconX < 1 || c.iconY < 1 || c.iconX > l.frame.w || c.iconY > l.frame.h) return false;
    }
    return l.sprite->px[(size_t(iy) * size_t(l.sprite->w) + size_t(ix)) * 4 + 3] != 0;
}

void fill(Hit& h, const HitAtom& a, int sx, int sy, const Sprite& sp) {
    h.name = a.name; h.kind = a.kind; h.ref = a.ref; h.worldX = a.worldX; h.worldY = a.worldY; h.screen = a.screen;
    h.appid = a.appid; h.dir = a.dir; h.wheel = a.wheel;
    h.spriteX = sx; h.spriteY = sy; h.spriteW = sp.w; h.spriteH = sp.h;
}

// Visits client.screen entries in `ord` order: a later entry (and a later dest of one entry) beats an earlier one.
// allowBBox=false withholds mouse_opacity 2's widening.
bool hudScan(const HitScene& s, const std::vector<size_t>& ord, int px, int py, bool allowBBox, Hit& out, double& plane) {
    bool found = false;
    for (size_t i : ord) {
        const HitHud& hu = s.hud[i];
        if (hu.atom < 0 || size_t(hu.atom) >= s.atoms.size()) continue;
        const HitAtom& a = s.atoms[size_t(hu.atom)];
        if (effOpacity(a.mouseOpacity) == kMouseOpacityNone) continue;
        bool whole = allowBBox && effOpacity(a.mouseOpacity) >= kMouseOpacityBBox;
        for (const auto& d : hu.dests) {
            int lx = px - d.x, ly = py - d.y;
            if (lx < 0 || ly < 0 || lx >= hu.tileW || ly >= hu.tileH) continue;
            const HitLayer* best = nullptr;
            for (const HitLayer& l : hu.layers)
                if (covers(l, lx, ly, whole)) best = &l;
            if (!best) continue;
            fill(out, a, d.x + hu.originX, d.y + hu.originY - best->sprite->h, *best->sprite);
            plane = best->plane;
            found = true;
        }
    }
    return found;
}

// client.screen order only. hitAt itself uses hudTop; a test-only hook uses this rule.
bool hudPass(const HitScene& s, int px, int py, bool allowBBox, Hit& out) {
    std::vector<size_t> ord(s.hud.size());
    for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
    double plane = 0;
    return hudScan(s, ord, px, py, allowBBox, out, plane);
}

// The topmost client.screen atom at (px,py) in DreamSeeker's HUD draw order: plane, then layer, then client.screen
// order (later on top). Each atom covers by its own mouse_opacity (2 = its whole rectangle). See hittest.h.
bool hudTop(const HitScene& s, int px, int py, Hit& out, double& plane) {
    std::vector<size_t> ord(s.hud.size());
    for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
    std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
        if (s.hud[a].plane != s.hud[b].plane) return s.hud[a].plane < s.hud[b].plane;
        return s.hud[a].layer < s.hud[b].layer;
    });
    return hudScan(s, ord, px, py, /*allowBBox=*/true, out, plane);
}

}  // namespace

bool hitAt(const HitScene& s, int px, int py, Hit& out) {
    if (s.cols <= 0 || s.rows <= 0 || px < 0 || py < 0 || px >= s.pixW() || py >= s.pixH()) return false;
    Hit h;
    double hudPlane = 0;
    bool hud = false;
    if (g_mutation == 5) {
        // test-only hook: a HUD atom wins outright where it paints, in client.screen order.
        if (hudPass(s, px, py, /*allowBBox=*/false, h)) { out = h; return true; }
    } else {
        hud = hudTop(s, px, py, h, hudPlane);
    }
    const HitLayer* best = nullptr;
    for (const HitLayer& l : s.world) {
        if (l.atom < 0 || size_t(l.atom) >= s.atoms.size()) continue;
        const HitAtom& a = s.atoms[size_t(l.atom)];
        if (effOpacity(a.mouseOpacity) == kMouseOpacityNone) continue;
        if (covers(l, px, py, effOpacity(a.mouseOpacity) >= kMouseOpacityBBox)) best = &l;
    }
    // A HUD atom is drawn above every map layer of its own plane and below those of a higher one (see hittest.h).
    if (hud && (!best || hudPlane >= best->plane)) { out = h; return true; }
    if (best) {
        fill(out, s.atoms[size_t(best->atom)], best->x, best->y, *best->sprite);
        out.frame = best->frame;
        return true;
    }
    // test-only hook: when nothing on the map answers, a mouse_opacity-2 HUD atom takes the click.
    Hit c;
    if (g_mutation == 5 && hudPass(s, px, py, /*allowBBox=*/true, c)) { out = c; return true; }
    return false;
}

std::vector<Hit> hitStack(const HitScene& s, int px, int py) {
    std::vector<Hit> out;
    if (s.cols <= 0 || s.rows <= 0 || s.tileW <= 0 || s.tileH <= 0 || px < 0 || py < 0 || px >= s.pixW() || py >= s.pixH()) return out;
    Hit hud;
    if (hudPass(s, px, py, /*allowBBox=*/false, hud)) out.push_back(hud);
    int fromBottom = s.pixH() - 1 - py;
    int wx = s.originX + px / s.tileW, wy = s.originY + fromBottom / s.tileH;
    std::vector<bool> seen(s.atoms.size(), false);
    for (auto it = s.world.rbegin(); it != s.world.rend(); ++it) {   // topmost first
        const HitLayer& l = *it;
        if (l.atom < 0 || size_t(l.atom) >= s.atoms.size() || seen[size_t(l.atom)] || !l.sprite) continue;
        const HitAtom& a = s.atoms[size_t(l.atom)];
        if (a.screen || a.worldX != wx || a.worldY != wy || effOpacity(a.mouseOpacity) == kMouseOpacityNone) continue;
        seen[size_t(l.atom)] = true;
        Hit h;
        fill(h, a, l.x, l.y, *l.sprite);
        h.atom = l.atom;
        out.push_back(h);
    }
    return out;
}

void setHitMutation(int m) { g_mutation = m; }

bool nameMouse(const HitScene& s, int px, int py, Naming& n) {
    if (s.cols <= 0 || s.rows <= 0 || s.tileW <= 0 || s.tileH <= 0) return false;
    if (px < 0 || py < 0 || px >= s.pixW() || py >= s.pixH()) return false;
    // The y flip is taken from the bottom pixel row (pixH-1).
    int fromBottom = s.pixH() - 1 - py;
    int tx = px / s.tileW, ty = fromBottom / s.tileH;
    if (tx >= s.cols || ty >= s.rows) return false;
    Hit h;
    if (!hitAt(s, px, py, h)) return false;

    n = Naming{};
    n.screenX = tx + 1; n.screenY = ty + 1;
    n.screenPX = px % s.tileW + 1; n.screenPY = fromBottom % s.tileH + 1;
    n.iconX = n.screenPX; n.iconY = n.screenPY;
    n.overKind = 3; n.overX = n.locX = std::max(1, s.originX + tx); n.overY = n.locY = std::max(1, s.originY + ty);
    if (h.kind == kAtomTurf) {
        // no movable answered: the turf under the cursor is the atom
    } else if (h.screen) {
        n.overKind = h.kind == kAtomMob ? 2 : 1; n.overRef = h.ref;
        n.locNull = true;                                    // a client.screen atom has no turf
    } else {
        n.overKind = h.kind == kAtomMob ? 2 : 1; n.overRef = h.ref;
        if (h.worldX > 0 && h.worldY > 0) { n.locX = h.worldX; n.locY = h.worldY; }   // the atom's OWN cell
    }
    if (h.frame.w > 0 && h.frame.h > 0 && g_mutation != 3) {
        FrameCoords c = frameCoords(h.frame, px, py);
        n.iconX = c.iconX; n.iconY = c.iconY; n.visX = c.visX; n.visY = c.visY;
        n.hasVis = n.visX != n.iconX || n.visY != n.iconY;
    } else if (h.spriteW > 0 && h.spriteH > 0) {
        n.iconX = px - h.spriteX + 1;
        n.iconY = (h.spriteY + h.spriteH - 1) - py + 1;
    }
    if (!n.hasVis) { n.visX = n.iconX; n.visY = n.iconY; }
    n.hit = std::move(h);
    return true;
}

}  // namespace fugue::render
