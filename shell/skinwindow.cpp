#include "shell/skinwindow.h"
#include "shell/macrodialog.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <QCloseEvent>
#include <cstdlib>
#include <QAction>
#include <QApplication>
#include <QActionGroup>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QStyle>
#include <QToolButton>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QScreen>
#include <QScrollBar>
#include <QTextBrowser>
#include <QWindow>
#include <cstdio>
#include "shell/browserpane.h"
#include "shell/fugueicon.h"
#include "shell/paste.h"

namespace fugue::shell {

namespace {
// A space typed at the end of the command bar asks the completer (`say` becomes `say "`). Anywhere else, or when
// the completer declines, the space types normally.
class SpaceCompleterFilter : public QObject {
public:
    SpaceCompleterFilter(QLineEdit* le, const SkinWindow::SpaceCompleter* c) : QObject(le), le_(le), c_(c) {}
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::KeyPress || !*c_) return QObject::eventFilter(o, e);
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() != Qt::Key_Space || (k->modifiers() & ~Qt::KeypadModifier) || le_->hasSelectedText() ||
            le_->cursorPosition() != le_->text().size())
            return QObject::eventFilter(o, e);
        QString out;
        if (!(*c_)(le_->text(), out)) return QObject::eventFilter(o, e);
        le_->setText(out);
        le_->setCursorPosition(out.size());
        return true;
    }
private:
    QLineEdit* le_;
    const SkinWindow::SpaceCompleter* c_;
};

// One CHILD's divider, as a widget the player can drag. Stock DreamSeeker behaves as follows:
//   - the grab area is the 4 px bar itself;
//   - the pointer shows the split cursor;
//   - show-splitter = false leaves nothing to grab; lock = left does not stop a drag;
//   - the panes do not move while the button is held; the layout changes on release only;
//   - on release the bar's left (top) edge lands on the pointer, with no grab offset kept, and the value is
//     (x - box.x0) / (box width - 4) * 100. Dragged past the window edge, it stops at 0 or 100.
// While the button is held this widget follows the pointer as the only feedback. The value then goes through
// LiveSkin::setSplitter, which clamps it to [10,90]% as a winset does (skin/layout.cpp clampFrac); stock lays out 0..100.
class SplitterHandle : public QWidget {
public:
    using Drop = std::function<void(const std::string& path, double pct)>;
    SplitterHandle(QWidget* parent, std::string path, Drop drop) : QWidget(parent), path_(std::move(path)), drop_(std::move(drop)) {
        setObjectName(QString::fromStdString("splitter:" + path_));
    }
    // sp is in client coordinates; top is the client area's offset in the parent (the menu bar).
    void place(const skin::Splitter& sp, int top) {
        sp_ = sp; top_ = top;
        setCursor(sp.vert ? Qt::SplitHCursor : Qt::SplitVCursor);
        if (!dragging_) setGeometry(sp.rect.x0, sp.rect.y0 + top, sp.rect.dx(), sp.rect.dy());
    }
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
        dragging_ = true;
        raise();
        update();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!dragging_) return;
        int edge = edgeAt(e);
        if (sp_.vert) move(std::clamp(edge, sp_.box.x0, std::max(sp_.box.x0, sp_.box.x1 - width())), y());
        else move(x(), std::clamp(edge, sp_.box.y0, std::max(sp_.box.y0, sp_.box.y1 - height())) + top_);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!dragging_ || e->button() != Qt::LeftButton) return QWidget::mouseReleaseEvent(e);
        dragging_ = false;
        update();
        int bar = sp_.vert ? sp_.rect.dx() : sp_.rect.dy();
        int avail = (sp_.vert ? sp_.box.dx() : sp_.box.dy()) - bar;
        if (avail <= 0) return;
        int from = sp_.vert ? sp_.box.x0 : sp_.box.y0;
        double pct = std::clamp(double(edgeAt(e) - from) / avail * 100, 0.0, 100.0);
        place(sp_, top_);   // back to rest; the relayout below moves it if the value was taken
        if (drop_) drop_(path_, pct);   // the relayout re-places this widget
    }
    void paintEvent(QPaintEvent*) override {
        if (!dragging_) return;   // at rest the window background shows through the 4 px gap
        QPainter(this).fillRect(rect(), palette().color(QPalette::Dark));
    }
private:
    // The pointer position on the split axis, in client coordinates.
    int edgeAt(QMouseEvent* e) const {
        QPoint p = mapToParent(e->position().toPoint());
        return sp_.vert ? p.x() : p.y() - top_;
    }
    std::string path_;
    Drop drop_;
    skin::Splitter sp_;
    int top_ = 0;
    bool dragging_ = false;
};
}  // namespace

// The status strip is 18 px and sits below the client area. Stock DreamSeeker shows it unless the skin sets
// statusbar = false, and the layout uses the same height. Without the strip the input bar would sit flush on the
// window's bottom edge.
constexpr int kStatusBarH = 18;

