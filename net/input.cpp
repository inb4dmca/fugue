#include "net/input.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

namespace fugue::net {
using proto::enc::VerbArg;

namespace {
constexpr uint32_t kArgNum = 8, kArgText = 0x04, kArgNull = 0x80, kArgMessage = 0x800, kArgAnything = 0x1000;
constexpr uint32_t kArgAtoms = 0x01 | 0x02 | 0x20;   // mob | obj | turf: a typed name, never a string
constexpr uint8_t kFilterContents = 0x08, kFilterWorld = 0x10;   // `in usr`, `in world` (FilterKind on the 0x0011)
constexpr uint8_t kFilterList = 0x40;   // VerbArgSpec.filterKind for an `in <list>` clause
constexpr uint8_t kFlagInstant = 0x80;

std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }
bool ieq(const std::string& a, const std::string& b) { return a.size() == b.size() && lower(a) == lower(b); }
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::string ltrim(const std::string& s) { size_t a = s.find_first_not_of(" \t"); return a == std::string::npos ? "" : s.substr(a); }

// Typed-line quoting, as stock DreamSeeker and DreamDaemon parse it:
//   * A typed argument's backslashes go out raw, and DreamDaemon then parses DM escapes in it.
//   * Only `"` quotes, and a backslash escapes the byte after it while scanning.
//   * A `"` token closes at its first unescaped `"` only when whitespace or the end of the line follows it. Otherwise
//     the token is the rest of the line minus its opening quote.
//   * A closed `"` token is one token even as the last argument.
//   * A `'` is not a quote, but a `'` token spans to its closing `'` the same way. Only the last text argument may be
//     one, and it takes the rest of the line verbatim.
//   * Any other token runs to whitespace, with a backslash escaping the byte after it (`Pick Big\ Box` is one token).
size_t unescapedQuote(const std::string& s, size_t from, char q = '"') {   // first unescaped q at or after `from`
    for (size_t i = from; i < s.size(); i++) {
        if (s[i] == '\\') { i++; continue; }
        if (s[i] == q) return i;
    }
    return std::string::npos;
}
bool isSpace(char c) { return c == ' ' || c == '\t'; }
struct Token {
    enum Kind { Bare, Quoted, QuotedRest, Single } kind = Bare;
    std::string raw;     // as typed: what a refusal prints, and what a list argument's 0x0039 carries
    std::string value;   // Quoted / QuotedRest: the text between the quotes (backslashes still raw)
};
// One argument token off the front of `s` (no leading whitespace); `rest` gets what follows it, left-trimmed.
Token nextToken(const std::string& s, std::string& rest) {
    Token t;
    rest.clear();
    if (s.empty()) return t;
    if (s[0] == '"' || s[0] == '\'') {
        size_t q = unescapedQuote(s, 1, s[0]);
        bool closed = q != std::string::npos && (q + 1 == s.size() || isSpace(s[q + 1]));
        if (s[0] == '"') {
            if (closed) { t.kind = Token::Quoted; t.raw = s.substr(0, q + 1); t.value = s.substr(1, q - 1); rest = ltrim(s.substr(q + 1)); }
            else { t.kind = Token::QuotedRest; t.raw = s; t.value = s.substr(1); }
            return t;
        }
        t.kind = Token::Single;
        if (closed) { t.raw = s.substr(0, q + 1); rest = ltrim(s.substr(q + 1)); }
        else t.raw = s;   // unclosed or run-on: the rest of the line, as with "..."
        return t;
    }
    size_t i = 0;
    for (; i < s.size() && !isSpace(s[i]); i++)
        if (s[i] == '\\') i++;
    if (i > s.size()) i = s.size();
    t.raw = s.substr(0, i);
    rest = ltrim(s.substr(i));
    return t;
}
bool parseRefArg(const std::string& s, uint32_t& out) {
    std::string raw;
    if (s.rfind("ref:", 0) == 0) raw = s.substr(4);
    else if (!s.empty() && s[0] == '#') raw = s.substr(1);
    else return false;
    if (raw.empty()) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(raw.c_str(), &end, 0);
    if (*end || v > 0xffffffffull) return false;
    out = uint32_t(v);
    return true;
}
bool parseNumArg(const std::string& s, float& out) {
    if (s.rfind("num:", 0) != 0) return false;
    char* end = nullptr;
    out = std::strtof(s.c_str() + 4, &end);
    return s.size() > 4 && !*end;
}
std::string argTypeName(uint32_t m) {
    if (m == 0) return "arg";
    if ((m & kArgNum) && !(m & kArgText)) return "num";
    if (m & kArgText) return "\"text\"";
    if (m & 0x01) return "mob";
    if (m & 0x02) return "obj";
    if (m & 0x20) return "turf";
    if (m & 0x100) return "area";
    return "arg";
}
bool looksMoveNamed(const std::string& n) {
    std::string l = lower(n);
    for (const char* w : {"move", "walk", "step", "dir"}) if (l.find(w) != std::string::npos) return true;
    return false;
}
bool moveShaped(const VerbEntry& e) {
    if (!(e.flags & kFlagInstant)) return false;
    if (e.args.empty() || e.args.size() > 2) return false;
    for (auto& a : e.args) if (a.typeMask != kArgNum) return false;
    return true;
}
}  // namespace

