// The mouse hit test: viewport pixel -> the ATOM under the cursor, and that atom's mouse-command naming (over-ref, loc,
// icon-x/y, screen-loc).
//
// The hit test reads the same placed layers the compositor draws. scene.cpp's Assembler records, in draw order, every
// sprite it placed together with the atom that owns it (HitScene::world), and the HUD passes with their screen_loc
// destinations. Nothing here re-derives a position, plane or layer, so the hit test cannot drift from the pixels.
// Collection is additive: the DrawList the executor consumes is byte-identical with or without it.
//
// Rules, as stock DreamSeeker 516 applies them:
//   mouse_opacity 0   the atom is skipped entirely, overlays included (an overlay's own value is ignored).
//   mouse_opacity 1   (default) a target only where the source sprite pixel's alpha is not 0. atom.alpha, colour and
//                     tint are not part of the test.
//   mouse_opacity 2   a target over the sprite's whole rectangle, transparent pixels included.
//   draw order        topmost drawn wins: planes ascending, then layer, then creation order (the order of HitScene::world).
//                     An overlay answers for its parent atom.
//   HUD               client.screen atoms are hit-tested in the order they are drawn: by plane, then layer, then
//                     client.screen order (later on top). Against the map they sit by plane: a HUD atom is above every
//                     map layer of its own plane and below every map layer of a higher one. So a mouse_opacity 2 HUD
//                     item takes its whole rectangle over the slot background beneath it, and over the map beneath its
//                     plane. The full-screen click catcher (mouse_opacity 2 on plane -99) answers only where no map layer
//                     of plane >= -99 covers the pixel. An item at layer 2 over its background at layer 1 answers at a pixel
//                     the background paints, whichever joined client.screen first; a background at the higher layer still
//                     answers where it paints and the item's rectangle where it does not; at equal layers the later one
//                     is on top; plane beats layer. A HUD atom's location is NULL.
//   nothing covers    no hit, so no mouse frame at all.
//   icon-x/icon-y     relative to the named atom's own icon frame (HitFrame), 1-based, with icon-y counting up from the
//                     bottom row. Through an atom.transform the cursor is mapped back into the untransformed icon. Over an
//                     overlay it is still the atom's frame and is not clamped.
//   vis-x/vis-y       the cursor within the bounding box of the transformed icon (centred on the icon's centre plus the
//                     translation, half extents floored), sent only when it differs from icon-x/icon-y: for a scale, rotate,
//                     flip or skew, never for a pure translation, a pixel offset or an overlay, and never when either half
//                     is the 0x8000 "unset" value. Sub-pixel rule (hittest.cpp frameCoords): the cursor is the pixel's
//                     top-left corner and every coordinate is floored in +y-down image space. So Turn(90) gives
//                     vis-x = icon-y + 1, vis-y = 33 - icon-x, and its leftmost box column (icon-y 0) is not the atom: the
//                     turf beneath answers.
//   vis_contents      a child is named itself, in its own frame (a child at pixel_x 16 has icon-x 23), at its holder's cell (a
//                     map atom shown in another atom's vis_contents is located at the holder, not its own loc). It is drawn
//                     and named through the holder's transform about its own centre.
//   location          the named atom's own cell, not the cursor's.
//   plane master      a PLANE_MASTER at mouse_opacity 0 hides everything drawn on its plane (per placed layer: an
//                     overlay on that plane drops out, while its atom's own icon on another plane still answers). The
//                     layers never reach HitScene::world (scene.cpp MasterRec::hideFromMouse). A master at
//                     mouse_opacity 2 (takes every click off its plane) is not modelled.
//   maptext           never takes a click by its ink. An obj's own maptext, or a client.image's, answers with the turf
//                     beneath at mouse_opacity 1. At the atom's mouse_opacity 2 its whole maptext box is part of its
//                     rectangle. A maptext link (<a href>) is not modelled.
//   client.images     an image's pixels answer for its parent (HitAtom = the parent's: its name, location and
//                     mouse_opacity; the image's own mouse_opacity and alpha are ignored), named in the image's own icon
//                     frame. An override image replaces the parent's shape (scene.cpp imagesOn).
//
// Not modelled (the scene stats count these): HUD atoms ordered against each other by an overlay's plane (each atom is
// one unit at its own plane/layer; against the map each layer goes by its own plane), a plane master's mouse_opacity 2,
// its rule for client.screen objects on its plane, slate/relay folding, render_source relays, maptext links (<a href>),
// animated appearances' mid-tween positions, and an image on a parent at mouse_opacity 2 (whether the image's rectangle
// joins the parent's).
// A HUD object is hit over everything it draws: a sprite larger than a tile, its pixel offsets and its attached client.images
// are not clipped to the screen_loc cell. icon-x/icon-y count from the atom's own icon, so an overlay drawn 24 px east of it
// answers icon-x 25..56.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "render/ir.h"
#include "render/xform.h"