SkinWindow::SkinWindow(QWidget* parent) : QWidget(parent), live_(std::make_shared<skin::LiveSkin>()) {
    setObjectName("skinwindow");
    setAutoFillBackground(true);
    setWindowIcon(fugueIcon());   // until the world names an icon (setSkinIcon)
    host_ = new BrowseHost(this);
    host_->setLiveSkin(live_);
    host_->setDeclaredPredicate([this](const QString& n) { return declaresBrowser(n); });
    host_->setHiddenPredicate([this](const QString& n) { return declaresHiddenBrowser(n); });
    host_->setChatBacklog([this] { return chat_; });
    connect(host_, &BrowseHost::byondLink, this, &SkinWindow::byondLink);
    connect(host_, &BrowseHost::commandRequested, this, &SkinWindow::commandEntered);
    connect(host_, &BrowseHost::onCloseCommand, this, [this](const QString&, const QString& cmd) { emit worldCommand(cmd); });
    // The live skin is mutated from the session thread, so this window syncs on the GUI thread, coalescing a burst
    // of changes into one sync.
    live_->setOnChange([this] {
        QMetaObject::invokeMethod(this, [this] { syncQueued_ = false; syncFromLive(); }, Qt::QueuedConnection);
    });
    // winget(null, "focus") follows the real keyboard focus, on a focus move and on a window activation. A fresh popup
    // has no focus widget yet, and stock answers "<popup>.browser" for it shortly after browse().
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) { if (now) publishFocus(now); });
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow* w) {
        if (!w) return;   // focus left the application: keep the last answer
        QWidget* fw = QApplication::focusWidget();
        publishFocus(fw && fw->window() == QApplication::activeWindow() ? fw : QApplication::activeWindow());
    });
}

// The system-menu corner button. QMenuBar::sizeHint uses a corner widget's sizeHint, and a plain QToolButton's hint
// is a few pixels taller than a menu item, which would grow the bar. This one reports the size it is given.
struct BarCornerButton : QToolButton {
    using QToolButton::QToolButton;
    QSize hint;
    QSize sizeHint() const override { return hint.isValid() ? hint : QToolButton::sizeHint(); }
};

void SkinWindow::publishFocus(QWidget* now) {
    if (!now) return;
    // Check popups first: a popup is a top-level child of this widget, so the parent walk would reach `this` through it.
    QString id = host_->focusIdFor(now);   // a popup's page, "<popup>.browser"
    if (!id.isEmpty()) { live_->setGuiFocus(id.toStdString()); return; }
    for (QWidget* w = now; w; w = w->parentWidget()) {
        for (const auto& [key, cw] : widgets_)
            if (cw == w) { live_->setGuiFocus(key); return; }
        if (w == this) { live_->setGuiFocus(""); return; }   // the main window itself
        if (w->isWindow()) return;                            // another top-level, such as a dialog: not a skin control
    }
}
SkinWindow::~SkinWindow() {
    live_->setOnChange({});
    clearWidgets();   // the panes first: the host's profile must outlive every page
    delete host_;
}

void SkinWindow::clearWidgets() {
    if (host_) for (auto* bp : browsers_) host_->releaseControl(bp);
    for (auto& [k, w] : widgets_) delete w;
    for (auto& [k, h] : handles_) delete h;
    handles_.clear();
    widgets_.clear(); types_.clear(); outputs_.clear(); infos_.clear(); browsers_.clear();
    map_ = nullptr;
    lastMapSignalled_ = nullptr;
}

int SkinWindow::menuBarHeight() const { return bar_ && !bar_->isHidden() ? bar_->height() : 0; }
int SkinWindow::statusBarHeight() const { return status_ && !status_->isHidden() ? kStatusBarH : 0; }

skin::Rect SkinWindow::clientRect() const { return {0, 0, width(), std::max(0, height() - chromeHeight())}; }

namespace {
// The main window's `statusbar` setting comes from the skin file, whose default is true. Nothing at runtime reads a winset of it.
bool wantsStatusBar(const skin::SkinSnapshot& s) {
    if (s.file)
        if (const skin::Block* mw = skin::findMainWindow(*s.file))
            if (const skin::Elem* m = mw->main()) return m->boolv("statusbar", true);
    return true;
}
}  // namespace

void SkinWindow::syncStatusBar(const skin::SkinSnapshot& s) {
    if (!wantsStatusBar(s)) { if (status_) status_->hide(); return; }
    if (!status_) {
        status_ = new QLabel(this);   // shows the map's hoverName() (makeWidget connects MapWidget::hoverChanged)
        status_->setObjectName("statusbar");
        status_->setFrameShape(QFrame::Panel);
        status_->setFrameShadow(QFrame::Sunken);
        status_->setAutoFillBackground(true);
        status_->setBackgroundRole(QPalette::Button);   // OS chrome, not the skin's background colour
        status_->setTextFormat(Qt::PlainText);           // an atom's name is data, not markup
        if (map_) status_->setText(map_->hoverName());
    }
    status_->setGeometry(0, height() - kStatusBarH, width(), kStatusBarH);
    status_->show();
}

QSize SkinWindow::designSize() const {
    int bar = 0;
    if (!snap_.menu.empty()) {
        if (bar_) bar = bar_->sizeHint().height();
        else bar = 22;
    }
    int status = wantsStatusBar(snap_) ? kStatusBarH : 0;
    return QSize(design_.width(), design_.height() + bar + status);
}

