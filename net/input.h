// Client input state and the command line -> frame path (verb matching, arguments, `say "text"`).
//
// InputModel is fed the s2c frames it needs (0x0011 verb entries, 0x003b mouse subscription flags) and turns a typed
// command line into the 0x0002 bodies a stock DreamSeeker sends. It does not depend on model/ (net must stay below
// it). The one thing it cannot know alone, which atom's verb section holds a verb id (model::verbSrcCandidates), is a
// callback the caller supplies.
//
// Verb matching (InputModel::matchVerb): a verb is typed under its name with each space written `-`, case-insensitively.
// Ties go to the lower id. Each argument's typemask decides how it is parsed, and the last text-like argument takes the
// rest of the line unquoted. The verb's src gives
// the src-kind, and exactly one frame is sent with it. No verb is ever chosen by its argument shape: movement is the
// stock move command (stockMoveFrame) or whatever verb a macro names. Mouse subscription flags are the 0x003b bits.
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "net/bytes.h"
#include "proto/enc_c2s_0002_input.h"

namespace fugue::net {

struct VerbArgSpec { uint32_t typeMask = 0; uint8_t filter = 0, filterKind = 0; };
struct VerbEntry {
    uint16_t id = 0;
    std::string name, desc, category;
    uint8_t head = 0, src = 0, flags = 0;
    std::vector<VerbArgSpec> args;
};

constexpr uint32_t kMouseFlagClick = 0x00000004, kMouseFlagDblClick = 0x00000008, kMouseFlagUp = 0x00200000,
                   kMouseFlagDown = 0x00400000, kMouseFlagDrop = 0x00800000, kMouseFlagDrag = 0x01000000,
                   kMouseFlagExtra = 0x80000000;
// The second flags word (present iff kMouseFlagExtra): client/MouseMove 0x01, client/MouseWheel 0x02.
constexpr uint32_t kMouseExtraWheel = 0x00000002;

struct FrameOut { uint16_t type = 0x0002; Bytes body; };
// A verb's src as the wire carries it: [kind][ref u32] (0x08 client / ref 0, 0x02 mob, 0x01 obj, 0x03 turf cell).
struct VerbSrc { uint8_t kind = proto::enc::kVerbSrcMob; uint32_t ref = 0; };
// One atom a verb could run on (model::verbSrcCandidates) and the name DreamSeeker lists it under.
struct SrcCandidate { VerbSrc src; std::string name; bool named = false; };
// A verb whose command line ran out before its arguments did. DreamSeeker does not refuse such a line: it opens a dialog
// for the argument and, on OK, sends the verb with that value as the argument verbatim. The frame is built by
// completeArg, never by re-parsing a line.
struct MissingArg {
    VerbEntry verb;
    size_t index = 0;   // the zero-based argument the dialog fills
    std::string line;   // the command line so far (the dialog's title), for display only - never re-parsed
    std::vector<proto::enc::VerbArg> args;   // the arguments before `index`, already parsed (args.size() == index)
    VerbSrc src;        // the verb's src, resolved when the line was parsed: the 0x003f request and the final 0x0002 carry it
    bool promptNull = false;   // a nullable argument gets a dialog too, not a null (see verbCommand)
    // An atom argument's listbox choices (no 0x003f is sent).
    std::optional<std::vector<SrcCandidate>> atoms;
};
// A verb that several differently-named atoms carry. Stock DreamSeeker does not guess: it opens a listbox titled
// `<Verb> word` of their names and sends the verb on the picked one. The caller asks, then re-runs `line` with only
// the picked candidate.
struct SrcChoice {
    std::string title;                    // `Use word`
    std::string line;                     // the command line, re-run once a src is picked
    std::vector<SrcCandidate> candidates; // DreamSeeker's list order
    bool promptNull = false;
};
// An `in <list>` argument DreamSeeker does not send as text: it asks the server for the value first. The client sends
// c2s 0x0039 = the 0x003f request's prefix with the choice text in the last slot. The server answers with s2c 0x0039 =
// [kind u8][value u32][u32 0], and the 0x0002 then carries that value as a tag-0 argument [kind][value]. A text not in
// the list comes back null (`00 ffff0000`), and DreamSeeker sends nothing. This applies to FilterKind 0x40 with no
// text, message or num bit in the mask. `as text in list` goes out as a plain string, with no 0x0039.
struct ListResolve {
    MissingArg at;      // the verb, at.index = the list argument, at.args the arguments before it, at.src, at.line (display)
    std::string text;   // the 0x0039's text: the typed token as typed, or listPickText of a listbox choice
    std::string rest;   // the typed line after this argument, still to parse ("" from a dialog)
};
// A command line either becomes frames (sent in order) or a local refusal with DreamSeeker's own wording. A refusal for a
// MISSING argument also carries `missing`: the caller should prompt (argPrompt) instead of printing `error`; one for an
// ambiguous src carries `srcChoice`: the caller asks which; one for a list argument carries `resolve`: the caller sends
// encodeVerbExpansionRequest(resolve->at, resolve->text) as a 0x0039 and feeds the s2c 0x0039 to completeResolve.
struct CommandResult {
    std::string error;
    bool quiet = false;   // `error` is for the log: DreamSeeker shows nothing
    std::vector<FrameOut> frames;
    std::optional<MissingArg> missing;
    std::optional<SrcChoice> srcChoice;
    std::optional<ListResolve> resolve;
};

// The dialog DreamSeeker opens for a missing verb argument.
struct ArgPrompt {
    enum Kind { Text, Message, Num, List } kind = Text;
    std::string title;      // `Say "text"`: the line so far + the argument's token
    std::string message;    // the verb's own `set desc` (empty: no text above the field)
    bool allowNull = false; // the mask carries null (0x80): the dialog has a None button (completeNull)
    uint8_t ordinal = 0;    // List: the 0x003f verb-expansion ordinal (the argument's own Filter byte)
    std::vector<std::string> choices;   // List of atoms DreamSeeker lists itself (MissingArg::atoms): no 0x003f
};
ArgPrompt argPrompt(const MissingArg& m);
// The candidate a TYPED atom-argument token names (raw, as typed: backslashes and all), or null, by stock DreamSeeker's
// own matching. `cands` is in model::verbArgCandidates' order: ties go to the first.
const SrcCandidate* atomTyped(const std::vector<SrcCandidate>& cands, const std::string& raw);
// How stock DreamSeeker names an atom to the server in a menu pick's 0x0039: its name with each space written `-`,
// then `.0x` and its DM \ref number in lowercase hex. E.g. "Big-Box.0x2000005" for an obj named "Big Box".
std::string atomArgumentText(const std::string& name, uint32_t dmRef);
// A verb name as typed and as DreamSeeker titles its dialogs: each space written `-`.
std::string verbCommandName(const std::string& name);
// A listbox choice as stock DreamSeeker names it to the server: escapeVerbText, a tab as `\t`, a CR dropped.
std::string escapeChoiceText(const std::string& choice);
// A listbox pick's text in the c2s 0x0039: escapeChoiceText, each space written `-`.
std::string listPickText(const std::string& choice);
// A 0x003f choice as DreamSeeker's listbox shows it: DreamDaemon backslashes a choice's first character when it is `\`,
// `'` or `"`, and nothing else.
std::string unescapeVerbText(const std::string& v);
// A dialog's or an input()'s text as stock DreamSeeker puts it on the wire: `\`, `"` and `'` backslashed, a newline as
// `\n`. DreamDaemon parses those escapes back out, so the world receives the text as typed.
std::string escapeVerbText(const std::string& v);
// The c2s 0x003f "verb expansion" request for a List prompt's choices: [filter u16][typeMask u32][src kind][src ref u32]
// [prior-arg count][prior args][typed text ""\0]. `text` is the argument's text so far: "" for the 0x003f a dialog
// sends, the choice for the 0x0039 (ListResolve).
Bytes encodeVerbExpansionRequest(const MissingArg& m, const std::string& text = "");
constexpr uint16_t kVerbExpansionMsg = 0x003f, kListResolveMsg = 0x0039;
// The s2c 0x0039 answer (ListResolve): [kind u8][value u32][u32]. False: too short.
bool parseListResolveReply(ByteSpan b, uint8_t& kind, uint32_t& value);

class InputModel {
public:
    // Feed every s2c frame (plain body). Cheap; ignores types it does not need.
    void onFrame(uint16_t type, ByteSpan body);