int parseDir(const std::string& s) {
    std::string l = lower(s);
    if (l == "north" || l == "up" || l == "n") return 1;
    if (l == "south" || l == "down" || l == "s") return 2;
    if (l == "east" || l == "right" || l == "e") return 4;
    if (l == "west" || l == "left" || l == "w") return 8;
    if (l == "northeast" || l == "ne") return 5;
    if (l == "southeast" || l == "se") return 6;
    if (l == "northwest" || l == "nw") return 9;
    if (l == "southwest" || l == "sw") return 10;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (!s.empty() && !*end && (v == 1 || v == 2 || v == 4 || v == 8 || v == 5 || v == 6 || v == 9 || v == 10)) return int(v);
    return 0;
}

void InputModel::onFrame(uint16_t type, ByteSpan b) {
    if (type == kListResolveMsg) {
        uint8_t k; uint32_t v;
        if (parseListResolveReply(b, k, v)) listReplies_.push_back({k, v});
        return;
    }
    if (type == 0x003b) {
        if (b.size() < 16) return;
        flags_ = le32(&b[12]);
        extra_ = 0;
        if (flags_ & kMouseFlagExtra) { if (b.size() < 20) return; extra_ = le32(&b[16]); }
        haveSubs_ = true;
        return;
    }
    if (type != 0x0011) return;
    // [id u16][name\0][desc\0][cat\0][head u8][src u8][flags u8]([flagsExt u32][extByte u8] iff flags&0x80)
    // [argc u8] argc x [typemask u32][filter u8][filterKind u8]
    size_t p = 0;
    auto cstr = [&](std::string& o) { size_t q = p; while (q < b.size() && b[q]) q++; if (q >= b.size()) return false; o.assign((const char*)&b[p], q - p); p = q + 1; return true; };
    if (b.size() < 2) return;
    VerbEntry e;
    e.id = le16(&b[0]); p = 2;
    if (!cstr(e.name) || !cstr(e.desc) || !cstr(e.category)) return;
    if (p + 3 > b.size()) return;
    e.head = b[p++]; e.src = b[p++]; e.flags = b[p++];
    if (e.flags & 0x80) { if (p + 5 > b.size()) return; p += 5; }
    if (p >= b.size()) return;
    unsigned argc = b[p++];
    for (unsigned i = 0; i < argc; i++) {
        if (p + 6 > b.size()) return;
        VerbArgSpec a; a.typeMask = le32(&b[p]); a.filter = b[p + 4]; a.filterKind = b[p + 5]; p += 6;
        e.args.push_back(a);
    }
    if (!verbs_.count(e.id)) order_.push_back(e.id);
    verbs_[e.id] = std::move(e);
}

proto::enc::MouseHooks InputModel::hooks() const {
    proto::enc::MouseHooks h;
    h.click = flags_ & kMouseFlagClick; h.dblClick = flags_ & kMouseFlagDblClick; h.up = flags_ & kMouseFlagUp;
    h.down = flags_ & kMouseFlagDown; h.drop = flags_ & kMouseFlagDrop; h.drag = flags_ & kMouseFlagDrag;
    return h;
}

const VerbEntry* InputModel::verbByName(const std::string& name) const {
    for (auto& [id, e] : verbs_) if (ieq(e.name, name)) return &e;
    return nullptr;
}

const VerbEntry* InputModel::matchVerb(const std::string& line0, std::string& rest) const {
    std::string line = trim(line0);
    const VerbEntry* best = nullptr;
    for (auto& [id, e] : verbs_) {
        if (e.name.empty() || e.name.size() > line.size()) continue;
        if (!ieq(line.substr(0, e.name.size()), e.name)) continue;
        if (line.size() > e.name.size() && line[e.name.size()] != ' ' && line[e.name.size()] != '\t') continue;
        if (!best || e.name.size() > best->name.size() || (e.name.size() == best->name.size() && e.id < best->id)) {
            best = &e;
            rest = trim(line.substr(e.name.size()));
        }
    }
    return best;
}

