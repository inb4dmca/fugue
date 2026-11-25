#include "shell/statview.h"
#include "model/statpanel.h"
namespace fugue::shell {
StatView buildStatView(const model::World& w, StatThumbnails& thumbnails, AtomTrace* diagnostic) {
    if (diagnostic) { diagnostic->info = {}; diagnostic->info.tab = w.stat.selected; }
    StatView v;
    model::VerbPanel panel = model::verbPanel(w);   // share the panel between the tab strip and selected rows
    // stat() outside any statpanel() goes to a panel with no name, which stock labels "Stats" (stock 516.1689).
    for (const std::string& t : model::statTabs(w, panel)) v.tabs << (t.empty() ? QStringLiteral("Stats") : QString::fromStdString(t));
    v.selected = w.stat.selected;
    v.wireTab = QByteArray::fromStdString(model::statTabWireName(w, w.stat.selected));
    std::string cat;
    if (model::verbTabCategory(w, panel, w.stat.selected, cat)) {
        v.verbTab = true;
        for (const model::VerbDef& e : panel.byCategory[cat]) v.rows.append({QString::fromStdString(e.name), QString(), false});
    } else {
        for (const auto& l : w.stat.lines) {
            const model::StatValue sv = model::statValueOf(l.value);
            proto::enc::AtomRef ref;
            StatRowView row{QString::fromStdString(l.name), QString::fromStdString(model::statText(w, l)), model::statRowRef(w, sv, ref)};
            row.identity = l.identity;
            AtomTrace::Info* d = diagnostic && diagnostic->infoRow && *diagnostic->infoRow == unsigned(v.rows.size()) ? &diagnostic->info : nullptr;
            if (d) {
                d->lifetime = l.identity; d->appearance = sv.appid; d->outcome = TraceReason::NotAtomRow;
                d->hasAtomKey = sv.isAtom() && (sv.kind == 2 || sv.kind == 3);
                if (d->hasAtomKey) d->atomKey = sv.head | (sv.kind == 3 ? model::kRegistryA : 0u);
            }
            if (sv.isAtom()) row.thumbnail = thumbnails.resolve(sv.appid, l.icon.ns, d);
            v.rows.append(std::move(row));
        }
    }
    return v;
}
bool statRowCurrent(const model::World& w, const StatView& published, int row, quint64 generation) {
    return generation != 0 && generation == published.generation && !published.verbTab &&
        published.selected == w.stat.selected && published.wireTab == QByteArray::fromStdString(model::statTabWireName(w, w.stat.selected)) &&
        row >= 0 && row < published.rows.size() && size_t(row) < w.stat.lines.size() && published.rows[row].clickable &&
        published.rows[row].identity != 0 && published.rows[row].identity == w.stat.lines[size_t(row)].identity;
}
}