QString SkinWindow::loadSkin(const std::string& dmf) {
    std::string err = live_->load(dmf);
    if (!err.empty()) return QString::fromStdString(err);
    // Size the client from the window (or the design size before the first resize), then build from one snapshot.
    skin::SkinSnapshot s = live_->snapshot();
    int w = width() > 1 ? width() : 0, h = height() > 1 ? height() : 0;
    if (!w || !h) {
        w = 640; h = 480;
        if (s.file)
            if (const skin::Block* mw = skin::findMainWindow(*s.file))
                if (const skin::Elem* m = mw->main()) {
                    int64_t dw, dh;
                    if (m->size("size", dw, dh) && dw > 0 && dh > 0) { w = int(dw); h = int(dh); }
                }
    }
    live_->setClient(w, std::max(0, h - chromeHeight()));
    syncFromLive();
    return {};
}

QWidget* SkinWindow::makeWidget(const skin::Control& c) {
    QWidget* w = nullptr;
    if (c.type == "MAP") {
        if (map_) return nullptr;   // the first .dmf map only; winset-created maps are built by syncRuntimeMaps
        map_ = new MapWidget(this);
        connect(map_, &MapWidget::hoverChanged, this, [this](const QString& name) { if (status_) status_->setText(name); });
        w = map_;
    } else if (c.type == "INFO") {
        auto* sp = new StatPanelWidget(this);
        sp->setControlId(QString::fromStdString(c.window + "." + c.name));
        infos_.append(sp);
        sp->setView(statView_);
        connect(sp, &StatPanelWidget::tabRequested, this, &SkinWindow::statTabRequested);
        connect(sp, &StatPanelWidget::rowClicked, this, &SkinWindow::statRowClicked);
        connect(sp, &StatPanelWidget::verbClicked, this, &SkinWindow::statVerbClicked);
        w = sp;
    } else if (c.type == "OUTPUT") {
        auto* tb = new QTextBrowser(this);
        tb->setOpenLinks(false);
        tb->setProperty("fugueZone", "output");
        outputs_.append(tb);
        tb->setHtml(chat_.join("<br>"));
        tb->verticalScrollBar()->setValue(tb->verticalScrollBar()->maximum());
        w = tb;
    } else if (c.type == "INPUT") {
        auto* le = wirePaste(new QLineEdit(this));
        // The INPUT's `command` (kept in "fugueInputCommand" by applyProps). A `!text` command is a sticky default
        // that the bar returns to after every send. A bare `text` is put in front of what the player typed. With a
        // `!say "` default in force, Enter on `say "hello` sends say("hello") and the bar goes back to `say "`.
        connect(le, &QLineEdit::returnPressed, this, [this, le] {
            QString cmd = le->property("fugueInputCommand").toString();
            QString line = le->text();
            if (!cmd.isEmpty() && !cmd.startsWith('!') && !line.isEmpty()) line = cmd + line;
            emit commandEntered(line);
            le->setText(cmd.startsWith('!') ? cmd.mid(1) : QString());
        });
        le->installEventFilter(new SpaceCompleterFilter(le, &spaceCompleter_));
        w = le;
    } else if (c.type == "BROWSER") {
        auto* bp = new BrowserPane(this, host_->profile());
        browsers_.insert(QString::fromStdString(c.name), bp);
        w = bp;
        host_->adoptControl(QString::fromStdString(c.window), QString::fromStdString(c.name), bp);   // shows a page sent before the control existed
    } else if (c.type == "BUTTON") {
        auto* b = new QPushButton(QString::fromStdString(c.name), this);
        QString cmd = QString::fromStdString(c.elem ? c.elem->str("command", "") : "");
        // A `button-type = pushbox` button sets its own is-checked before its command runs (a grouped one is a radio
        // member and only ever goes true). The command must see the new state, since vg's commands test it.
        bool pushbox = c.elem && skin::lowerAscii(c.elem->str("button-type", "")) == "pushbox";
        std::string path = c.window + "." + c.name, group = c.elem ? c.elem->str("group", "") : "";
        if (!cmd.isEmpty() || pushbox)
            connect(b, &QPushButton::clicked, this, [this, cmd, pushbox, path, group] {
                if (pushbox) {
                    std::string cur;
                    bool now = group.empty() ? !(live_->winget(path, "is-checked", cur) && skin::winsetBool(cur)) : true;
                    live_->winset("", path + ".is-checked=" + (now ? "true" : "false"));
                    syncFromLive();
                }
                if (!cmd.isEmpty()) emit buttonCommand(cmd);
            });
        w = b;
    } else if (c.type == "LABEL") {
        w = new QLabel(this);
    } else {
        auto* f = new QFrame(this);   // placeholder for a control type this client does not model
        f->setFrameShape(QFrame::StyledPanel);
        w = f;
    }
    return w;
}

