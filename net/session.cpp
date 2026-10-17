#include "net/session.h"
#include "platform/socket.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <thread>
#include "net/cauth.h"
#include "net/cipher.h"
#include "net/guest.h"
#include "net/handshake.h"
#include "net/seq.h"
#include "render/dsmaptext.h"

namespace fugue::net {
namespace {
std::mt19937& rng() {
    static std::mt19937 g{std::random_device{}()};
    return g;
}
std::string cstrAt(ByteSpan b, size_t& p, bool& ok) {
    std::string s;
    while (p < b.size() && b[p]) s.push_back(char(b[p++]));
    if (p >= b.size()) { ok = false; return s; }
    p++;
    return s;
}
}  // namespace

Session::Session(SessionConfig cfg, Log log) : cfg_(std::move(cfg)), log_(std::move(log)) {
    cfg_.stateDirExplicit = !cfg_.stateDir.empty();
    if (cfg_.stateDir.empty()) cfg_.stateDir = defaultStateDir();
    // Resource cache: an explicit rscDir wins; else an explicit stateDir keeps it under that dir; else the
    // $HOME/.fugue/rsc.
    std::string rsc = !cfg_.rscDir.empty() ? cfg_.rscDir : cfg_.stateDirExplicit ? cfg_.stateDir + "/rsc" : RscStore::defaultDir();
    if (rsc.empty()) rsc = cfg_.stateDir + "/rsc";
    res_ = std::make_unique<ResourcePump>(
        rsc, [this](uint16_t t, ByteSpan b) { return sendFrame(t, b); },
        [this](const std::string& s) { logf(s); });
    t0_ = std::chrono::steady_clock::now();
}

Session::~Session() {
    close();
    if (wireFile_) std::fclose(static_cast<FILE*>(wireFile_));
}

void Session::close() {
    if (fd_ >= 0) { socket::close(fd_); fd_ = -1; }
}

void Session::pace(int lo, int hi) {
    if (!cfg_.humanPacing) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(std::uniform_int_distribution<int>(lo, hi)(rng())));
}

void Session::wire(const char* dir, uint16_t type, ByteSpan body) {
    if (!wireFile_) return;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_).count();
    FILE* f = static_cast<FILE*>(wireFile_);
    // The 0x1a and 0x0036 bodies are live credentials: never logged.
    if ((type == 0x001a || type == 0x0036) && dir[0] == 'c')
        std::fprintf(f, "%lld %s %04x %zu <redacted>\n", (long long)ms, dir, type, body.size());
    else
        std::fprintf(f, "%lld %s %04x %zu %s\n", (long long)ms, dir, type, body.size(), hexEncode(body).c_str());
}

bool Session::writeAll(const Bytes& b) {
    size_t off = 0;
    while (off < b.size()) {
        auto n = socket::send(fd_, b.data() + off, b.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { socket::PollFd p{fd_, POLLOUT, 0}; socket::poll(&p, 1, 1000); continue; }
            return false;
        }
        if (n == 0) return false;
        off += size_t(n);
    }
    return true;
}

bool Session::sendFrame(uint16_t type, ByteSpan plain) {
    if (fd_ < 0) return false;
    if (rekeyPending_) {
        // Between our 0x0036 and the server's ack, c2s frames are held: the server re-keys on that ack, and a frame
        // written under the old key is garbage to it. Held frames are flushed in order by handle(0x0036).
        deferredPost_.emplace_back(type, Bytes(plain.begin(), plain.end()));
        return true;
    }
    auto f = encodeC2S(seq_, type, plain, key_);
    if (!f) return false;
    seq_ = nextSeq(seq_);
    wire("c2s", type, plain);
    st_.c2s[type]++;
    return writeAll(*f);
}

