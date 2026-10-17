#include "shell/inputrouter.h"
#include <algorithm>
#include <cctype>
#include "skin/winset.h"

namespace fugue::shell {
namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::string lower(std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; }
}  // namespace

Route routeCommand(const std::string& command, const std::string& keyName, bool up) {
    std::string cmd = command;
    for (size_t p; (p = cmd.find("[[*]]")) != std::string::npos;) cmd.replace(p, 5, keyName);   // the Any macro's [[*]] is the key that fired
    cmd = trim(cmd);
    Route r;
    if (cmd.empty()) return r;
    if (cmd[0] != '.') { r.kind = Route::Command; r.text = cmd; return r; }
    size_t sp = cmd.find(' ');
    std::string verb = lower(cmd.substr(0, sp));
    static const struct { const char* name; int dir; } dirs[] = {
        {".north", 1}, {".south", 2}, {".east", 4}, {".west", 8},
        {".northeast", 5}, {".northwest", 9}, {".southeast", 6}, {".southwest", 10}};
    // A `.northwest` macro is the stock move command, not a walk. The server answers it by calling client.Northwest(),
    // which SS13 forks override: vg's Northwest() treats the hotkey as a drop. Stock DreamSeeker never routes it to
    // the held-key MoveKey verb, so it is sent as the sub-0 move.
    for (auto& d : dirs)
        if (verb == d.name) { r.kind = Route::Move; r.dir = d.dir; r.down = !up; return r; }
    if (verb == ".quit") { r.kind = Route::Quit; return r; }
    if (verb == ".screenshot") {   // the Client menu and F2 / Shift+F2 (semantics in shell/screenshot.h)
        r.kind = Route::Screenshot;
        if (sp != std::string::npos && lower(trim(cmd.substr(sp))) == "auto") r.text = "auto";
        return r;
    }
    // Stock DreamSeeker's own command table lists both next to `.screenshot`: a skin menu item `.macros` opens the
    // Macros dialog and `.options` the Options and Messages window (skin/usermacros.h).
    if (verb == ".macros") { r.kind = Route::Macros; return r; }
    if (verb == ".options") { r.kind = Route::Options; return r; }
    if (verb == ".winset") {
        // The macro swap is detected for the decision, but the whole string is passed on (winsetBody), so every other
        // key in it (focus, is-checked, left and right) still applies.
        std::string body;
        if (!skin::winsetBody(cmd, body)) body = trim(cmd.substr(sp == std::string::npos ? cmd.size() : sp));
        r.winsetBody = body;
        std::string l = lower(cmd);
        size_t i = l.find(".macro=");
        if (i != std::string::npos) {
            std::string rest = cmd.substr(i + 7);
            size_t j = rest.find_first_of(" ;\"'");
            if (j != std::string::npos) rest = rest.substr(0, j);
            rest = trim(rest);
            if (!rest.empty()) { r.kind = Route::SetMacroSet; r.text = rest; return r; }
        }
        if (!body.empty()) { r.kind = Route::Winset; r.text = body; return r; }
    }
    r.kind = Route::Unsupported;
    r.text = cmd;
    return r;
}

Route routeWorldCommand(const std::string& command) {
    Route r = routeCommand(command, "", false);
    if (r.kind == Route::Unsupported) r.kind = Route::Command;
    return r;
}

}  // namespace fugue::shell