// Space completion, as in the stock command bar: a space typed at the end of the bar becomes ` "` when the verb's next
// unfilled argument is text or message (`say` -> `say "`, `numtext 5` -> `numtext 5 "`), and a plain space for num,
// anything, or no arguments. A unique prefix completes to the verb's own spelling. Inside an open quote, after
// whitespace, or for an ambiguous prefix, a plain space is typed. An escaped `\"` neither opens nor closes a quote.
bool InputModel::spaceCompletion(const std::string& line, std::string& out) const {
    size_t quotes = 0;
    for (size_t q = unescapedQuote(line, 0); q != std::string::npos; q = unescapedQuote(line, q + 1)) quotes++;
    if (quotes % 2 == 1 || trim(line) != line) return false;
    std::string rest;
    const VerbEntry* e = matchVerb(line, rest);
    std::string head;
    if (e) {
        head = e->name + line.substr(e->name.size());
    } else {
        if (line.find_first_of(" \t") != std::string::npos) return false;
        int n = 0;
        for (auto& [id, v] : verbs_) {
            if (v.name.empty() || v.name.find_first_of(" \t") != std::string::npos || v.name.size() <= line.size()) continue;
            if (ieq(v.name.substr(0, line.size()), line)) { e = &v; n++; }
        }
        if (n != 1) return false;
        head = e->name;
    }
    size_t filled = 0;
    for (std::string r = rest, nr; !r.empty(); filled++) { nextToken(r, nr); r = nr; }
    bool quote = filled < e->args.size() && (e->args[filled].typeMask & (kArgText | kArgMessage)) != 0;
    out = head + (quote ? " \"" : " ");
    return true;
}

const VerbEntry* InputModel::moveVerb() const {
    const VerbEntry* best = nullptr; bool bestNamed = false;
    for (uint16_t id : order_) {
        auto it = verbs_.find(id);
        if (it == verbs_.end() || !moveShaped(it->second)) continue;
        bool named = looksMoveNamed(it->second.name);
        if (!best || (named && !bestNamed) || (named == bestNamed && it->second.id < best->id)) { best = &it->second; bestNamed = named; }
    }
    return best;
}

// An `in <list>` argument whose value the server resolves (ListResolve, input.h).
static bool resolvesThroughServer(const VerbArgSpec& a) {
    return a.filterKind == kFilterList && !(a.typeMask & (kArgText | kArgMessage | kArgNum));
}
// A nullable argument the line ran out before goes out as null (tag 0, kind 0, ref 0xffff) without a dialog. A required
// argument after it still prompts.
static VerbArg nullArg() { VerbArg a; a.tag = 0; a.kind = 0; a.ref = 0xffff; return a; }

static VerbArg atomArg(const SrcCandidate& c) { VerbArg a; a.tag = 0; a.kind = c.src.kind; a.ref = c.src.ref; return a; }

// Atom-argument matching, by stock DreamSeeker's own rules. A token names a candidate as follows (case-insensitive
// unless backslashed):
//   * the WHOLE name, or else the LAST WORDS of a longer name (a word that starts after a space). No prefixes or substrings.
//   * a typed `-` matches a `-` or a space in the name.
//   * a backslashed character matches only itself, case-sensitively: `\ ` matches a space, `\b` a lowercase b only.
//     `\n`, `\t` and `\r` are newline, tab and CR. A lone trailing `\` is ignored.
//   * if nothing matches, the token is cut at its last backslash and tried again.
// A whole-name match beats a last-words match, and ties go to the first candidate (verbArgCandidates' order, lowest ref).
// The candidates are model::verbArgCandidates, never a guess at what is carried. A refusal names the token (refusedAtom).
namespace {
struct NameChar { char c; bool exact; };
std::vector<NameChar> namePattern(const std::string& raw) {
    std::vector<NameChar> p;
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] != '\\') { p.push_back({raw[i], false}); continue; }
        if (i + 1 == raw.size()) break;   // a lone trailing `\`
        char c = raw[++i];
        p.push_back({c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c, true});
    }
    return p;
}
size_t lastEscape(const std::string& raw) {   // where the last `\c` (or a lone trailing `\`) starts
    size_t at = std::string::npos;
    for (size_t i = 0; i < raw.size(); i++)
        if (raw[i] == '\\') at = i++;
    return at;
}
char lowerc(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
bool patternIs(const std::vector<NameChar>& p, const std::string& name, size_t at) {
    if (name.size() - at != p.size()) return false;
    for (size_t i = 0; i < p.size(); i++) {
        char n = name[at + i];
        bool ok = p[i].exact ? n == p[i].c : lowerc(n) == lowerc(p[i].c) || (p[i].c == '-' && n == ' ');
        if (!ok) return false;
    }
    return true;
}
const SrcCandidate* atomForPattern(const std::vector<SrcCandidate>& cands, const std::vector<NameChar>& p) {
    if (p.empty()) return nullptr;
    for (const SrcCandidate& c : cands)
        if (patternIs(p, c.name, 0)) return &c;
    for (const SrcCandidate& c : cands)
        for (size_t k = 1; k < c.name.size(); k++)
            if (c.name[k - 1] == ' ' && patternIs(p, c.name, k)) return &c;
    return nullptr;
}
}  // namespace

