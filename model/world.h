// World model: a single-writer value type, fed frame by frame through Session::ingest. Frames are decoded by the
// proto/ decoders and applied per handler in world.cpp.
//
// snapshot() is the canonical JSON of the world.
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "model/json.h"
#include "net/bytes.h"
#include "proto/registry.h"

namespace fugue::model {

// One in-view movable. All fields default to zero (what an A1 / 0x000a add inserts).
struct Atom {
    uint32_t appid = 0;
    int x = 0, y = 0, z = 0;
    bool hasPos = false;
    int dir = 0;
    int16_t pixelX = 0, pixelY = 0, pixelW = 0, pixelZ = 0;
    int16_t boundX = 0, boundY = 0, boundW = 0, boundH = 0;
    bool hasBounds = false;
    int16_t stepX = 0, stepY = 0;
    uint32_t stepSizeBits = 0;   // float32 bits
    bool hasStep = false;
    int visCount = 0;
    bool gliding = false;     // transient: armed by a one-tile Pos (presence only)
    bool animating = false;   // transient: an animate() chain was recorded (never cleared by time)
    // Record identity and bind, as every 0x0007/0x0008 record carries them. Not in the snapshot. A record arriving for
    // a held slot with a different Key is a different atom; the stock client tears the old record down first.
    uint16_t key = 0;
    bool haveKey = false;
    uint32_t bindOther = 0xffff;   // A3 Bind.Other; 0xffff = none (the create default), cleared by an A1
    uint32_t backlink = 0xffff;    // a record whose Bind names this one, as of the last relink
    uint64_t touchTick = 0;        // ResyncEvents tick when a record array last named it
    bool pollAnswerDue = false;    // mob only: polled as a new record, the server's 0x0009 not yet in

    // Render state behind the two transient flags above (not in the snapshot: wall-clock based).
    // Times are Unix ns on the ingest clock (Session::setIngestClockNs).
    struct Glide { int fromX = 0, fromY = 0; int64_t startNs = 0, durNs = 0; };
    struct AnimStep { uint32_t from = 0, to = 0; uint32_t timeBits = 0; uint8_t easing = 0; };
    struct Chain { int64_t startNs = 0, tickNs = 0; std::vector<AnimStep> steps; };
    Glide glide;              // valid iff gliding
    Chain chain;              // valid iff animating
};

// An appearance kept as its decoded canonical fields JSON (embedded verbatim in the snapshot) plus the scalars
// the model itself reads.
struct Appearance {
    std::string fieldsJson;
    uint32_t layerBits = 0;
    // The scalars the glide arming and animate-endpoint code reads.
    bool hasGlideSize = false;
    uint32_t glideSizeBits = 0;   // float32 bits
    bool longGlide = false;       // DM appearance_flags LONG_GLIDE (AppearanceFlags >> 21 & 1)
    bool hasPixelOffsets = false; // a 0x0008 pixel-offset record was present
    // The verb section, parsed once at ingest (verbmenu.cpp reads it per atom per keypress). haveVerbs false means
    // not filled: parse fieldsJson.
    std::vector<uint16_t> verbs;
    bool haveVerbs = false;
};

// Bounds of the world (0x003e), raw and unclamped.
struct Bounds {
    bool have = false;
    int maxX = 0, maxY = 0, view = 0, winW = 0, winH = 0, viewW = 0, viewH = 0;
};

struct SessionEnd {
    bool seen = false;
    uint16_t reason = 0;
    uint32_t version = 0;
    std::string text;
};

struct MapGrid {   // the single-frame 0x0061 grid (Cells() fallback path)
    bool have = false;
    uint16_t x0 = 0, y0 = 0, z = 0;
    std::vector<uint32_t> cells;
};

struct World {
    // Meta, eye and self.
    std::string name;
    Bounds bounds;
    int64_t tickNanos = 0;   // 0 = never set -> DefaultTickNanos
    int mapViewErrs = 0, cellRefreshErrs = 0;
    uint32_t clientAppid = 0;
    bool haveClientAppid = false;
    std::string chatControl, hostMessage;
    SessionEnd sessionEnd;

    bool haveEye = false;
    int eyeX = 0, eyeY = 0;
    uint32_t eyeGlideSizeBits = 0, stepSizeBits = 0;
    bool eyeLongGlide = false;
    uint8_t seeInvisible = 0;
    bool haveSeeInvisible = false;
    uint32_t sight = 0;
    bool haveSight = false;
    // Per-axis eye tracker (0x0007 carries one axis at a time).
    uint16_t trkX = 0, trkY = 0;
    bool seenX = false, seenY = false;
    // One per axis. A move arms only the axes it touched; an untouched axis keeps running its old scroll.
    // See eyeScrollOffset.
    struct EyeAxisGlide {
        bool live = false;
        int from = 0;   // the eye tile on this axis before the move
        int64_t startNs = 0, durNs = 0;
    };
    EyeAxisGlide eyeGlideX, eyeGlideY;

    bool haveSelf = false;
    uint32_t selfRef = 0;