void SkinWindow::rebuild(const skin::SkinSnapshot& s) {
    clearWidgets();
    mapPaneShown_ = infoPaneShown_ = false;   // a new skin shows its panes afresh (setPaneShown)
    builtFileId_ = s.fileId;
    lastGen_ = ~0ull;
    design_ = QSize(640, 480);
    if (s.file)
        if (const skin::Block* mw = skin::findMainWindow(*s.file))
            if (const skin::Elem* m = mw->main()) {
                int64_t w, h;
                if (m->size("size", w, h) && w > 0 && h > 0) design_ = QSize(int(w), int(h));
                skinTitle_ = QString::fromStdString(m->str("title", ""));
                uint8_t c[4];
                if (m->color("background-color", c)) {
                    QPalette pal = palette();
                    pal.setColor(QPalette::Window, QColor(c[0], c[1], c[2]));
                    setPalette(pal);
                }
            }
    updateTitle();
    menuSig_.clear();
}

// Per-control live properties: the overlay on top of the skin file.
void SkinWindow::applyProps(const skin::Control& c, QWidget* w, const skin::SkinSnapshot& s) {
    auto prop = [&](const char* key, std::string& out) {
        if (s.props.controlProp(c.window, c.name, key, out)) return true;
        if (c.elem)
            if (const std::string* v = c.elem->get(key)) { out = *v; return true; }
        return false;
    };
    std::string v;
    w->setEnabled(!c.disabled);
    if (auto* b = qobject_cast<QPushButton*>(w)) {
        if (prop("text", v)) b->setText(QString::fromStdString(v));
        std::string chk;
        bool pushbox = c.elem && skin::lowerAscii(c.elem->str("button-type", "")) == "pushbox";
        if (s.props.controlProp(c.window, c.name, "is-checked", chk) || (c.elem && c.elem->get("is-checked")) || pushbox) {
            if (!(s.props.controlProp(c.window, c.name, "is-checked", chk))) chk = c.elem->str("is-checked", "false");
            b->setCheckable(true);
            b->setChecked(skin::winsetBool(chk));
        }
    } else if (auto* le = qobject_cast<QLineEdit*>(w); le && c.type == "INPUT") {
        // `command` goes to the bar. A new `!` default replaces the text only while the bar still shows the old default
        // unedited. Otherwise the player's text is left alone.
        std::string cmd;
        prop("command", cmd);
        QString now = QString::fromStdString(cmd), was = le->property("fugueInputCommand").toString();
        if (now != was || !le->property("fugueInputCommand").isValid()) {
            auto shown = [](const QString& c) { return c.startsWith('!') ? c.mid(1) : QString(); };
            if (le->text() == shown(was)) { le->setText(shown(now)); le->setCursorPosition(le->text().size()); }
            le->setProperty("fugueInputCommand", now);
        }
    } else if (auto* l = qobject_cast<QLabel*>(w)) {
        if (prop("text", v)) l->setText(QString::fromStdString(v));
    } else if (c.type == "MAP" && map_ == w) {
        bool rc = c.elem ? c.elem->boolv("right-click", false) : false;
        if (s.props.controlProp(c.window, c.name, "right-click", v)) rc = skin::winsetBool(v);
        map_->setRightClickToWire(rc);
        // Scaling: the live overlay (a runtime winset, such as vg's "Stretch to fit" setting icon-size=0) wins over the
        // file. Setting zoom or icon-size clears the other.
        std::string zs, is, lb, zm;
        double zoom = c.elem ? std::atof(c.elem->str("zoom", "0").c_str()) : 0;
        int icon = c.elem ? int(c.elem->intv("icon-size", 0)) : 0;
        if (s.props.controlProp(c.window, c.name, "icon-size", is)) { icon = std::atoi(is.c_str()); zoom = 0; }
        if (s.props.controlProp(c.window, c.name, "zoom", zs)) { zoom = std::atof(zs.c_str()); icon = 0; }
        bool letterbox = c.elem ? c.elem->boolv("letterbox", true) : true;
        if (s.props.controlProp(c.window, c.name, "letterbox", lb)) letterbox = skin::winsetBool(lb);
        std::string mode = c.elem ? c.elem->str("zoom-mode", "normal") : "normal";
        if (s.props.controlProp(c.window, c.name, "zoom-mode", zm)) mode = zm;
        map_->setMapScale(zoom, icon, letterbox, mode);
    }
    // Colours come from the overlay only; the file's own per-control colours are not applied.
    QString css;
    if (s.props.controlProp(c.window, c.name, "background-color", v)) css += QString("background-color:%1;").arg(QString::fromStdString(v));
    if (s.props.controlProp(c.window, c.name, "text-color", v)) css += QString("color:%1;").arg(QString::fromStdString(v));
    if (css != w->property("fugueCss").toString()) {
        w->setProperty("fugueCss", css);
        w->setStyleSheet(css);
    }
}

