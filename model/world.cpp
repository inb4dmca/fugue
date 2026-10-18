// Session::ingest: the world model's frame handlers.
//
// The wire is parsed by the proto/ decoders; this file holds the model rules, i.e. what a decoded record does to the world.
#include <cstdlib>
#include <cstring>

#include "model/gomath.h"
#include "model/htmltext.h"
#include "model/resync.h"
#include "model/world.h"
#include "net/textmacro.h"
#include "proto/g2_mapview.h"

namespace fugue::model {

namespace {

// ---- small string helpers

bool endsWith(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

// Window name of a browse() params string.
bool startsPair(std::string_view s) {
    for (char c : s) {
        if (c == '=') return true;
        if (c == ';' || c == '&') return false;
    }
    return false;
}

std::string trimAscii(std::string_view s) {
    size_t a = 0, b = s.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; };
    while (a < b && sp(s[a])) a++;
    while (b > a && sp(s[b - 1])) b--;
    return std::string(s.substr(a, b - a));
}

std::string browserWindowName(const std::string& params) {
    std::string window;
    std::vector<std::string_view> parts;
    size_t start = 0;
    std::string_view s = params;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == ';' || c == '&') {
        } else if (c == ',') {
            if (!startsPair(s.substr(i + 1))) continue;
        } else continue;
        parts.push_back(s.substr(start, i - start));
        start = i + 1;
    }
    parts.push_back(s.substr(start));
    for (auto part : parts) {
        std::string p = trimAscii(part);
        if (p.empty()) continue;
        size_t eq = p.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trimAscii(std::string_view(p).substr(0, eq));
        for (auto& ch : k) {
            if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
            else if (ch == '_') ch = '-';
        }
        if (k == "window") window = trimAscii(std::string_view(p).substr(eq + 1));
    }
    return window;
}

// Splits an output control into its window and page function.
bool splitBrowserOutput(const std::string& control, std::string& window, std::string& fn) {
    window.clear(); fn.clear();
    size_t colon = control.find(':');
    std::string ctrl = colon == std::string::npos ? control : control.substr(0, colon);
    if (colon == std::string::npos) {
        if (!endsWith(ctrl, ".browser")) return false;
        window = ctrl.substr(0, ctrl.size() - 8);
        return !window.empty();
    }
    fn = control.substr(colon + 1);
    if (fn.empty() || fn == "output") { fn.clear(); return false; }   // ":output" is chat (model/browsing.h)
    size_t dot = ctrl.rfind('.');
    window = dot == std::string::npos ? ctrl : ctrl.substr(0, dot);
    if (window.empty()) { fn.clear(); return false; }
    return true;
}

// The window a browser control belongs to.
bool browserControlWindow(std::string control, std::string& window) {
    size_t colon = control.find(':');
    if (colon != std::string::npos) control.resize(colon);
    size_t dot = control.rfind('.');
    if (dot == std::string::npos || control.compare(dot + 1, std::string::npos, "browser") != 0) return false;
    window = control.substr(0, dot);
    return true;
}

// Text for a session-end reason. The reason codes are BYOND's; the wording is the model's own.
std::string sessionEndText(uint16_t reason, uint32_t version) {
    static const char* t[12] = {
        "the server closed the connection",
        "the connection died",
        "the server killed the connection",
        "another connection has been made using your login key -- this session has been terminated",
        "the server's DreamDaemon is too far out of date",
        "this client is too far out of date",
        "your BYOND key is corrupt -- to restore it, log in again while connected to the Internet",
        "your BYOND key has expired -- to renew it, log in again while connected to the Internet",
        "the connection timed out",
        "failed to get a security certificate from BYOND Central -- your account password may have changed, or the host has a problem",
        "access denied -- the host refused this login (a guest ban, a key ban, or a whitelist)",
        "the server killed the connection after a network read error"};
    if (reason >= 12) return "the server is closing the connection (reason " + std::to_string(reason) + ")";
    std::string line = t[reason];
    if ((reason == 4 || reason == 5) && version != 0) line += " -- version " + std::to_string(version) + " is required";
    return line;
}

float bitsToFloat(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }

int64_t tickNs(const World& w) { return w.tickNanos > 0 ? w.tickNanos : kDefaultTickNanos; }   // tick duration, or the default when unset

// Glide size and long-glide flag, read off the appearance the atom holds (Pos is processed before the same frame's
// Appearance array, which re-times a glide that frame armed).
float glideSizeOf(const World& w, uint32_t appid) {
    auto it = w.appearances.find(appid);
    if (it == w.appearances.end() || !it->second.hasGlideSize) return 0;
    float g = bitsToFloat(it->second.glideSizeBits);
    return g > 0 ? g : 0;
}
bool longGlideOf(const World& w, uint32_t appid) {
    auto it = w.appearances.find(appid);
    return it != w.appearances.end() && it->second.longGlide;
}

}  // namespace

// The world's icon size.
int iconSizeOf(const World& w) {
    float s = bitsToFloat(w.stepSizeBits);
    return s > 0 ? int(s) : kDefaultIconSize;
}

static int g_modelMutation = 0;
void setModelMutation(int m) { g_modelMutation = m; }
static bool altRecordModel() { return g_modelMutation == 11; }

std::vector<AnimSeq> animSequences(const Atom::Chain& c) {
    std::vector<AnimSeq> out;
    for (size_t i = 0; i < c.steps.size(); i++) {
        const Atom::AnimStep& st = c.steps[i];
        if (i == 0 || st.startNs != 0) {
            AnimSeq q;
            q.first = i;
            q.startNs = st.startNs;
            q.head = (st.flags & 0x40000000u) != 0;
            q.parallel = (st.flags & 0x4u) != 0;
            out.push_back(q);
        }
        AnimSeq& q = out.back();
        q.last = i;
        q.durNs += int64_t(double(bitsToFloat(st.timeBits)) * double(c.tickNs));
    }
    for (AnimSeq& q : out) {
        int32_t loop = c.steps[q.first].loop;
        q.endNs = loop < 1 ? -1 : q.startNs + int64_t(loop) * q.durNs;
    }
    return out;
}

// The eye scroll offset. X is negated, Y is not: the image y-flip already lives in placement, so flipping it here too
// would move the sprite a whole tile on a northward step.
void eyeScrollOffset(const World& w, int64_t nowNs, int& sx, int& sy) {
    sx = sy = 0;
    const GlideMotion& g = w.eyeGlide;
    if (!w.eyeGliding || nowNs == 0 || g.startNs == 0) return;
    double rem = glideLeft(g, nowNs);
    if (rem <= 0) return;
    int icon = iconSizeOf(w);
    sx = -int(std::lround(g.sx * icon * rem));
    sy = int(std::lround(g.sy * icon * rem));
}

