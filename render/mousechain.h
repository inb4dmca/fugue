// The mouse CHAIN: pointer press / motion / release on the map image -> the 0x0002 frames a stock DreamSeeker
// sends. Pure state machine (no Qt, no socket, no clock of its own): the caller supplies timestamps and the
// scene, and sends the returned bodies on the SESSION thread.
//
// Rules, as stock DreamSeeker 516 sends them:
//   press    MouseDown iff the world subscribed (hooks.down). If this press is the second half of a double-click
//            (same button, within kDblWindowMs of the previous press, within kDragThresholdPx of it), ONE extra
//            MouseDblClick frame follows the MouseDown - only if the world declared DblClick. An undeclared
//            DblClick is just a second ordinary click.
//   motion   while a button is held: nothing until the pointer is kDragThresholdPx away from the press (a smaller
//            movement is still a click, as stock DreamSeeker treats a pixel or two of jitter);
//            after that, one MouseDrag (sub 7, event 0x93) per motion IFF hooks.drag, naming the CURRENT over.
//   release  THREE outcomes: (1) no real movement -> MouseUp then Click (each iff subscribed);
//            (2) real drag, world wants MouseDrop -> MouseUp then MouseDrop, NO Click;
//            (3) real drag, world wants no drop -> MouseUp ALONE (DreamSeeker never falls back to Click).
//   buttons  left/middle always; the RIGHT button reaches the wire only if the map control declares
//            `right-click = true`, otherwise DreamSeeker answers it locally (verb popup) and sends nothing.
//   nothing under the pointer (hitAt fails): no frame (DreamSeeker sends none).
//   wheel    one MouseWheel frame per notch (wheelFrames), iff the world declares client/MouseWheel (anywhere over the
//            map, the letterbox included, naming a null object when nothing is hit) or the atom under the pointer
//            carries the wheel flag (HitAtom::wheel). Not gated by the button hooks.
// Deviation from DreamSeeker's drop path: the MouseUp that ends a drag is gated on hooks.up like every other MouseUp.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "net/bytes.h"
#include "proto/enc_c2s_0002_input.h"
#include "render/hittest.h"

namespace fugue::render {

struct PointerEvent {
    int imgX = 0, imgY = 0;        // pixel inside the rendered map image (top-left origin) - what the hit test takes
    int winX = 0, winY = 0;        // window pixels - only differences matter (drag threshold, double-click radius)
    int button = 1;                // X11 numbering: 1 left, 2 middle, 3 right
    proto::enc::Mods mods;
    int64_t tMs = 0;               // any monotonic millisecond clock
};

struct ChainEnv {
    const HitScene* scene = nullptr;
    proto::enc::MouseHooks hooks = proto::enc::allMouseHooks();
    bool rightClickToWire = false;
    std::string control = "mapwindow.map";
    bool clientWheel = true;   // the 0x003b second flags word's client/MouseWheel bit (true until 0x003b arrives)
};

// "left" / "middle" / "right", or "" for the wheel and unknown buttons.
const char* buttonName(int x11Button);
// Naming -> the encoder's command (button/mods/control filled in).
proto::enc::ClickCmd clickCmdFrom(const Naming& n, int x11Button, const proto::enc::Mods& mods, const std::string& control);

// A wheel notch at e (imgX/imgY may lie outside the image: over the letterbox the cell is extrapolated, as stock does).
// e.button is the button held during the notch (0 none); deltaY > 0 is away from the user (stock: +120 per notch).
std::vector<Bytes> wheelFrames(const PointerEvent& e, const ChainEnv& env, int deltaX, int deltaY);

class MouseChain {
public:
    static constexpr int kDblWindowMs = 400;
    static constexpr int kDragThresholdPx = 4;

    // True when this button would go on the wire at all (false: right button without right-click=true).
    static bool wired(int button, const ChainEnv& env) { return button == 3 ? env.rightClickToWire : (button == 1 || button == 2); }

    std::vector<Bytes> press(const PointerEvent& e, const ChainEnv& env);
    std::vector<Bytes> move(const PointerEvent& e, const ChainEnv& env);
    std::vector<Bytes> release(const PointerEvent& e, const ChainEnv& env);

    bool held() const { return held_; }
    bool dragging() const { return dragging_; }
    // What the last call decided, for logs and tests: "down" "dbl" "drag" "click" "drop" "move-only" "idle" "nohit" "unwired".
    const std::string& note() const { return note_; }

private:
    bool held_ = false, dragging_ = false;  // dragging_: the pointer left the press radius
    int button_ = 0, pressWinX_ = 0, pressWinY_ = 0;
    // Drag state: the pressed target, captured at press.
    bool dragActive_ = false, dragMoved_ = false;
    proto::enc::AtomRef src_, srcLoc_;
    std::string srcControl_;
    // Double-click detector: last press position, button and time.
    bool haveLast_ = false;
    int lastBtn_ = 0, lastWinX_ = 0, lastWinY_ = 0;
    int64_t lastAt_ = 0;
    std::string note_;
};

}  // namespace fugue::render
