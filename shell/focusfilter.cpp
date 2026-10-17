#include "shell/focusfilter.h"
#include "shell/paste.h"
#include <QApplication>
#include <QKeySequence>
#include <QLineEdit>
#include <cstdio>
#include "shell/browserpane.h"
#include "shell/mapwidget.h"

namespace fugue::shell {

const char* zoneName(Zone z) {
    switch (z) {
        case Zone::Map: return "map"; case Zone::Output: return "output"; case Zone::Info: return "info";
        case Zone::Input: return "input"; case Zone::Browser: return "browser"; default: return "other";
    }
}

std::string macroSpecFor(const QKeyEvent* e) {
    std::string s;
    auto m = e->modifiers();
    if (m & Qt::ControlModifier) s += "CTRL+";
    if (m & Qt::ShiftModifier) s += "SHIFT+";
    if (m & Qt::AltModifier) s += "ALT+";
    std::string k;
    switch (e->key()) {
        case Qt::Key_Up: k = "NORTH"; break;
        case Qt::Key_Down: k = "SOUTH"; break;
        case Qt::Key_Left: k = "WEST"; break;
        case Qt::Key_Right: k = "EAST"; break;
        case Qt::Key_Return: case Qt::Key_Enter: k = "RETURN"; break;
        case Qt::Key_Escape: k = "ESCAPE"; break;
        case Qt::Key_Backspace: k = "BACK"; break;
        case Qt::Key_Space: k = "SPACE"; break;
        case Qt::Key_Tab: k = "TAB"; break;
        case Qt::Key_Delete: k = "DELETE"; break;
        case Qt::Key_Insert: k = "INSERT"; break;
        case Qt::Key_Home: k = "NORTHWEST"; break;   // stock names the navigation keys by compass point
        case Qt::Key_End: k = "SOUTHWEST"; break;
        case Qt::Key_PageUp: k = "NORTHEAST"; break;
        case Qt::Key_PageDown: k = "SOUTHEAST"; break;
        default: {
            if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Control || e->key() == Qt::Key_Alt || e->key() == Qt::Key_Meta) return "";
            k = QKeySequence(e->key()).toString().toUpper().toStdString();
        }
    }
    return s + k;
}

Zone FocusFilter::zoneOf(QObject* o) {
    // A client dialog that takes key names as text (the macro editor, shell/macrodialog.h) owns its keys, so typing F1
    // into its Key field does not fire the skin's F1.
    for (QObject* p = o; p; p = p->parent())
        if (p->property("fugueZone").toString() == "other") return Zone::Other;
    for (; o; o = o->parent()) {
        if (qobject_cast<BrowserPane*>(o)) return Zone::Browser;
        if (qobject_cast<MapWidget*>(o)) return Zone::Map;
        if (qobject_cast<QLineEdit*>(o)) return Zone::Input;
        QVariant z = o->property("fugueZone");
        if (z.isValid()) return z.toString() == "output" ? Zone::Output : Zone::Info;
    }
    return Zone::Other;
}

// The macro a release fires is named by the key and the modifiers its press carried, so a CTRL+A+UP binding still
// fires when Ctrl is released first.
static std::string releaseSpecFor(const QKeyEvent* k, Qt::KeyboardModifiers pressMods) {
    std::string base = macroSpecFor(k);
    size_t plus = base.rfind('+');
    std::string key = plus == std::string::npos ? base : base.substr(plus + 1);
    if (k->key() == Qt::Key_Plus) key = "+";
    return std::string((pressMods & Qt::ControlModifier) ? "CTRL+" : "") + ((pressMods & Qt::ShiftModifier) ? "SHIFT+" : "") +
           ((pressMods & Qt::AltModifier) ? "ALT+" : "") + key;
}