void SkinWindow::apply(const skin::SkinSnapshot& s) {
    std::string mainKey;
    // Create what is now visible and placed; show or hide and place everything.
    std::set<std::string> placed;
    for (const skin::Control& c : s.tree.controls) {
        if (c.type == "CHILD" || c.type == "MAIN") continue;
        std::string key = c.window + "." + c.name;
        bool show = c.visible && !c.offPane;
        auto it = widgets_.find(key);
        if (it == widgets_.end()) {
            if (!show) continue;
            QWidget* w = makeWidget(c);
            if (!w) continue;
            w->setObjectName(QString::fromStdString(key));
            widgets_[key] = w;
            types_[key] = c.type;
            it = widgets_.find(key);
        }
        QWidget* w = it->second;
        placed.insert(key);
        applyProps(c, w, s);
        int top = menuBarHeight();
        w->setGeometry(c.rect.x0, c.rect.y0 + top, c.rect.dx(), c.rect.dy());
        w->setVisible(show);
    }
    // A control that left the layout is hidden, not destroyed, so its content survives.
    for (auto& [key, w] : widgets_)
        if (!placed.count(key)) w->hide();

    // The CHILD dividers the layout placed (none for show-splitter=false or a cleared side).
    std::set<std::string> dividers;
    for (const skin::Splitter& sp : s.tree.splitters) {
        if (sp.rect.empty()) continue;
        QWidget*& h = handles_[sp.path];
        if (!h) h = new SplitterHandle(this, sp.path, [this](const std::string& path, double pct) {
            live_->setSplitter(path, pct);
            syncFromLive();
        });
        static_cast<SplitterHandle*>(h)->place(sp, menuBarHeight());
        h->show();
        h->raise();
        dividers.insert(sp.path);
    }
    for (auto& [path, h] : handles_)
        if (!dividers.count(path)) h->hide();

    // Window title, from a winset of the main window's `title`.
    std::string t;
    if (!s.mainWindow.empty() && (s.props.get(s.mainWindow, "title", t) || s.props.get("mainwindow", "title", t)))
        skinTitle_ = QString::fromStdString(t);
    updateTitle();
    // Popup titles follow the same props (shell/browsehost.cpp syncPopupTitles).
    if (host_) host_->syncPopupTitles(s.props);
}

