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
    std::string cmd = command, name;
    for (char c : keyName) {
        if (c == '\\' || c == '"' || c == '[' || c == ']') name += '\\';
        name += c;
    }
    for (size_t p = 0; (p = cmd.find("[[*]]", p)) != std::string::npos;) {
        bool bare = keyName.empty() || (p > 0 && cmd[p - 1] == '"' && p + 5 < cmd.size() && cmd[p + 5] == '"');
        std::string v = bare ? name : '"' + name + '"';
        cmd.replace(p, 5, v);
        p += v.size();
    }
    std::string line = cmd.substr(std::min(cmd.size(), cmd.find_first_not_of(" \t\r\n")));
    cmd = trim(cmd);
    Route r;
    if (cmd.empty()) return r;
    if (cmd[0] != '.') { r.kind = Route::Command; r.text = line; return r; }   // trailing spaces belong to a command_text argument
    size_t sp = cmd.find(' ');
    std::string verb = lower(cmd.substr(0, sp));
    static const struct { const char* name; int dir; } dirs[] = {
        {".north", 1}, {".south", 2}, {".east", 4}, {".west", 8},
        {".northeast", 5}, {".northwest", 9}, {".southeast", 6}, {".southwest", 10}, {".center", 0}};
    // A `.northwest` macro is the stock move command, not a walk. The server answers it by calling client.Northwest(),
    // which SS13 forks override: vg's Northwest() treats the hotkey as a drop. Stock DreamSeeker never routes it to
    // the held-key MoveKey verb, so it is sent as the sub-0 move.
    for (auto& d : dirs)
        if (verb == d.name) { r.kind = Route::Move; r.dir = d.dir; r.down = !up; return r; }
    if (verb == ".quit") { r.kind = Route::Quit; return r; }
    if (verb == ".reconnect") { r.kind = Route::Reconnect; return r; }
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

std::vector<std::string> macroCommandLines(const std::string& command) {
    std::vector<std::string> out;
    for (size_t a = 0; a <= command.size();) {
        size_t b = command.find('\n', a);
        if (b == std::string::npos) b = command.size();
        std::string l = command.substr(a, b - a);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty() && l[0] != ' ' && l[0] != '\t') out.push_back(l);
        a = b + 1;
    }
    return out;
}

Route routeWorldCommand(const std::string& command) {
    Route r = routeCommand(command, "", false);
    if (r.kind == Route::Unsupported) r.kind = Route::Command;
    return r;
}

}  // namespace fugue::shell
