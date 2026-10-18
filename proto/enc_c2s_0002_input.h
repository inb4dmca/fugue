// c2s 0x0002: the unified input command (sub-typed by body[0]) and c2s 0x0081 Topic. ENCODERS only.
//
// Byte layouts follow stock DreamSeeker 516. Subs: 0 move, 2 verb, 3-6 mouse, 7 extended mouse.
// Bodies are PLAINTEXT; net::Session body-ciphers them with K_login.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "net/bytes.h"

namespace fugue::proto::enc {

constexpr uint16_t kClientCmd = 0x0002;
constexpr uint16_t kTopicMsg = 0x0081;

// 0x0002 mouse subs
constexpr uint8_t kMouseClick = 0x03, kMouseDblClick = 0x04, kMouseUp = 0x05, kMouseDown = 0x06, kMouseExt = 0x07;
constexpr uint16_t kMouseEventDrop = 0x0092, kMouseEventDrag = 0x0093, kMouseEventWheel = 0x010a;
constexpr uint8_t kVerbSrcMob = 0x02, kVerbSrcClient = 0x08;

enum RefKind : uint8_t { RefNull = 0, RefObj = 1, RefMob = 2, RefTurf = 3, RefTurfId = 7 };   // RefTurfId: [7][turf id u32] (encoded like obj/mob)

struct AtomRef {
    uint8_t kind = RefNull;
    uint32_t ref = 0x0000ffff;  // RefObj/RefMob/RefTurfId/RefNull payload
    uint16_t x = 0, y = 0;      // RefTurf: WIRE coordinates (DM coordinate minus 1)
};
AtomRef nullRef();
AtomRef objRef(uint32_t ref);
AtomRef mobRef(uint32_t ref);
AtomRef turfRef(int worldX, int worldY);   // 1-based DM coordinates, clamped to >= 1
Bytes encodeAtomRef(const AtomRef&);        // 5 bytes: [kind][u32 LE] or [3][x u16][y u16]

struct Mods { bool ctrl = false, shift = false, alt = false; };

struct ClickCmd {
    uint8_t sub = kMouseClick;
    AtomRef over, loc = nullRef();
    std::string control = "mapwindow.map";
    int iconX = 16, iconY = 16;      // in the named atom's icon frame, sent unclamped (can be < 1 or > 32 over an overlay)
    // vis-x/vis-y: the cursor in a TRANSFORMED atom's bounding box (render/hittest.h). Sent, unclamped, right after
    // icon-y only when hasVis - stock DreamSeeker's rule (vis pair != icon pair).
    int visX = 0, visY = 0;
    bool hasVis = false;
    int screenX = 1, screenY = 1;    // 1-based viewport tile column/row
    int screenPX = 0, screenPY = 0;  // 0 = use icon px
    std::string button = "left";
    Mods mods;
};
std::string clickParams(const ClickCmd&);
Bytes encodeMouseCmd(const ClickCmd&);   // [sub 00 ff ff][over][03 00][loc][02 ctrl\0][02 params\0]
ClickCmd turfClick(int worldX, int worldY, int screenCol, int screenRow, int iconX, int iconY, Mods = {});

// A click on an INFO-control (stat panel) row, not the map shape. Stock DreamSeeker 516 sends:
//   [sub 03 click | 04 dblclick][00 ff ff][over 5B][03 = three values][02 panel\0][02 control\0][02 params\0]
// i.e. Click(location = the stat PANEL'S NAME, control = the INFO control's id, params). The map click's `03 00` after `over` is
// the same value count followed by a tag-0 (atom) location. A turf row's `over` is [07][the turf's id] (its \ref low bits; the
// head the 0x006a row carried), not the map's [03][x][y]. params has NO screen-loc and NO icon-x/icon-y unless the row DRAWS an
// icon (see infoClickParams); a click on the row's label, or past its text, sends nothing.
Bytes encodeInfoClick(uint8_t sub, const AtomRef& over, const std::string& panel, const std::string& control, const std::string& params);
// "left=1;button=left", with modifiers between the two in this order: "left=1;ctrl=1;shift=1;button=left",
// "left=1;alt=1;button=left", "middle=1;button=middle". When the row shows an icon (icon = true), stock prefixes
// "icon-x=X;icon-y=Y;" with the modifiers unmoved. X/Y count in the row's icon CELL (world icon size), not the icon's own
// pixels: x+1 from the cell's left edge, y+1 from its bottom edge, unclamped. A row with nothing drawn in its cell
// (no icon, no overlay) sends neither.
std::string infoClickParams(const std::string& button, const Mods&, bool icon = false, int iconX = 0, int iconY = 0);

// Which mouse events the world subscribed to (0x003b FormatFlags word, decoded by net/input.h).
struct MouseHooks { bool down = false, up = false, click = false, dblClick = false, drag = false, drop = false; };
MouseHooks allMouseHooks();
std::vector<Bytes> pressFrames(ClickCmd, const MouseHooks&);     // MouseDown iff subscribed
std::vector<Bytes> releaseFrames(ClickCmd, const MouseHooks&);  // MouseUp then Click, each iff subscribed
std::vector<Bytes> clickSequence(ClickCmd, const MouseHooks&);   // Down, Up, Click

struct DragCmd {
    bool drop = false;
    AtomRef src, srcLoc = nullRef(), over = nullRef(), overLoc = nullRef();
    std::string srcControl = "mapwindow.map", overControl = "mapwindow.map", params;
};
Bytes encodeDragCmd(const DragCmd&);   // sub 0x07, event 0x93 (drag) / 0x92 (drop), 7 values

// MouseWheel(object, delta_x, delta_y, location, control, params), as stock DreamSeeker 516 sends one per wheel notch:
//   [07 00][event 0a 01][00 ff ff 00 00][06][00 over][01 delta_x f32][01 delta_y f32][02 location\0][02 control\0][02 params\0]
// over is null (00 ffff0000) when nothing is under the pointer. location is the map control's own id (no window
// prefix), control its full id. params: icon-x/icon-y (only when an atom is named), the held button
// ("left=1;button=left"), the modifiers, then screen-loc prefixed with the map id ("screen-loc=map:8:16,7:16").
struct WheelCmd {
    AtomRef over = nullRef();
    float dx = 0, dy = 0;
    std::string location = "map", control = "mapwindow.map", params;
};
Bytes encodeWheelCmd(const WheelCmd&);

Bytes encodeMove(uint8_t dir);         // sub 0: 00 00 dir 00 00 ff ff 00 00 00

struct VerbArg {
    uint8_t tag = 0x02;                // 0 ref, 1 number (float32 LE), 2 string
    uint8_t kind = RefObj; uint32_t ref = 0;
    float num = 0;
    std::string str;
};
Bytes encodeVerb(uint16_t verbId, uint8_t srcKind, uint32_t srcRef, const std::vector<VerbArg>& args);
// Movement verb call: args {dir} or {dir, state} (state only when the verb declares 2 args), src-kind client, src-ref 0
// (what stock DreamSeeker sends for a /client/verb; the srcRef parameter is ignored).
Bytes encodeMoveVerb(uint16_t verbId, uint32_t srcRef, int dir, bool down, bool twoArgs);

Bytes encodeTopic(const std::string& href);   // strips "byond://", ensures leading '?', NUL-terminated

}  // namespace fugue::proto::enc