bool FocusFilter::eventFilter(QObject* o, QEvent* ev) {
    if (ev->type() == QEvent::ApplicationDeactivate) { releaseHeld("application deactivated"); return false; }
    if (ev->type() != QEvent::KeyPress && ev->type() != QEvent::KeyRelease) return false;
    if (inDispatch_) return false;                           // reroute guard: keys during our own dispatch
    auto* k = static_cast<QKeyEvent*>(ev);
    // A release belongs to its press, wherever it lands. Movement is a held-key state machine on the server (a press
    // starts a move, the release stops it), so a routed press without a routed release leaves the mob walking. A popup
    // takes the keyboard, so a key released while a menu is open reaches the menu, not the focus widget. The release
    // is therefore routed in the zone its press was routed in, whether it reaches the focus widget or the active
    // popup. The pressMods_ entry is taken on the first copy, so the double delivery still fires it once.
    if (ev->type() == QEvent::KeyRelease && !k->isAutoRepeat() && pressMods_.contains(k->key()) &&
        (o == QApplication::focusWidget() || (QApplication::activePopupWidget() && o == QApplication::activePopupWidget()))) {
        Held h = pressMods_.take(k->key());
        Zone here = o == QApplication::focusWidget() ? zoneOf(o) : Zone::Other;
        bool routed = route(h.zone, k->key(), k->modifiers(), false, releaseSpecFor(k, h.mods), true);
        return routed && here != Zone::Browser && here != Zone::Other;
    }
    if (o != QApplication::focusWidget()) return false;      // reroute guard: only the focus widget's copy
    Zone z = zoneOf(o);
    if (z == Zone::Browser || z == Zone::Other) {   // the page owns its keys
        std::string sp = macroSpecFor(k);
        if (ev->type() == QEvent::KeyPress && !sp.empty()) std::fprintf(stderr, "[focus] zone=%s key=%s -> page owns it (not routed)\n", zoneName(z), sp.c_str());
        return false;
    }
    // A paste chord in a paste-wired editor belongs to the editor (shell/paste.h), so a skin binding on CTRL+V does not take it.
    if (ev->type() == QEvent::KeyPress && z == Zone::Input && isPasteWired(qobject_cast<QWidget*>(o)) && isPasteChord(k)) return false;
    std::string spec = macroSpecFor(k);
    if (spec.empty() || spec.back() == '+') return false;   // a bare modifier key
    // An auto-repeat release is not a release. X11 auto-repeat sends release and press pairs while a key is held, and
    // Qt marks both isAutoRepeat(). Routing the repeat's release as +UP would stop held-key movement on every repeat,
    // so only the final, real release (isAutoRepeat() false) is a key-up.
    if (ev->type() == QEvent::KeyRelease && k->isAutoRepeat()) return true;
    if (ev->type() == QEvent::KeyPress && !k->isAutoRepeat()) pressMods_[k->key()] = Held{k->modifiers(), z};
    return route(z, k->key(), k->modifiers(), k->isAutoRepeat(), spec, ev->type() == QEvent::KeyRelease);
}

void FocusFilter::releaseHeld(const char* why) {
    // A key released while another application has the keyboard never reaches this filter, so a held walk key would
    // stay down on the server. Release everything still held, as if let go now.
    auto held = pressMods_;
    pressMods_.clear();
    for (auto it = held.cbegin(); it != held.cend(); ++it) {
        QKeyEvent synth(QEvent::KeyRelease, it.key(), Qt::NoModifier);
        std::fprintf(stderr, "[focus] %s: releasing held key %s\n", why, macroSpecFor(&synth).c_str());
        route(it.value().zone, it.key(), it.value().mods, false, releaseSpecFor(&synth, it.value().mods), true);
    }
}

bool FocusFilter::route(Zone z, int key, Qt::KeyboardModifiers mods, bool autoRep, const std::string& spec, bool up) {
    // Unbound arrow = built-in walk key (never in the input bar, where arrows move the text cursor).
    auto builtin = [&]() -> bool {
        if (z == Zone::Input || (mods & (Qt::ControlModifier | Qt::AltModifier))) return false;
        int dir = key == Qt::Key_Up ? 1 : key == Qt::Key_Down ? 2 : key == Qt::Key_Right ? 4 : key == Qt::Key_Left ? 8 : 0;
        if (!dir) return false;
        if (autoRep) return true;   // swallow repeats: the held state is the server's to track
        std::fprintf(stderr, "[focus] zone=%s key=%s%s -> BUILT-IN MOVE dir=%d\n", zoneName(z), spec.c_str(), up ? "+UP" : "", dir);
        emit builtinMove(dir, !up);
        return true;
    };
    if (spec.empty()) return false;
    if (!have_ && userGame_.entries.empty() && userGlobal_.entries.empty()) return builtin();
    if (z == Zone::Input) {
        // plain keys type; only Ctrl or Alt combinations, and the function keys, which never type, can be macros
        bool fkey = key >= Qt::Key_F1 && key <= Qt::Key_F35;
        if (!(mods & (Qt::ControlModifier | Qt::AltModifier)) && !fkey) return false;
    }
    skin::MacroKey mk; bool rep = false;
    if (!skin::parseMacroName(spec, mk, rep)) return false;
    mk.up = up;
    // The skin's set first; the player's lists only for a key it leaves unbound (skin/usermacros.h).
    const skin::MacroEntry* me = skin::lookupLayered(have_ ? &macros_ : nullptr, userGame_, userGlobal_, mk, !up && autoRep);
    if (!me) {
        if (builtin()) return true;
        std::fprintf(stderr, "[focus] zone=%s key=%s%s -> no macro (passes on)\n", zoneName(z), spec.c_str(), up ? "+UP" : "");
        return false;
    }
    inDispatch_ = true;
    fired_++;
    std::fprintf(stderr, "[focus] zone=%s key=%s%s -> MACRO \"%s\"%s\n", zoneName(z), spec.c_str(), up ? "+UP" : "",
                 me->command.c_str(), me->disabled ? " (disabled, still claims the key)" : "");
    emit macroFired(QString::fromStdString(spec), QString::fromStdString(me->command), up);
    inDispatch_ = false;
    return true;
}

}  // namespace fugue::shell