    bool haveMouseSubs() const { return haveSubs_; }
    uint32_t mouseFlags() const { return flags_; }
    proto::enc::MouseHooks hooks() const;
    // The world declares client/MouseWheel: stock sends every wheel notch over the map, whatever is under it.
    bool clientWheel() const { return extra_ & kMouseExtraWheel; }

    const std::map<uint16_t, VerbEntry>& verbs() const { return verbs_; }
    const VerbEntry* verbByName(const std::string& name) const;       // case-insensitive exact
    // The verb a command line names, and the rest of the line in `rest`. *firstWord: named by the first word.
    const VerbEntry* matchVerb(const std::string& line, std::string& rest, bool* firstWord = nullptr) const;
    // DreamSeeker's command-bar completion for a space typed at the end of `line`: true + the bar's new text (space
    // included; ` "` when the next argument is text), false to type a plain space.
    bool spaceCompletion(const std::string& line, std::string& out) const;

    // The verb's candidate srcs (model::verbSrcCandidates). None: the line is refused locally and nothing is sent.
    // One: it goes out on that src. Several: `srcChoice` (DreamSeeker asks).
    using SrcResolver = std::function<std::vector<SrcCandidate>(uint16_t verbId)>;
    // The atoms a typed atom argument may name (model::verbArgCandidates), or nullopt for an argument DreamSeeker does
    // not resolve itself (text, num, `in world`, a runtime list). A token names a candidate by its whole name or its
    // last words, with `-` for a space and a backslashed character matching only itself (atomTyped's table). A
    // candidate that is carried goes out as [tag 0][01][its ref]; a token that names none is refused locally. `as
    // anything` (no list) takes the same path. Without a resolver, or with nullopt, the token goes out as a string,
    // which stock DreamDaemon silently drops for an obj argument.
    using ArgResolver = std::function<std::optional<std::vector<SrcCandidate>>(const VerbArgSpec& arg)>;

