#include "model/statpanel.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include "model/json.h"
#include "net/textmacro.h"

namespace fugue::model {

bool parseStatValue(const std::string& js, StatValue& out) {
    JVal v;
    try { v = parseJson(js); } catch (...) { return false; }
    if (v.kind != JVal::Obj) return false;
    out = StatValue{};
    out.kind = uint8_t(v["kind"].n);
    out.text = net::plainText(v["text"].bytesOrStr());
    out.appid = v["appid"].u32();
    out.head = v["head"].u32();
    out.hasAppid = v["hasAppid"].b;
    const JVal& n = v["num"];
    if (n.kind == JVal::Obj) {
        const std::string& h = n["f64"].s;
        if (!h.empty()) {
            uint64_t bits = std::strtoull(h.c_str(), nullptr, 16);
            std::memcpy(&out.num, &bits, 8);
        }
    }
    return true;
}

std::string plainAtomName(const std::string& s) { return net::plainText(s); }

// DreamSeeker prints a stat number as %g: 6 significant digits, exponent form past them (1.23457e+06, 1e-07).
static std::string trimNum(double f) {
    char b[64];
    auto r = std::to_chars(b, b + sizeof b, f, std::chars_format::general, 6);   // %.6g, locale-independent
    std::string s(b, r.ptr);
    if (s == "-0") s = "0";
    return s;
}

std::string statText(const World& w, const World::StatLine& l) {
    StatValue v;
    if (!parseStatValue(l.valueJson, v)) return "";
    if (v.isAtom()) {
        auto it = w.appearances.find(v.appid);
        if (it == w.appearances.end()) return "";
        try { return plainAtomName(parseJson(it->second.fieldsJson)["name"].bytesOrStr()); } catch (...) { return ""; }
    }
    if (v.kind == 0x06) return v.text;
    if (v.kind == 0x2a) return trimNum(v.num);
    return "";
}

namespace {
std::vector<uint16_t> verbIdsOf(const World& w, uint32_t appid) {
    std::vector<uint16_t> out;
    auto it = w.appearances.find(appid);
    if (it == w.appearances.end()) return out;
    try {
        JVal v = parseJson(it->second.fieldsJson);
        for (const JVal& x : v["verbs"].a) out.push_back(x.u16());
    } catch (...) {}
    return out;
}

bool inPanel(const VerbDef& e) {
    if (e.name.empty()) return false;
    uint32_t fl = e.flagWord();
    return !(fl & kVerbFlagHidden) && !(fl & kVerbFlagNullCategory);
}
std::string categoryLabel(const VerbDef& e) {
    if (!inPanel(e)) return "";
    return e.category.empty() ? std::string(kDefaultVerbCategory) : e.category;
}
}  // namespace

bool verbScope(const World& w, std::vector<uint16_t>& ids) {
    ids.clear();
    if (!w.haveClientAppid || !w.appearances.count(w.clientAppid)) return false;
    if (!w.haveSelf) return false;
    std::vector<uint32_t> mobApps;
    for (uint32_t key : {w.selfRef | kRegistryA, w.selfRef}) {
        auto m = w.atoms.find(key);
        if (m != w.atoms.end() && w.appearances.count(m->second.appid)) mobApps.push_back(m->second.appid);
    }
    if (mobApps.empty()) return false;
    std::set<uint16_t> s;
    for (uint16_t id : verbIdsOf(w, w.clientAppid)) s.insert(id);
    for (uint32_t a : mobApps) for (uint16_t id : verbIdsOf(w, a)) s.insert(id);
    if (s.empty()) return false;   // no verbs is indistinguishable from a server that sends no verb sections
    ids.assign(s.begin(), s.end());
    return true;
}

std::vector<std::string> verbCategories(const World& w) {
    std::vector<uint16_t> scope;
    bool scoped = verbScope(w, scope);
    std::set<uint16_t> in(scope.begin(), scope.end());
    std::set<std::string> seen;
    for (const auto& [id, js] : w.verbById) {
        if (scoped && !in.count(uint16_t(id))) continue;
        VerbDef e;
        if (!parseVerbDef(js, e)) continue;
        std::string l = categoryLabel(e);
        if (!l.empty()) seen.insert(l);
    }
    return {seen.begin(), seen.end()};   // byte order, not case-folded
}

std::vector<VerbDef> verbsInCategory(const World& w, const std::string& label) {
    std::vector<uint16_t> scope;
    bool scoped = verbScope(w, scope);
    std::set<uint16_t> in(scope.begin(), scope.end());
    std::vector<VerbDef> out;
    for (const auto& [id, js] : w.verbById) {
        if (scoped && !in.count(uint16_t(id))) continue;
        VerbDef e;
        if (!parseVerbDef(js, e)) continue;
        if (categoryLabel(e) == label) out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const VerbDef& a, const VerbDef& b) { return a.name != b.name ? a.name < b.name : a.id < b.id; });
    return out;
}

