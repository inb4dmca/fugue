#include "model/prompt.h"
#include "net/input.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fugue::model {
namespace {
bool cstrAt(ByteSpan b, size_t& p, std::string& out) {
    for (size_t i = p; i < b.size(); i++)
        if (b[i] == 0) { out.assign(reinterpret_cast<const char*>(b.data() + p), i - p); p = i + 1; return true; }
    return false;
}
void putCStr(Bytes& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); b.push_back(0); }
void put32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i))); }
}  // namespace

std::string Prompt::typeName() const {
    switch (typeMask & ~kInputNull) {
    case kInputText: return "text";
    case kInputNum: return "num";
    case kInputMessage: return "message";
    case kInputAnything: return "anything";
    case kInputColor: return "color";
    case 0: return "anything";
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "mask 0x%04x", typeMask);
    return buf;
}

bool parseAlertRequest(ByteSpan b, Prompt& out) {
    if (b.size() < 4) return false;
    Prompt p;
    p.kind = Prompt::Alert;
    p.id = le32(b.data());
    size_t pos = 4;
    if (!cstrAt(b, pos, p.message) || !cstrAt(b, pos, p.title)) return false;
    if (b.size() - pos < 2) return false;
    unsigned count = le16(b.data() + pos);
    pos += 2;
    for (unsigned i = 0; i < count; i++) {
        std::string s;
        if (!cstrAt(b, pos, s)) return false;
        if (!s.empty()) p.buttons.push_back(std::move(s));
    }
    if (pos != b.size()) return false;
    if (p.buttons.empty()) p.buttons.push_back("OK");   // a one-argument alert() shows and returns "OK"
    out = std::move(p);
    return true;
}

bool parseInputRequest(ByteSpan b, Prompt& out) {
    Prompt p;
    p.kind = Prompt::Input;
    size_t pos = 0;
    if (!cstrAt(b, pos, p.title) || !cstrAt(b, pos, p.def) || !cstrAt(b, pos, p.message)) return false;
    if (b.size() - pos != 10) return false;
    p.typeMask = le32(b.data() + pos);
    p.filter = b[pos + 4];
    p.filterKind = b[pos + 5];
    p.id = le32(b.data() + pos + 6);
    p.inList = p.filterKind == kInputFilterList;
    out = std::move(p);
    return true;
}

bool parseExpansionReply(ByteSpan b, std::vector<std::string>& out) {
    if (b.size() < 2) return false;
    unsigned n = le16(b.data());
    size_t pos = 2;
    std::vector<std::string> v;
    for (unsigned i = 0; i < n; i++) {
        std::string s;
        if (!cstrAt(b, pos, s)) return false;
        v.push_back(std::move(s));
    }
    if (pos != b.size()) return false;
    out = std::move(v);
    return true;
}

Bytes encodeAlertReply(uint32_t id, const std::string& answer) {
    Bytes b;
    put32(b, id);
    putCStr(b, answer);
    return b;
}
// A text answer is DMValue kind 2 with the text DM-escaped, as stock DreamSeeker 516 writes it: input() `a\b` is sent
// as `02 "a\\b"`, and DM receives the text exactly as typed.
Bytes encodeInputText(uint32_t id, const std::string& text) {
    Bytes b{2};
    putCStr(b, net::escapeVerbText(text));
    put32(b, id);
    return b;
}
Bytes encodeInputNum(uint32_t id, float n) {
    Bytes b{1};
    uint32_t bits;
    std::memcpy(&bits, &n, 4);
    put32(b, bits);
    put32(b, id);
    return b;
}
// A cancelled `as null|...` input(): DMValue tag 0 (an atom reference) to the NULL reference [00][ffff0000], then the id.
// DM receives null. The null reference is 0xffff, as in every other c2s atom slot.
Bytes encodeInputNull(uint32_t id) {
    Bytes b{0, 0};
    put32(b, 0x0000ffff);
    put32(b, id);
    return b;
}
Bytes encodePendingInputExpansionRequest() { return Bytes(9, 0); }