void SkinWindow::setSkinIcon(const QString& name, const QByteArray& bytes) {
    QImage img = bytes.isEmpty() ? QImage() : skinIconImage(bytes);
    if (!bytes.isEmpty() && img.isNull())
        std::fprintf(stderr, "[icon] window icon \"%s\": %d bytes that do not decode - Fugue's icon instead\n", name.toUtf8().constData(), int(bytes.size()));
    skinIcon_ = !img.isNull();
    setWindowIcon(skinIcon_ ? QIcon(QPixmap::fromImage(img)) : fugueIcon());
    // A browse() popup is a child WINDOW: Qt reads the parent's icon when it is created but sends an open one no change,
    // so bring the open ones along. A dialog with an icon of its own (prompts, options, macros: Fugue's) keeps it.
    for (QWidget* c : findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        if (c->isWindow() && !c->testAttribute(Qt::WA_SetWindowIcon) && c->windowHandle()) c->windowHandle()->setIcon(windowIcon());
}

void SkinWindow::updateTitle() {
    QString t = skinTitle_;
    if (builtFileId_ == builtinFileId_ && t == "Dream Seeker") t.clear();   // the built-in skin's own default title
    if (t.isEmpty()) t = worldName_;
    setWindowTitle(t.isEmpty() ? QString("Fugue") : t);
}

void SkinWindow::rebuildMenu(const skin::Menu& m) {
    QString sig;
    for (const skin::MenuCat& c : m.cats) {
        sig += "|" + QString::fromStdString(c.name + ":" + c.label) + (c.disabled ? "!" : "");
        for (const skin::MenuItem& it : c.items)
            sig += "," + QString::fromStdString(it.name + ":" + it.label + ":" + it.command) + (it.disabled ? "!" : "") + (it.separator ? "-" : "");
    }
    if (sig == menuSig_) return;
    menuSig_ = sig;
    if (m.empty()) {
        if (bar_) { bar_->clear(); bar_->hide(); }
        return;
    }
    if (!bar_) {
        bar_ = new QMenuBar(this);
        // Stock DreamSeeker's system-menu entries (shell/macrodialog.h) sit under the caption icon. The window manager
        // owns that icon here, so the same entries sit behind the window icon in the menu bar's corner.
        sysBtn_ = new BarCornerButton(bar_);
        sysBtn_->setObjectName("systemmenu");
        sysBtn_->setAutoRaise(true);
        sysBtn_->setPopupMode(QToolButton::InstantPopup);
        sysBtn_->setToolTip("Options & Messages, Macros...");
        sysBtn_->setIcon(windowIcon());
        auto* sys = new QMenu(sysBtn_);
        addSystemMenuItems(sys, [this](const QString& c) { emit menuCommand(c); });
        sysBtn_->setMenu(sys);
        connect(this, &QWidget::windowIconChanged, sysBtn_, &QToolButton::setIcon);
        // The button must not make the bar taller, or every skin's client area would shrink. QMenuBar pads a corner
        // widget by its panel width and vertical margin, so the button is sized to the bar's item height.
        const int pad = bar_->style()->pixelMetric(QStyle::PM_MenuBarPanelWidth, nullptr, bar_) +
                        bar_->style()->pixelMetric(QStyle::PM_MenuBarVMargin, nullptr, bar_);
        QMenuBar probe;
        probe.setStyle(bar_->style());
        probe.addMenu("X");
        const int itemH = std::max(8, probe.sizeHint().height() - 2 * pad);
        sysBtn_->setIconSize(QSize(itemH - 4, itemH - 4));
        sysBtn_->setFixedSize(itemH, itemH);
        static_cast<BarCornerButton*>(sysBtn_)->hint = QSize(itemH, itemH);   // QMenuBar sizes itself from the HINT
        bar_->setCornerWidget(sysBtn_, Qt::TopLeftCorner);
    }
    bar_->clear();   // the skin's menus; the corner widget stays
    menuActs_.clear();
    menuChk_.clear();
    auto withAccel = [](const std::string& label, int accel) {
        QString t = QString::fromStdString(label);
        if (accel >= 0 && accel < t.size()) t.insert(accel, '&');
        return t;
    };
    for (const skin::MenuCat& c : m.cats) {
        QMenu* menu = bar_->addMenu(withAccel(c.label, c.accel));
        menu->setEnabled(!c.disabled);
        QHash<QString, QActionGroup*> groups;
        for (const skin::MenuItem& it : c.items) {
            if (it.separator) { menu->addSeparator(); continue; }
            QString text = withAccel(it.label, it.accel);
            if (!it.shortcut.empty()) text += "\t" + QString::fromStdString(it.shortcut);
            QAction* a = menu->addAction(text);
            a->setEnabled(!it.disabled);
            if (it.canCheck) {
                a->setCheckable(true);
                a->setChecked(it.checked);
                if (!it.name.empty()) { menuActs_.emplace(it.name, a); menuChk_[it.name] = it.checked; }
                if (!it.group.empty()) {
                    QString g = QString::fromStdString(it.group);
                    if (!groups.contains(g)) { groups[g] = new QActionGroup(menu); groups[g]->setExclusive(true); }
                    groups[g]->addAction(a);
                }
            }
            QString cmd = QString::fromStdString(it.command);
            // A can-check item's is-checked flips before its command runs, as a pushbox button's does (applyProps).
            // Worlds test the new state in the command, and the change also lands in the overlay.
            std::string name = it.canCheck ? it.name : std::string();
            if (!cmd.isEmpty() || !name.empty())
                connect(a, &QAction::triggered, this, [this, cmd, name, a] {
                    if (!name.empty()) {
                        menuChk_[name] = a->isChecked();
                        live_->winset("", name + ".is-checked=" + (a->isChecked() ? "true" : "false"));
                    }
                    if (!cmd.isEmpty()) emit menuCommand(cmd);
                });
        }
    }
    bar_->setGeometry(0, 0, width(), bar_->sizeHint().height());
    bar_->show();
}

// A winset of a menu item's is-checked moves the check mark, and a radio sibling unchecked by a pick follows.
// Only a changed value is pushed.
void SkinWindow::syncMenuChecks(const skin::Menu& m) {
    for (const skin::MenuCat& c : m.cats)
        for (const skin::MenuItem& it : c.items) {
            if (it.name.empty() || !it.canCheck) continue;
            auto was = menuChk_.find(it.name);
            if (was == menuChk_.end() || was->second == it.checked) continue;
            was->second = it.checked;
            auto [b, e] = menuActs_.equal_range(it.name);
            for (auto i = b; i != e; ++i) i->second->setChecked(it.checked);
        }
}

void SkinWindow::syncFromLive() {
    if (syncing_) return;
    syncing_ = true;
    skin::SkinSnapshot s = live_->snapshot();
    if (!s.file) { syncing_ = false; return; }
    if (s.fileId != builtFileId_) rebuild(s);
    // The menu bar and the status bar come first: their heights come off the client area the layout is measured in.
    int before = chromeHeight();
    rebuildMenu(s.menu);
    syncMenuChecks(s.menu);
    if (bar_ && !bar_->isHidden()) bar_->setGeometry(0, 0, width(), bar_->sizeHint().height());
    syncStatusBar(s);
    if (chromeHeight() != before) {
        live_->setClient(width(), std::max(0, height() - chromeHeight()));
        s = live_->snapshot();
    }
    if (s.generation != lastGen_ || s.fileId != snap_.fileId) {
        apply(s);
        lastGen_ = s.generation;
    }
    snap_ = s;
    markMapShown();
    markInfoShown();
    if (map_ != lastMapSignalled_) { lastMapSignalled_ = map_; if (map_) emit mapReady(map_); }
    if (QSize cl(s.client.dx(), s.client.dy()); cl != lastClient_ && cl.width() > 0 && cl.height() > 0) { lastClient_ = cl; emit clientResized(cl); }
    if (s.macro != lastMacro_) {
        bool first = lastMacro_.empty();
        lastMacro_ = s.macro;
        if (!first || !s.macro.empty()) emit macroChanged(QString::fromStdString(s.macro));
    }
    if (s.focusSeq != lastFocusSeq_) {
        lastFocusSeq_ = s.focusSeq;
        if (const skin::Control* c = s.tree.byName(s.focus)) {
            auto it = widgets_.find(c->window + "." + c->name);
            if (it != widgets_.end() && it->second->isVisible()) it->second->setFocus(Qt::OtherFocusReason);
        }
    }
    syncRuntimeMaps(s);
    syncing_ = false;
}

namespace {
std::string lowerKey(const std::string& s) { std::string o = s; for (char& c : o) c = char(std::tolower(static_cast<unsigned char>(c))); return o; }
}  // namespace

// The runtime MAP controls (skin::SkinSnapshot::runtime) inside their browse() popups. pos is in the popup's client area.
// tgui's ByondUi sends pos/size in device pixels (which may be fractional), so they are divided by the popup's
// devicePixelRatioF() into the logical pixels Qt places widgets in, then rounded. A control with no `size` takes the
// popup's whole client area. A parent that is a skin window gets no widget. A widget built here starts with the last
// picture held for its id; it also asks for a fresh one (auxMapBuilt), since an unmount drops the held picture and the
// render thread would otherwise skip the unchanged scene a remount brings.
void SkinWindow::syncRuntimeMaps(const skin::SkinSnapshot& s) {
    std::set<std::string> live;
    for (const skin::SkinSnapshot::RuntimeControl& rc : s.runtime) {
        if (rc.type != "MAP" || !host_) continue;
        QWidget* popup = host_->popupFor(QString::fromStdString(rc.parent));
        if (!popup) continue;
        std::string key = lowerKey(rc.name);
        live.insert(key);
        QPointer<MapWidget>& mw = auxMaps_[key];
        if (mw && mw->parentWidget() != popup) delete mw.data();
        if (!mw) {
            mw = new MapWidget(popup);
            mw->setObjectName(QString::fromStdString(rc.name));
            auto img = auxImg_.find(key);
            if (img != auxImg_.end()) mw->setFrame(img->second);
            emit auxMapBuilt(QString::fromStdString(rc.name));
        }
        std::string v;
        double x = 0, y = 0, w = popup->width(), h = popup->height();
        if (s.props.get(rc.name, "pos", v)) std::sscanf(v.c_str(), "%lf,%lf", &x, &y);
        if (s.props.get(rc.name, "size", v)) {
            double sw = 0, sh = 0;
            if (std::sscanf(v.c_str(), "%lfx%lf", &sw, &sh) == 2 && sw > 0 && sh > 0) { w = sw; h = sh; }
        }
        bool visible = !(s.props.get(rc.name, "is-visible", v) && !skin::winsetBool(v));
        const double dpr = popup->devicePixelRatioF() > 0 ? popup->devicePixelRatioF() : 1.0;
        x /= dpr; y /= dpr; w /= dpr; h /= dpr;
        mw->setGeometry(int(std::lround(x)), int(std::lround(y)), std::max(1, int(std::lround(w))), std::max(1, int(std::lround(h))));
        mw->setVisible(visible);
        mw->raise();   // above the popup's page
    }
    for (auto it = auxMaps_.begin(); it != auxMaps_.end();) {
        if (live.count(it->first)) { ++it; continue; }
        if (it->second) delete it->second.data();
        auxImg_.erase(it->first);
        it = auxMaps_.erase(it);
    }
}

void SkinWindow::setAuxMapFrame(const QString& id, const QImage& img) {
    std::string key = lowerKey(id.toStdString());
    auxImg_[key] = img;
    // No widget yet (its popup is not open, or its parent is a skin window): the picture is held for the widget
    // syncRuntimeMaps builds when the popup opens (showBrowse) or the control changes (syncFromLive).
    auto it = auxMaps_.find(key);
    if (it == auxMaps_.end() || !it->second) return;
    it->second->setFrame(img);
}

MapWidget* SkinWindow::auxMap(const QString& id) const {
    auto it = auxMaps_.find(lowerKey(id.toStdString()));
    return it == auxMaps_.end() ? nullptr : it->second.data();
}

void SkinWindow::closeEvent(QCloseEvent* e) {
    host_->setQuitting();
    QWidget::closeEvent(e);
}

// What winget(client, null, ...) reports for the OS: dpi, screen-pos and screen-size.
//
// dpi is the logical DPI scale, not the device pixel ratio. Stock DreamSeeker sizes its windows in physical pixels
// even when it reports a dpi of 2, and vg multiplies its browse() sizes by that answer. This client sizes in Qt
// logical pixels, which already carry the device pixel ratio, so the answer is logicalDotsPerInch / 96. Answering
// with the device pixel ratio would double vg's windows on any HiDPI screen.
void SkinWindow::publishClientEnv() {
    char dpi[32];
    QScreen* scr = screen();
    std::snprintf(dpi, sizeof dpi, "%g", scr ? scr->logicalDotsPerInch() / 96.0 : 1.0);
    live_->setClientEnv("dpi", dpi);
    if (QScreen* sc = screen()) {
        QRect a = sc->availableGeometry();
        live_->setClientEnv("screen-pos", std::to_string(a.x()) + "," + std::to_string(a.y()));
        live_->setClientEnv("screen-size", std::to_string(a.width()) + "x" + std::to_string(a.height()));
    }
}

// The main window's place on the screen, as its frame's top-left. Stock answers this for mainwindow.pos and outer-pos.
void SkinWindow::moveEvent(QMoveEvent* e) {
    QWidget::moveEvent(e);
    if (isWindow()) live_->setWindowPos("", frameGeometry().x(), frameGeometry().y());
}

void SkinWindow::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (isWindow()) live_->setWindowPos("", frameGeometry().x(), frameGeometry().y());
}