    // Tables.
    std::map<uint32_t, Appearance> appearances;
    std::map<uint32_t, Atom> atoms;                         // key = id | registryA bit
    // flick(): the live one-shot per atom, from 0x00c7 + 0x006c, keyed by AtomLoc.ref (the atoms key for 73/74 refs).
    // startNs = ingest clock of the latest half.
    struct Flick { uint32_t appid = 0; bool haveAppid = false; std::string iconState; uint32_t iconResIndex = 0; bool haveRes = false; int64_t startNs = 0; };
    std::map<uint32_t, Flick> flicks;
    std::map<uint32_t, std::vector<uint32_t>> visContents;
    // vis_contents items that are TURFS (item Type 1), per holder key: the turf's flat cell index (x + y*maxx + z*maxx*maxy,
    // all 0-based). The turfs' looks arrive in 0x00f1 (into `tiles`); what stands on them, as ordinary 0x0008 objects.
    // Drawn only on a secondary map (render/scene.h assembleSecondaryMap). Kept apart from visContents.
    std::map<uint32_t, std::vector<uint32_t>> visTurfs;
    std::map<uint32_t, uint32_t> refBind, turfBinds;
    // usr.contents as stock DreamSeeker tracks it, from the 0x000a arrays: key = obj id, or mob id | kRegistryA;
    // value = the pair's appid. An obj entering arrives as an array-2 [id][appid] pair; leaving for another mob, as an
    // array-1 [id]. A mob entering arrives as an array-4 pair, and leaving as an array-3 [id], which may come late in
    // a later 0x000a. A del() of a carried obj is an array-1 removal. The floor (0x0008) never enters the set.
    // Only these ids are admitted by `set src in usr` (verbSrcCandidates). A client.mob switch to a new mob clears the
    // set: that is safe only because the server sends 0x0071 before the new mob's array-2 pairs (world.cpp case 0x0071).
    std::map<uint32_t, uint32_t> usrContents;
    // The 0x0020 grid: the AREA index of each cell, key (x,y,z) 0-based wire. Stock DreamSeeker sends an area verb as
    // src [04][index of the player's cell]. turfBinds above is the same list.
    std::map<std::array<uint16_t, 3>, uint32_t> areaCells;
    // Appearances whose layer is 2.0 (the default-turf rule, verbmenu.cpp defaultTurfAppid), filled at ingest
    // (0x0026). layer2Indexed false means a hand-built World, which is scanned instead.
    std::set<uint32_t> layer2Appids;
    bool layer2Indexed = false;
    std::map<std::array<uint16_t, 3>, uint32_t> tiles;      // cumulative map, key (x,y,z) 0-based wire
    MapGrid grid;

    // Remaining sections. Decoder-owned records are kept as the decoder's canonical fields JSON, embedded verbatim
    // like appearances.
    std::map<uint32_t, std::string> images;                 // client.images, key = imageID
    std::set<uint32_t> imageAnims;                          // keys of the image animation chains (existence only)
    // The image animation chains themselves (the snapshot prints only the keys above). Stamped at ingest like
    // Atom::chain and sampled by render/scene.cpp, so a client.image fades and slides. Transient: not in the snapshot.
    std::map<uint32_t, Atom::Chain> imageChains;
    std::map<uint32_t, std::string> hud;                    // client.screen, key = ref
    std::map<uint32_t, uint32_t> hudAppid;                  // hud's appid per ref, filled at ingest (0x0076) for verbmenu
    std::vector<uint32_t> hudOrder;
    std::map<uint32_t, std::string> verbById;
    std::vector<uint32_t> verbOrder;

    struct StatLine { uint16_t ordinal = 0; std::string name; std::string valueJson; };
    struct Stat {
        std::vector<std::string> tabs;
        int selected = 0;
        uint8_t token = 0;   // only ever set by SelectingTab (client-to-server side)
        bool owns = false;   // the token above was chosen by this client (statpanel.h selectStatTab); the gate only bites then
        int frames = 0, errors = 0, stale = 0;
        std::vector<StatLine> lines;
    } stat;

    struct Browser { std::string params, html; };
    std::map<std::string, Browser> browsers;   // open browse windows (0x0084), key = window name
    std::vector<std::string> browserOrder;     // arrival order
    std::map<std::string, uint64_t> browserCalls;   // page-JS call counts, never reset by a close

    std::vector<std::pair<uint32_t, uint8_t>> resources;   // 0x000e availability list, arrival order

    // Filled only by noteResource(): file bytes reduced to {len, sha256} (the snapshot never embeds bytes), keyed by the
    // verified resID. Only the verified key is filed.
    struct RecvRes { uint64_t len = 0; std::string sha; };
    std::map<uint32_t, RecvRes> received;
    std::map<uint32_t, std::string> resNames;

    std::string skinDmf;
    uint32_t skinResID = 0xFFFFFFFFu;   // no skin resource
    int skinSeq = 0;

    std::map<std::string, std::string> controls;   // winset-applied props, key = control

    std::vector<std::string> chat, chatHtml;   // scrollback rings (plain / markup), capped at kChatCap
    uint64_t chatTotal = 0;