namespace fugue::render {

constexpr int kMouseOpacityNone = 0, kMouseOpacityIcon = 1, kMouseOpacityBBox = 2;
// Atom kinds, numbered like the wire's ref kinds (proto::enc::RefKind) minus null.
constexpr uint8_t kAtomTurf = 0, kAtomObj = 1, kAtomMob = 2;

struct HitAtom {
    std::string name;            // atom.name off the 0x0026 appearance, text-macro markers already stripped (scene.cpp)
    int mouseOpacity = 1;        // decoded: ((flags >> 12) + 1) & 3
    uint8_t kind = kAtomTurf;    // kAtomTurf = no movable: the caller names the turf under the cursor
    uint32_t ref = 0;            // obj/mob ref (registry bit stripped)
    int worldX = 0, worldY = 0;  // the cell it is drawn on (1-based); 0,0 for a screen object
    bool screen = false;
    uint32_t appid = 0;          // the appearance it is drawn from (the right-click menu's icon: render::appearanceIcon)
    int dir = -1;                // the facing it is drawn with; -1 = the appearance's own
};

// An atom's ICON FRAME in map-image pixels: its untransformed icon rectangle where the atom is anchored (pixel offsets
// applied), and the atom.transform it is drawn through (image space, render/xform.h). Every layer of one atom, overlays and
// underlays included, carries the frame of that atom's own icon, because stock DreamSeeker reports a click in that frame.
// w == 0: no frame (a client.screen layer; the sprite rectangle is used).
struct HitFrame {
    int x = 0, y = 0, w = 0, h = 0;
    bool hasXf = false;
    Mat m;
};

struct HitLayer {
    SpritePtr sprite;
    int x = 0, y = 0;            // top-left in the map image (HUD: inside the pass canvas)
    int atom = -1;               // index into HitScene::atoms
    HitFrame frame;              // map layers only
    bool own = false;            // the atom's OWN icon (not one of its overlays/underlays): see hittest.cpp covers()
    double plane = 0;            // the (resolved) plane it is drawn on: HUD and map layers meet by it (hitAt)
    bool maptext = false;        // a maptext box: answers only under mouse_opacity 2, never by its ink (see "maptext" above)
};

struct HitHud {
    int atom = -1;
    double plane = 0, layer = 0;                  // the screen atom's resolved plane and layer: its hit-test order
    int tileW = 0, tileH = 0;                     // the hit canvas: the cell joined with every layer
    int originX = 0, originY = 0;                 // the atom's own icon's bottom-left: icon-x/icon-y count from it
    std::vector<HitLayer> layers;                 // draw order, canvas coordinates, each on its own resolved plane
    struct Dest { int x, y; };
    std::vector<Dest> dests;                      // canvas top-left; screen_loc range form repeats it
};

// Everything a pointer question needs, immutable once built (share it across threads via shared_ptr).
struct HitScene {
    int tileW = 32, tileH = 32;
    int cols = 0, rows = 0;                       // viewport extent in tiles
    int originX = 0, originY = 0;                 // 1-based world coords of the viewport's BOTTOM-LEFT tile
    std::vector<HitAtom> atoms;
    std::vector<HitLayer> world;                  // every placed map layer, in DRAW order (last = topmost)
    std::vector<HitHud> hud;                      // client.screen passes, client.screen order (hitAt sorts by plane/layer)
    int pixW() const { return cols * tileW; }
    int pixH() const { return rows * tileH; }
};

struct Hit {
    std::string name;
    uint8_t kind = kAtomTurf;
    uint32_t ref = 0;
    int worldX = 0, worldY = 0;
    bool screen = false;
    // The WINNING SPRITE's rectangle in map-image pixels (top-left origin).
    int spriteX = 0, spriteY = 0, spriteW = 0, spriteH = 0;
    int atom = -1;               // index into HitScene::atoms when known (hitStack sets it), else -1
    uint32_t appid = 0;          // HitAtom::appid / dir of the named atom (map AND screen atoms)
    int dir = -1;
    HitFrame frame;              // the winning layer's frame (HitLayer::frame)
};

// DreamSeeker's right-click STACK: every atom on the clicked TILE, topmost first, with a client.screen atom under the
// cursor leading (map atoms are scoped to the tile; HUD atoms are scoped to the pixel). mouse_opacity 0 is skipped.
std::vector<Hit> hitStack(const HitScene& s, int px, int py);

// The atom drawn at map-image pixel (px,py), or false when no sprite answers there.
bool hitAt(const HitScene& s, int px, int py, Hit& out);

// What a mouse command at that pixel names. overKind: 1 obj, 2 mob, 3 turf (the wire's RefKind numbering).
struct Naming {
    uint8_t overKind = 3;
    uint32_t overRef = 0;                 // obj/mob
    int overX = 0, overY = 0;             // turf, 1-based world
    bool locNull = false;
    int locX = 0, locY = 0;               // turf, 1-based world
    int iconX = 1, iconY = 1;             // within the NAMED ATOM's icon, before its transform (unclamped, on the wire too)
    int visX = 1, visY = 1;               // within the transformed icon's bounding box (DreamSeeker's vis-x/vis-y)
    bool hasVis = false;                  // vis != icon: the only case DreamSeeker sends vis-x/vis-y
    int screenX = 1, screenY = 1;         // 1-based viewport cell
    int screenPX = 1, screenPY = 1;       // pixel within that cell, 1-based, lower-left origin
    Hit hit;
};
// false: pixel outside the image, unusable geometry, or no sprite covers it (=> send nothing).
bool nameMouse(const HitScene& s, int px, int py, Naming& out);

// Test-only hook; 0 is the shipped behaviour.
void setHitMutation(int m);

}  // namespace fugue::render