bool Session::readFrameBlocking(Frame& out, int timeoutMs) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        ParseStatus s = reader_.next(out);
        if (s == ParseStatus::Ok) return true;
        if (s == ParseStatus::Error) return false;
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return false;
        socket::PollFd p{fd_, POLLIN, 0};
        int r = socket::poll(&p, 1, int(left));
        if (r <= 0) { if (r < 0 && errno == EINTR) continue; return false; }
        uint8_t buf[16384];
        auto n = socket::recv(fd_, buf, sizeof buf, 0);
        if (n <= 0) { st_.serverClosed = true; st_.closeReason = n == 0 ? "server closed the connection" : std::strerror(errno); return false; }
        reader_.feed(ByteSpan(buf, size_t(n)));
    }
}

std::string Session::connectAndLogin() {
    if (!cfg_.wireLogPath.empty()) wireFile_ = std::fopen(cfg_.wireLogPath.c_str(), "w");

    // --- TCP connect (non-blocking connect + poll so the timeout is ours) ---
    if (!socket::initialize()) return "Winsock initialization failed";
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (int gai = getaddrinfo(cfg_.host.c_str(), std::to_string(cfg_.port).c_str(), &hints, &res); gai != 0)
        return std::string("resolve ") + cfg_.host + ": " + socket::addressError(gai);
    std::string lastErr = "no address";
    for (addrinfo* a = res; a && fd_ < 0; a = a->ai_next) {
        auto fd = socket::create(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        if (socket::nonblocking(fd, true) != 0) { socket::close(fd); continue; }
        int rc = socket::connect(fd, a->ai_addr, int(a->ai_addrlen));
        if (rc < 0 && errno == EINPROGRESS) {
            socket::PollFd p{fd, POLLOUT, 0};
            if (socket::poll(&p, 1, cfg_.connectTimeoutMs) == 1) {
                int e = 0; socket::Length el = sizeof e;
                if (socket::getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el) != 0) e = errno;
                rc = e ? -1 : 0;
                if (e) errno = e;
            } else { rc = -1; errno = ETIMEDOUT; }
        }
        if (rc == 0 && socket::nonblocking(fd, false) == 0) { fd_ = fd; }   // back to blocking; we poll() before every recv
        else { lastErr = std::strerror(errno); socket::close(fd); }
    }
    freeaddrinfo(res);
    if (fd_ < 0) return "connect " + cfg_.host + ":" + std::to_string(cfg_.port) + ": " + lastErr;
    int one = 1;
    socket::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    logf("connected tcp " + cfg_.host + ":" + std::to_string(cfg_.port));

    // --- 0x01 handshake: 22-byte body with a random field3 and seq0. The server validates neither.
    Handshake hs;
    hs.proto = 516;
    hs.field2 = 276;
    hs.field3 = rng()();
    uint16_t seq0 = uint16_t(rng()());
    if (seq0 == 0) seq0 = 1;
    Bytes body;
    putLe32(body, hs.proto);
    putLe32(body, hs.field2);
    putLe32(body, hs.field3);
    putLe32(body, (0x0694u << 16) | seq0);   // build 1684 rides extra1's high half
    putLe32(body, 0);
    putLe16(body, 0x4000);   // DreamSeeker sends 0xc0e0; DD accepts both
    auto hf = encodeFrame(0x0001, body);
    if (!hf || !writeAll(*hf)) return "send handshake failed";
    wire("c2s", 0x0001, body);
    st_.c2s[0x0001]++;

    Frame reply;
    if (!readFrameBlocking(reply, 15000))
        return "no handshake reply (" + (reader_.error().empty() ? std::string("timeout/closed") : reader_.error()) + ")";
    wire("s2c", reply.type, reply.payload);
    uint32_t base = sessionKey(hs);
    Bytes plain;
    if (reply.type != 0x0001 || !decryptBody(reply.payload, base, plain))
        return "handshake reply type " + std::to_string(reply.type) + " did not decrypt under the base key";
    HandshakeReply hr;
    if (!parseHandshakeReply(plain, hr)) return "handshake reply too short";
    key_ = loginKey(base, hr.nonce);
    logf("handshake ok: server major " + std::to_string(hr.major) + ", nonce " + std::to_string(hr.nonce.size()) +
         " bytes, K_login derived");

    // --- 0x1a credential: the guest block from the per-install digest, or a caller-minted account record ---
    pace(40, 150);
    seq_ = seq0;
    if (!cfg_.accountRecord.empty()) {
        st_.accountLogin = true;
        logf("account login: sending a " + std::to_string(cfg_.accountRecord.size()) + "-byte 0x1a record for " +
             cfg_.accountLabel + " (contents never logged)");
        // The record's cauthKey ([2+n:6+n] = size-27, account/records.cpp) is the cached per-endpoint key, as in the
        // guest path below. The hub 0x001c request asks about this same key.
        Bytes rec = cfg_.accountRecord;
        cauth_ = std::make_unique<CauthCache>(cfg_.stateDir, cauthEntryName(cfg_.accountLabel, cfg_.host, cfg_.port));
        std::string cerr;
        const uint32_t k = cauth_->loginKey(hr.flags[1] != 0, &cerr);
        if (!cerr.empty()) logf("cauth: " + cerr + " (this login's key is not remembered)");
        if (rec.size() >= 44)
            for (int i = 0; i < 4; i++) rec[rec.size() - 27 + i] = uint8_t(k >> (8 * i));
        if (!sendFrame(0x001a, rec)) return "send login failed";
        // A hub-registered server answers with a 0x001c login-cert challenge ~270 ms later; a bare one starts its
        // world-init burst (or closes, for a rejected identity). A challenge found here is answered by handle(), and
        // one arriving later is answered from run(). Until the server's 0x0036 ack, c2s frames are held (sendFrame).
        Frame first;
        if (readFrameBlocking(first, 1500)) processFrame(first);
        if (st_.serverClosed) {
            close();
            return "server closed the connection after the account 0x1a record (identity rejected, or the server is still initialising)";
        }
    } else {
        std::string err;
        std::string sid = loadOrCreateSid(cfg_.stateDir, &err);
        if (!err.empty()) logf("identity: " + err + " (using an ephemeral sid for this run)");
        Digest16 dg = digestForInstall(sid);
        st_.computerId = computerId(dg);
        // [16:20] is the guest's cauth key for this endpoint, cached in <stateDir>/cauth.txt (net/cauth.h). Stock
        // DreamSeeker sends the same key on every login to that endpoint until the entry is pruned.
        // hr.flags[1] is the server's cauth-engaged byte.
        cauth_ = std::make_unique<CauthCache>(cfg_.stateDir, cauthEntryName("Guest", cfg_.host, cfg_.port));
        std::string cerr;
        const uint32_t k = cauth_->loginKey(hr.flags[1] != 0, &cerr);
        if (!cerr.empty()) logf("cauth: " + cerr + " (this login's key is not remembered)");
        uint8_t window[4];
        for (int i = 0; i < 4; i++) window[i] = uint8_t(k >> (8 * i));
        Bytes cred = mintGuestCredential(dg, window);
        logf("machine identity: computer_id=" + std::to_string(st_.computerId) + " (per-install sid, state dir " + cfg_.stateDir + ")");
        if (!sendFrame(0x001a, cred)) return "send login failed";
    }

    // --- No post-login frames are sent here. Stock DreamSeeker sends 0xb7, 0x0b, 0x0d and 0x19 only in reply to
    //     server frames, and handle() does the same.
    logf(std::string(st_.accountLogin ? "account" : "guest") + " login sent");
    return "";
}

