#include "model/resync.h"

#include <cstdlib>

namespace fugue::model {

void relinkObjs(World& w) {
    // Back-links first: an obj's back-link is the lowest id of the objs whose Bind names it, placed or not, and 0xffff
    // when none does. Only the relinks write it, so between them it is stale.
    for (auto& [key, a] : w.atoms)
        if (!(key & kRegistryA)) a.backlink = 0xffff;
    for (auto it = w.atoms.rbegin(); it != w.atoms.rend(); ++it) {
        if (it->first & kRegistryA || it->second.bindOther == 0xffff || w.resync.freed.count(it->first)) continue;
        auto t = w.atoms.find(it->second.bindOther);
        if (t != w.atoms.end() && !w.resync.freed.count(t->first)) t->second.backlink = it->first;
    }
    // Then the cell lists, cleared and refilled from every placed obj.
    w.resync.objCells.clear();
    for (const auto& [key, a] : w.atoms)
        if (!(key & kRegistryA) && a.hasPos && !w.resync.freed.count(key)) w.resync.objCells.insert({a.x, a.y, a.z});
    w.resync.relinkObjs = false;
}

void relinkMobs(World& w) {
    // The mob cell lists only: a mob's back-link is left as it was (relinkBindsOnly writes it).
    w.resync.mobCells.clear();
    for (const auto& [key, a] : w.atoms)
        if ((key & kRegistryA) && a.hasPos && !w.resync.freed.count(key)) w.resync.mobCells.insert({a.x, a.y, a.z});
    w.resync.relinkMobs = false;
}

// Back-links from the placed binders only, cells untouched: the relink for a frame that carried Bind entries but no New
// or Pos ones. This is the one place a mob's back-link is written.
static void relinkBindsOnly(World& w, bool mobs) {
    for (auto& [key, a] : w.atoms)
        if (bool(key & kRegistryA) == mobs) a.backlink = 0xffff;
    for (auto it = w.atoms.rbegin(); it != w.atoms.rend(); ++it) {
        if (bool(it->first & kRegistryA) != mobs || !it->second.hasPos || it->second.bindOther == 0xffff) continue;
        if (w.resync.freed.count(it->first)) continue;
        auto t = w.atoms.find(it->second.bindOther | (mobs ? kRegistryA : 0u));
        if (t != w.atoms.end() && !w.resync.freed.count(t->first)) t->second.backlink = it->first & ~kRegistryA;
    }
}

void resyncAfterArrays(World& w, const JVal& m, bool mobs) {
    // The 0x0008 tail relinks objs when the window moved or the frame had a New or Pos entry, else rebuilds their
    // back-links alone when it had a Bind entry. The 0x0007 tail does the same for mobs and also runs a pending obj relink.
    bool moved = m["new"].size() || m["pos"].size(), bound = m["bind"].size();
    if (mobs && w.resync.relinkObjs) relinkObjs(w);
    if (mobs ? w.resync.relinkMobs : w.resync.relinkObjs) moved = true;
    if (moved) (mobs ? relinkMobs : relinkObjs)(w);
    else if (bound) relinkBindsOnly(w, mobs);
}

void ResyncPoller::sweepSet(const World& w, std::set<uint32_t>& mobs, std::set<uint32_t>& objs) {
    // Per held record: placed, its tile's cell held a record of its table at the last relink, bound to nothing, not
    // back-linked, and a mob not still awaiting its creation poll's 0x0009.
    for (const auto& [key, a] : w.atoms) {
        if (!a.hasPos || a.bindOther != 0xffff || w.resync.freed.count(key)) continue;
        std::array<int, 3> cell{a.x, a.y, a.z};
        if (key & kRegistryA) {
            if (!a.pollAnswerDue && a.backlink == 0xffff && w.resync.mobCells.count(cell)) mobs.insert(key & ~kRegistryA);
        } else if (a.backlink == 0xffff && w.resync.objCells.count(cell)) {
            objs.insert(key);
        }
    }
}

// FUGUE_RESYNC_SWEEP=1 enables the sweep (default off); see resync.h.
bool ResyncPoller::sweepFromEnv() {
    const char* e = std::getenv("FUGUE_RESYNC_SWEEP");
    return e && e[0] && e[0] != '0';
}

// FUGUE_RESYNC_GC=1 enables the GC shadow (gc() below; default off); see resync.h.
bool ResyncPoller::gcFromEnv() {
    const char* e = std::getenv("FUGUE_RESYNC_GC");
    return e && e[0] && e[0] != '0';
}

void ResyncPoller::gc(World& w) {
    if (!gc_) return;
    // The record half of the stock GC. A record is kept when placed, back-linked, named in the current tick, or reached
    // from a kept root: the player's mob, usr.contents, a turf's vis_contents, and recursively the vis_contents of a kept
    // record. A table frees its unkept records only when there are more than 32 of them. Each free is a teardown, which
    // arms the sweep.
    gcAppearances_ = w.resync.appearances;
    std::set<uint32_t> kept;
    std::vector<uint32_t> work;
    auto keep = [&](uint32_t key) {
        if (w.atoms.count(key) && !w.resync.freed.count(key) && kept.insert(key).second) work.push_back(key);
    };
    for (const auto& [key, a] : w.atoms)
        if (a.hasPos || a.backlink != 0xffff || a.touchTick == w.resync.tick) keep(key);
    if (w.haveSelf) keep(w.selfRef | kRegistryA);
    for (const auto& [key, appid] : w.usrContents) keep(key);
    for (const auto& [cell, kids] : w.turfVis)
        for (uint32_t key : kids) keep(key);
    while (!work.empty()) {
        uint32_t key = work.back();
        work.pop_back();
        auto vc = w.visContents.find(key);
        if (vc != w.visContents.end())
            for (uint32_t child : vc->second) keep(child);
    }
    for (bool mobs : {true, false}) {
        std::vector<uint32_t> dead;
        for (const auto& [key, a] : w.atoms)
            if (bool(key & kRegistryA) == mobs && !kept.count(key) && !w.resync.freed.count(key)) dead.push_back(key);
        if (dead.size() <= kGcMinFree) continue;
        for (uint32_t key : dead) {
            w.resync.freed.insert(key);
            (mobs ? markMob_ : markObj_).erase(key & ~kRegistryA);
        }
        counter_ = 1;
    }
}

std::vector<ResyncFrame> ResyncPoller::poll(World& w, int64_t nowNs) {
    int64_t tick = w.tickNanos > 0 ? w.tickNanos : kDefaultTickNanos;
    // The client tick is free-running, anchored at the first call. The counter steps on each boundary crossed, and the
    // sweep runs on the fifth, so it fires four to five ticks after the teardown.
    if (!clockStarted_) {
        clockStarted_ = true;
        nextTickNs_ = nowNs + tick;
    }
    // Ticks that passed since the last call run first: they came before whatever the caller just ingested.
    while (nowNs >= nextTickNs_) {
        nextTickNs_ += tick;
        if (counter_ != 0 && sweep_) {
            if (counter_ <= kResyncSweepTicks - 1) counter_++;
            else { counter_ = 0; sweepSet(w, markMob_, markObj_); }
        }
        if ((w.resync.tick++ & 0x3ff) == 0) gc(w);   // every 1024th tick collects
    }
    // A collection also runs when the 501st appearance record since the last one is built.
    if (w.resync.appearances - gcAppearances_ > kGcAppearances) gc(w);
    if (w.resync.teardowns != seenTeardowns_) {   // a teardown (re)arms the counter to 1, not an increment
        seenTeardowns_ = w.resync.teardowns;
        counter_ = 1;
    }
    markMob_.insert(w.resync.newMobs.begin(), w.resync.newMobs.end());
    w.resync.newMobs.clear();
    markObj_.insert(w.resync.newObjs.begin(), w.resync.newObjs.end());
    w.resync.newObjs.clear();
    std::vector<ResyncFrame> out;
    auto emit = [&](uint16_t type, std::set<uint32_t>& ids) {
        if (ids.empty()) return;
        ResyncFrame f;
        f.type = type;
        for (uint32_t id : ids) putLe32(f.body, id);
        ids.clear();
        out.push_back(std::move(f));
    };
    emit(kResyncMobMsg, markMob_);
    emit(kResyncObjMsg, markObj_);
    return out;
}

}  // namespace fugue::model
