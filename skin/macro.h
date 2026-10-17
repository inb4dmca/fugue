// skin/macro.h - `.dmf` macro specs and macro sets (the keybind tables).
//
// The DM Reference "macros (skin)" entry: the macro `name` is a key combination as it would appear in the macro editor,
//   such as CTRL+E, Space+REP or Alt+Shift+F1. It is not case-specific and modifiers may go anywhere, so the spec is
//   parsed into a set rather than string-compared. `+REP` repeats while held; `+UP` fires on release; `Any` matches any
//   key and allows only UP.
// Real skins bind both `F2` and `F2+REP` (baystation12), and `WEST` and `WEST+UP` (vg). So +REP and +UP are part of a
//   binding's identity, not flags on one binding. A press prefers the non-REP binding and falls back to the REP one; a
//   repeat matches only a REP binding; a disabled binding still claims its key (no fall-through).
// Not stated by any document: an all-modifier spec (vg's `CTRL+SHIFT`) reads its last modifier as the key; a second key
//   token is dropped; the aliases ENTER, ESC, BACKSPACE/BKSP, INS and DEL fold to RETURN, ESCAPE, BACK, INSERT and
//   DELETE; the key is upper-cased.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "skin/dmf.h"

namespace fugue::skin {

struct MacroKey {
    std::string key;  // upper-cased canonical name: "A", "F11", "NORTH", "ANY"
    bool ctrl = false, shift = false, alt = false, up = false;
    bool operator==(const MacroKey& o) const {
        return key == o.key && ctrl == o.ctrl && shift == o.shift && alt == o.alt && up == o.up;
    }
    bool operator<(const MacroKey& o) const;
    std::string toString() const;  // CTRL+SHIFT+ALT+KEY[+UP]
};

// Returns false when the spec names no key (empty, or only REP/UP tokens); rep is still set then.
bool parseMacroName(const std::string& spec, MacroKey& k, bool& rep);

struct MacroEntry {
    std::string id, spec, command;
    MacroKey key;
    bool rep = false, disabled = false;
};

struct MacroSet {
    std::string name;
    std::vector<MacroEntry> entries;
    std::map<std::pair<MacroKey, bool>, size_t> byKey;  // (combination, rep) -> entries index

    const MacroEntry* lookup(const MacroKey& k, bool repeat) const;
    // The binding that CLAIMS the key, disabled or not (lookup() is find() minus a disabled binding).
    const MacroEntry* find(const MacroKey& k, bool repeat) const;
};

struct MacroTable {
    std::map<std::string, MacroSet> sets;  // later datum of the same name replaces the earlier
    std::string active;                    // the MAIN window's `macro`, or the sole set; "" when none
};

MacroTable resolveMacros(const File& f);
// One macro datum's entries (the rule resolveMacros applies per set: a later duplicate binding wins, in place).
MacroSet macroSetFromBlock(const Block& b);

}  // namespace fugue::skin