// Glide duration: distance in tiles (Chebyshev with long glide, else Euclidean) over the glide size, times the tick.
int64_t glideDurationNs(int dx, int dy, float glidePx, int iconSize, int64_t tick, bool longGlide) {
    if (iconSize <= 0) iconSize = kDefaultIconSize;
    if (tick <= 0) tick = kDefaultTickNanos;
    if (glidePx <= 0) glidePx = float(iconSize);
    double ax = std::fabs(double(dx)), ay = std::fabs(double(dy));
    double dist = (longGlide && g_modelMutation != 9) ? std::fmax(ax, ay) : gomath::hypot(ax, ay);
    double ticks = dist * double(iconSize) / double(glidePx);
    if (ticks <= 0) return 0;
    return int64_t(ticks * double(tick));
}

// The stock client moves a drawn atom's offset toward its tile by glide_size per tick along the offset's own direction
// (stock DreamSeeker 516; the view's scroll the same way), so a new target redirects what
// is left. While the offset is at least a tile plus one step on either axis the step is scaled by (tiles left on the
// longer axis + 1/16). Here that is in closed form over u = base tiles travelled: the distance D obeys
// dD/du = -(c*D + 1/16) while c*D > 1, where c*D is the longer axis in tiles, then falls linearly. An offset of at most
// one tile per axis (every fresh one-tile move) has no catch-up phase, and its duration is glideDurationNs's.
GlideMotion glideMotion(double sx, double sy, float glidePx, int iconSize, int64_t tick, bool longGlide, int64_t startNs,
                        bool catchUp) {
    if (iconSize <= 0) iconSize = kDefaultIconSize;
    if (tick <= 0) tick = kDefaultTickNanos;
    if (glidePx <= 0) glidePx = float(iconSize);
    GlideMotion g;
    g.sx = sx; g.sy = sy; g.startNs = startNs;
    g.chebyshev = longGlide && g_modelMutation != 9;
    g.catchUp = catchUp;
    double ax = std::fabs(sx), ay = std::fabs(sy);
    double dist = g.chebyshev ? std::fmax(ax, ay) : gomath::hypot(ax, ay);
    if (dist <= 0) return g;
    g.rate = double(glidePx) / double(iconSize) / double(tick);
    double m = std::fmax(ax, ay), u = dist;
    if (catchUp && m > 1) {
        double c = m / dist;
        u = std::log((m + 1.0 / 16) / (1 + 1.0 / 16)) / c + 1 / c;
    }
    double ticks = u * double(iconSize) / double(glidePx);
    g.durNs = int64_t(ticks * double(tick));
    return g;
}

double glideLeft(const GlideMotion& g, int64_t nowNs) {
    if (g.durNs <= 0) return 0;
    int64_t el = nowNs - g.startNs;
    if (el <= 0) return 1;
    if (el >= g.durNs) return 0;
    double ax = std::fabs(g.sx), ay = std::fabs(g.sy), m = std::fmax(ax, ay);
    if (!g.catchUp || m <= 1) return 1 - double(el) / double(g.durNs);
    double dist = g.chebyshev ? m : gomath::hypot(ax, ay), c = m / dist;
    double u = double(el) * g.rate, u1 = std::log((m + 1.0 / 16) / (1 + 1.0 / 16)) / c;
    double d = u < u1 ? ((m + 1.0 / 16) * std::exp(-c * u) - 1.0 / 16) / c : 1 / c - (u - u1);
    return std::fmax(0, d / dist);
}

// The stock client's re-arm. Under TILE or PIXEL movement mode the move adds to the offset left, and an
// offset of two tiles on either axis is scaled back to two (the advancer). Under LEGACY mode an axis still moving the
// way it was drops what it had left, a turn that is not a reversal drops it all, and two tiles on either axis snaps.
bool armGlideOffset(const World& w, const GlideMotion* inFlight, int64_t nowNs, int mx, int my, uint8_t& lastDir,
                    double& sx, double& sy) {
    double rx = 0, ry = 0;
    if (inFlight && g_modelMutation != 30) {
        double left = glideLeft(*inFlight, nowNs);
        rx = inFlight->sx * left;
        ry = inFlight->sy * left;
    }
    uint8_t dir = uint8_t((mx < 0 ? 4 : mx > 0 ? 8 : 0) | (my < 0 ? 1 : my > 0 ? 2 : 0));
    bool legacy = w.movementMode == 0 && g_modelMutation != 31;
    if (legacy && (rx != 0 || ry != 0)) {
        if (rx * mx > 0) rx = 0;
        if (ry * my > 0) ry = 0;
        auto reverse = [](uint8_t d) { return uint8_t(((d & 3) ? (d & 3) ^ 3 : 0) | ((d & 12) ? (d & 12) ^ 12 : 0)); };
        if (dir != lastDir && dir != reverse(lastDir)) rx = ry = 0;
    }
    lastDir = dir;
    sx = rx + mx;
    sy = ry + my;
    double m = std::fmax(std::fabs(sx), std::fabs(sy));
    if (m >= kGlideMaxTiles && g_modelMutation != 31) {
        if (legacy) return false;
        sx *= kGlideMaxTiles / m;
        sy *= kGlideMaxTiles / m;
    }
    return true;
}

bool Session::decode(uint16_t typ, ByteSpan body, std::string& jsonOut) {
    const proto::DecoderInfo* d = proto::Registry::instance().find(proto::Dir::S2C, typ);
    if (!d) { decodeErrors_++; return false; }
    try {
        jsonOut = d->fn(st_, body);
        return true;
    } catch (const std::exception&) {
        decodeErrors_++;
        return false;
    }
}

// A new mob record is marked for an immediate c2s 0x000b and flagged as awaiting the server's 0x0009, which the
// resync sweep skips until it arrives.
static void markNewMob(World& w, uint32_t key) {
    w.resync.newMobs.insert(key & ~kRegistryA);
    w.atoms[key].pollAnswerDue = true;
}

// Loop order: New, Pos, Bind, Dir, Appearance, Pixel, Bounds, Step, VisContents, Particles. A1 and the off-view
// sentinel retract the atom's position; the high bit namespaces the registry.
// Record identity: the stock client keys every record by {id, Key}. A record for a held slot with a different Key tears
// the old atom down and builds a new one; a zero Key on either side is not compared. The teardown also arms the resync
// sweep. Without it, a reused slot would keep the dead atom's bounds, step and vis_contents. Every new record is marked
// for the immediate poll: a mob keeps the mark, an obj loses it to its own appearance binding. fresh = false is the
// resurrection guard (the record must already be held); true creates an absent id. id carries the registry bit.
Atom* Session::record(uint32_t id, uint16_t key, bool fresh) {
    World& w = w_;
    auto created = [&] {
        if (id & kRegistryA) markNewMob(w, id);
        else w.resync.newObjs.insert(id);
    };
    auto it = w.atoms.find(id);
    if (it == w.atoms.end()) {
        if (!fresh) return nullptr;
        Atom& a = w.atoms[id];
        a.key = key; a.haveKey = true;
        a.touchTick = w.resync.tick;
        created();
        return &a;
    }
    Atom& a = it->second;
    a.touchTick = w.resync.tick;
    if (w.resync.freed.erase(id)) {   // freed by the GC: rebuilt here, with no teardown
        a.bindOther = a.backlink = 0xffff;
        a.iconStartNs = 0;
        a.key = key; a.haveKey = true;
        created();
        return &a;
    }
    if (a.haveKey && a.key != key && a.key != 0 && key != 0 && g_modelMutation != 10 && !altRecordModel()) {
        a = Atom{};
        w.visContents.erase(id);
        w.visTurfs.erase(id);
        w.particles.erase(id);
        w.flicks.erase(id);
        w.resync.teardowns++;
        created();
    }
    a.key = key; a.haveKey = true;
    return &a;
}

