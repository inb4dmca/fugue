#include "skin/macro.h"

#include <tuple>

#include "skin/layout.h"

namespace fugue::skin {

bool MacroKey::operator<(const MacroKey& o) const {
    return std::tie(key, ctrl, shift, alt, up) < std::tie(o.key, o.ctrl, o.shift, o.alt, o.up);
}

std::string MacroKey::toString() const {
    std::string s;
    if (ctrl) s += "CTRL+";
    if (shift) s += "SHIFT+";
    if (alt) s += "ALT+";
    s += key;
    if (up) s += "+UP";
    return s;
}

static std::string canonKeyName(const std::string& t) {
    if (t == "ENTER") return "RETURN";
    if (t == "ESC") return "ESCAPE";
    if (t == "BACKSPACE" || t == "BKSP") return "BACK";
    if (t == "INS") return "INSERT";
    if (t == "DEL") return "DELETE";
    return t;
}

bool parseMacroName(const std::string& spec, MacroKey& k, bool& rep) {
    k = MacroKey{};
    rep = false;
    std::string key;
    std::vector<std::string> mods;
    size_t pos = 0;
    while (pos <= spec.size()) {
        size_t pl = spec.find('+', pos);
        std::string tok = spec.substr(pos, (pl == std::string::npos ? spec.size() : pl) - pos);
        pos = pl == std::string::npos ? spec.size() + 1 : pl + 1;
        std::string t = upperAscii(trimSpace(tok));
        if (t.empty()) continue;
        if (t == "CTRL" || t == "CONTROL") { mods.push_back("CTRL"); k.ctrl = true; }
        else if (t == "SHIFT") { mods.push_back("SHIFT"); k.shift = true; }
        else if (t == "ALT") { mods.push_back("ALT"); k.alt = true; }
        else if (t == "REP") rep = true;
        else if (t == "UP") k.up = true;
        else if (key.empty()) key = canonKeyName(t);  // a second key token is dropped
    }
    if (key.empty()) {
        if (mods.empty()) { k = MacroKey{}; return false; }
        key = mods.back();  // all-modifier spec: the LAST one is the key
        if (key == "CTRL") k.ctrl = false;
        else if (key == "SHIFT") k.shift = false;
        else if (key == "ALT") k.alt = false;
    }
    k.key = key;
    return true;
}

const MacroEntry* MacroSet::lookup(const MacroKey& k, bool repeat) const {
    const MacroEntry* e = find(k, repeat);
    return e && !e->disabled ? e : nullptr;  // a disabled binding still CLAIMS the key
}

const MacroEntry* MacroSet::find(const MacroKey& k, bool repeat) const {
    MacroKey any;
    any.key = "ANY";
    any.up = k.up;
    std::vector<std::pair<MacroKey, bool>> cands;
    if (repeat) {
        cands.push_back({k, true});
        if (k.key != "ANY") cands.push_back({any, true});
    } else if (k.key == "ANY") {
        cands = {{k, false}, {k, true}};
    } else {
        cands = {{k, false}, {k, true}, {any, false}, {any, true}};
    }
    for (const auto& c : cands) {
        auto it = byKey.find(c);
        if (it == byKey.end()) continue;
        return &entries[it->second];
    }
    return nullptr;
}

MacroSet macroSetFromBlock(const Block& b) {
    MacroSet set;
    set.name = b.name;
    for (const Elem& e : b.elems) {
        std::string spec = e.str("name", e.name);
        MacroKey k;
        bool rep;
        if (!parseMacroName(spec, k, rep)) continue;
        MacroEntry ent;
        ent.id = e.name; ent.spec = spec; ent.command = e.str("command", "");
        ent.key = k; ent.rep = rep; ent.disabled = e.boolv("is-disabled", false);
        auto lk = std::make_pair(k, rep);
        auto prev = set.byKey.find(lk);
        if (prev != set.byKey.end()) { set.entries[prev->second] = ent; continue; }  // later wins, in place
        set.byKey[lk] = set.entries.size();
        set.entries.push_back(ent);
    }
    return set;
}

MacroTable resolveMacros(const File& f) {
    MacroTable T;
    for (const Block& b : f.blocks) {
        if (b.kind != "macro") continue;
        T.sets[b.name] = macroSetFromBlock(b);
    }
    if (const Block* mw = findMainWindow(f))
        if (const Elem* m = mw->main()) T.active = m->str("macro", "");
    if (!T.active.empty() && !T.sets.count(T.active)) T.active.clear();
    if (T.active.empty() && T.sets.size() == 1) T.active = T.sets.begin()->first;
    return T;
}

}  // namespace fugue::skin