const SrcCandidate* atomTyped(const std::vector<SrcCandidate>& cands, const std::string& raw) {
    for (std::string r = raw;;) {
        if (const SrcCandidate* c = atomForPattern(cands, namePattern(r))) return c;
        size_t cut = lastEscape(r);
        if (cut == std::string::npos) return nullptr;
        r.resize(cut);
    }
}

// A dialog value naming one of an atom argument's candidates: the name as listed, or with its spaces written `-`,
// case-insensitive. The dialog is a text box here, a listbox in DreamSeeker.
static const SrcCandidate* atomNamed(const std::vector<SrcCandidate>& cands, const std::string& tok) {
    for (const SrcCandidate& c : cands) {
        std::string dashed = c.name;
        std::replace(dashed.begin(), dashed.end(), ' ', '-');
        if (ieq(dashed, tok) || ieq(c.name, tok)) return &c;
    }
    return nullptr;
}

// DreamSeeker's usage line: the type bits in ascending order joined by `|`, square brackets for null, and `in inventory`
// or `in world` for the filter.
static std::string usageToken(const VerbArgSpec& a) {
    uint32_t m = a.typeMask & ~kArgNull;
    if (a.filterKind == kFilterList) m &= ~kArgAnything;
    if ((m & kArgMessage) && (m & kArgText)) m &= ~kArgMessage;
    static const std::pair<uint32_t, const char*> names[] = {{0x01, "mob"}, {0x02, "obj"}, {kArgText, "\"text\""}, {kArgNum, "number"},
                                                            {0x20, "turf"}, {0x100, "area"}, {kArgMessage, "\"text\""}, {kArgAnything, "anything"}};
    std::string t;
    for (const auto& [bit, name] : names)
        if (m & bit) t += (t.empty() ? "" : "|") + std::string(name);
    if (t.empty()) t = "parameter";
    if (a.typeMask & kArgNull) t = "[" + t + "]";
    if (a.filterKind == kFilterContents) t += " in inventory";
    else if (a.filterKind == kFilterWorld) t += " in world";
    return t;
}
static std::string usageLine(const VerbEntry& e) {
    std::string u = "usage: " + e.name;
    for (const VerbArgSpec& a : e.args) u += " " + usageToken(a);
    return u;
}
// DreamSeeker prints two lines and sends nothing for a refusal: `Sorry, the following is not valid: X`, then `usage: ...`.
// They are joined by '\n' here; the caller prints each.
static std::string notValid(const VerbEntry& e, const std::string& what) {
    return "Sorry, the following is not valid: " + what + "\n" + usageLine(e);
}
// The token an atom refusal names: the token itself, unless it and the tokens after it, joined by spaces, would have
// named a candidate. Then it is the second token: `Pick Big Box` names Box, `Pick red box zzz` names box. The join's
// space is a real space.
static std::string refusedAtom(const std::vector<SrcCandidate>& cands, const Token& tok, const std::string& after) {
    std::string joined = tok.raw, second;
    for (std::string r = after; !r.empty();) {
        std::string nr;
        Token t = nextToken(r, nr);
        if (t.kind != Token::Bare) break;
        if (second.empty()) second = t.raw;
        joined += "\\ " + t.raw;
        if (atomTyped(cands, joined)) return second;
        r = nr;
    }
    return tok.raw;
}

