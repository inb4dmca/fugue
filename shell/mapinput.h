#pragma once
#include <QMargins>
#include <QMetaType>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <optional>
#include "proto/enc_c2s_0002_input.h"
#include "shell/mapsprite.h"

namespace fugue::model { struct World; }
namespace fugue::shell {
struct VisibilityFrame;
class CompositionPlan;
// client.dir: the map is drawn with this world direction up. Tiles and pixel offsets turn, icons stay upright on the
// bottom-left of their tile and show their direction turned the same way; transforms and the HUD do not turn, and the
// HUD is laid out on the turned view. SOUTH, EAST and WEST turn; anything else draws as NORTH (stock 516.1689,
// stock-parity-world client-dir-*).
struct MapTurn {
    int dir = 1;
    explicit MapTurn(int clientDir) : dir(clientDir == 2 || clientDir == 4 || clientDir == 8 ? clientDir : 1) {}
    bool turned() const { return dir != 1; }
    // The presented rectangle of an unturned view and back: the same centre, sides swapped for EAST and WEST.
    QRect rect(QRect r) const {
        return dir == 4 || dir == 8 ? QRect(r.x() + (r.width() - r.height()) / 2, r.y() + (r.height() - r.width()) / 2,
                                            r.height(), r.width()) : r;
    }
    // A point (pixel corner, y down) of the unturned view `world` in the presented view, and back.
    QPoint point(QPoint p, QRect world) const {
        const QPoint q = p - world.topLeft(), o = rect(world).topLeft();
        return o + (dir == 2 ? QPoint(world.width() - q.x(), world.height() - q.y()) :
                    dir == 4 ? QPoint(q.y(), world.width() - q.x()) :
                    dir == 8 ? QPoint(world.height() - q.y(), q.x()) : q);
    }
    QPoint back(QPoint p, QRect world) const {
        const QPoint q = p - rect(world).topLeft();
        return world.topLeft() + (dir == 2 ? QPoint(world.width() - q.x(), world.height() - q.y()) :
                                  dir == 4 ? QPoint(world.width() - q.y(), q.x()) :
                                  dir == 8 ? QPoint(q.y(), world.height() - q.x()) : q);
    }
    // The bottom-left corner of an icon whose unturned bottom-left corner is `p`, on a grid of `tile` pixels.
    QPoint bottomLeft(QPoint p, QRect world, int tile) const {
        return point(p + (dir == 2 ? QPoint(tile, -tile) : dir == 4 ? QPoint(0, -tile) : dir == 8 ? QPoint(tile, 0) : QPoint()),
                     world);
    }
    // The direction an icon shows for its world direction.
    int icon(int d) const {
        if (dir == 1) return d;
        static constexpr int south[4] = {2, 1, 8, 4}, east[4] = {8, 4, 1, 2}, west[4] = {4, 8, 2, 1};
        const int* to = dir == 2 ? south : dir == 4 ? east : west;
        int out = 0;
        for (int bit = 0; bit < 4; ++bit) if (d & (1 << bit)) out |= to[bit];
        return out;
    }
};
struct MapGeometry {
    QRect region; // world tiles, zero-based, y increases north
    QSize world;
    int tile = 0, z = 0;
    uint64_t session = 0;
    // Whole tiles the scene has around the view, for screen objects placed outside it (ScreenLoc::outside).
    QMargins hud;
    int dir = 1; // client.dir as MapTurn draws it; `region` and the sprites' cells stay world tiles
    // Worker unified-scene evidence. When present, turf hits require a rendered
    // sprite; a lit cell alone must not manufacture a background target.
    std::shared_ptr<const VisibilityFrame> visibility;
    bool operator==(const MapGeometry&) const = default;
};
struct RelayHit;
struct PlaneMousePath;
struct MapPointer {
    enum Kind { Down, Up, Click, DoubleClick, Wheel, Hover, Leave, Drag, Drop } kind = Click;
    proto::enc::ClickCmd click;
    int z = 0;
    uint64_t session = 0;
    // Optional GUI surface lifetime fence, independent of the model/session.
    // Destruction or rebinding of a MAP invalidates already queued input too.
    std::optional<std::weak_ptr<const uint8_t>> surface;
    uint16_t atomKey = 0;
    bool haveAtomKey = false;
    bool screen = false; // immutable rendered HUD hit; worker validates HUD membership/key
    bool filteredPlane = false; // admitted stock-filter original-source endpoint
    bool masterFallback = false; // only stock-verified Down/Up/Click, never menu ownership
    bool boundedFilterInput = false; // default MAP only; also gates independent menu rectangles
    std::shared_ptr<const VisHit> visHit;
    std::shared_ptr<const RelayHit> relayHit;
    std::shared_ptr<const PlaneMousePath> planeMousePath;
    uint32_t imageId = 0;
    uint64_t imageIdentity = 0, targetIdentity = 0, hudIdentity = 0;
    QSize turfDimensions;
    QPoint scenePixel;
    QRect renderedSource; // viewport pivot/clipping for plane-surface input
    bool insideScene = false;
    // Implicitly shared last-rendered evidence for bounded worker stacked menus.
    // Never consult a newer prepared scene when expanding a right-click.
    std::shared_ptr<const VisibilityFrame> visibility;
    MapSprites renderedScene;
    std::shared_ptr<const CompositionPlan> composition;
    // Press-time rendered identity/location. Never replace it with a later pick.
    std::shared_ptr<const MapPointer> source;
    bool backgroundHidden = false; // no new press/hover, but a null gesture destination with coordinates
    bool outside = false; // grabbed pointer released outside this MAP
    bool dragCell = false; // outside every window, straight from the map: click's screen cell is the last one dragged over
    bool closedPopup = false; // Down: the right press that closed the verb menu, consumed whatever the flag
    QPoint global;
    float dx = 0, dy = 0;
};
struct RelayHit {
    MapPointer producer, consumer;
    QString name;
    std::shared_ptr<const std::vector<uint32_t>> consumerPath;
    // Plane-backed sources retain the selected master and the member supplying
    // original ink. Endpoint snapshots contain no second scene or nested relay.
    std::optional<int> plane;
    std::shared_ptr<const MapPointer> ink;
    uint32_t producerAppearance = 0, consumerAppearance = 0, inkAppearance = 0;
};
// Bounded viewport PASS_MOUSE route to original world/HUD ink. Flat endpoints
// avoid recursive scene ownership; worker revalidates every hop and typed leaf.
struct PlaneMousePath {
    struct Hop {
        MapPointer master, relay;
        QString name;
        int plane = 0;
        uint32_t masterAppearance = 0, relayAppearance = 0;
    };
    std::vector<Hop> hops;
    uint32_t leafAppearance = 0;
};
// Logical widget coordinates; scale and letterboxing match the renderer. Input
// uses the geometry of the last rendered frame, never the worker's newer eye.
std::optional<MapPointer> mapPointerAt(QPointF position, QSizeF widgetSize, QSize view,
                                     QRect source, const MapGeometry& geometry,
                                     const QString& control, bool defaultMap, const MapSprites& sprites = {});
// Explicit last-rendered destination and submitted plan. The optional plan-less
// form is for synthetic callers/tests and assumes renderable float surfaces;
// MapWidget always supplies its successfully submitted plan.
std::optional<MapPointer> mapPointerAtPresentation(QPointF position, QRectF destination, QSize view,
                                     QRect source, const MapGeometry& geometry,
                                     const QString& control, bool defaultMap, const MapSprites& sprites = {},
                                     std::shared_ptr<const CompositionPlan> composition = {});
// Rectangle stack, topmost first, deduplicated by named atom, at most 128 entries.
// Each returned reference must still pass worker lifetime validation.
QVector<MapPointer> mapPointerStack(const MapPointer& pointer);
Bytes mapPointerBody(const MapPointer& pointer);
bool mapPointerWanted(MapPointer::Kind kind, const proto::enc::MouseHooks& hooks, bool wheel);
// Worker routing only; Open still requires surface/session and per-candidate
// validation. Popup gestures never also dispatch ordinary mouse hooks.
enum class MapPopupAction { None, Open, Consume };
// client.show_popup_menus is read at the right press and again at its release. pressSent: this gesture's press went to
// the wire, so its release finishes as MouseUp and Click whatever the flag is by then.
MapPopupAction mapPopupAction(const MapPointer&, bool popupMenus, bool pressSent = false);

// Worker-only validation and hover subscription state. No model access in the presenter.
bool mapPointerCurrent(const MapPointer&, const model::World&, uint64_t session);
// What a pointer names in the model, for hover flags, menus and menu picks alike: a vis_contents member is its own
// atom or its own turf (on its own z), a HUD hit its HUD record, anything else the atom or the cell on the map's z.
// Says nothing about lifetime; mapPointerCurrent does.
struct MapPointerTarget {
    enum Kind { Atom, Hud, Turf } kind = Atom;
    uint32_t atom = 0; // Atom: the World::atoms key (registry bit set for a mob). Hud: the HUD ref
    std::array<uint16_t,3> cell{}; // Turf: zero-based
    std::optional<uint32_t> appid; // absent: the model no longer holds it
};
std::optional<MapPointerTarget> mapPointerTarget(const MapPointer&, const model::World&);
bool sameMapTarget(const MapPointer&, const MapPointer&);
class MapHover {
public:
    Bytes update(const MapPointer&, bool current, bool entered, bool moved);
    // A successfully sent Drop makes the server enter its destination implicitly.
    // Adopt it without sending Hover/Leave; returning to the old source must enter.
    void dropped(const MapPointer& p) { last_ = p; last_->kind = MapPointer::Hover; last_->source.reset(); }
private:
    std::optional<MapPointer> last_;
};
struct MapMenuItem { int group = 0; QString atom, label; };
struct MapMenu { quint64 token = 0; QPoint global; QVector<MapMenuItem> items; };
}
Q_DECLARE_METATYPE(fugue::shell::MapMenu)
Q_DECLARE_METATYPE(fugue::shell::MapPointer)