int Session::pollTimeoutMs(std::chrono::microseconds left) {
    long long us = left.count();
    if (us <= 0) return 0;
    return int(std::min<long long>((us + 999) / 1000, 100));
}

void Session::run(std::chrono::milliseconds duration) {
    auto end = std::chrono::steady_clock::now() + duration;
    auto lastTick = std::chrono::steady_clock::now();
    while (fd_ >= 0 && std::chrono::steady_clock::now() < end) {
        Frame f;
        for (;;) {   // drain whole frames already buffered
            ParseStatus s = reader_.next(f);
            if (s == ParseStatus::Incomplete) break;
            if (s == ParseStatus::Error) {
                st_.framingErrors++;
                st_.serverClosed = true;
                st_.closeReason = "framing error: " + reader_.error();
                close();
                return;
            }
            processFrame(f);
        }
        if (prodDue_ && fd_ >= 0) {   // the 0xb7 for the first 0x000e (handle()), after the frames read with it
            prodDue_ = false;
            sentProd_ = true;
            sendFrame(0x00b7, Bytes{});
        }
        auto now = std::chrono::steady_clock::now();
        if (now - lastTick >= std::chrono::milliseconds(500)) { res_->tick(); lastTick = now; }
        // No periodic frame of our own here: stock DreamSeeker sends no periodic 0x0b, and its keepalive is the 0x0016
        // driven by the s2c 0x0007 count (handle()). The owner's pump hook runs the resync sweep's clock.
        if (pumpHook_) pumpHook_();
        if (fd_ < 0) return;
        // Rounded up to whole ms: truncating would turn the last sub-millisecond of each call into a poll(0) spin.
        socket::PollFd p{fd_, POLLIN, 0};
        int r = socket::poll(&p, 1, pollTimeoutMs(std::chrono::duration_cast<std::chrono::microseconds>(end - now)));
        if (r < 0 && errno == EINTR) continue;
        if (r > 0) {
            uint8_t buf[65536];
            auto n = socket::recv(fd_, buf, sizeof buf, 0);
            if (n <= 0) {
                st_.serverClosed = true;
                st_.closeReason = n == 0 ? "server closed the connection" : std::strerror(errno);
                close();
                return;
            }
            reader_.feed(ByteSpan(buf, size_t(n)));
        }
    }
}