    // Inputs to the atom-table resync poll (model/resync.h): written by ingest, drained by ResyncPoller, not in the
    // snapshot. teardowns counts records freed (a Key change on a held slot, s2c 0x006e/0x006f); each arms the sweep.
    // newMobs/newObjs are ids just built and still marked; the client asks the server about each (c2s 0x000b / 0x003d).
    // relinkObjs/relinkMobs arm the rebuild of cell lists and back-links after a 0x0007/0x0008 parse. objCells and
    // mobCells are the tiles that held an obj or mob at the last relink, which the sweep's cell test reads.
    struct ResyncEvents {
        uint64_t teardowns = 0;
        std::set<uint32_t> newMobs, newObjs;
        bool relinkObjs = false, relinkMobs = false;
        bool haveOrigin = false;
        uint16_t originX = 0, originY = 0;             // the last 0x0012 / 0x0014 / 0x0061 window origin
        std::set<std::array<int, 3>> objCells, mobCells;
        uint64_t tick = 0;          // the poller's client-tick count
        uint64_t appearances = 0;   // s2c 0x0026 frames; each builds an appearance record (the stock GC's 500 trigger)
        // Records the stock client's GC would have freed (model/resync.h). The state is kept, since the server does not
        // know they were freed and sends no fresh copy; the next record naming one is built anew: marked, unbound,
        // with no back-link.
        std::set<uint32_t> freed;
    } resync;
};

constexpr size_t kChatCap = 500;
constexpr size_t kMaxStatLines = 4096;

constexpr uint32_t kRegistryA = 0x80000000u;
constexpr int64_t kDefaultTickNanos = 100'000'000;
constexpr int kGlideThresholdTiles = 1;
constexpr int kDefaultIconSize = 32;
// The world's icon size: the eye's step_size, since a movable's default step_size is world.icon_size.
// Falls back to kDefaultIconSize.
int iconSizeOf(const World& w);

// Glide duration: distance in px over the glide px-per-tick, times the tick, truncated to whole ns.
// glidePx <= 0 means one full tile per tick.
int64_t glideDurationNs(int dx, int dy, float glidePx, int iconSize, int64_t tickNs, bool longGlide);
// The sub-tile scroll, in image pixels, that the map layers are shifted by while an eye glide is in flight: +x draws
// map content right, +y draws it down (image space). Zero with no glide or once the glide has played out. It starts
// at a whole tile (undoing the snap of the view origin) and slides to zero.
void eyeScrollOffset(const World& w, int64_t nowNs, int& sx, int& sy);
// Selects an alternative rule for test controls; 0 is the shipped behaviour.
void setModelMutation(int m);

// Sections the snapshot does not model, in schema order. Empty: every section is modelled.
const std::vector<std::string>& unimplementedSections();

struct CellGrid {
    bool err = true;
    int w = 0, h = 0, originX = 0, originY = 0;
    int viewZ = 1;   // the refresh grid's z (1-based) when in use, else 1
    std::vector<uint32_t> appids;
};

class Session {
public:
    Session() = default;
    // The instant "now" for time-stamped state armed by ingested frames (a glide's start, an animate() chain's start).
    // The default is far in the future, which pins every glide and chain at its start for any render clock below it.
    // A test that scripts the clock sets it before each frame.
    void setIngestClockNs(int64_t ns) { ingestNs_ = ns; }
    int64_t ingestClockNs() const { return ingestNs_; }
    // Feeds one frame. Never throws: a decoder failure drops the frame. 0x009f must NOT be fed.
    void ingest(uint16_t typ, ByteSpan body);
    // Resource availability: call once per file the resource pump
    // has verified and stored. Idempotent per id. Same single-writer rule as ingest().
    void noteResource(uint32_t id, ByteSpan data, const std::string& name);

    // The canonical JSON of the "world" object.
    std::string snapshot() const;
    // The same sections, each as its own JSON text, in schema order.
    std::vector<std::pair<std::string, std::string>> sections() const;
    // The "cells" section as data: the viewport window of turf appids, row-major from the bottom-left origin
    // (1-based world coords). err = no bounds or no grid yet (then w = h = 0 and appids is empty).
    CellGrid cells() const;

    const World& world() const { return w_; }
    // The single writer's handle for client-side state changes (statpanel.h selectStatTab). Session thread only.
    World& worldMut() { return w_; }
    int ingestErrors() const { return decodeErrors_; }   // frames a decoder rejected (informational)

private:
    bool decode(uint16_t typ, ByteSpan body, std::string& jsonOut);
    void applyObjectArrays(const JVal& m, uint32_t reg);
    Atom* record(uint32_t id, uint16_t key, bool fresh);   // get-or-create by {id, Key} (world.cpp)
    void addChat(const std::string& plain, const std::string& markup);

    World w_;
    mutable proto::State st_;   // snapshot() reads the map-refresh machine through get<>()
    int decodeErrors_ = 0;
    int64_t ingestNs_ = (INT64_MAX / 4);
};

}  // namespace fugue::model