// Parses arguments [start..] of e from `rest` onto args. Returns the local error ("" = complete). *missing is the first
// required argument the line ran out before. *listAt is the first argument that needs a server resolve, with its token
// in *listText and the rest of the line in *listRest; parsing stops there.
static std::string verbArgsFor(const VerbEntry& e, size_t start, std::string rest, std::vector<VerbArg>& args, size_t* missing,
                               const InputModel::ArgResolver& atoms, size_t* listAt = nullptr, std::string* listText = nullptr,
                               std::string* listRest = nullptr) {
    for (size_t i = start; i < e.args.size(); i++) {
        const auto& spec = e.args[i];
        rest = ltrim(rest);
        if (rest.empty()) {
            if (spec.typeMask & kArgNull) { args.push_back(nullArg()); continue; }
            if (missing) *missing = i;
            return e.name + ": missing argument " + std::to_string(i + 1) + " \xe2\x80\x94 " + usageLine(e);
        }
        bool last = i + 1 == e.args.size();
        bool textLike = (spec.typeMask == 0 && spec.filterKind != kFilterList) || (spec.typeMask & kArgText);
        std::string after;
        Token tok = nextToken(rest, after);
        // `as text|anything`: a "..." token is the text, a bare one an atom name. A single-quoted or backslashed token is refused.
        bool atomText = textLike && (spec.typeMask & kArgAnything) && tok.kind != Token::Quoted && tok.kind != Token::QuotedRest;
        if (textLike && !atomText) {
            // A text argument is a "..." token. A bare or '...' one is refused unless it is the last argument, which then
            // takes the rest of the line verbatim.
            VerbArg a; a.tag = 2;
            if (tok.kind == Token::Quoted || tok.kind == Token::QuotedRest) a.str = tok.value;
            else if (last) { a.str = trim(rest); after.clear(); }
            else return notValid(e, tok.raw);
            args.push_back(a);
            rest = after;
            continue;
        }
        rest = after;
        // A number, an atom, `anything` and a list choice are a bare token. Quoted or single-quoted forms are refused,
        // and list choices send no 0x0039 for them.
        if (tok.kind != Token::Bare) {
            if ((spec.typeMask & (kArgNum | kArgAtoms | kArgAnything)) || resolvesThroughServer(spec)) return notValid(e, tok.raw);
            VerbArg a; a.tag = 2; a.str = tok.kind == Token::Single ? tok.raw : tok.value; args.push_back(a);
            continue;
        }
        uint32_t ref; float n;
        if (parseRefArg(tok.raw, ref)) { VerbArg a; a.tag = 0; a.kind = proto::enc::RefObj; a.ref = ref; args.push_back(a); continue; }
        if (parseNumArg(tok.raw, n)) { VerbArg a; a.tag = 1; a.num = n; args.push_back(a); continue; }
        if (atoms) {
            if (std::optional<std::vector<SrcCandidate>> cands = atoms(spec)) {
                // An atom the client names itself: [tag 0][kind][ref] at once, or a local refusal with no frame.
                const SrcCandidate* c = atomTyped(*cands, tok.raw);
                if (!c) return notValid(e, refusedAtom(*cands, tok, rest));
                args.push_back(atomArg(*c));
                continue;
            }
        }
        if (listAt && resolvesThroughServer(spec)) {
            // A typed list argument goes out as typed: the server matches it, escapes and all, and the answer arrives
            // as s2c 0x0039.
            *listAt = i; *listText = tok.raw; *listRest = rest;
            return "";
        }
        if (spec.typeMask & kArgNum) {
            char* end = nullptr;
            float f = std::strtof(tok.raw.c_str(), &end);
            if (tok.raw.empty() || *end) return notValid(e, tok.raw);
            VerbArg a; a.tag = 1; a.num = f; args.push_back(a);
            continue;
        }
        VerbArg a; a.tag = 2; a.str = tok.raw; args.push_back(a);
    }
    // Text left over is refused at its first token: `SetNum 5 6 7` refuses `6`, `Pick thing extra` refuses `extra`.
    rest = ltrim(rest);
    if (!rest.empty()) { std::string r2; return notValid(e, nextToken(rest, r2).raw); }
    return "";
}

// The rest of a verb from argument `start` on: frames, a missing argument (titled from `line`), or a list resolve.
static CommandResult continueVerb(const VerbEntry& e, const VerbSrc& vs, const std::string& line, std::vector<VerbArg> args, size_t start,
                                  const std::string& rest, const InputModel::ArgResolver& atoms) {
    CommandResult r;
    size_t missing = SIZE_MAX, listAt = SIZE_MAX;
    std::string listText, listRest;
    r.error = verbArgsFor(e, start, rest, args, &missing, atoms, &listAt, &listText, &listRest);
    if (listAt != SIZE_MAX) {
        r.resolve = ListResolve{MissingArg{e, listAt, line, args, vs}, listText, listRest};
        r.error = "verb " + e.name + ": argument " + std::to_string(listAt + 1) + " resolves through the server (0x0039)";
        return r;
    }
    if (missing != SIZE_MAX) r.missing = MissingArg{e, missing, line, args, vs};
    if (!r.error.empty()) return r;
    r.frames.push_back({0x0002, proto::enc::encodeVerb(e.id, vs.kind, vs.ref, args)});
    return r;
}

