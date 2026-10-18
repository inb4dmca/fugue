// model/prompt.h: DM's alert() and input() dialogs, and the exact bytes that answer them.
// Qt-free and socket-free: parse a request frame, hold it pending, encode the one reply. The GUI and a headless
// client are both front ends over this, so the reply bytes exist in exactly one place.
//
// Request and reply layouts follow the stock client, and the InputType masks are the stock
// ones. Kind 2 is escaped text (kind 4 = verbatim), kind 1 a number, kind 0 a null reply.
//
//   alert out  0x0092 [id u32][message\0][title\0][count u16][button\0]*count   (DreamDaemon always writes 3, pads with "")
//   alert back 0x0092 [id u32][answer\0]               refusal = "" (alert has no null)
//   input out  0x0062 [title\0][default\0][message\0][mask u32][filter u8][filterKind u8][id u32]
//   input back 0x0062 [kind u8][value][id u32]         kind 2 [cstring] DM-escaped text (prompt.cpp), kind 1 [f32] number,
//                                                      kind 0 [00][ref u32] null, kind 0 [value kind u8][value u32] the
//                                                      server's own value (in list)
//   in-list pick   0x0063 [choice\0][id u32][u32 0]  -> s2c 0x0039 [kind u8][value u32][u32 0] -> 0x0062 kind 0
//                                                      (Prompt::resolvesThroughServer)
//
// THE ANSWER IS THE PLAYER'S. `as num` answers with a NUMBER when the text parses (DreamDaemon does no coercion; a
// text "42" reaches DM as a string), otherwise falls back to TEXT, never to 0.
#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>
#include "net/bytes.h"

namespace fugue::model {

constexpr uint32_t kInputText = 0x0004, kInputNum = 0x0008, kInputNull = 0x0080, kInputMessage = 0x0800,
                   kInputAnything = 0x1000, kInputColor = 0x020000;
constexpr uint8_t kInputFilterList = 0x40;
constexpr uint16_t kAlertMsg = 0x0092, kInputMsg = 0x0062, kPendingInputExpansionMsg = 0x0064, kExpansionReplyMsg = 0x003f;
constexpr uint16_t kInputResolveMsg = 0x0063, kInputResolveReplyMsg = 0x0039;

struct Prompt {
    enum Kind { Alert, Input } kind = Alert;
    uint32_t id = 0;
    std::string message, title;
    std::vector<std::string> buttons;   // alert only; empty slots removed, never empty ("OK" when nothing was labelled)
    std::string def;                    // input: seeds the editor
    uint32_t typeMask = 0;
    uint8_t filter = 0, filterKind = 0;
    bool inList = false;                // `in <list>`: the choices are not on this frame; see PromptBook::needsChoices
    std::vector<std::string> choices;   // filled from the 0x003f answer to our 0x0064
    bool choicesKnown = false;
    uint64_t seq = 0;