// Stock DreamSeeker plays a non-looping icon state from its first frame when an atom, turf, image, screen object or stat
// row first shows it and again whenever the icon or icon_state changes; any other appearance change (dir, name, a moved
// screen_loc, an added overlay) keeps the running sequence, as do leaving and re-entering the view or the screen.
void Session::stampIcon(int64_t& startNs, uint32_t from, uint32_t to) const {
    bool changed = startNs == 0;
    if (!changed && from != to) {
        auto f = w_.appearances.find(from), t = w_.appearances.find(to);
        changed = f == w_.appearances.end() || t == w_.appearances.end() ||
                  f->second.iconResID != t->second.iconResID || f->second.iconState != t->second.iconState;
    }
    if (changed) startNs = ingestNs_;
}

void Session::bindAppearance(Atom& a, uint32_t appid) {
    stampIcon(a.iconStartNs, a.appid, appid);
    a.appid = appid;
}

void Session::setTile(const std::array<uint16_t, 3>& key, uint32_t appid) {
    w_.tiles[key] = appid;
    stampIcon(w_.tileIconStart[key], appid);
}

void Session::applyObjectArrays(const JVal& m, uint32_t reg) {
    World& w = w_;
    auto rec = [&](const JVal& r, bool fresh) { return record(reg | r["id"].u32(), r["key"].u16(), fresh); };
    for (const JVal& r : m["new"].a) {
        Atom* e = rec(r, true);   // an A1 on a held atom (same Key) retracts its location, keeps slot + appearance
        e->hasPos = false;
        e->bindOther = 0xffff;    // and its Bind
        e->x = e->y = e->z = 0;
        e->gliding = false;
    }
    for (const JVal& p : m["pos"].a) {
        int px = int(p["x"].i()), py = int(p["y"].i()), pz = int(p["z"].i());
        if (px == 0xffff && py == 0xffff) {   // OffView
            Atom* e = rec(p["inViewRec"], true);
            e->hasPos = false;
            e->x = e->y = e->z = 0;
            e->gliding = false;
            continue;
        }
        Atom& e = *rec(p["inViewRec"], true);
        int fx = e.x, fy = e.y, fz = e.z;
        bool hadPos = e.hasPos;
        e.x = px + 1; e.y = py + 1; e.z = pz; e.hasPos = true;
        // Arming a glide: presence only (the interpolation itself is render state).
        // A glide is armed for a one-tile (|dx|,|dy| <= GlideThresholdTiles = 1) same-z move of an atom
        // that already had a position; everything else (first sighting, z change, no move, a jump)
        // snaps, which clears it. Nothing else clears it but a retraction (A1 / off-view) or removal.
        // It starts from where the sprite is drawn (armGlideOffset).
        int dx = e.x - fx, dy = e.y - fy;
        bool glide = !(!hadPos || e.z != fz || (dx == 0 && dy == 0) || std::abs(dx) > kGlideThresholdTiles || std::abs(dy) > kGlideThresholdTiles);
        double sx = 0, sy = 0;
        if (glide) glide = armGlideOffset(w, e.gliding ? &e.glide : nullptr, ingestNs_, -dx, -dy, e.glideDir, sx, sy);
        if (glide)
            e.glide = glideMotion(sx, sy, glideSizeOf(w, e.appid), iconSizeOf(w), tickNs(w), longGlideOf(w, e.appid), ingestNs_,
                                  g_modelMutation != 31);
        e.gliding = glide;
    }
    for (const JVal& b : m["bind"].a) {
        if (Atom* e = rec(b["inViewRec"], false)) e->bindOther = b["other"].u32();   // read only by the resync sweep
    }
    for (const JVal& d : m["dir"].a) {
        Atom* e = rec(d["inViewRec"], false);
        if (!e) continue;   // resurrection guard
        e->dir = int(d["dir"].i());
    }
    for (const JVal& a : m["appearance"].a) {
        Atom* e = rec(a["inViewRec"], false);
        if (!e) continue;
        bindAppearance(*e, a["appearance"].u32());
        // The stock client reads the glide size off the atom's current appearance as the glide runs, so a glide this
        // frame armed takes the rate of the appearance the same frame binds.
        if (e->gliding && e->glide.startNs == ingestNs_)
            e->glide = glideMotion(e->glide.sx, e->glide.sy, glideSizeOf(w, e->appid), iconSizeOf(w), tickNs(w), longGlideOf(w, e->appid),
                                   ingestNs_, e->glide.catchUp);
        // The obj binder clears the poll mark; the mob binder does not. So an obj that arrives with its appearance is
        // never polled, and one that arrives without is polled at once.
        if (reg != kRegistryA) w.resync.newObjs.erase(reg | a["inViewRec"]["id"].u32());
    }
    for (const JVal& p : m["pixel"].a) {
        Atom* e = rec(p["inViewRec"], false);
        if (!e) continue;
        e->pixelX = int16_t(p["pixelX"].i()); e->pixelY = int16_t(p["pixelY"].i());
        e->pixelW = int16_t(p["pixelW"].i()); e->pixelZ = int16_t(p["pixelZ"].i());
    }
    for (const JVal& b : m["bounds"].a) {
        Atom* e = rec(b["inViewRec"], false);
        if (!e) continue;
        e->boundX = int16_t(b["boundX"].i()); e->boundY = int16_t(b["boundY"].i());
        e->boundW = int16_t(b["boundW"].i()); e->boundH = int16_t(b["boundH"].i());
        e->hasBounds = true;
    }
    for (const JVal& s : m["step"].a) {
        Atom* e = rec(s["inViewRec"], false);
        if (!e) continue;
        e->stepX = int16_t(s["stepX"].i()); e->stepY = int16_t(s["stepY"].i());
        e->stepSizeBits = s["extra"].u32();
        e->hasStep = true;
    }
    for (const JVal& vc : m["visContents"].a) {
        std::vector<uint32_t> ids;
        std::vector<uint32_t> turfs;
        for (const JVal& it : vc["items"].a)
            // An item's Type is the child's kind, not the frame's: 2 = obj, 3 = mob. Any other Type is unmodelled and skipped.
            if (it["type"].i() == 2) ids.push_back(it["value"].u32());
            else if (it["type"].i() == 3) ids.push_back(kRegistryA | it["value"].u32());
            else if (it["type"].i() == 1) turfs.push_back(it["value"].u32());   // a turf: World::visTurfs
        uint32_t pid = reg | vc["inViewRec"]["id"].u32();
        if (turfs.empty()) w.visTurfs.erase(pid); else w.visTurfs[pid] = std::move(turfs);
        if (Atom* a = rec(vc["inViewRec"], false)) a->visCount = int(ids.size());
        if (ids.empty()) { w.visContents.erase(pid); continue; }
        w.visContents[pid] = std::move(ids);
    }
    // The particle set: particleID 0xffff detaches it. Another particleID is another /particles
    // datum, which clears the live particles; the same one only updates the definition, and they carry on.
    for (const JVal& pr : m["particles"].a) {
        uint32_t key = reg | pr["inViewRec"]["id"].u32();
        rec(pr["inViewRec"], false);
        uint32_t pid = pr["particleID"].u32();
        if (pid == 0xffff) { w.particles.erase(key); continue; }
        auto fixed = hexDecode(pr["fixed"].s);
        if (!fixed) continue;
        std::vector<std::pair<uint16_t, Bytes>> gens;
        for (const JVal& g : pr["gens"].a) {
            Bytes b{uint8_t(g["type"].i())};
            if (auto pl = hexDecode(g["payload"].s)) b.insert(b.end(), pl->begin(), pl->end());
            gens.push_back({g["key"].u16(), std::move(b)});
        }
        ParticleSet& ps = w.particles[key];
        if (ps.pid != pid) ps.live.clear();
        ps.pid = pid;
        applyParticleBlock(ps, *fixed, gens);
    }
}