CommandResult InputModel::verbCommand(const std::string& line, bool haveSelf, const SrcResolver& src, const ArgResolver& atoms) const {
    CommandResult r;
    std::string rest;
    const VerbEntry* e = matchVerb(line, rest);
    if (!e) { r.error = "Unrecognized or inaccessible verb: " + trim(line); return r; }
    if (!haveSelf) { r.error = "verb " + e->name + ": no own-mob ref yet (no 0x0071 seen) \xe2\x80\x94 the server would drop this"; return r; }
    // Exactly one frame, with the verb's real src: a /client verb goes out as [08][0], an own /mob verb as [02][mob ref],
    // an /obj verb as [01][obj ref], a /turf verb as [03][(x-1) | (y-1) << 16]. An unresolvable src is a local refusal,
    // never a guess on the wire. The src comes from model::verbSrcCandidates. A verb that no known atom carries is not
    // sent. When differently-named atoms carry it, the caller is asked which one.
    std::vector<SrcCandidate> cands = src ? src(e->id) : std::vector<SrcCandidate>{};
    if (cands.empty()) { r.error = "verb " + e->name + ": no atom the client knows carries it \xe2\x80\x94 not sent"; return r; }
    if (cands.size() > 1) {
        r.srcChoice = SrcChoice{e->name + " word", line, std::move(cands)};
        r.error = "verb " + e->name + ": " + std::to_string(r.srcChoice->candidates.size()) + " atoms carry it \xe2\x80\x94 asking which";
        return r;
    }
    return continueVerb(*e, cands[0].src, line, {}, 0, rest, atoms);
}

// ================================================================================================================
// THE MISSING-ARGUMENT PROMPT
// ================================================================================================================
// When a typed line runs out before a required argument, DreamSeeker opens a dialog for it. The control comes from the
// argument's type mask, not its `in` clause: a text or message mask gets a text box (multi-line for message), a num
// mask a number box, and a list filter (FilterKind 0x40) a listbox of the list's choices, except that `as text in list`
// stays a text box. The title is the line so far plus the argument's token. The text above the field is the verb's
// `set desc`.
// OK sends the value verbatim as that argument; an empty box sends "", not null. Cancel or Escape sends nothing. A
// non-number in a number box is refused in place.
//
// The value is never appended to the line and re-parsed. The typed-line rules (an unclosed leading quote is dropped,
// a "..." token closes only before whitespace) fit typed text, not dialog text. completeArg() builds the argument
// directly and encodes the verb with the arguments already parsed.
//
// A nullable argument (0x80) gets no dialog: it goes out as null. A single-candidate `as anything` or `mob in world`
// argument is auto-filled by DreamSeeker with no dialog; that scope is server-side and is not modelled here.
// A List prompt's choice resolves through the server (ListResolve, input.h).
namespace {
std::string argToken(const VerbArgSpec& a) {
    uint32_t m = a.typeMask & ~kArgNull;
    if (m & (kArgText | kArgMessage)) return "\"text\"";
    if (m & kArgNum) return "number";
    if (m & kArgAnything) return "anything";
    if (m == 0) return "word";
    return argTypeName(m);   // mob / obj / turf / area: the usage-line tokens
}
// A prompted value as the next dialog's title shows it (display only, never parsed). Both quote characters are
// backslash-escaped, and the value is double-quoted.
std::string quoteArg(const std::string& v) {
    if (v.empty()) return "\"\"";
    if (v.find_first_of(" \t\"'") == std::string::npos && v[0] != '#' && v.rfind("num:", 0) != 0 && v.rfind("ref:", 0) != 0) return v;
    std::string q = "\"";
    for (char c : v) { if (c == '"' || c == '\'') q += '\\'; q += c; }
    return q + "\"";
}
}  // namespace

ArgPrompt argPrompt(const MissingArg& m) {
    ArgPrompt p;
    p.message = m.verb.desc;
    std::string rest;
    {   // the line so far, under the verb's own spelling
        std::string line = trim(m.line);
        rest = line.size() > m.verb.name.size() ? trim(line.substr(m.verb.name.size())) : "";
    }
    p.title = m.verb.name + (rest.empty() ? "" : " " + rest);
    if (m.index >= m.verb.args.size()) return p;
    const VerbArgSpec& a = m.verb.args[m.index];
    p.title += " " + argToken(a);
    p.allowNull = a.typeMask & kArgNull;
    if (a.typeMask & kArgMessage) p.kind = ArgPrompt::Message;
    else if (a.typeMask & kArgText) p.kind = ArgPrompt::Text;   // `as text in list` is a text box, not a list
    else if (a.typeMask & kArgNum) p.kind = ArgPrompt::Num;
    else if (a.filterKind == kFilterList) { p.kind = ArgPrompt::List; p.ordinal = a.filter; }
    return p;
}

