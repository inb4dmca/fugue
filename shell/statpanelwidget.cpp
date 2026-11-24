#include "shell/statpanelwidget.h"
#include <QHeaderView>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QMouseEvent>
#include <QTabBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>

namespace fugue::shell {
namespace {
constexpr int thumbnailSize = 32;  // Qt logical pixels; independent of screen DPR
constexpr int thumbnailRole = Qt::UserRole + 1;
QRect thumbnailRect(const QRect& cell) {
    return {cell.left() + 2, cell.top() + (cell.height() - thumbnailSize) / 2, thumbnailSize, thumbnailSize};
}
class StatDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QImage image = index.data(thumbnailRole).value<QImage>();
        auto text = option;
        if (!image.isNull()) text.rect.setLeft(thumbnailRect(option.rect).right() + 5);
        QStyledItemDelegate::paint(painter, text, index);
        if (!image.isNull()) painter->drawImage(thumbnailRect(option.rect), image);
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        if (!index.data(thumbnailRole).value<QImage>().isNull()) {
            size.setHeight(std::max(size.height(), thumbnailSize + 4));
            size.rwidth() += thumbnailSize + 6;
        }
        return size;
    }
};
}  // namespace

StatPanelWidget::StatPanelWidget(QWidget* parent) : QWidget(parent) {
    setProperty("fugueZone", "info");  // shell/focusfilter.h: keys over the stat panel go to macro routing
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    tabs_ = new QTabBar(this);
    tabs_->setObjectName("statTabs");
    tabs_->setExpanding(false);
    tabs_->setDrawBase(true);
    tabs_->setUsesScrollButtons(true);
    tabs_->setFocusPolicy(Qt::NoFocus);
    table_ = new QTableWidget(this);
    table_->setObjectName("statRows");
    table_->setItemDelegate(new StatDelegate(table_));
    table_->setProperty("fugueZone", "info");
    table_->horizontalHeader()->hide();
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setShowGrid(false);
    table_->setFocusPolicy(Qt::NoFocus);
    l->addWidget(tabs_);
    l->addWidget(table_, 1);
    // QTabBar moves its own selection on a click, but the model decides which tab is selected. A change the model did
    // not make becomes a request to the server, and is undone until the model's `selected` says otherwise.
    connect(tabs_, &QTabBar::currentChanged, this, [this](int i) {
        if (setting_ || i < 0 || i == view_.selected) return;
        setting_ = true;
        tabs_->setCurrentIndex(view_.selected);
        setting_ = false;
        emit tabRequested(i);
    });
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int c) {
        if (view_.verbTab) {
            if (auto* it = table_->item(r, c); it && !it->text().isEmpty()) emit verbClicked(it->text());
        }
    });
    // Atom rows are read from the raw mouse events: cellClicked is left-button only and has no double-click position.
    // Qt delivers a double click as press, release, double-click, release; one report per release plus one for the
    // double-click event gives stock's click, dblclick, click sequence.
    table_->viewport()->installEventFilter(this);
}

void StatPanelWidget::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::FontChange && table_) rebuildRows();  // clickable rows carry their own copy of the font
}