std::vector<std::string> statTabs(const World& w) {
    std::vector<std::string> t;
    for (const std::string& n : w.stat.tabs) t.push_back(net::plainText(n));
    for (auto& c : verbCategories(w)) t.push_back(std::move(c));
    return t;
}

bool verbTabCategory(const World& w, int index, std::string& label) {
    int n = int(w.stat.tabs.size());
    if (index < n) return false;
    auto cats = verbCategories(w);
    if (index - n >= int(cats.size())) return false;
    label = cats[size_t(index - n)];
    return true;
}

std::string statTabWireName(const World& w, int index) {
    if (index >= 0 && index < int(w.stat.tabs.size())) return w.stat.tabs[size_t(index)];
    std::string l;
    if (index >= 0 && verbTabCategory(w, index, l)) return kVerbTabWireName;
    return "";
}

bool statRowRef(const World& w, const StatValue& v, proto::enc::AtomRef& out) {
    if (!v.hasAppid) return false;
    switch (v.kind) {
    case 0x01:
        // Stock DreamSeeker names a turf row by its id, [07][head u32], not by the map's [03][x][y]. Bounds are required
        // so a row is clickable only once the world is known.
        if (w.bounds.maxX <= 0 || w.bounds.maxY <= 0) return false;
        out = proto::enc::AtomRef{proto::enc::RefTurfId, v.head, 0, 0};
        return true;
    case 0x02: out = proto::enc::objRef(v.head); return true;
    case 0x03: out = proto::enc::mobRef(v.head); return true;
    default: return false;
    }
}

Bytes statRowClickBody(const World& w, const proto::enc::AtomRef& ref, const StatRowClick& c) {
    // Stock DreamSeeker 516 sends Click(location, control, params) with location = the tab the row is on, control =
    // "infowindow.info" and params "left=1;button=left", for an obj, a turf or a mob alike. The map click's shape
    // (mapwindow.map, a screen-loc, the atom's turf as loc) is not used here.
    return proto::enc::encodeInfoClick(c.sub, ref, statTabWireName(w, w.stat.selected), c.control,
                                       proto::enc::infoClickParams(c.button, c.mods, c.icon, c.iconX, c.iconY));
}

Bytes encodeStatTabBody(uint16_t index, uint8_t token, const std::string& name) {
    Bytes b{uint8_t(index), uint8_t(index >> 8), token};
    b.insert(b.end(), name.begin(), name.end());
    b.push_back(0);
    return b;
}

bool selectStatTab(World& w, int index, StatTabSelect& out) {
    std::string name = statTabWireName(w, index);
    if (name.empty()) return false;
    auto& st = w.stat;
    st.token++;
    if (st.token == 0) st.token = 1;   // 0 is the server's "unsolicited/reset" value and must never be ours
    st.owns = true;
    st.selected = index;
    st.lines.clear();
    out.index = uint16_t(index);
    out.token = st.token;
    out.name = std::move(name);
    return true;
}

}  // namespace fugue::model