void SkinWindow::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    publishClientEnv();
    if (bar_ && !bar_->isHidden()) bar_->setGeometry(0, 0, width(), bar_->sizeHint().height());
    if (status_ && !status_->isHidden()) status_->setGeometry(0, height() - kStatusBarH, width(), kStatusBarH);
    if (!live_->loaded()) return;
    live_->setClient(width(), std::max(0, height() - chromeHeight()));
    syncFromLive();
}

void SkinWindow::setMapFrame(const QImage& img, HitPtr hit) {
    mapImg_ = img;
    markMapShown();
    if (map_) map_->setFrame(img, std::move(hit));
}

// The map pane is shown once the map has a picture, since stock runs the pane's on-show only from then on. This is
// keyed on the picture, not the widget, because the side is empty until then. The pane is the window of the skin's
// default MAP, or its first.
void SkinWindow::markMapShown() {
    if (mapPaneShown_ || mapImg_.isNull() || !snap_.file) return;
    std::string first, def;
    for (const skin::Block& b : snap_.file->blocks)
        if (b.kind == "window")
            for (const skin::Elem* e : b.controls())
                if (e->type() == "MAP") {
                    if (first.empty()) first = b.name;
                    if (def.empty() && e->boolv("is-default", false)) def = b.name;
                }
    mapPaneShown_ = true;
    if (!first.empty()) live_->setPaneShown(def.empty() ? first : def);
}