// A dialog value travels DM-escaped. DreamDaemon parses escapes in a verb's string argument, and stock DreamSeeker
// escapes exactly four characters: backslash, double quote, single quote, and a newline (as `\n`). Other characters go
// out bare. The 0x003f choices are not in that form: DreamDaemon sends `\'q` for 'q', and DreamSeeker's listbox shows
// unescapeVerbText() of each. The picked value is escaped like any other. The same writer serves input() (model/prompt.cpp).
std::string escapeVerbText(const std::string& v) {
    std::string o;
    for (char c : v) {
        if (c == '\n') { o += "\\n"; continue; }
        if (c == '\\' || c == '"' || c == '\'') o += '\\';
        o += c;
    }
    return o;
}

static std::string argDisplayLine(const MissingArg& m, const std::string& value) {   // the title's "line so far" for the next argument
    size_t end = m.line.find_last_not_of(" \t");
    return (end == std::string::npos ? "" : m.line.substr(0, end + 1)) + " " + quoteArg(value);
}

CommandResult InputModel::completeArg(const MissingArg& m, const std::string& value, bool haveSelf, const ArgResolver& atoms) {
    CommandResult r;
    if (m.index >= m.verb.args.size() || m.args.size() != m.index) { r.error = m.verb.name + ": stale argument dialog"; return r; }
    if (!haveSelf) { r.error = "verb " + m.verb.name + ": no own-mob ref yet (no 0x0071 seen) \xe2\x80\x94 the server would drop this"; return r; }
    const VerbArgSpec& spec = m.verb.args[m.index];
    std::vector<VerbArg> args = m.args;
    VerbArg a;
    if (resolvesThroughServer(spec)) {
        // A listbox choice is sent back verbatim in a 0x0039, and the 0x0002 carries the server's answer. `#1` and
        // `num:5` are the choice's own text here, never the typed path's ref or number.
        r.resolve = ListResolve{m, value, ""};
        r.resolve->at.line = argDisplayLine(m, value);
        r.error = m.verb.name + ": argument " + std::to_string(m.index + 1) + " resolves through the server (0x0039)";
        return r;
    }
    if (spec.filterKind == kFilterList || (spec.typeMask & (kArgText | kArgMessage))) {
        // A text or message box, or `as text in list`: the string as given, escaped (escapeVerbText).
        a.tag = 2; a.str = escapeVerbText(value);
    } else if (spec.typeMask & kArgNum) {
        std::string t = trim(value);
        char* end = nullptr;
        float f = std::strtof(t.c_str(), &end);
        if (t.empty() || *end) { r.error = "Sorry, the following is not valid: " + value; return r; }   // the dialog refuses it first
        a.tag = 1; a.num = f;
    } else {
        // mob / obj / anything with no list: the typed path's ref:/num: forms, an atom the client names itself
        // (ArgResolver), else a string. The dialog is a text box here, so its text is matched as a typed token would be.
        std::string t = trim(value);
        uint32_t ref; float n;
        std::optional<std::vector<SrcCandidate>> cands;
        if (atoms) cands = atoms(spec);
        if (cands) {
            const SrcCandidate* c = atomNamed(*cands, t);
            if (!c) { r.error = "Sorry, the following is not valid: " + t; return r; }
            a = atomArg(*c);
        } else if (parseRefArg(t, ref)) { a.tag = 0; a.kind = proto::enc::RefObj; a.ref = ref; }
        else if (parseNumArg(t, n)) { a.tag = 1; a.num = n; }
        else { a.tag = 2; a.str = escapeVerbText(value); }
    }
    args.push_back(a);
    // The next argument may still be missing: a second dialog, titled with the value so far. A nullable one goes out null.
    return continueVerb(m.verb, m.src, argDisplayLine(m, value), std::move(args), m.index + 1, "", atoms);
}

