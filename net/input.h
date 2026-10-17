// Client input state and the command line -> frame path (verb matching, arguments, `say "text"`).
//
// InputModel is fed the s2c frames it needs (0x0011 verb entries, 0x003b mouse subscription flags) and turns a typed
// command line into the 0x0002 bodies a stock DreamSeeker sends. It does not depend on model/ (net must stay below
// it). The one thing it cannot know alone, which atom's verb section holds a verb id (model::verbSrcCandidates), is a
// callback the caller supplies.
//
// Verb matching: the longest case-insensitive name prefix wins; ties go to the lower id. Each argument's typemask
// decides how it is parsed, and the last text-like argument takes the rest of the line unquoted. The verb's src gives
// the src-kind, and exactly one frame is sent with it. MoveVerb is identified by its signature: instant flag 0x80,
// 1-2 args, all typemask 8; a name containing move/walk/step/dir is preferred. Mouse subscription flags are the 0x003b
// bits.
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

struct FrameOut { uint16_t type = 0x0002; Bytes body; };
// A verb's src as the wire carries it: [kind][ref u32] (0x08 client / ref 0, 0x02 mob, 0x01 obj, 0x03 turf cell).
struct VerbSrc { uint8_t kind = proto::enc::kVerbSrcMob; uint32_t ref = 0; };
// A verb whose command line ran out before its arguments did. DreamSeeker does not refuse such a line: it opens a dialog
// for the argument and, on OK, sends the verb with that value as the argument verbatim. The frame is built by
// completeArg, never by re-parsing a line.
struct MissingArg {
    VerbEntry verb;
    size_t index = 0;   // the zero-based argument the dialog fills
    std::string line;   // the command line so far (the dialog's title), for display only - never re-parsed
    std::vector<proto::enc::VerbArg> args;   // the arguments before `index`, already parsed (args.size() == index)
    VerbSrc src;        // the verb's src, resolved when the line was parsed: the 0x003f request and the final 0x0002 carry it
};
// One atom a verb could run on (model::verbSrcCandidates) and the name DreamSeeker lists it under.
struct SrcCandidate { VerbSrc src; std::string name; };
// A verb that several differently-named atoms carry. Stock DreamSeeker does not guess: it opens a listbox titled
// `<Verb> word` of their names and sends the verb on the picked one. The caller asks, then re-runs `line` with only
// the picked candidate.
struct SrcChoice {
    std::string title;                    // `Use word`
    std::string line;                     // the command line, re-run once a src is picked
    std::vector<SrcCandidate> candidates; // DreamSeeker's list order
};
// An `in <list>` argument DreamSeeker does not send as text: it asks the server for the value first. The client sends
// c2s 0x0039 = the 0x003f request's prefix with the choice text in the last slot. The server answers with s2c 0x0039 =
// [kind u8][value u32][u32 0], and the 0x0002 then carries that value as a tag-0 argument [kind][value]. A text not in
// the list comes back null (`00 ffff0000`), and DreamSeeker sends nothing. This applies to FilterKind 0x40 with no
// text, message or num bit in the mask. `as text in list` goes out as a plain string, with no 0x0039.
struct ListResolve {
    MissingArg at;      // the verb, at.index = the list argument, at.args the arguments before it, at.src, at.line (display)
    std::string text;   // the 0x0039's text: the typed token as typed, or a listbox choice exactly as the 0x003f sent it
    std::string rest;   // the typed line after this argument, still to parse ("" from a dialog)
};
// A command line either becomes frames (sent in order) or a local refusal with DreamSeeker's own wording. A refusal for a
// MISSING argument also carries `missing`: the caller should prompt (argPrompt) instead of printing `error`; one for an
// ambiguous src carries `srcChoice`: the caller asks which; one for a list argument carries `resolve`: the caller sends
// encodeVerbExpansionRequest(resolve->at, resolve->text) as a 0x0039 and feeds the s2c 0x0039 to completeResolve.
struct CommandResult {
    std::string error;
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
    bool allowNull = false; // the mask carries null (0x80); never prompted from a typed line, it goes out null
    uint8_t ordinal = 0;    // List: the 0x003f verb-expansion ordinal (the argument's own Filter byte)
};
ArgPrompt argPrompt(const MissingArg& m);
// The candidate a TYPED atom-argument token names (raw, as typed: backslashes and all), or null, by stock DreamSeeker's
// own matching. `cands` is in model::verbArgCandidates' order: ties go to the first.
const SrcCandidate* atomTyped(const std::vector<SrcCandidate>& cands, const std::string& raw);
// How stock DreamSeeker names an atom to the server in a menu pick's 0x0039: its name with each space written `-`,
// then `.0x` and its DM \ref number in lowercase hex. E.g. "Big-Box.0x2000005" for an obj named "Big Box".
std::string atomArgumentText(const std::string& name, uint32_t dmRef);
// A 0x003f choice as DreamSeeker's listbox shows it: DreamDaemon backslashes a quote in it (`\'q` for 'q), and only a quote.
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

    const std::map<uint16_t, VerbEntry>& verbs() const { return verbs_; }
    const VerbEntry* verbByName(const std::string& name) const;       // case-insensitive exact
    const VerbEntry* matchVerb(const std::string& line, std::string& rest) const;
    const VerbEntry* moveVerb() const;
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
    // inaccessible verb: <line>`. A missing argument sets `missing`.
    CommandResult verbCommand(const std::string& line, bool haveSelf, const SrcResolver& src, const ArgResolver& atoms = {}) const;
    // OK on a missing-argument dialog: `value` becomes argument m.index verbatim (a string for text / message / a list
    // choice, a number for `as num`). It is never appended to the line and re-parsed. Returns the frames, or `missing`
    // for the next argument's dialog. The verb keeps the src resolved when its line was parsed (m.src).
    // A List prompt's OK returns `resolve`. Its text is `value`, which must be the 0x003f string as sent, so the caller
    // maps the listbox's unescaped display text back to it.
    static CommandResult completeArg(const MissingArg& m, const std::string& value, bool haveSelf, const ArgResolver& atoms = {});
    // The server's s2c 0x0039 answer to r: the argument becomes [kind][value] and the rest of the line is parsed on
    // (another list argument, a missing one, or the frames). A null answer (kind 0, value 0xffff) is the local refusal.
    // There is no fallback: nothing else is sent meanwhile, so a server that never answers never runs the verb.
    static CommandResult completeResolve(const ListResolve& r, uint8_t kind, uint32_t value, bool haveSelf, const ArgResolver& atoms = {});
    // A right-click row the player's verb offers by argument (model::atomMenu viaArgument), picked on atom [kind][ref]:
    // the frames, or for a runtime-list argument the resolve stock DreamSeeker sends instead (c2s 0x0039 with the atom's
    // text, and the answer becomes the 0x0002's argument). `text` is atomArgumentText(). Other arguments go out at once
    // as [tag 0][kind][ref].
    static CommandResult atomArgument(const VerbEntry& e, const VerbSrc& src, uint8_t kind, uint32_t ref, const std::string& text, bool haveSelf);
    // s2c 0x0039 answers in arrival order (one TCP stream: the oldest outstanding 0x0039 owns the first). onFrame queues them.
    bool takeListReply(uint8_t& kind, uint32_t& value);
    void clearListReplies() { listReplies_.clear(); }

    // Held-key movement: the movement verb when the world has one (client src-kind), else sub 0 on key-down only.
    // Returns false when no frame is due (sub-0 key-up, or a verb world that has no self ref yet).
    bool moveFrame(int dir, bool down, bool haveSelf, uint32_t selfRef, FrameOut& out) const;
    // A skin's `.north` ... `.southwest` command: always the stock move (sub 0), whatever movement verb the world has.
    // The server runs client.North() ... client.Southwest() for it.
    FrameOut stockMoveFrame(int dir) const;

private:
    std::map<uint16_t, VerbEntry> verbs_;
    std::deque<std::pair<uint8_t, uint32_t>> listReplies_;
    std::vector<uint16_t> order_;
    bool haveSubs_ = false;
    uint32_t flags_ = 0, extra_ = 0;
};

// "north"/"up"/"n" ... or a numeric BYOND dir (1 2 4 8, diagonals 5 6 9 10). Returns 0 if bad.
int parseDir(const std::string& s);

}  // namespace fugue::net