    bool multiline() const { return (typeMask & kInputMessage) != 0; }
    bool numeric() const { return (typeMask & kInputNum) != 0; }
    bool isColor() const { return (typeMask & kInputColor) != 0; }
    bool nullable() const { return (typeMask & kInputNull) != 0; }
    // Whether stock DreamSeeker lets the player dismiss the prompt without an answer. Only an `as null|...` input() has a
    // Cancel button, and only there do Escape and the close box do anything (all three send the same null). An alert()
    // and every non-null input() have no Cancel: Escape is ignored, the close box is greyed out, and nothing is sent
    // until an answer.
    bool cancellable() const { return kind == Input && nullable(); }
    // Whether the answer is the SERVER's value for the chosen entry rather than its text. For `as null|anything in L`
    // (mask 0x1080) and a bare `in L` (mask 0x0), stock DreamSeeker sends c2s 0x0063 (encodeInputResolveRequest); the
    // server answers s2c 0x0039 with the list member ([kind][value]), and the input() is answered with that value
    // (encodeInputValue), so DM receives the object itself. `as text in L` (mask 0x4) is an ordinary text field.
    // A plain text answer to an object list is accepted by DreamDaemon as that text, not the object, so the server
    // value path is required for `for(var/turf/T in A)`-style code.
    bool resolvesThroughServer() const { return kind == Input && inList && !(typeMask & (kInputText | kInputMessage | kInputNum)); }
    std::string typeName() const;
};

// Request decoders (false = malformed; the caller then does NOT answer, as the decoder does on error). The title,
// message, buttons and choices come back without text-macro markers (net::plainText), as stock shows and answers them.
bool parseAlertRequest(ByteSpan body, Prompt& out);
bool parseInputRequest(ByteSpan body, Prompt& out);
// 0x003f reply -> choices (marker-free).
bool parseExpansionReply(ByteSpan body, std::vector<std::string>& out);

// Reply encoders: the BODY of the c2s frame (type is kAlertMsg / kInputMsg).
Bytes encodeAlertReply(uint32_t id, const std::string& answer);
Bytes encodeInputText(uint32_t id, const std::string& text);
Bytes encodeInputNum(uint32_t id, float n);
Bytes encodeInputNull(uint32_t id);           // [00][null ref: 00 ffff0000][id], the stock cancel
Bytes encodePendingInputExpansionRequest();   // nine zero bytes
// The c2s 0x0063 that asks the server for the list member a choice names (Prompt::resolvesThroughServer):
// [inputChoiceText(choice)\0][id u32][u32 0].
Bytes encodeInputResolveRequest(uint32_t id, const std::string& choice);
// A choice as stock DreamSeeker names it in that 0x0063: DM-escaped (net::escapeVerbText), each space written as `-`,
// and a trailing space. The 0x003f lists each choice unescaped.
std::string inputChoiceText(const std::string& choice);
// The input() answer carrying the server's value from that s2c 0x0039: [00][kind u8][value u32][id u32].
Bytes encodeInputValue(uint32_t id, uint8_t kind, uint32_t value);
// The reply for a typed answer: a number for `as num` when it parses, else text.
Bytes encodeInputAnswer(const Prompt& p, const std::string& text);

struct PromptReply { uint16_t type = 0; Bytes body; };

// The pending prompts of one session. Single-thread (the session thread); no locking.
class PromptBook {
public:
    // Feeds an s2c frame. Returns the newly pended prompt (0x0092/0x0062), or nullptr. A 0x003f with a prompt waiting
    // for its list attaches the choices and returns that prompt.
    const Prompt* onFrame(uint16_t type, ByteSpan body);
    const std::deque<Prompt>& pending() const { return pending_; }
    const Prompt* find(uint32_t id) const;
    // The in-list prompt (if any) that has asked for choices and not yet had them: the session sends 0x0064 for it.
    const Prompt* needsChoices() const;
    void giveUpOnChoices(uint32_t id);   // fall back to typed text (timeout)

    // Each retires the prompt and returns the frame to send. Unknown ids still answer: a double click is harmless to
    // the server, and an out-of-order reply must still reach a parked proc.
    PromptReply answerAlert(uint32_t id, const std::string& button);
    // For a prompt that resolvesThroughServer() the reply is the 0x0063 (type kInputResolveMsg), and the input() is not
    // answered yet: the caller feeds the s2c 0x0039 to resolvedInput() and sends that. The 0x0039 names no request, so
    // while verb-argument resolves (net::ListResolve, also answered on 0x0039) are in flight, the OLDEST owns it.
    PromptReply answerInput(uint32_t id, const std::string& text);
    // The input() answer for a 0x0063 the server answered with (kind, value). A not-in-list text comes back null
    // (`00 ffff0000`), sent as the null answer.
    static PromptReply resolvedInput(uint32_t id, uint8_t kind, uint32_t value);
    // Null for a cancellable input (or an unknown id). For a prompt stock DreamSeeker cannot dismiss (Prompt::cancellable)
    // it returns an EMPTY reply (type 0, nothing to send) and the prompt stays pending, since the world's proc waits.
    PromptReply cancel(uint32_t id);
private:
    void retire(uint32_t id);
    std::deque<Prompt> pending_;
    uint64_t total_ = 0;
    static constexpr size_t kCap = 32;
};

}  // namespace fugue::model