bool StatPanelWidget::eventFilter(QObject* o, QEvent* e) {
    if (o != table_->viewport() || view_.verbTab) return QWidget::eventFilter(o, e);
    auto t = e->type();
    if (t != QEvent::MouseButtonPress && t != QEvent::MouseButtonDblClick && t != QEvent::MouseButtonRelease)
        return QWidget::eventFilter(o, e);
    auto* me = static_cast<QMouseEvent*>(e);
    QModelIndex ix = table_->indexAt(me->position().toPoint());
    int row = (ix.isValid() && ix.column() == 1 && ix.row() < view_.rows.size() && view_.rows[ix.row()].clickable)
                  ? ix.row()
                  : -1;
    QString button = me->button() == Qt::LeftButton ? "left" : me->button() == Qt::MiddleButton ? "middle" : QString();
    if (t == QEvent::MouseButtonPress) {
        pressRow_ = row;
        return QWidget::eventFilter(o, e);
    }
    Qt::KeyboardModifiers km = me->modifiers();
    int mods = (km & Qt::ControlModifier ? ModCtrl : 0) | (km & Qt::ShiftModifier ? ModShift : 0) |
               (km & Qt::AltModifier ? ModAlt : 0);
    auto report = [&](bool dbl) {
        const auto& image = view_.rows[row].thumbnail;
        const QRect icon = thumbnailRect(table_->visualRect(ix));
        const QPoint point = me->position().toPoint() - icon.topLeft();
        // Stock reports unclamped icon coordinates even when clicking the text.
        const int x = image.isNull() ? 0 : int(std::floor(double(point.x()) * image.width() / thumbnailSize)) + 1;
        const int y =
            image.isNull() ? 0 : image.height() - int(std::floor(double(point.y()) * image.height() / thumbnailSize));
        emit rowClicked(row, controlId_, button, mods, dbl, view_.generation, !image.isNull(), x, y);
    };
    if (t == QEvent::MouseButtonDblClick) {
        pressRow_ = row;
        if (row >= 0 && !button.isEmpty()) report(true);
    } else {
        if (row >= 0 && row == pressRow_ && !button.isEmpty()) report(false);
        pressRow_ = -1;
    }
    return QWidget::eventFilter(o, e);
}

void StatPanelWidget::setPanelFont(const QFont& f) {
    setFont(f);
    tabs_->setFont(f);
    table_->setFont(f);
    rebuildRows();
}

void StatPanelWidget::setTextColors(const QColor& prefix, const QColor& suffix) {
    if (prefix == prefix_ && suffix == suffix_) return;
    prefix_ = prefix;
    suffix_ = suffix;
    rebuildRows();
}

void StatPanelWidget::setView(const StatView& v) {
    if (v == view_ && v.generation == view_.generation && tabs_->count() == v.tabs.size()) return;
    pressRow_ = -1;  // an update between press/release must never click a replacement
    setting_ = true;
    bool tabsChanged = v.tabs != view_.tabs;
    bool rowsChanged = !(v.rows == view_.rows) || v.verbTab != view_.verbTab;
    view_ = v;
    if (tabsChanged) {
        while (tabs_->count()) tabs_->removeTab(0);
        for (const QString& t : v.tabs) tabs_->addTab(t);
    }
    if (v.selected >= 0 && v.selected < tabs_->count()) tabs_->setCurrentIndex(v.selected);
    if (rowsChanged) rebuildRows();
    setting_ = false;
}

void StatPanelWidget::rebuildRows() {
    table_->clearContents();
    if (view_.verbTab) {
        const int cols = 4;  // verbs flow row-major across a uniform four-column grid, as in stock
        table_->setColumnCount(cols);
        table_->setRowCount((view_.rows.size() + cols - 1) / cols);
        for (int i = 0; i < view_.rows.size(); i++) {
            auto* it = new QTableWidgetItem(view_.rows[i].name);
            it->setFlags(Qt::ItemIsEnabled);
            table_->setItem(i / cols, i % cols, it);
        }
        for (int c = 0; c < cols; c++) table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
    } else {
        table_->setColumnCount(2);
        table_->setRowCount(view_.rows.size());
        for (int i = 0; i < view_.rows.size(); i++) {
            auto* a = new QTableWidgetItem(view_.rows[i].name);
            auto* b = new QTableWidgetItem(view_.rows[i].text);
            if (!view_.rows[i].thumbnail.isNull()) b->setData(thumbnailRole, view_.rows[i].thumbnail);
            Qt::ItemFlags f = Qt::ItemIsEnabled;
            a->setFlags(f);
            b->setFlags(f);
            a->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
            if (prefix_.isValid()) a->setForeground(prefix_);
            if (suffix_.isValid()) b->setForeground(suffix_);
            if (view_.rows[i].clickable) {
                QFont ft = table_->font();  // the INFO control's font (an item's own font() is the application's)
                ft.setUnderline(true);
                b->setFont(ft);
                b->setToolTip("Click to click this atom");
            }
            table_->setItem(i, 0, a);
            table_->setItem(i, 1, b);
        }
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    }
    table_->resizeRowsToContents();
}

}  // namespace fugue::shell