    // `say "hi"` / `say hi` / `Pick thing` / `ExamineArg ref:12` ... Local refusals carry DreamSeeker's two lines (`Sorry,
    // the following is not valid: X` + `usage: ...`, joined by '\n'; the caller prints each), or `Unrecognized or
    // inaccessible verb: <first word>`. A missing argument sets `missing`. A nullable argument the line ran out before goes
    // out null (as from the command bar), unless something earlier was filled in by dialog or a named src, or promptNull
    // (a stat-panel or right-click verb) is set: then it gets a dialog with None.
    CommandResult verbCommand(const std::string& line, bool haveSelf, const SrcResolver& src, const ArgResolver& atoms = {},
                              bool promptNull = false) const;
    // OK on a missing-argument dialog: `value` becomes argument m.index verbatim (a string for text / message / a list
    // choice, a number for `as num`). It is never appended to the line and re-parsed. Returns the frames, or `missing`
    // for the next argument's dialog. The verb keeps the src resolved when its line was parsed (m.src).
    // A List prompt's OK returns `resolve`. Its text is `value`, which must be the 0x003f string as sent, so the caller
    // maps the listbox's unescaped display text back to it.
    static CommandResult completeArg(const MissingArg& m, const std::string& value, bool haveSelf, const ArgResolver& atoms = {});
    // None on a nullable argument's dialog: the argument goes out null (tag 0, kind 0, ref 0xffff), with no 0x0039.
    static CommandResult completeNull(const MissingArg& m, bool haveSelf, const ArgResolver& atoms = {});
    // The server's s2c 0x0039 answer to r: the argument becomes [kind][value] and the rest of the line is parsed on
    // (another list argument, a missing one, or the frames). A null answer (kind 0, value 0xffff) is the local refusal.
    // There is no fallback: nothing else is sent meanwhile, so a server that never answers never runs the verb.
    static CommandResult completeResolve(const ListResolve& r, uint8_t kind, uint32_t value, bool haveSelf, const ArgResolver& atoms = {});
    // A right-click row the player's verb offers by argument (model::atomMenu viaArgument), picked on atom [kind][ref]:
    // the frames, or for a runtime-list argument the resolve stock DreamSeeker sends instead (c2s 0x0039 with the atom's
    // text, and the answer becomes the 0x0002's argument). `text` is atomArgumentText(). Other arguments go out at once
    // as [tag 0][kind][ref].
    static CommandResult atomArgument(const VerbEntry& e, const VerbSrc& src, uint8_t kind, uint32_t ref, const std::string& text, bool haveSelf,
                                      const std::string& atomName = "", const ArgResolver& atoms = {});
    // A right-click row of the atom's own verb on that src.
    static CommandResult atomVerb(const VerbEntry& e, const VerbSrc& src, const std::string& atomName, bool haveSelf, const ArgResolver& atoms = {});
    // s2c 0x0039 answers in arrival order (one TCP stream: the oldest outstanding 0x0039 owns the first). onFrame queues them.
    bool takeListReply(uint8_t& kind, uint32_t& value);
    void clearListReplies() { listReplies_.clear(); }

private:
    std::map<uint16_t, VerbEntry> verbs_;
    std::deque<std::pair<uint8_t, uint32_t>> listReplies_;
    bool haveSubs_ = false;
    uint32_t flags_ = 0, extra_ = 0;
};

// The stock move command (sub 0) that a skin's `.north` ... `.southwest` and `.center` send, once per firing and
// nothing on release. The server runs client.North() ... client.Center() for it.
FrameOut stockMoveFrame(int dir);

// "north"/"up"/"n" ... or a numeric BYOND dir (1 2 4 8, diagonals 5 6 9 10). Returns 0 if bad.
int parseDir(const std::string& s);

}  // namespace fugue::net