CommandResult InputModel::completeResolve(const ListResolve& rs, uint8_t kind, uint32_t value, bool haveSelf, const ArgResolver& atoms) {
    CommandResult r;
    const MissingArg& m = rs.at;
    if (m.index >= m.verb.args.size() || m.args.size() != m.index) { r.error = m.verb.name + ": stale list resolve"; return r; }
    if (!haveSelf) { r.error = "verb " + m.verb.name + ": no own-mob ref yet (no 0x0071 seen) \xe2\x80\x94 the server would drop this"; return r; }
    // Text that names no list entry draws null (00 ffff0000): stock DreamSeeker sends no 0x0002 and prints the usage line.
    if (kind == 0 && value == 0xffff) { r.error = notValid(m.verb, rs.text); return r; }
    std::vector<VerbArg> args = m.args;
    VerbArg a;
    a.tag = 0; a.kind = kind; a.ref = value;
    args.push_back(a);
    // Stock DreamSeeker re-sends the 0x0039 for a typed list argument when a dialog follows; this keeps the first answer.
    return continueVerb(m.verb, m.src, m.line, std::move(args), m.index + 1, rs.rest, atoms);
}

CommandResult InputModel::atomArgument(const VerbEntry& e, const VerbSrc& src, uint8_t kind, uint32_t ref, const std::string& text, bool haveSelf) {
    CommandResult r;
    if (e.args.empty()) { r.error = "verb " + e.name + ": takes no argument"; return r; }
    if (!haveSelf) { r.error = "verb " + e.name + ": no own-mob ref yet (no 0x0071 seen) \xe2\x80\x94 the server would drop this"; return r; }
    std::string line = e.name + " " + text;
    if (resolvesThroughServer(e.args[0])) {
        r.resolve = ListResolve{MissingArg{e, 0, line, {}, src}, text, ""};
        r.error = "verb " + e.name + ": argument 1 resolves through the server (0x0039)";
        return r;
    }
    VerbArg a;
    a.tag = 0; a.kind = kind; a.ref = ref;
    // Later arguments follow the typed-line path.
    return continueVerb(e, src, line, {a}, 1, "", {});
}

std::string atomArgumentText(const std::string& name, uint32_t dmRef) {
    std::string o = name;
    std::replace(o.begin(), o.end(), ' ', '-');
    char hex[16];
    std::snprintf(hex, sizeof hex, ".0x%x", dmRef);
    return o + hex;
}

bool parseListResolveReply(ByteSpan b, uint8_t& kind, uint32_t& value) {
    if (b.size() < 5) return false;
    kind = b[0];
    value = le32(&b[1]);
    return true;
}

bool InputModel::takeListReply(uint8_t& kind, uint32_t& value) {
    if (listReplies_.empty()) return false;
    kind = listReplies_.front().first;
    value = listReplies_.front().second;
    listReplies_.pop_front();
    return true;
}

std::string unescapeVerbText(const std::string& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size() && (v[i + 1] == '\'' || v[i + 1] == '"')) i++;   // only a quote is escaped
        o += v[i];
    }
    return o;
}

// 0x003f layout: [0:2] Filter (u16; byte 1 is always 0), [2:6] TypeMask with the null and anything bits cleared,
// [6] src kind, [7:11] src ref (a turf: x-1 u16, y-1 u16), [11] prior-argument count, the prior arguments, then the
// argument's text so far as a C string. The src is the verb's real src; the own mob is not assumed to be mob 0.
Bytes encodeVerbExpansionRequest(const MissingArg& m, const std::string& text) {
    const VerbArgSpec a = m.index < m.verb.args.size() ? m.verb.args[m.index] : VerbArgSpec{};
    Bytes b;
    putLe16(b, a.filter);
    // The null and anything bits are cleared for the request, as for the 0x0002 argument.
    putLe32(b, a.typeMask & ~(kArgNull | kArgAnything));
    b.push_back(m.src.kind);
    putLe32(b, m.src.ref);
    Bytes v = proto::enc::encodeVerb(0, 0, 0, m.args);   // [02 00][id][kind][ref] = 9 bytes, then [count][args]: the same value encoding
    b.insert(b.end(), v.begin() + 9, v.end());
    b.insert(b.end(), text.begin(), text.end());   // the text so far: "" for a dialog's 0x003f, the choice for a 0x0039
    b.push_back(0x00);
    return b;
}

bool InputModel::moveFrame(int dir, bool down, bool haveSelf, uint32_t selfRef, FrameOut& out) const {
    const VerbEntry* mv = moveVerb();
    if (mv && haveSelf) {
        out = {0x0002, proto::enc::encodeMoveVerb(mv->id, selfRef, dir, down, mv->args.size() >= 2)};
        return true;
    }
    if (!down) return false;
    out = {0x0002, proto::enc::encodeMove(uint8_t(dir))};
    return true;
}

// Stock DreamSeeker's `.northwest` macro runs client.Northwest() even in a world with an instant MoveKey verb.
FrameOut InputModel::stockMoveFrame(int dir) const { return {0x0002, proto::enc::encodeMove(uint8_t(dir))}; }

}  // namespace fugue::net
