// Macro and command-bar routing: what a matched macro's command, or a typed line, does. Pure std::string in and a
// decision out, with no Qt and no socket, so the app only has to act on the decision.
//
// A macro's command takes the same path as a typed command, with no second parser. Server commands (verbs, `say`,
// `MoveKey`) become a command line for net::InputModel. DreamSeeker's own `.`-prefixed client commands run locally
// and are never sent.
#pragma once
#include <string>

namespace fugue::shell {

struct Route {
    enum Kind {
        Swallow,      // empty command: a deliberate "this key does nothing" (masks a default) - claim the key, do nothing
        Command,      // text = a command line for the session (verb invocation)
        Move,         // `.north` ... `.southwest`: dir = BYOND dir bits. The stock move command, which the server answers
                      // by calling client.North() and so on. Never the world's held-key movement verb (see routeCommand)
        SetMacroSet,  // text = macro set name (`.winset "mainwindow.macro=hotkeymode ..."`); winsetBody holds the whole winset
        Winset,       // a `.winset "..."` with no macro key (a skin button's `.winset "rpanewindow.left=;"`): text = params
        Quit,         // `.quit`
        Screenshot,   // `.screenshot` (dialog) or `.screenshot auto` (text = "auto": no dialog); shell/screenshot.h
        Macros,       // `.macros`: the macro editor (Client > Macros...; shell/macrodialog.h)
        Options,      // `.options`: the "Options and Messages" window, whose Client menu holds Macros...
        Unsupported,  // a `.` command this client cannot honour: recorded by the caller, never sent as a verb
    } kind = Swallow;
    std::string text;
    // For every `.winset` (SetMacroSet or Winset), the bare params inside the quotes. The app applies the whole string
    // through the live skin (skin/liveskin.h), since one string can swap the macro set, check a button and focus the
    // map. The macro swap is an effect of that, not a second parser.
    std::string winsetBody;
    int dir = 0;
    bool down = true;
};

// `keyName` is the macro key that fired (the `[[*]]` substitution of the Any macro); `up` is a +UP firing.
Route routeCommand(const std::string& command, const std::string& keyName, bool up);

// A command the world registered for the client to run (a browse window's on-close). It uses the same router with one
// difference: a `.`-prefixed name that is not a stock client command is the world's own verb, and is sent. The session
// checks it against the verb list (net::InputModel::verbCommand), so an unknown verb is refused there and never
// reaches the wire. For keys and the command bar, routeCommand never sends an unknown `.` command.
Route routeWorldCommand(const std::string& command);

}  // namespace fugue::shell
