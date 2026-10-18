#include "render/mousechain.h"

namespace fugue::render {
namespace enc = proto::enc;

const char* buttonName(int b) { return b == 1 ? "left" : b == 2 ? "middle" : b == 3 ? "right" : ""; }

enc::ClickCmd clickCmdFrom(const Naming& n, int x11Button, const enc::Mods& mods, const std::string& control) {
    enc::ClickCmd c;
    c.sub = enc::kMouseClick;
    c.over = n.overKind == 3 ? enc::turfRef(n.overX, n.overY) : n.overKind == 2 ? enc::mobRef(n.overRef) : enc::objRef(n.overRef);
    c.loc = n.locNull ? enc::nullRef() : enc::turfRef(n.locX, n.locY);
    c.control = control;
    c.iconX = n.iconX; c.iconY = n.iconY;
    c.visX = n.visX; c.visY = n.visY; c.hasVis = n.hasVis;
    c.screenX = n.screenX; c.screenY = n.screenY;
    c.screenPX = n.screenPX; c.screenPY = n.screenPY;
    c.button = buttonName(x11Button);
    c.mods = mods;
    return c;
}

namespace {
int64_t distSq(int ax, int ay, int bx, int by) { return int64_t(ax - bx) * (ax - bx) + int64_t(ay - by) * (ay - by); }

bool commandAt(const PointerEvent& e, const ChainEnv& env, enc::ClickCmd& cmd) {
    if (!env.scene) return false;
    Naming n;
    if (!nameMouse(*env.scene, e.imgX, e.imgY, n)) return false;
    cmd = clickCmdFrom(n, e.button, e.mods, env.control);
    return true;
}
int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
}  // namespace

std::vector<Bytes> wheelFrames(const PointerEvent& e, const ChainEnv& env, int deltaX, int deltaY) {
    std::vector<Bytes> out;
    if (!env.scene || (deltaX == 0 && deltaY == 0)) return out;
    const HitScene& s = *env.scene;
    if (s.cols <= 0 || s.rows <= 0 || s.tileW <= 0 || s.tileH <= 0) return out;
    Naming n;
    bool named = nameMouse(s, e.imgX, e.imgY, n);
    if (!env.clientWheel && !(named && n.hit.wheel)) return out;

    enc::WheelCmd w;
    w.dx = float(deltaX); w.dy = float(deltaY);
    w.control = env.control;
    size_t dot = env.control.rfind('.');
    w.location = dot == std::string::npos ? env.control : env.control.substr(dot + 1);
    std::string p;
    int sx, spx, sy, spy;
    if (named) {
        enc::ClickCmd c = clickCmdFrom(n, 0, e.mods, env.control);
        w.over = c.over;
        p = "icon-x=" + std::to_string(n.iconX) + ";icon-y=" + std::to_string(n.iconY) + ";";
        if (n.hasVis) p += "vis-x=" + std::to_string(n.visX) + ";vis-y=" + std::to_string(n.visY) + ";";
        sx = n.screenX; spx = n.screenPX; sy = n.screenY; spy = n.screenPY;
    } else {
        int fromBottom = s.pixH() - 1 - e.imgY;
        int tx = floorDiv(e.imgX, s.tileW), ty = floorDiv(fromBottom, s.tileH);
        sx = tx + 1; spx = e.imgX - tx * s.tileW + 1; sy = ty + 1; spy = fromBottom - ty * s.tileH + 1;
    }
    std::string btn = buttonName(e.button);
    if (!btn.empty()) p += btn + "=1;button=" + btn + ";";
    if (e.mods.ctrl) p += "ctrl=1;";
    if (e.mods.shift) p += "shift=1;";
    if (e.mods.alt) p += "alt=1;";
    p += "screen-loc=" + w.location + ":" + std::to_string(sx) + ":" + std::to_string(spx) + "," + std::to_string(sy) + ":" +
         std::to_string(spy);
    w.params = p;
    out.push_back(enc::encodeWheelCmd(w));
    return out;
}

std::vector<Bytes> MouseChain::press(const PointerEvent& e, const ChainEnv& env) {
    std::vector<Bytes> out;
    if (!wired(e.button, env)) { note_ = "unwired"; held_ = false; return out; }
    held_ = true; dragging_ = false; button_ = e.button; pressWinX_ = e.winX; pressWinY_ = e.winY;
    dragActive_ = dragMoved_ = false;

    // The double-click detector is window-pixel + button + time, independent of the hit.
    bool dbl = false;
    if (haveLast_ && e.button == lastBtn_ && e.tMs - lastAt_ <= kDblWindowMs && e.tMs >= lastAt_ &&
        distSq(e.winX, e.winY, lastWinX_, lastWinY_) < int64_t(kDragThresholdPx) * kDragThresholdPx) {
        dbl = true;
        haveLast_ = false;   // consumed: a third rapid press starts a fresh pair
    } else {
        haveLast_ = true; lastAt_ = e.tMs; lastWinX_ = e.winX; lastWinY_ = e.winY; lastBtn_ = e.button;
    }

    enc::ClickCmd cmd;
    if (!commandAt(e, env, cmd)) { note_ = "nohit"; return out; }
    dragActive_ = true; src_ = cmd.over; srcLoc_ = cmd.loc; srcControl_ = cmd.control;
    out = enc::pressFrames(cmd, env.hooks);
    note_ = out.empty() ? "down(unsubscribed)" : "down";
    if (dbl && env.hooks.dblClick) {
        cmd.sub = enc::kMouseDblClick;
        out.push_back(enc::encodeMouseCmd(cmd));
        note_ = "down+dbl";
    }
    return out;
}

std::vector<Bytes> MouseChain::move(const PointerEvent& e, const ChainEnv& env) {
    std::vector<Bytes> out;
    if (!held_) { note_ = "idle"; return out; }
    if (!dragging_) {
        if (distSq(e.winX, e.winY, pressWinX_, pressWinY_) < int64_t(kDragThresholdPx) * kDragThresholdPx) { note_ = "idle"; return out; }
        dragging_ = true;
    }
    if (!dragActive_) { note_ = "nohit"; return out; }
    dragMoved_ = true;  // a drag is marked whether or not drags are subscribed
    if (!env.hooks.drag) { note_ = "drag(unsubscribed)"; return out; }
    enc::ClickCmd cmd;
    PointerEvent pe = e; pe.button = button_;
    if (!commandAt(pe, env, cmd)) { note_ = "drag(nohit)"; return out; }
    enc::DragCmd d;
    d.drop = false; d.src = src_; d.srcLoc = srcLoc_; d.over = cmd.over; d.overLoc = cmd.loc;
    d.srcControl = srcControl_; d.overControl = cmd.control; d.params = enc::clickParams(cmd);
    out.push_back(enc::encodeDragCmd(d));
    note_ = "drag";
    return out;
}

std::vector<Bytes> MouseChain::release(const PointerEvent& e, const ChainEnv& env) {
    std::vector<Bytes> out;
    if (!held_) { note_ = "idle"; return out; }
    bool active = dragActive_, moved = dragMoved_;
    enc::AtomRef src = src_, srcLoc = srcLoc_;
    std::string srcCtl = srcControl_;
    int btn = button_;
    held_ = dragging_ = dragActive_ = dragMoved_ = false;
    PointerEvent pe = e; pe.button = btn;
    enc::ClickCmd cmd;
    if (!commandAt(pe, env, cmd)) { note_ = "nohit"; return out; }

    if (active && moved) {
        if (env.hooks.up) { cmd.sub = enc::kMouseUp; out.push_back(enc::encodeMouseCmd(cmd)); }
        if (env.hooks.drop) {
            enc::DragCmd d;
            d.drop = true; d.src = src; d.srcLoc = srcLoc; d.over = cmd.over; d.overLoc = cmd.loc;
            d.srcControl = srcCtl; d.overControl = cmd.control; d.params = enc::clickParams(cmd);
            out.push_back(enc::encodeDragCmd(d));
            note_ = "drop";
        } else {
            note_ = "move-only";
        }
        return out;
    }
    out = enc::releaseFrames(cmd, env.hooks);
    note_ = "click";
    return out;
}

}  // namespace fugue::render