std::string inputChoiceText(const std::string& choice) {
    std::string t = net::escapeVerbText(choice);
    for (char& c : t) if (c == ' ') c = '-';
    return t + ' ';
}
Bytes encodeInputResolveRequest(uint32_t id, const std::string& choice) {
    Bytes b;
    putCStr(b, inputChoiceText(choice));
    put32(b, id);
    put32(b, 0);
    return b;
}
Bytes encodeInputValue(uint32_t id, uint8_t kind, uint32_t value) {
    Bytes b{0, kind};
    put32(b, value);
    put32(b, id);
    return b;
}

Bytes encodeInputAnswer(const Prompt& p, const std::string& text) {
    if (p.typeMask & kInputNum) {
        // Parses the trimmed text as a 32-bit float. A non-numeric answer stays TEXT (0 is a value the
        // player did not type); an out-of-float32-range one is a range error, so it is text too.
        size_t a = text.find_first_not_of(" \t\r\n\v\f"), z = text.find_last_not_of(" \t\r\n\v\f");
        if (a != std::string::npos) {
            std::string t = text.substr(a, z - a + 1);
            errno = 0;
            char* end = nullptr;
            float f = std::strtof(t.c_str(), &end);
            if (end && *end == 0 && end != t.c_str() && errno != ERANGE) return encodeInputNum(p.id, f);
        }
    }
    return encodeInputText(p.id, text);
}

const Prompt* PromptBook::find(uint32_t id) const {
    for (const Prompt& p : pending_) if (p.id == id) return &p;
    return nullptr;
}

void PromptBook::retire(uint32_t id) {
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
        if (it->id == id) { pending_.erase(it); return; }
}

const Prompt* PromptBook::onFrame(uint16_t type, ByteSpan body) {
    Prompt p;
    if (type == kAlertMsg) {
        if (!parseAlertRequest(body, p)) return nullptr;
    } else if (type == kInputMsg) {
        if (!parseInputRequest(body, p)) return nullptr;
    } else if (type == kExpansionReplyMsg) {
        // The answer to our 0x0064: it belongs to the in-list prompt that asked (BYOND's prompts are modal, so one).
        std::vector<std::string> ch;
        for (Prompt& q : pending_)
            if (q.kind == Prompt::Input && q.inList && !q.choicesKnown) {
                if (parseExpansionReply(body, ch)) { q.choices = std::move(ch); }
                q.choicesKnown = true;
                return &q;
            }
        return nullptr;
    } else {
        return nullptr;
    }
    p.seq = total_++;
    pending_.push_back(std::move(p));
    if (pending_.size() > kCap) pending_.pop_front();
    return &pending_.back();
}

const Prompt* PromptBook::needsChoices() const {
    for (const Prompt& p : pending_)
        if (p.kind == Prompt::Input && p.inList && !p.choicesKnown) return &p;
    return nullptr;
}
void PromptBook::giveUpOnChoices(uint32_t id) {
    for (Prompt& p : pending_) if (p.id == id) p.choicesKnown = true;
}

PromptReply PromptBook::answerAlert(uint32_t id, const std::string& button) {
    retire(id);
    return {kAlertMsg, encodeAlertReply(id, button)};
}
PromptReply PromptBook::answerInput(uint32_t id, const std::string& text) {
    Prompt p;
    if (const Prompt* q = find(id)) p = *q;   // not held: answer as plain text rather than dropping it
    p.id = id;
    retire(id);
    if (p.resolvesThroughServer()) return {kInputResolveMsg, encodeInputResolveRequest(id, text)};
    return {kInputMsg, encodeInputAnswer(p, text)};
}
PromptReply PromptBook::resolvedInput(uint32_t id, uint8_t kind, uint32_t value) { return {kInputMsg, encodeInputValue(id, kind, value)}; }
PromptReply PromptBook::cancel(uint32_t id) {
    // Stock DreamSeeker cannot dismiss an alert() or a non-null input() (Prompt::cancellable), so nothing is sent for one.
    if (const Prompt* q = find(id); q && !q->cancellable()) return {};
    retire(id);
    return {kInputMsg, encodeInputNull(id)};
}

}  // namespace fugue::model
