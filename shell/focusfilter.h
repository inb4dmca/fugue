// FocusFilter: the app-level key router.
//   map / output / info focused -> every key goes to macro routing
//   input focused               -> plain keys type; Ctrl/Alt combinations that are bound go to macros
//   browser focused             -> the page owns its keys (the filter never touches them)
// A matched macro is emitted as macroFired(); the app routes its command (app/inputrouter: client commands such as
// `.north`, `.winset` macro-set swaps, and everything else as a command line for the session). An arrow key that no
// macro binds, with the map, output or info focused, is a built-in walk key: builtinMove(dir, down). That keeps a
// world with no macros walkable.
//
// Reroute guard: the filter never reposts a key. It dispatches directly, acts only on the event delivered to the
// focus widget (so the window-to-widget double delivery is handled once), and ignores keys that arrive while it is
// dispatching (inDispatch_).
#pragma once
#include <QKeyEvent>
#include <QHash>
#include <QObject>
#include <string>
#include "skin/macro.h"
#include "skin/usermacros.h"

namespace fugue::shell {

enum class Zone { Map, Output, Info, Input, Browser, Other };
const char* zoneName(Zone z);

// "CTRL+SHIFT+A" style spec for a key event (the macro naming of skin/macro.h), without +UP/+REP.
std::string macroSpecFor(const QKeyEvent* e);

class FocusFilter : public QObject {
    Q_OBJECT
public:
    explicit FocusFilter(QObject* parent = nullptr) : QObject(parent) {}
    void setMacros(const skin::MacroSet& set) { macros_ = set; have_ = true; }
    // The player's own lists (Client > Macros..., skin/usermacros.h): consulted only for keys the skin's set leaves unbound.
    void setUserMacros(const skin::MacroSet& game, const skin::MacroSet& global) { userGame_ = game; userGlobal_ = global; }
    static Zone zoneOf(QObject* o);   // walks parents for a "fugueZone" property, MapWidget, BrowserPane or QLineEdit
    bool eventFilter(QObject* o, QEvent* e) override;
    uint64_t fired() const { return fired_; }
    // Fires the release of every key this filter routed a press for and has not seen released. Called on application
    // deactivation, since a key released in another window never reaches the filter.
    void releaseHeld(const char* why);
signals:
    void macroFired(const QString& key, const QString& command, bool up);
    void builtinMove(int dir, bool down);   // BYOND direction bits: N=1 S=2 E=4 W=8
private:
    skin::MacroSet macros_, userGame_, userGlobal_;
    bool have_ = false, inDispatch_ = false;
    uint64_t fired_ = 0;
    // Key to what its routed press saw. A release matches the modifiers its press carried, and is routed in the press's
    // zone even when it lands elsewhere, such as a popup menu or a dialog (see eventFilter).
    struct Held { Qt::KeyboardModifiers mods; Zone zone; };
    QHash<int, Held> pressMods_;
    bool route(Zone z, int key, Qt::KeyboardModifiers mods, bool autoRep, const std::string& spec, bool up);
};

}  // namespace fugue::shell