bool Session::replyWinget(uint32_t reqId, const std::string& value) {
    Bytes r(value.begin(), value.end());
    r.push_back(0);
    putLe32(r, reqId);
    bool ok = sendFrame(0x00a9, r);
    st_.promptsAnswered++;
    return ok;
}

// Answer a 0x001c with a 0x0036 carrying a token the hub mints for this session. The flag byte is ignored, as stock
// DreamSeeker ignores it. Blocks the session thread for the hub round trip. With no hub link, or if the hub
// refuses, nothing is sent: the server keeps the connection but never initialises the world.
void Session::answerCertChallenge(uint8_t flag) {
    const std::string tag = "login-cert challenge (0x1c flag=" + std::to_string(flag) + ")";
    if (!cfg_.hubCertAnswer || !cfg_.hubRekey) {
        logf(tag + ": no hub link for this account (no hub credentials in its identity) - NOT answered; the server will not initialise the world");
        return;
    }
    // The 0x1a's key, with the certStr minted with it or a fresh one written back to cauth.txt (net/cauth.h). The
    // 0x0036 ack re-keys with this same certStr.
    std::string err, cerr, certStr;
    uint32_t cauthKey = 0;
    if (!cauth_) cauth_ = std::make_unique<CauthCache>(cfg_.stateDir, cauthEntryName(cfg_.accountLabel, cfg_.host, cfg_.port));
    cauth_->certRequest(&cauthKey, &certStr, &cerr);
    if (!cerr.empty()) logf("cauth: " + cerr + " (this login's certificate is not remembered)");
    auto body = cfg_.hubCertAnswer(cauthKey, certStr, &err);
    if (!body) {
        logf(tag + ": hub token mint failed (" + err + ") - NOT answered; the server will not initialise the world");
        return;
    }
    pace(200, 600);   // a jittered delay before the answer, as stock DreamSeeker has
    if (!sendFrame(0x0036, *body)) { logf(tag + ": send 0x0036 failed"); return; }
    st_.certAnswers++;
    rekeyPending_ = true;
    logf(tag + " -> hub-minted token -> answered 0x0036 (" + std::to_string(body->size()) + " bytes, contents never logged)");
}