// Adds a chat line: both rings grow together, an all-empty line is dropped, the oldest
// lines fall off past the cap.
void Session::addChat(const std::string& plain, const std::string& markup) {
    World& w = w_;
    if (plain.empty() && markup.empty()) return;
    w.chat.push_back(net::plainText(plain));
    w.chatHtml.push_back(net::plainText(markup));
    w.chatTotal++;
    if (w.chat.size() > kChatCap) {
        size_t over = w.chat.size() - kChatCap;
        w.chat.erase(w.chat.begin(), w.chat.begin() + long(over));
        w.chatHtml.erase(w.chatHtml.begin(), w.chatHtml.begin() + long(over));
    }
}

void Session::ingest(uint16_t typ, ByteSpan body) {
    World& w = w_;
    std::string js;
    auto parsed = [&](JVal& out) {
        if (!decode(typ, body, js)) return false;
        try { out = parseJson(js); } catch (const std::exception&) { decodeErrors_++; return false; }
        return true;
    };
    JVal v;
    switch (typ) {
    case 0x0061: {
        // The 0x0061 map grid. The single-frame grid is stored (the Cells() fallback), then the refresh machine is driven.
        // The machine runs through decodeMapRefresh directly, since the registry decoder swallows the refresh result.
        if (body.size() < 6) return;
        MapGrid g;
        g.have = true;
        g.x0 = le16(&body[0]); g.y0 = le16(&body[2]); g.z = le16(&body[4]);
        size_t p = 6;
        uint32_t cur = 0;
        bool first = true;
        while (p < body.size()) {
            if (p + 4 > body.size()) return;   // truncated cell: returns before storing anything
            uint32_t c = le32(&body[p]);
            p += 4;
            if (c == 0xffffffffu) {
                if (p >= body.size()) return;
                unsigned run = unsigned(body[p]) + 1;
                if (first) run++;
                p++;
                for (unsigned i = 0; i < run; i++) g.cells.push_back(cur);
            } else { cur = c; g.cells.push_back(c); }
            first = false;
        }
        w.grid = std::move(g);
        w.resync.relinkObjs = w.resync.relinkMobs = true;   // the window reset arms both relinks
        w.resync.haveOrigin = true; w.resync.originX = w.grid.x0; w.resync.originY = w.grid.y0;
        try {
            JVal r = parseJson(proto::g2::decodeMapRefresh(st_, 0x61, body));
            uint16_t z = r["z"].u16();
            for (const JVal& c : r["cells"].a) setTile({c["x"].u16(), c["y"].u16(), z}, c["appid"].u32());
        } catch (const std::exception&) { w.mapViewErrs++; }
        return;
    }
    case 0x0012: case 0x0014: {
        if (!decode(typ, body, js)) { w.mapViewErrs++; return; }
        JVal r = parseJson(js);
        // The stock handler arms both relinks only when the window origin moves on either axis. 0x0061 always arms them.
        uint16_t ox = r["originX"].u16(), oy = r["originY"].u16();
        if (!w.resync.haveOrigin || ox != w.resync.originX || oy != w.resync.originY)
            w.resync.relinkObjs = w.resync.relinkMobs = true;
        w.resync.haveOrigin = true; w.resync.originX = ox; w.resync.originY = oy;
        uint16_t z = r["z"].u16();
        for (const JVal& c : r["cells"].a) setTile({c["x"].u16(), c["y"].u16(), z}, c["appid"].u32());
        return;
    }
    case 0x00f1: {
        // The remote turfs a vis_contents names (vg's camera console), sent just before the 0x0008 that sets the holder's
        // vis_contents: [count u16] count x [turf cell index u32] (World::visTurfs' index), then count cells of the RLE
        // stream used by 0x0012/0x0014 (ffffffff + u8 n repeats the last literal for n+1 cells) holding each turf's
        // appearance id (the space of `tiles`), then a second such stream of turf-registry indices (0x0020's space).
        // A trailing [count u16] count x [index u32][appid u32] table, as in 0x0020, is read when bytes remain.
        // Wide ids only. A malformed body changes nothing.
        size_t o = 0;
        auto u16 = [&](uint32_t& v) { if (o + 2 > body.size()) return false; v = uint32_t(body[o]) | uint32_t(body[o + 1]) << 8; o += 2; return true; };
        auto u32 = [&](uint32_t& v) { if (o + 4 > body.size()) return false; v = le32(&body[o]); o += 4; return true; };
        auto rle = [&](size_t n, std::vector<uint32_t>& out) {
            uint32_t last = 0xffff, cur = 0;
            unsigned run = 0;
            for (size_t i = 0; i < n; i++) {
                if (run > 0) { run--; out.push_back(cur); continue; }
                uint32_t v;
                if (!u32(v)) return false;
                if (v == 0xffffffffu) {
                    if (o >= body.size()) return false;
                    cur = last;
                    run = body[o++];
                } else cur = last = v;
                out.push_back(cur);
            }
            return true;
        };
        uint32_t n = 0;
        if (!u16(n)) return;
        std::vector<uint32_t> cells(n), looks, regs;
        for (uint32_t& c : cells) if (!u32(c)) return;
        if (!rle(n, looks) || !rle(n, regs)) return;
        std::vector<std::pair<uint32_t, uint32_t>> binds;
        uint32_t nb = 0;
        if (o < body.size()) {
            if (!u16(nb)) return;
            for (uint32_t i = 0; i < nb; i++) {
                uint32_t idx, app;
                if (!u32(idx) || !u32(app)) return;
                binds.push_back({idx, app});
            }
        }
        int mx = w.bounds.maxX, my = w.bounds.maxY;
        if (mx <= 0 || my <= 0) return;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t c = cells[i];
            std::array<uint16_t, 3> key{uint16_t(c % uint32_t(mx)), uint16_t(c / uint32_t(mx) % uint32_t(my)), uint16_t(c / (uint32_t(mx) * uint32_t(my)))};
            setTile(key, looks[i]);
            w.areaCells[key] = regs[i];
        }
        for (auto& [idx, app] : binds) w.turfBinds[idx] = app;
        return;
    }
    case 0x0020:
        if (!decode(typ, body, js)) { w.mapViewErrs++; return; }
        v = parseJson(js);
        for (const JVal& b : v["binds"].a) w.turfBinds[b["index"].u32()] = b["appid"].u32();
        // The grid is per-cell AREA indices (World::areaCells), on the z of the refresh that armed it.
        for (const JVal& c : v["cells"].a) w.areaCells[{c["x"].u16(), c["y"].u16(), st_.get<proto::g2::MapView>().z}] = c["index"].u32();
        return;
    case 0x00d8: {
        // vis_contents refresh. Tag 1 = a turf: its appearance and its vis_contents, item Type 2 obj, 3 mob
        // (any other Type is skipped). appid 0xffff with no items empties the vis_contents and leaves the turf's appearance
        // as it was; otherwise both are replaced. Tags 2/3 (an obj's / mob's vis_contents) and 13 are not modelled here.
        if (!parsed(v)) { w.cellRefreshErrs++; return; }
        for (const JVal& r : v["records"].a) {
            if (r["tag"].i() != 1) continue;
            std::array<uint16_t, 3> key{r["x"].u16(), r["y"].u16(), r["z"].u16()};
            if (r["appid"].i() == 0xffff && r["contents"].size() == 0) {
                w.turfVis.erase(key);
                if (g_modelMutation == 12) { w.tiles.erase(key); w.tileIconStart.erase(key); }   // test-only: Go's clear
                continue;
            }
            setTile(key, r["appid"].u32());
            std::vector<uint32_t> ids;
            for (const JVal& it : r["contents"].a)
                if (it["type"].i() == 2) ids.push_back(it["value"].u32());
                else if (it["type"].i() == 3) ids.push_back(kRegistryA | it["value"].u32());
            if (ids.empty()) w.turfVis.erase(key); else w.turfVis[key] = std::move(ids);
        }
        return;
    }
    case 0x0037:
        // Frame 0x0037: [tick ms u32][client frame ms u32], each accepted in [10,1000] ms.
        if (body.size() >= 4) {
            uint32_t ms = le32(&body[0]);
            if (ms >= 10 && ms <= 1000) w.tickNanos = int64_t(ms) * 1'000'000;
        }
        if (body.size() >= 8) {
            uint32_t ms = le32(&body[4]);
            if (ms >= 10 && ms <= 1000) w.frameNanos = int64_t(ms) * 1'000'000;
        }
        return;
    case 0x003e: {
        if (!parsed(v)) return;
        w.bounds = {true, int(v["maxX"].i()), int(v["maxY"].i()), int(v["view"].i()), int(v["winW"].i()),
                    int(v["winH"].i()), int(v["viewW"].i()), int(v["viewH"].i())};
        return;   // the decoder already fed st_.mapView (SetBounds)
    }
    case 0x00c7: {   // flick() command half
        if (!parsed(v)) return;
        World::Flick& f = w.flicks[v["loc"]["ref"].u32()];
        f.appid = v["appid"].u32();
        f.haveAppid = v["hasAppid"].b;
        f.startNs = ingestNs_;
        return;
    }
    case 0x006c: {   // flick() payload half
        if (!parsed(v)) return;
        World::Flick& f = w.flicks[v["loc"]["ref"].u32()];
        f.iconState = v["iconState"].s;
        f.iconResIndex = v["iconResIndex"].u32();
        f.haveRes = true;
        f.startNs = ingestNs_;
        return;
    }
    case 0x0026: {
        w.resync.appearances++;   // the stock GC collects past 500 of these
        if (parsed(v)) {
            uint32_t appid = v["appid"].u32();
            Appearance ap{js, v["layer"].f32bits()};
            ap.hasGlideSize = v["hasGlideSize"].b;
            ap.glideSizeBits = v["glideSize"].f32bits();
            ap.longGlide = ((v["appearanceFlags"].u32() >> 21) & 1) != 0;   // appearance_flags LONG_GLIDE
            ap.hasPixelOffsets = v["hasPixelOffsets"].b;
            ap.iconResID = v["iconResID"].u32();
            ap.iconState = v["iconState"].bytesOrStr();
            for (const JVal& id : v["verbs"].a) ap.verbs.push_back(id.u16());
            ap.haveVerbs = true;
            if (ap.layerBits == 0x40000000u) w.layer2Appids.insert(appid); else w.layer2Appids.erase(appid);   // World::layer2Appids
            w.layer2Indexed = true;
            w.appearances[appid] = std::move(ap);
            return;
        }
        // A 0x0026 decode error is counted in ingestErrors() and the appearance is absent from the snapshot.
        return;
    }
    case 0x0009: case 0x0024: {
        if (!parsed(v)) return;
        uint32_t ref = v["ref"].u32(), appid = v["appid"].u32();
        w.refBind[ref] = appid;   // keyed by ref alone, not by table
        // Each message names ONE table. The s2c 0x0009 binds the MOB table (the one c2s 0x000b polls) and clears the record's
        // resync marks. 0x0024 binds the OBJ table (the one c2s 0x003d polls). Both create an absent record, as DreamDaemon
        // answers a poll for an id outside the client's window with its binding. The same binding clears the new record's poll mark.
        if (altRecordModel()) {   // test-only hook (setModelMutation)
            if (w.haveSelf && ref == w.selfRef) {
                auto it = w.atoms.find(ref | kRegistryA);
                if (it != w.atoms.end()) it->second.appid = appid;
                return;
            }
            if (w.atoms.count(ref) && w.atoms.count(ref | kRegistryA)) return;
            for (uint32_t key : {ref, ref | kRegistryA}) {
                auto it = w.atoms.find(key);
                if (it != w.atoms.end()) it->second.appid = appid;
            }
            return;
        }
        uint32_t id = typ == 0x0009 ? (ref | kRegistryA) : ref;
        if (w.resync.freed.erase(id)) w.atoms[id].bindOther = w.atoms[id].backlink = 0xffff;   // rebuilt, bound: no mark
        Atom& a = w.atoms[id];
        a.touchTick = w.resync.tick;   // a lookup marks the record for the GC (FUGUE_RESYNC_GC) for this tick
        bindAppearance(a, appid);
        a.pollAnswerDue = false;
        (typ == 0x0009 ? w.resync.newMobs : w.resync.newObjs).erase(ref);
        return;
    }
    case 0x006e: case 0x006f: {
        // The server's "gone" answer to a resync poll is 0x006e for a mob, 0x006f for an obj, body [id u32]. The stock client
        // frees each held record it names, which also arms the sweep. Freeing keeps a dead atom's record, and its vis_contents,
        // from lingering into a reused slot.
        uint32_t reg = typ == 0x006e ? kRegistryA : 0u;
        for (size_t p = 0; p + 4 <= body.size(); p += 4) {
            uint32_t key = reg | le32(&body[p]);
            if (w.resync.freed.erase(key)) { w.atoms.erase(key); continue; }   // the GC freed it: not held, nothing to tear down
            if (!w.atoms.erase(key)) continue;   // stock: a record it does not hold is skipped and arms nothing
            (reg ? w.resync.newMobs : w.resync.newObjs).erase(key & ~kRegistryA);
            w.visContents.erase(key);
            w.visTurfs.erase(key);
            w.particles.erase(key);
            w.flicks.erase(key);
            w.resync.teardowns++;
        }
        (reg ? relinkMobs : relinkObjs)(w);   // the handler relinks at once
        return;
    }
    case 0x000a: {
        if (!parsed(v)) return;
        // The in-view delta, except that every array's meaning is usr.contents (below). Array 1 is an obj leaving it, so it
        // creates no dead atom. Array 3 is a mob leaving it, so it erases nothing from the atom table. Array 4 is a mob
        // entering it, so its appid goes on the mob's key, not the bare obj key. The cell-less record that array 2 leaves
        // in the atom table is kept: every verb path ignores a record with no cell, and a drop
        // re-sends the obj in full, with its array-1 before the 0x0008 that places it again.
        for (const JVal& p : v["bindA"].a) bindAppearance(w.atoms[p["id"].u32()], p["appearance"].u32());
        for (const JVal& p : v["bindB"].a)
            if (auto it = w.atoms.find(p["id"].u32() | kRegistryA); it != w.atoms.end()) bindAppearance(it->second, p["appearance"].u32());
        // usr.contents (World::usrContents): array 1 = an obj leaving it, 2 = an obj entering it, 3 = a mob leaving it,
        // 4 = a mob entering it.
        for (const JVal& id : v["addA"].a) w.usrContents.erase(id.u32());
        for (const JVal& p : v["bindA"].a) w.usrContents[p["id"].u32()] = p["appearance"].u32();
        for (const JVal& id : v["removeB"].a) w.usrContents.erase(id.u32() | kRegistryA);
        for (const JVal& p : v["bindB"].a) w.usrContents[p["id"].u32() | kRegistryA] = p["appearance"].u32();
        return;
    }
    case 0x0008:
        if (!parsed(v)) return;
        applyObjectArrays(v, 0);
        resyncAfterArrays(w, v, false);   // the parser's tail relinks
        return;
    case 0x003b:
        if (parsed(v)) w.movementMode = uint8_t((v["extra"].u32() >> 2) & 3);
        return;
    case 0x0007: {
        if (!parsed(v)) return;
        const JVal& eye = v["eye"];
        if (!eye.isNull()) {
            w.eyeGlideSizeBits = eye["glideSize"].f32bits();
            w.eyeLongGlide = ((uint32_t(eye["flags32"].i()) >> 21) & 1) != 0;   // AppearanceFlagsShift=21, LONG_GLIDE=1<<0
            uint32_t sb = eye["stepSize"].f32bits();
            if (bitsToFloat(sb) > 0) w.stepSizeBits = sb;
        }
        const JVal& vis = v["vision"];
        if (!vis.isNull() && (vis["sub"].i() & 0x02)) {
            w.seeInvisible = uint8_t(vis["seeInvisible"].i());
            w.haveSeeInvisible = true;
        }
        if (v["hasSight"].b) { w.sight = uint32_t(v["sight"].i()); w.haveSight = true; }
        if (!vis.isNull() && (vis["sub"].i() & 0x04)) { w.sight = vis["sightWide"].u32(); w.haveSight = true; }
        // client.pixel_x/y (Sub&0x10), and all four of x/y/w/z (Sub&0x20); the server sends both together.
        if (!vis.isNull() && (vis["sub"].i() & 0x10)) {
            w.viewPixelX = int16_t(vis["viewOrigin"].at(0).u32());
            w.viewPixelY = int16_t(vis["viewOrigin"].at(1).u32());
        }
        if (!vis.isNull() && (vis["sub"].i() & 0x20)) {
            const JVal& so = vis["screenOffset"];
            w.viewPixelX = int16_t(so.at(0).u32()); w.viewPixelY = int16_t(so.at(1).u32());
            w.viewPixelW = int16_t(so.at(2).u32()); w.viewPixelZ = int16_t(so.at(3).u32());
        }
        applyObjectArrays(v["objects"], kRegistryA);
        resyncAfterArrays(w, v["objects"], true);   // the parser's tail relinks
        int oldEyeX = w.eyeX, oldEyeY = w.eyeY;
        bool hadEye = w.haveEye;
        if (v["hasX"].b) { w.trkX = v["eyeX"].u16(); w.seenX = true; }
        if (v["hasY"].b) { w.trkY = v["eyeY"].u16(); w.seenY = true; }
        if (w.seenX && w.seenY) { w.eyeX = w.trkX + 1; w.eyeY = w.trkY + 1; w.haveEye = true; }
        // Arms the eye glide: ONE vector for the whole (possibly diagonal) move, re-armed like an atom's
        // (armGlideOffset). A move further than the threshold on either axis snaps the view, as it snaps the player's
        // sprite.
        if (hadEye) {
            int dx = w.eyeX - oldEyeX, dy = w.eyeY - oldEyeY;
            if (dx != 0 || dy != 0) {
                double sx = 0, sy = 0;
                w.eyeGliding = std::abs(dx) <= kGlideThresholdTiles && std::abs(dy) <= kGlideThresholdTiles &&
                               armGlideOffset(w, w.eyeGliding ? &w.eyeGlide : nullptr, ingestNs_, -dx, -dy, w.eyeGlideDir, sx, sy);
                if (w.eyeGliding)
                    w.eyeGlide = glideMotion(sx, sy, bitsToFloat(w.eyeGlideSizeBits), iconSizeOf(w), tickNs(w), w.eyeLongGlide, ingestNs_, true);
                // The player's own mob stays where the view puts it in the stock client (oracle-measured, not found in
                // the handler): a glide this frame armed on the eye's tile runs the view's motion.
                auto self = w.atoms.find(w.selfRef | kRegistryA);
                if (w.haveSelf && self != w.atoms.end() && self->second.gliding && self->second.glide.startNs == ingestNs_ &&
                    self->second.x == w.eyeX && self->second.y == w.eyeY)
                    self->second.glide = w.eyeGliding ? w.eyeGlide : GlideMotion{};
            }
        }
        return;
    }
    case 0x00f0: {
        // Only the settled appid matters: an atom held with no appearance adopts the last step's `to`.
        if (!parsed(v)) return;
        if (v["kind"].i() == 5) {
            // animate(client), scheduled as the stock handler builds the chain: Chain != 0 (or no chain held) replaces
            // it, so a Count = 0 frame clears it; Chain = 0 appends. The first step starts at Delay ticks from now, a
            // step with a StepDelay (a parallel or a head) opens a sequence at StepDelay ticks from now, and any other
            // step follows the one before it.
            Atom::Chain& ch = w.clientChain;
            const bool replace = v["chain"].i() != 0 || ch.steps.empty();
            if (replace) { ch = Atom::Chain{}; ch.tickNs = tickNs(w); }
            auto at = [&](uint32_t bits) { return ingestNs_ + int64_t(double(bitsToFloat(bits)) * double(ch.tickNs)); };
            for (const JVal& st : v["steps"].a) {
                Atom::AnimStep s{st["from"].u32(), st["to"].u32(), st["time"].f32bits(), uint8_t(st["easing"].i())};
                s.flags = st["flags"].u32();
                s.loop = st["loop"].i() == 0 ? 1 : int32_t(st["loop"].i());
                if (ch.steps.empty()) { s.startNs = at(v["delay"].f32bits()); s.flags &= ~0x40000000u; }
                else if (st["hasDelay"].b) s.startNs = at(st["delay"].f32bits());
                ch.steps.push_back(s);
            }
            if (!ch.steps.empty()) ch.startNs = ch.steps[0].startNs;
            return;
        }
        // An animate() names its target by {ref, Key} like an 0x0008 record does, so it runs the same Key check and
        // get-or-create: a Key change is a teardown, and an absent record is built and marked.
        // This runs before the step check: the closing frame of a tween has no steps and still carries the Key.
        if ((v["kind"].i() == 2 || v["kind"].i() == 3) && !altRecordModel())
            record(v["ref"].u32() | (v["kind"].i() == 3 ? kRegistryA : 0u), uint16_t(v["ver"].u32()), true);
        if (v["steps"].size() == 0) return;
        // An obj animate with steps binds the obj's appearance, which clears the poll mark like an A5 does.
        if (v["kind"].i() == 2 && !altRecordModel()) w.resync.newObjs.erase(v["ref"].u32());
        int64_t kind = v["kind"].i();
        if (kind == 13) {
            // Only an image we hold. A chain is never expired by time, so its snapshot effect is set membership.
            uint32_t img = v["ref"].u32();
            if (!w.images.count(img)) return;
            w.imageAnims.insert(img);
            // A Chain==0 frame while the held chain is still running amends it (its steps are appended); anything else
            // replaces it, stamped like a movable's (below). The zero-step frame returned above arrives when the tween
            // completes, so it must not settle the chain early.
            Atom::Chain& ch = w.imageChains[img];
            int64_t end = ch.startNs;
            for (const Atom::AnimStep& st : ch.steps) end += int64_t(double(bitsToFloat(st.timeBits)) * double(ch.tickNs));
            bool amend = !ch.steps.empty() && v["chain"].i() == 0 && ingestNs_ < end;
            if (!amend) {
                ch = Atom::Chain{};
                ch.tickNs = tickNs(w);
                ch.startNs = ingestNs_ + int64_t(bitsToFloat(v["delay"].f32bits())) * ch.tickNs;
            }
            for (const JVal& st : v["steps"].a)
                ch.steps.push_back({st["from"].u32(), st["to"].u32(), st["time"].f32bits(), uint8_t(st["easing"].i())});
            return;
        }
        if (kind != 2 && kind != 3) return;
        uint32_t key = v["ref"].u32() | (kind == 3 ? kRegistryA : 0u);
        auto it = w.atoms.find(key);
        if (it == w.atoms.end()) return;
        uint32_t to = v["steps"].at(v["steps"].size() - 1)["to"].u32();
        if (to == 0) return;
        if (it->second.appid == 0) bindAppearance(it->second, to);
        it->second.animating = true;   // Set while an animate() chain is held (never cleared by elapsed time)
        // The chain: start at ingest-now plus the delay in ticks, with its steps and tick duration. A movable's
        // chain is REPLACED by every frame (only client.images amend).
        Atom::Chain& ch = it->second.chain;
        ch = Atom::Chain{};
        ch.tickNs = tickNs(w);
        ch.startNs = ingestNs_ + int64_t(bitsToFloat(v["delay"].f32bits())) * ch.tickNs;   // float32 delay times the tick duration
        for (const JVal& st : v["steps"].a)
            ch.steps.push_back({st["from"].u32(), st["to"].u32(), st["time"].f32bits(), uint8_t(st["easing"].i())});
        return;
    }
    case 0x0071:
        if (!parsed(v)) return;
        // A new own mob starts usr.contents empty. This is order-dependent: the server sends this 0x0071 before the new
        // mob's array-2 pairs, so the clear never removes them.
        if (w.haveSelf && w.selfRef != v["value"].u32()) w.usrContents.clear();
        w.selfRef = v["value"].u32(); w.haveSelf = true;
        // The stock 0x0071 handler builds the mob record when it is absent, and a new mob record is
        // marked for the resync poll. So the client's first word about its own mob is c2s 0x000b [it]. A held record is not marked.
        if (!altRecordModel() && (!w.atoms.count(w.selfRef | kRegistryA) || w.resync.freed.erase(w.selfRef | kRegistryA))) {
            Atom& a = w.atoms[w.selfRef | kRegistryA];
            a.bindOther = a.backlink = 0xffff;
            markNewMob(w, w.selfRef | kRegistryA);
        }
        return;
    case 0x0072: {
        size_t n = 0;
        while (n < body.size() && body[n] != 0) n++;
        w.name.assign(reinterpret_cast<const char*>(body.data()), n);
        return;
    }
    case 0x0038:
        if (!parsed(v)) return;
        w.clientAppid = v["appid"].u32(); w.haveClientAppid = true;
        return;
    case 0x00b6: {
        // cstr; trailing CR/LF trimmed; blank ignored.
        size_t n = 0;
        while (n < body.size() && body[n] != 0) n++;
        std::string msg(reinterpret_cast<const char*>(body.data()), n);
        while (!msg.empty() && (msg.back() == '\r' || msg.back() == '\n')) msg.pop_back();
        if (trimAscii(msg).empty()) return;
        w.hostMessage = msg;
        // Each non-blank trimmed line goes to the chat ring.
        size_t ls = 0;
        while (ls <= msg.size()) {
            size_t le = msg.find('\n', ls);
            if (le == std::string::npos) le = msg.size();
            std::string ln = trimAscii(std::string_view(msg).substr(ls, le - ls));
            if (!ln.empty()) { std::string line = "*** " + ln + " ***"; addChat(line, htmlEscape(line)); }
            ls = le + 1;
        }
        return;
    }
    case 0x0000:
        if (!parsed(v)) return;
        w.sessionEnd = {true, v["reason"].u16(), v["requiredVersion"].u32(),
                        sessionEndText(v["reason"].u16(), v["requiredVersion"].u32())};
        {   // Shown in chat as a "*** ... ***" line.
            std::string line = "*** " + w.sessionEnd.text + " ***";
            addChat(line, htmlEscape(line));
        }
        return;
    case 0x0084: {
        // Keyed by the window= param. Close removes the window and its place in the arrival order; a re-browse
        // of an open window keeps its place.
        if (!parsed(v)) return;
        std::string params = v["params"].bytesOrStr();
        std::string name = browserWindowName(params);
        if (v["close"].b) {
            w.browsers.erase(name);
            for (size_t i = 0; i < w.browserOrder.size(); i++)
                if (w.browserOrder[i] == name) { w.browserOrder.erase(w.browserOrder.begin() + long(i)); break; }
        } else {
            if (!w.browsers.count(name)) w.browserOrder.push_back(name);
            w.browsers[name] = {params, v["html"].bytesOrStr()};
        }
        return;
    }
    case 0x00a7: {
        // Case 0x00a7: a page-JS call, a whole-page replace of a live window, or chat.
        if (!parsed(v)) return;
        std::string control = v["control"].bytesOrStr();
        std::string value = v["value"].bytesOrStr();
        std::string win, fn;
        if (splitBrowserOutput(control, win, fn) && !fn.empty()) {   // page JS call: counted, not chat
            w.browserCalls[win]++;
            return;
        }
        if (browserControlWindow(control, win)) {
            auto b = w.browsers.find(win);
            if (b != w.browsers.end()) { b->second.html = decodeChatHTML(value); return; }   // page replacement
        }
        if (!control.empty()) {
            std::string sw, sfn;
            splitBrowserOutput(control, sw, sfn);
            w.chatControl = !sw.empty() ? sw : control;
        }
        std::string markup = decodeChatHTML(value);
        addChat(stripChatMarkup(markup), markup);
        return;
    }
    case 0x0027: {
        // No proto/ decoder: cstring, one trailing newline stripped, then the markup is url-decoded and added to chat.
        size_t n = 0;
        while (n < body.size() && body[n] != 0) n++;
        std::string t(reinterpret_cast<const char*>(body.data()), n);
        if (!t.empty() && t.back() == '\n') t.pop_back();
        std::string markup = decodeChatHTML(t);
        addChat(stripChatMarkup(markup), markup);
        return;
    }
    case 0x000e:
        // Appends: the ordinal space runs across frames.
        if (!parsed(v)) return;
        for (const JVal& e : v.a) w.resources.emplace_back(e["id"].u32(), uint8_t(e["type"].i()));
        return;
    case 0x0011: {
        // De-dup by id, keep first-arrival order, last record wins.
        if (!parsed(v)) return;
        uint32_t id = v["id"].u32();
        if (!w.verbById.count(id)) w.verbOrder.push_back(id);
        w.verbById[id] = js;
        return;
    }
    case 0x0033: case 0x0034: case 0x0035: {
        // 0x34 deletes imageID and every moreIDs entry; 0x33/0x35 store the record under imageID.
        // imageAnims mirrors the image animation keys.
        if (!parsed(v)) return;
        uint32_t id = v["imageID"].u32();
        if (typ == 0x0034) {
            auto drop = [&](uint32_t i) { w.images.erase(i); w.imageAnims.erase(i); w.imageChains.erase(i); w.imageIconStart.erase(i); };
            drop(id);
            for (const JVal& m : v["moreIDs"].a) drop(m.u32());
        } else {
            w.images[id] = js;
            if (typ == 0x0033) { w.imageAnims.erase(id); w.imageChains.erase(id); w.imageIconStart.erase(id); }   // a recycled id is a new image
            stampIcon(w.imageIconStart[id], v["appid"].u32());
        }
        return;
    }
    case 0x0076: {
        // op 1 = remove, and its index is a LIST POSITION into hudOrder
        // (an out-of-range position is ignored); any other op adds/updates by ref (= index), appending
        // to hudOrder on first sight.
        if (!parsed(v)) return;
        for (const JVal& s : v.a) {
            uint32_t index = s["index"].u32();
            if (s["op"].i() == 1) {
                int64_t pos = int64_t(index);
                if (pos < 0 || pos >= int64_t(w.hudOrder.size())) continue;
                w.hud.erase(w.hudOrder[size_t(pos)]);
                w.hudAppid.erase(w.hudOrder[size_t(pos)]);
                w.hudOrder.erase(w.hudOrder.begin() + pos);
                continue;
            }
            if (!w.hud.count(index)) w.hudOrder.push_back(index);
            w.hud[index] = dumpCanon(s);
            w.hudAppid[index] = s["appid"].u32();
            stampIcon(w.hudIconStart[index], s["appid"].u32());
        }
        return;
    }
    case 0x006a: {
        // A frame the decoder rejects counts as Frames+1 Errors+1. The token gate and the client-side verb-tab guard need a
        // token this client owns, which only the tab select creates.
        if (!parsed(v)) { w.stat.frames++; w.stat.errors++; return; }
        auto& st = w.stat;
        st.frames++;
        // Token gate: token 0 = unsolicited, and releases ownership; otherwise a frame is accepted while no token is owned
        // or it matches ours.
        {
            int tok = int(v["token"].i());
            if (tok == 0) st.owns = false;
            else if (st.owns && tok != st.token) { st.stale++; return; }
        }
        if (v["hasTabs"].b) {
            st.tabs.clear();
            for (const JVal& t : v["tabs"].a) st.tabs.push_back(t.bytesOrStr());
        }
        // While a client-side verb-category tab is up, the server's echoed index must not move the selection, unless the
        // same frame carries the tab list.
        bool onClientTab = st.owns && !v["hasTabs"].b && st.selected >= int(st.tabs.size());
        if (v["hasSelected"].b && !onClientTab) {
            if (v["hasTabs"].b || int(v["selected"].i()) != st.selected) st.lines.clear();
            st.selected = int(v["selected"].i());
        }
        for (const JVal& r : v["rows"].a) {
            World::StatLine l{r["ordinal"].u16(), net::plainText(r["name"].bytesOrStr()), dumpCanon(r["value"]), {}};
            size_t i = l.ordinal;
            bool same = i < st.lines.size() && st.lines[i].name == l.name;
            if (same) l.icon = st.lines[i].icon;
            if (r["value"]["hasAppid"].b) stampIcon(l.icon, r["value"]["appid"].u32());
            if (same) st.lines[i] = std::move(l);
            else if (st.lines.size() >= kMaxStatLines) continue;
            else if (i < st.lines.size()) st.lines.insert(st.lines.begin() + long(i), std::move(l));
            else st.lines.push_back(std::move(l));
        }
        for (const JVal& o : v["clears"].a) {
            size_t i = o.u16();
            if (i < st.lines.size()) st.lines.erase(st.lines.begin() + long(i));
        }
        return;
    }
    case 0x00af:
        // Case 0x00af - only a NON-empty dmf replaces the skin and bumps the seq.
        if (!parsed(v)) return;
        if (!v["dmf"].bytesOrStr().empty()) { w.skinDmf = v["dmf"].bytesOrStr(); w.skinSeq++; }
        return;
    case 0x00ef:
        if (!parsed(v)) return;
        w.skinResID = v["resID"].u32();
        return;
    case 0x00a8:
        // Props are kept raw; last write per control wins.
        if (!parsed(v)) return;
        w.controls[v["control"].bytesOrStr()] = v["props"].bytesOrStr();
        return;
    default:
        return;   // every other type is not a modelled section
    }
}

}  // namespace fugue::model

#include "model/sha256.h"
namespace fugue::model {
void Session::noteResource(uint32_t id, ByteSpan data, const std::string& name) {
    World::RecvRes r;
    r.len = data.size();
    r.sha = sha256Hex(data.data(), data.size());
    w_.received[id] = std::move(r);
    if (!name.empty()) w_.resNames[id] = name;
}
}  // namespace fugue::model
