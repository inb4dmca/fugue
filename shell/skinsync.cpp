#include "shell/skinsync.h"
#include "app/defaultskin.h"
#include "shell/focusfilter.h"
#include "shell/skinwindow.h"

namespace fugue::shell {

void applySkinMacros(const SkinWindow& win, FocusFilter& filt) {
    const skin::MacroTable t = win.macros();
    auto it = t.sets.find(win.activeMacro().toStdString());
    if (it != t.sets.end()) filt.setMacros(it->second);
    else if (!t.sets.empty()) filt.setMacros(t.sets.begin()->second);
    else filt.setMacros(skin::MacroSet{});
}

void worldSkinLoaded(SkinWindow& win, FocusFilter& filt, bool& sized) {
    win.syncFromLive();
    if (win.live()->loadedFrom(app::kDefaultSkin)) win.markBuiltinSkin();   // a world with no skin of its own
    if (!sized) { win.resize(win.designSize()); sized = true; }
    applySkinMacros(win, filt);
}

}  // namespace fugue::shell