void Session::processFrame(const Frame& f) {
    Bytes plain;
    bool ok = f.payload.empty() ? true : decryptBody(f.payload, key_, plain);
    if (!ok) { st_.badChecksum++; return; }
    st_.s2c[f.type]++;
    if (st_.s2c[f.type] == 1) st_.s2cOrder.push_back(f.type);
    wire("s2c", f.type, plain);
    handle(f.type, plain);
}

void Session::handle(uint16_t type, ByteSpan body) {
    if (observer_) observer_(type, body);
    switch (type) {
    case 0x000e:
        res_->onResList(body);
        reportNewResources();
        // The 0xb7 DreamDaemon waits for before the rest of the world-init burst. It is sent once, after the buffered
        // frames are drained. Stock DreamSeeker sends it after DD's login 0x0e and the 0xcd that follows it; sending it
        // at login instead would change DD's stream.
        // With a cold cache, stock DreamSeeker's 0xb7 comes only after its whole resource preload, and DD sends no world
        // until then. This client enters at once and fetches behind play (respump.cpp prioritize); holdEntry is the
        // warm-up pass, which never enters.
        if (!sentProd_ && !cfg_.holdEntry) prodDue_ = true;
        break;
    case 0x009f: res_->onFrame(body); reportNewResources(); break;
    case 0x003b: st_.sawWorldInit = true; break;
    case 0x0071:
        // 0x0071 is a bare u32, the first thing the server emits for a /client it created: the "accepted" signal.
        if (body.size() == 4) {
            st_.sawSelfRef = true;
            st_.selfRefValue = le32(body.data());
            // Only the FIRST 0x0071 is answered: 0x0d [7f 00], then 0x19 (world refresh, which re-sends the whole world).
            // The stock handler sends both when its login state is 0xf and then moves it on, so a later 0x0071 sends
            // neither. The model's resync poll sends 0x0b [this mob] before these, as stock does.
            if (!sentRefresh_) {
                sentRefresh_ = true;
                sendFrame(0x000d, Bytes{0x7f, 0x00});
                sendFrame(0x0019, Bytes{});
            }
        }
        break;
    case 0x0007:
        // Keepalive. With no ping outstanding, the count reaching 1000 sends an empty c2s 0x0016 and resets the count.
        // The server's empty 0x0016 echo clears the outstanding flag. On an idle world the client sends no 0x0016, and
        // DreamDaemon keeps the connection anyway, so liveness does not depend on it.
        if (!pingOutstanding_ && uint16_t(pingCount_ + 1) == kKeepaliveFrames) {
            pingCount_ = 0;
            pingOutstanding_ = true;
            sendFrame(0x0016, Bytes{});
        } else {
            pingCount_ = uint16_t(pingCount_ + 1);
        }
        break;
    case 0x0016:
        pingOutstanding_ = false;
        pingCount_ = 0;
        break;
    case 0x00a9: {
        // winget(control, prop): [control\0][prop\0][reqid u32][flag u8]. Always answer, or the DM proc parks. With no
        // skin, the honest answer is the empty string: [value\0][reqid u32 LE].
        if (wingetHandler_) { wingetHandler_(body); break; }
        size_t p = 0; bool ok = true;
        cstrAt(body, p, ok); cstrAt(body, p, ok);
        if (ok && body.size() >= p + 4) replyWinget(le32(&body[p]), "");
        break;
    }
    case 0x00fc: {
        // MeasureText: [text\0][style\0][width u16][id u32] -> reply [w u16][h u16][id u32]. The layout follows stock
        // DreamSeeker 516 (render/dsmaptext.h). A malformed body gets no reply (decodeMeasureTextRequest).
        if (auto req = render::decodeMeasureTextRequest(body.data(), body.size())) {
            std::string rb = render::dsMeasureTextReplyBody(*req, mapStyle_ ? mapStyle_() : std::string());
            Bytes r(rb.begin(), rb.end());
            sendFrame(0x00fc, r);
            st_.promptsAnswered++;
        }
        break;
    }
    case 0x0092: {
        if (manualPrompts_) break;   // the owner answers through sendInput()
        // alert(): [id u32][...]. Headless only: answered with the empty string [id u32][\0] so a headless run never parks the
        // proc. "" is the one answer that names no button; stock alerts have no cancel.
        if (body.size() >= 4) {
            Bytes r; putLe32(r, le32(body.data())); r.push_back(0);
            sendFrame(0x0092, r);
            st_.promptsAnswered++;
        }
        break;
    }
    case 0x0062: {
        if (manualPrompts_) break;
        // input(): [title\0][default\0][message\0][typeMask u32][filter u8][kind u8][id u32]. The null answer is the
        // stock cancel bytes, [00][00 ffff0000][id u32], sent for every form (stock offers a cancel only for
        // `as null|...`).
        size_t p = 0; bool ok = true;
        cstrAt(body, p, ok); cstrAt(body, p, ok); cstrAt(body, p, ok);
        if (ok && body.size() >= p + 10) {
            Bytes r{0, 0}; putLe32(r, 0x0000ffff); putLe32(r, le32(&body[p + 6]));
            sendFrame(0x0062, r);
            st_.promptsAnswered++;
        }
        break;
    }
    case 0x0000:
    case 0x00b6:
        logf("server control frame " + std::to_string(type) + ": " + hexEncode(body.subspan(0, std::min<size_t>(body.size(), 48))));
        break;
    case 0x001c:
        if (st_.certChallenges++ == 0) st_.certChallengeFlag = body.empty() ? 0 : body[0];
        if (!st_.accountLogin) logf("login-cert challenge (0x1c): not answered - guest login");
        else answerCertChallenge(body.empty() ? 0 : body[0]);
        break;
    case 0x0036:
        // The server's ack of our 0x0036 triggers the re-key (K_login' = K_login + Rekey(certStr, K_login)), and only
        // after we sent a hub-minted answer. The ack itself is decrypted under the old key.
        if (rekeyPending_) {
            rekeyPending_ = false;
            key_ = cfg_.hubRekey(cauth_->certStr(), key_);   // the certStr the 0x1c sent
            st_.rekeyed = true;
            logf("login-cert ack (0x0036) -> re-keyed the session cipher");
            auto post = std::move(deferredPost_);
            deferredPost_.clear();
            for (auto& p : post)
                if (!sendFrame(p.first, p.second)) { logf("held frame (post-rekey) write failed"); break; }
            if (!post.empty()) logf("flushed " + std::to_string(post.size()) + " held c2s frames under the re-keyed cipher");
        }
        break;
    default: break;
    }
}

}  // namespace fugue::net

namespace fugue::net {
void Session::reportNewResources() {
    if (!resObserver_) return;
    const ResourceStats& r = res_->stats();
    uint64_t now = r.verified + r.alreadyHeld + res_->storedCount() + res_->repushCount();
    if (now == resSeen_) return;   // nothing new was stored by this frame: skip the scan
    resSeen_ = now;
    // Copy the ids first: readStored() may erase from storedThisRun() when a cached file fails verification (it is then
    // declared again and reported when the real bytes arrive).
    for (uint32_t id : res_->takeRepushed()) resReported_.erase(id);
    std::vector<uint32_t> fresh;
    for (const auto& kv : res_->storedThisRun())
        if (resReported_.insert(kv.first).second) fresh.push_back(kv.first);
    for (uint32_t id : fresh) {
        Bytes data;
        if (!res_->readStored(id, data)) { resReported_.erase(id); continue; }
        auto it = res_->storedThisRun().find(id);
        resObserver_(id, it == res_->storedThisRun().end() ? std::string() : it->second, ByteSpan(data.data(), data.size()));
    }
}
}  // namespace fugue::net