// The statpanel pane is shown the same way, once it has a tab to show.
void SkinWindow::markInfoShown() {
    if (infoPaneShown_ || statView_.tabs.isEmpty() || !snap_.file) return;
    infoPaneShown_ = true;
    for (const skin::Block& b : snap_.file->blocks)
        if (b.kind == "window")
            for (const skin::Elem* e : b.controls())
                if (e->type() == "INFO") live_->setPaneShown(b.name);
}

void SkinWindow::setMapHit(HitPtr hit) {
    if (!map_) return;
    markMapShown();
    if (!map_->hasFrame() && !mapImg_.isNull()) map_->setFrame(mapImg_, std::move(hit));
    else map_->setHit(std::move(hit));
}

void SkinWindow::appendChat(const QStringList& html) {
    chat_ += html;
    while (chat_.size() > 500) chat_.removeFirst();
    if (host_->chatHosted()) {   // the world's own chat page replaces the log: lines go to its output(), once READY
        for (const QString& h : html) host_->chatLine(h);
        return;
    }
    for (auto* o : outputs_) {
        for (const QString& h : html) o->append(h);
        o->verticalScrollBar()->setValue(o->verticalScrollBar()->maximum());
    }
}

void SkinWindow::setStat(const QString& html) { stat_ = html; }   // the INFO control renders setStatView()

void SkinWindow::setStatView(const StatView& v) {
    statView_ = v;
    markInfoShown();
    for (auto* i : infos_) i->setView(v);
}

void SkinWindow::showBrowse(const QString& name, const QString& params, const QString& html) {
    host_->open(name, params, html);
    if (live_->loaded()) syncRuntimeMaps(live_->snapshot());   // a runtime map may already name this popup as its parent
}
void SkinWindow::closeBrowse(const QString& name) { host_->close(name); }
void SkinWindow::pageCall(const QString& window, const QString& fn, const QStringList& args) { host_->pageCall(window, fn, args); }

static const skin::Elem* declaredBrowser(const skin::File* f, const QString& name) {
    if (!f) return nullptr;
    const QString ctl = name.section('.', -1);
    for (const skin::Block& b : f->blocks) {
        if (b.kind != "window") continue;
        for (const skin::Elem& e : b.elems)
            if (e.type() == "BROWSER" && (QString::fromStdString(e.name) == name || (QString::fromStdString(e.name) == ctl && (!name.contains('.') || QString::fromStdString(b.name) == name.section('.', 0, -2)))))
                return &e;
    }
    return nullptr;
}

bool SkinWindow::declaresBrowser(const QString& name) const { return declaredBrowser(snap_.file.get(), name) != nullptr; }

// The skin file declares it: vg's interface/skin.dmf declares rpane.hosttracker and rpane.hosttracker2 at 1x1 with
// `is-visible = false`.
bool SkinWindow::declaresHiddenBrowser(const QString& name) const {
    const skin::Elem* e = declaredBrowser(snap_.file.get(), name);
    return e && !e->boolv("is-visible", true);
}

QWidget* SkinWindow::controlWidget(const QString& windowDotName) const {
    auto it = widgets_.find(windowDotName.toStdString());
    return it == widgets_.end() ? nullptr : it->second;
}

QWidget* SkinWindow::splitterHandle(const QString& windowDotName) const {
    auto it = handles_.find(windowDotName.toStdString());
    return it == handles_.end() ? nullptr : it->second;
}

QString SkinWindow::describe() const {
    QString s;
    for (const skin::Control& c : snap_.tree.controls) {
        auto it = widgets_.find(c.window + "." + c.name);
        s += QString("%1 %2.%3 (%4,%5 %6x%7)%8\n").arg(QString::fromStdString(c.type), QString::fromStdString(c.window),
              QString::fromStdString(c.name)).arg(c.rect.x0).arg(c.rect.y0).arg(c.rect.dx()).arg(c.rect.dy())
              .arg(it == widgets_.end() ? " [no widget]" : "");
    }
    return s;
}

}  // namespace fugue::shell
