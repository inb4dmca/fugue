// shell/ unit tests (offscreen QPA): skin -> widget geometry and the focus table.
#include <QApplication>
#include <QLineEdit>
#include <QFile>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>
#include "tests/testmain.h"
#include "shell/browserpane.h"
#include "shell/focusfilter.h"
#include "shell/skinwindow.h"

using namespace fugue::shell;

static const char* kSkin = R"(macro "m"
	elem "q"
		name = "Q"
		command = "qmacro"
	elem "ck"
		name = "CTRL+K"
		command = "ctrlk"
window "main"
	elem "main"
		type = MAIN
		size = 560x560
		is-default = true
		macro = "m"
		title = "t"
	elem "map"
		type = MAP
		pos = 0,0
		size = 480x480
		anchor1 = 0,0
		anchor2 = 0,0
		icon-size = 32
	elem "input"
		type = INPUT
		pos = 0,500
		size = 560x24
		anchor1 = 0,100
		anchor2 = 100,100
	elem "browser"
		type = BROWSER
		pos = 0,524
		size = 560x36
		anchor1 = 0,100
		anchor2 = 100,100
)";

class ShellTest : public QObject {
    Q_OBJECT
    SkinWindow* win = nullptr;
    FocusFilter* filt = nullptr;
private slots:
    void initTestCase() {
        win = new SkinWindow;
        QVERIFY2(win->loadSkin(kSkin).isEmpty(), "skin parses");
        win->resize(win->designSize());
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        filt = new FocusFilter;
        auto t = win->macros();
        filt->setMacros(t.sets.at(t.active));
        qApp->installEventFilter(filt);
    }
    void cleanupTestCase() {
        qApp->removeEventFilter(filt);
        delete filt;
        delete win;
    }

    void paneClipsButtonWithoutResizingIt() {
        QFile file(QFINDTESTDATA("../../tests/data/splitter-world/skin.dmf"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        SkinWindow window;
        QVERIFY(window.loadSkin(file.readAll().toStdString()).isEmpty());
        window.resize(window.designSize());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* button = window.findChild<QPushButton*>("row.menu");
        QVERIFY(button);
        QCOMPARE(button->height(), 6);  // 9px pane, y=5, 27px design button in a 30px design pane
        QCOMPARE(button->mask().boundingRect(), QRect(0, 0, button->width(), 4));
        // The clipped tail must not receive input in the adjacent pane.
        const QPoint tail = button->mapTo(&window, QPoint(1, 5));
        QVERIFY(window.childAt(tail) != button);
        QVERIFY(window.childAt(button->mapTo(&window, QPoint(1, 1))) == button);
        window.live()->winset("nested.split", "splitter=0");
        window.syncFromLive();
        QVERIFY(!button->isVisible());
        window.live()->winset("nested.split", "splitter=50");
        window.syncFromLive();
        QVERIFY(button->isVisible());
        QCOMPARE(button->height(), 216);
        QCOMPARE(button->mask().boundingRect().height(), 214);
        // A smaller design button fully inside the pane removes the previous mask.
        window.resize(640, 880 + window.statusBarHeight());
        window.live()->winset("row.menu", "size=88x1");
        window.syncFromLive();
        QVERIFY(button->mask().isEmpty());
        window.close();
        win->activateWindow();
        QVERIFY(QTest::qWaitFor([&] { return win->isActiveWindow(); }));
    }
    void singleChildBrowserFillsPane() {
        QFile file(QFINDTESTDATA("../../tests/data/child-sizing-world/skin.dmf"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        SkinWindow window;
        QVERIFY(window.loadSkin(file.readAll().toStdString()).isEmpty());
        window.resize(window.designSize());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* browser = window.findChild<BrowserPane*>("chat.browser");
        QVERIFY(browser);
        QCOMPARE(browser->geometry(), QRect(0, 160, 300, 150));
        QVERIFY(!window.splitterHandle("main.nested"));
        window.live()->winset("main.nested", "size=420x210");
        window.syncFromLive();
        QCOMPARE(browser->geometry(), QRect(0, 160, 420, 210));
        window.live()->winset("main.nested", "right=extra_right");
        window.syncFromLive();
        // Default axis is horizontal; attaching a second pane brings the divider back.
        QCOMPARE(browser->geometry(), QRect(0, 160, 420, 103));
        QVERIFY(window.splitterHandle("main.nested"));
        window.live()->winset("main.nested", "right=\"\"");
        window.syncFromLive();
        QCOMPARE(browser->geometry(), QRect(0, 160, 420, 210));
        QVERIFY(window.splitterHandle("main.nested")->isHidden());
        window.close();
        win->activateWindow();
        QVERIFY(QTest::qWaitFor([&] { return win->isActiveWindow(); }));
    }
    void designSizeAndMapRect() {
        // the MAIN window states no `statusbar`, so it gets DreamSeeker's default strip below the client area
        QCOMPARE(win->statusBarHeight(), 18);
        QCOMPARE(win->designSize(), QSize(560, 560 + 18));
        QCOMPARE(win->map()->geometry(), QRect(0, 0, 480, 480));
    }
    void relayoutOnResize() {
        win->resize(800, 700);
        QCOMPARE(win->map()->geometry().topLeft(), QPoint(0, 0));  // anchors none: designed extent kept
        QCOMPARE(win->map()->size(), QSize(480, 480));
        auto* in = win->findChild<QLineEdit*>();
        QVERIFY(in);
        QCOMPARE(in->width(), 800);                 // anchored 0%..100% horizontally
        QCOMPARE(in->y(), 500 + (700 - 18 - 560));  // anchored to the client area's bottom edge (above the status bar)
        win->resize(560, 560);
        QCOMPARE(in->width(), 560);
    }
    void focusTable() {
        QSignalSpy spy(filt, &FocusFilter::macroFired);
        auto* in = win->findChild<QLineEdit*>();
        auto* browser = win->findChild<BrowserPane*>();
        QVERIFY(in && browser);
        // map focused: a plain bound key fires a macro
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        QTest::keyClick(win->map(), Qt::Key_Q);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toString(), QString("qmacro"));
        // input focused: plain keys type (no macro), Ctrl-combos fire
        in->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == in; }));
        QTest::keyClick(in, Qt::Key_Q);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(in->text(), QString("q"));
        QTest::keyClick(in, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(1).toString(), QString("ctrlk"));
        QCOMPARE(in->text(), QString("q"));  // the macro swallowed it
        // browser focused: the page owns its keys
        browser->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return FocusFilter::zoneOf(qApp->focusWidget()) == Zone::Browser; }));
        QTest::keyClick(qApp->focusWidget(), Qt::Key_Q);
        QTest::keyClick(qApp->focusWidget(), Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(spy.count(), 2);
    }
    void macroSpecNames() {
        QKeyEvent e(QEvent::KeyPress, Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(QString::fromStdString(macroSpecFor(&e)), QString("CTRL+SHIFT+WEST"));
        QKeyEvent m(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
        QCOMPARE(QString::fromStdString(macroSpecFor(&m)), QString(""));
        QKeyEvent c(QEvent::KeyPress, Qt::Key_Clear, Qt::KeypadModifier);
        QCOMPARE(QString::fromStdString(macroSpecFor(&c)), QString("CENTER"));
    }
};

int FUGUE_TEST_ENTRY(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    ShellTest t;
    return QTest::qExec(&t, argc, argv);
}
#include "shell_test.moc"
