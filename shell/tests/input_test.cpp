// Shell input wiring (offscreen QPA): the map widget turns Qt mouse events into MapPointer values hit-tested against the scene the frame was drawn from;
// unbound arrow keys become builtinMove; routeCommand classifies a macro or typed command. Mouse frame sequences are in
// tests/mousechain_test.cpp.
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QtTest>
#include "shell/browserpane.h"
#include "shell/focusfilter.h"
#include "shell/inputrouter.h"
#include "shell/screenshot.h"
#include <QTemporaryDir>
#include "shell/macrodialog.h"
#include <QCheckBox>
#include <QMenuBar>
#include <QToolButton>
#include <QTreeWidget>
#include "shell/mapwidget.h"
#include "shell/skinwindow.h"
#include <QLabel>

using namespace fugue::shell;
using namespace fugue::render;

static const char* kSkin = R"(macro "m"
	elem "q"
		name = "Q"
		command = "qmacro"
window "main"
	elem "main"
		type = MAIN
		size = 560x560
		is-default = true
		macro = "m"
	elem "map"
		type = MAP
		pos = 0,0
		size = 480x480
		anchor1 = 0,0
		anchor2 = 0,0
		icon-size = 32
		right-click = true
	elem "input"
		type = INPUT
		pos = 0,500
		size = 560x24
		anchor1 = 0,100
		anchor2 = 100,100
)";

static HitPtr oneAtomScene() {
    auto s = std::make_shared<HitScene>();
    s->cols = s->rows = 15; s->originX = s->originY = 1;
    s->atoms.push_back({"thing", 1, kAtomObj, 9, 3, 3, false});
    auto sp = std::make_shared<Sprite>();
    sp->w = sp->h = 32;
    sp->px.assign(32 * 32 * 4, 255);
    s->world.push_back({sp, 2 * 32, (14 - 2) * 32, 0});  // cell (3,3)
    return s;
}

class InputTest : public QObject {
    Q_OBJECT
    SkinWindow* win = nullptr;
    FocusFilter* filt = nullptr;
private slots:
    void initTestCase() {
        qRegisterMetaType<MapPointer>();
        win = new SkinWindow;
        QVERIFY2(win->loadSkin(kSkin).isEmpty(), "skin parses");
        win->resize(win->designSize());
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        filt = new FocusFilter;
        auto t = win->macros();
        filt->setMacros(t.sets.at(t.active));
        qApp->installEventFilter(filt);
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, oneAtomScene());
    }
    void cleanupTestCase() { qApp->removeEventFilter(filt); delete filt; delete win; }

    void skinRightClickParamReachesTheWidget() { QVERIFY(win->map()->rightClickToWire()); }

    void pressMoveReleaseBecomePointerValues() {
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        // cell (3,3): image x 64..95, y from the top 12*32.. => (80, 400)
        QTest::mousePress(win->map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QTest::mouseMove(win->map(), QPoint(120, 400));
        QTest::mouseRelease(win->map(), Qt::LeftButton, Qt::ShiftModifier, QPoint(120, 400));
        QCOMPARE(spy.count(), 3);
        auto p0 = spy.at(0).at(0).value<MapPointer>();
        QCOMPARE(int(p0.kind), int(MapPointer::Press));
        QCOMPARE(p0.ev.imgX, 80); QCOMPARE(p0.ev.imgY, 400); QCOMPARE(p0.ev.button, 1);
        QVERIFY(p0.scene);
        Naming n;
        QVERIFY(nameMouse(*p0.scene, p0.ev.imgX, p0.ev.imgY, n));  // the scene travels with the event
        QCOMPARE(n.overRef, 9u);
        QCOMPARE(int(spy.at(1).at(0).value<MapPointer>().kind), int(MapPointer::Move));
        auto p2 = spy.at(2).at(0).value<MapPointer>();
        QCOMPARE(int(p2.kind), int(MapPointer::Release));
        QVERIFY(p2.ev.mods.shift);
    }
    void rightClickNotDeclaredIsLocal() {
        win->map()->setRightClickToWire(false);
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        QSignalSpy menu(win->map(), &MapWidget::localMenuRequested);
        QTest::mouseClick(win->map(), Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.at(0).at(1).toString(), QString("thing"));
        win->map()->setRightClickToWire(true);
        QTest::mouseClick(win->map(), Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(spy.count(), 2);  // press + release, on the wire path
        QCOMPARE(spy.at(0).at(0).value<MapPointer>().ev.button, 3);
    }
    // DreamSeeker's status bar names the atom under the pointer (DM Reference, `statusbar`), with no button held.
    // See MapWidget::updateHover for what it names and the one way it differs from stock.
    void statusBarNamesTheAtomUnderThePointer() {
        QLabel* bar = win->statusBar();
        QVERIFY(bar && !bar->isHidden());
        auto s = std::make_shared<HitScene>(*oneAtomScene());
        auto sp = s->world[0].sprite;
        s->atoms.push_back({"ghost", 0, kAtomObj, 10, 5, 5, false});  // mouse_opacity 0
        s->world.push_back({sp, 4 * 32, (14 - 4) * 32, 1});  // cell (5,5)
        s->atoms.push_back({"", 1, kAtomObj, 11, 7, 7, false});  // name = ""
        s->world.push_back({sp, 6 * 32, (14 - 6) * 32, 2});  // cell (7,7)
        s->atoms.push_back({"space", 1, kAtomTurf, 0, 9, 9, false});  // vg's space marker, as the scene hands it over
        s->world.push_back({sp, 8 * 32, (14 - 8) * 32, 3});  // cell (9,9)
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, s);
        QSignalSpy wire(win->map(), &MapWidget::pointer);
        QTest::mouseMove(win->map(), QPoint(80, 400));  // over "thing"
        QCOMPARE(bar->text(), QString("thing"));
        QTest::mouseMove(win->map(), QPoint(10, 10));  // over no sprite at all
        QCOMPARE(bar->text(), QString());
        QTest::mouseMove(win->map(), QPoint(4 * 32 + 5, 10 * 32 + 5));  // mouse_opacity 0: nothing under it
        QCOMPARE(bar->text(), QString());
        QTest::mouseMove(win->map(), QPoint(6 * 32 + 5, 8 * 32 + 5));  // nameless atom: blank, not a label
        QCOMPARE(bar->text(), QString());
        QTest::mouseMove(win->map(), QPoint(8 * 32 + 5, 6 * 32 + 5));  // the marker is dropped upstream
        QCOMPARE(bar->text(), QString("space"));
        QCOMPARE(wire.count(), 0);  // hovering sends nothing to the server
        // a new frame re-resolves under a still pointer: the atom moved away
        win->setMapFrame(f, oneAtomScene());
        QCOMPARE(bar->text(), QString());
        QTest::mouseMove(win->map(), QPoint(80, 400));
        QCOMPARE(bar->text(), QString("thing"));
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(win->map(), &leave);  // off the map: blank
        QCOMPARE(bar->text(), QString());
    }
    void letterboxAndMissingSceneSendNothing() {
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        win->map()->setIconSize(32);
        QImage small(160, 160, QImage::Format_RGBX8888);
        small.fill(Qt::black);
        win->setMapFrame(small, oneAtomScene());
        QRect ir = win->map()->imageRect();
        QVERIFY(ir.width() < win->map()->width());
        QTest::mousePress(win->map(), Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));  // in the letterbox
        QTest::mouseRelease(win->map(), Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
        QCOMPARE(spy.count(), 0);
        // inside the image the pointer maps through the zoom: image centre pixel
        QTest::mouseClick(win->map(), Qt::LeftButton, Qt::NoModifier, ir.center());
        QCOMPARE(spy.count(), 2);
        auto p = spy.at(0).at(0).value<MapPointer>();
        QVERIFY(std::abs(p.ev.imgX - 80) <= 1 && std::abs(p.ev.imgY - 80) <= 1);
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, oneAtomScene());
    }
    void unboundArrowsAreBuiltinMoves() {
        QSignalSpy mv(filt, &FocusFilter::builtinMove);
        QSignalSpy mac(filt, &FocusFilter::macroFired);
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        QTest::keyPress(win->map(), Qt::Key_Up);
        QTest::keyRelease(win->map(), Qt::Key_Up);
        QCOMPARE(mv.count(), 2);
        QCOMPARE(mv.at(0).at(0).toInt(), 1); QVERIFY(mv.at(0).at(1).toBool());
        QCOMPARE(mv.at(1).at(0).toInt(), 1); QVERIFY(!mv.at(1).at(1).toBool());
        QTest::keyClick(win->map(), Qt::Key_Left);
        QCOMPARE(mv.at(2).at(0).toInt(), 8);
        QCOMPARE(mac.count(), 0);
        // in the command bar the arrows are the text cursor's
        auto* in = win->findChild<QLineEdit*>();
        in->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == in; }));
        int before = mv.count();
        QTest::keyClick(in, Qt::Key_Left);
        QCOMPARE(mv.count(), before);
    }
    // The right-click verb menu is a QMenu popup, so a key released while it is open is delivered to the menu. A held key's release must
    // reach the server wherever it lands: the popup, another zone, or nowhere at all (application deactivated).
    void heldKeyReleasedInsideAPopupStillReleases() {
        QSignalSpy mv(filt, &FocusFilter::builtinMove);
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        QTest::keyPress(win->map(), Qt::Key_Left);
        QCOMPARE(mv.count(), 1);
        QVERIFY(mv.at(0).at(1).toBool());
        QMenu menu(win);
        menu.addAction("poke");
        menu.popup(win->mapToGlobal(QPoint(80, 80)));
        QVERIFY(QTest::qWaitFor([&] { return QApplication::activePopupWidget() == &menu; }));
        QVERIFY(qApp->focusWidget() != &menu);  // the popup holds the keyboard without becoming the focus widget
        QTest::keyRelease(&menu, Qt::Key_Left);
        QCOMPARE(mv.count(), 2);
        QCOMPARE(mv.at(1).at(0).toInt(), 8);
        QVERIFY(!mv.at(1).at(1).toBool());
        menu.close();
        QVERIFY(QTest::qWaitFor([&] { return !QApplication::activePopupWidget(); }));
        // released once: a second copy (window and widget both receive it) finds nothing held
        QTest::keyRelease(win->map(), Qt::Key_Left);
        QCOMPARE(mv.count(), 3);  // an unmatched release still routes on the focus widget
        // a release that lands in the command bar is the walk key's too (plain keys there never route)
        QTest::keyPress(win->map(), Qt::Key_Up);
        auto* in = win->findChild<QLineEdit*>();
        in->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == in; }));
        QTest::keyRelease(in, Qt::Key_Up);
        QCOMPARE(mv.count(), 5);
        QCOMPARE(mv.at(4).at(0).toInt(), 1);
        QVERIFY(!mv.at(4).at(1).toBool());
    }
    void heldKeyIsReleasedWhenTheApplicationDeactivates() {
        QSignalSpy mv(filt, &FocusFilter::builtinMove);
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        QTest::keyPress(win->map(), Qt::Key_Right);
        QCOMPARE(mv.count(), 1);
        QEvent deact(QEvent::ApplicationDeactivate);
        QCoreApplication::sendEvent(qApp, &deact);
        QCOMPARE(mv.count(), 2);
        QCOMPARE(mv.at(1).at(0).toInt(), 4);
        QVERIFY(!mv.at(1).at(1).toBool());
        QCoreApplication::sendEvent(qApp, &deact);  // nothing held any more
        QCOMPARE(mv.count(), 2);
    }

    void routing_data() {
        QTest::addColumn<QString>("cmd"); QTest::addColumn<bool>("up"); QTest::addColumn<int>("kind");
        QTest::addColumn<int>("dir"); QTest::addColumn<bool>("down"); QTest::addColumn<QString>("text");
        QTest::newRow("verb") << "say \"hi\"" << false << int(Route::Command) << 0 << true << "say \"hi\"";
        QTest::newRow("movekey") << "MoveKey 8 0" << true << int(Route::Command) << 0 << true << "MoveKey 8 0";
        QTest::newRow("empty masks") << "  " << false << int(Route::Swallow) << 0 << true << "";
        QTest::newRow(".north") << ".north" << false << int(Route::Move) << 1 << true << "";
        QTest::newRow(".southwest up") << ".SouthWest" << true << int(Route::Move) << 10 << false << "";
        QTest::newRow("macro swap") << ".winset \"mainwindow.macro=hotkeymode mapwindow.map.focus=true\"" << false
                                    << int(Route::SetMacroSet) << 0 << true << "hotkeymode";
        QTest::newRow("winset no macro") << ".winset \"rpanewindow.left=;\"" << false << int(Route::Winset) << 0 << true << "rpanewindow.left=;";
        QTest::newRow("winset unquoted") << ".winset rpane.lbl.text=hi" << false << int(Route::Winset) << 0 << true << "rpane.lbl.text=hi";
        QTest::newRow("quit") << ".quit" << false << int(Route::Quit) << 0 << true << "";
        QTest::newRow("unknown dot cmd is NOT a verb") << ".profile" << false << int(Route::Unsupported) << 0 << true << ".profile";
        QTest::newRow("screenshot dialog") << ".screenshot" << false << int(Route::Screenshot) << 0 << true << "";
        QTest::newRow("screenshot auto") << ".screenshot auto" << false << int(Route::Screenshot) << 0 << true << "auto";
        QTest::newRow("screenshot AUTO") << "  .Screenshot  AUTO " << false << int(Route::Screenshot) << 0 << true << "auto";
        QTest::newRow(".macros opens the macro editor") << ".macros" << false << int(Route::Macros) << 0 << true << "";
        QTest::newRow(".options opens Options and Messages") << ".options" << false << int(Route::Options) << 0 << true << "";
        QTest::newRow("any-key") << "say [[*]]" << false << int(Route::Command) << 0 << true << "say K";
    }
    void winsetBodyIsAlwaysHandedOn() {
        // A hotkey toggle: the macro swap AND a checkbox AND the map focus travel together; the whole body must reach the live skin.
        Route r = routeCommand(".winset \"mainwindow.macro=hotkeymode hotkey_toggle.is-checked=true mapwindow.map.focus=true\"", "", false);
        QCOMPARE(int(r.kind), int(Route::SetMacroSet));
        QCOMPARE(QString::fromStdString(r.winsetBody), QString("mainwindow.macro=hotkeymode hotkey_toggle.is-checked=true mapwindow.map.focus=true"));
        QVERIFY(routeCommand("say hi", "", false).winsetBody.empty());
    }
    // A popup's on-close is the world's command: a world may define a verb whose name starts with a dot (`.windowclose <ref>`), and it must be sent.
    // From a key or the bar, the same text is not routed to the world.
    void worldCommandSendsTheWorldsOwnDotVerb() {
        Route r = routeWorldCommand(".windowclose 0x2001a2b");
        QCOMPARE(int(r.kind), int(Route::Command));
        QCOMPARE(QString::fromStdString(r.text), QString(".windowclose 0x2001a2b"));
        QCOMPARE(int(routeCommand(".windowclose 0x2001a2b", "", false).kind), int(Route::Unsupported));  // unchanged for keys/bar
        r = routeWorldCommand("nanoclose [0x21004c7]");
        QCOMPARE(int(r.kind), int(Route::Command));
        QCOMPARE(QString::fromStdString(r.text), QString("nanoclose [0x21004c7]"));
        QCOMPARE(int(routeWorldCommand(".quit").kind), int(Route::Quit));  // builtins still local
        QCOMPARE(int(routeWorldCommand(".winset \"a.is-visible=false\"").kind), int(Route::Winset));
    }
    // .screenshot naming + save, as stock DreamSeeker 516 writes them (shell/screenshot.h). Writes only into a temp dir.
    void screenshotNamingMatchesDreamSeeker() {
        QDateTime t(QDate(2000, 1, 2), QTime(2, 46, 29));
        QCOMPARE(screenshotBaseName("shotworld", t), QString("shotworld 2000-01-02 024629"));
        QCOMPARE(screenshotBaseName("/vg/ Sta:tion* 13?", t), QString("vg Station 13 2000-01-02 024629"));  // stock drops the bad characters, not replaces them
        QCOMPARE(screenshotBaseName("", t), QString("screenshot 2000-01-02 024629"));
        QCOMPARE(withPngSuffix("/x/zq"), QString("/x/zq.png"));
        QCOMPARE(withPngSuffix("/x/zq.png"), QString("/x/zq.png"));
        QCOMPARE(withPngSuffix(""), QString(""));  // a cancelled dialog stays cancelled
    }
    void screenshotSavesTheNativeFrameAndNeverOverwrites() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QImage frame(352, 352, QImage::Format_RGBX8888);  // view 5 at 32 px
        frame.fill(QColor(200, 40, 40));
        frame.setPixelColor(7, 9, QColor(40, 40, 200));
        QDateTime t(QDate(2000, 1, 2), QTime(2, 43, 14));
        QString dir = tmp.filePath("screenshots");  // does not exist yet: the save creates it
        QString p1 = screenshotAutoPath(dir, "shotworld", t);
        QCOMPARE(p1, dir + "/shotworld 2000-01-02 024314.png");
        QString err;
        QVERIFY2(saveScreenshot(frame, p1, &err), qPrintable(err));
        QImage back(p1);
        QCOMPARE(back.size(), QSize(352, 352));
        QVERIFY(!back.hasAlphaChannel());
        QCOMPARE(back.pixelColor(7, 9), QColor(40, 40, 200));
        QCOMPARE(back.pixelColor(0, 0), QColor(200, 40, 40));
        QString p2 = screenshotAutoPath(dir, "shotworld", t);  // F2+REP inside one second
        QCOMPARE(p2, dir + "/shotworld 2000-01-02 024314 2.png");
        QVERIFY(!saveScreenshot(QImage(), p2, &err));
        QVERIFY(!QFileInfo::exists(p2));
    }
    // Client > Macros (skin/usermacros.h, as stock DreamSeeker 516 behaves): the player's lists fire only for a key the skin's set leaves unbound,
    // this game's list before the global one; typing in the macro editor fires nothing.
    void userMacrosSitBeneathTheSkinSet() {
        QSignalSpy mac(filt, &FocusFilter::macroFired);
        fugue::skin::MacroSet game, global;
        fugue::skin::editUserMacro(global, "", "Q", "user q");  // the skin binds Q: in use
        fugue::skin::editUserMacro(global, "", "W", "global w");
        fugue::skin::editUserMacro(game, "", "W", "game w");
        fugue::skin::editUserMacro(global, "", "E", "global e");
        filt->setUserMacros(game, global);
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        QTest::keyClick(win->map(), Qt::Key_Q);
        QTest::keyClick(win->map(), Qt::Key_W);
        QTest::keyClick(win->map(), Qt::Key_E);
        QTest::keyClick(win->map(), Qt::Key_R);  // nobody binds R
        QCOMPARE(mac.count(), 3);
        QCOMPARE(mac.at(0).at(1).toString(), QString("qmacro"));
        QCOMPARE(mac.at(1).at(1).toString(), QString("game w"));
        QCOMPARE(mac.at(2).at(1).toString(), QString("global e"));
        // the editor owns its keys: an F-key typed into its Key field (a QLineEdit = the input zone) fires no macro
        fugue::skin::editUserMacro(global, "", "F5", "global f5");
        filt->setUserMacros(game, global);
        MacroEditDialog ed("New Macro", nullptr);
        ed.show();
        QVERIFY(QTest::qWaitForWindowExposed(&ed));
        ed.activateWindow();
        ed.keyEdit()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == ed.keyEdit(); }));
        QCOMPARE(int(FocusFilter::zoneOf(ed.keyEdit())), int(Zone::Other));
        QTest::keyClick(ed.keyEdit(), Qt::Key_F5);
        QCOMPARE(mac.count(), 3);
        filt->setUserMacros({}, {});
    }
    void macroEditorSplitsAndJoinsTheSpec() {
        fugue::skin::MacroSet s;
        fugue::skin::editUserMacro(s, "", "CTRL+SHIFT+F8+REP", "say \"hi\"", "myid");
        MacroEditDialog ed("Edit Macro", &s.entries[0]);
        QCOMPARE(ed.keyEdit()->text(), QString("F8"));
        QVERIFY(ed.box("ctrl")->isChecked() && ed.box("shift")->isChecked() && !ed.box("alt")->isChecked());
        QVERIFY(ed.box("rep")->isChecked() && !ed.box("up")->isChecked());
        QCOMPARE(ed.command(), QString("say \"hi\""));
        QCOMPARE(ed.id(), QString("myid"));
        QCOMPARE(ed.spec(), QString("CTRL+SHIFT+F8+REP"));
        ed.box("rep")->setChecked(false); ed.box("up")->setChecked(true); ed.box("shift")->setChecked(false);
        ed.keyEdit()->setText("west");
        QCOMPARE(ed.spec(), QString("CTRL+WEST+UP"));
        ed.keyEdit()->clear();
        QCOMPARE(ed.spec(), QString());  // blank key = delete
    }
    void macroDialogEditsTheChosenList() {
        fugue::skin::MacroSet global, game;
        fugue::skin::editUserMacro(global, "", "F6", "hello");
        fugue::skin::editUserMacro(global, "", "F1", "zap user");
        fugue::skin::editUserMacro(game, "", "F7", "hello");
        MacroDialog d(global, game, true);
        QVERIFY(d.scopeBox()->isEnabled() && d.scopeBox()->isChecked());  // default for a game with its own list
        QCOMPARE(d.list()->topLevelItemCount(), 1);
        QCOMPARE(d.list()->topLevelItem(0)->text(0), QString("F7"));
        d.scopeBox()->setChecked(false);  // the global list, sorted by key
        QCOMPARE(d.list()->topLevelItemCount(), 2);
        QCOMPARE(d.list()->topLevelItem(0)->text(0), QString("F1"));
        QCOMPARE(d.list()->topLevelItem(1)->text(1), QString("hello"));
        QVERIFY(d.commit("", "F9", "zap nine"));
        QVERIFY(d.commit("F1", "", "zap user"));  // blank key deletes
        QCOMPARE(int(d.globalSet().entries.size()), 2);
        QVERIFY(!d.globalSet().lookup([] { fugue::skin::MacroKey k; bool r; fugue::skin::parseMacroName("F1", k, r); return k; }(), false));
        QCOMPARE(int(d.gameSet().entries.size()), 1);  // the other list is untouched
        // New Macro... through the real editor: fill it, OK, and the row is there
        MacroEditDialog* ed = d.openEditor(-1);
        QCOMPARE(ed->windowTitle(), QString("New Macro"));
        ed->keyEdit()->setText("F10");
        ed->commandEdit()->setText("hello");
        ed->accept();
        QCOMPARE(d.list()->topLevelItemCount(), 3);
        // double-click an existing row: Edit Macro, prefilled
        MacroEditDialog* ed2 = d.openEditor(0);
        QCOMPARE(ed2->windowTitle(), QString("Edit Macro"));
        QCOMPARE(ed2->keyEdit()->text(), QString("F10"));
        ed2->commandEdit()->clear();  // blank command deletes
        ed2->accept();
        QCOMPARE(d.list()->topLevelItemCount(), 2);
        MacroDialog noGame(global, game, false);  // no per-game list: greyed, global shown
        QVERIFY(!noGame.scopeBox()->isEnabled());
        QCOMPARE(noGame.list()->topLevelItemCount(), 2);
    }
    // Stock DreamSeeker 516 behaviour, driven through the real path: user F6 -> Rest and R -> "zap userR", beside a skin with sets "macro" (no R)
    // and "hotkeymode" (R -> zap hotkeyR). F6 Rest, R userR | switch to hotkeymode | F6 Rest, R hotkeyR | back | F6 Rest, R userR:
    // the user's lists sit beneath whichever set is active and survive the switch. This is a winset of the main window's macro, the
    // window's macroChanged, and the app's re-pick of the active set (app/main.cpp applyMacros).
    void userMacrosSurviveAMacroSetSwitch() {
        static const char* kTwoSets = R"(macro "macro"
	elem
		name = "F1"
		command = "hello"
macro "hotkeymode"
	elem
		name = "R"
		command = "zap hotkeyR"
window "mainwindow"
	elem "mainwindow"
		type = MAIN
		size = 320x320
		is-default = true
		macro = "macro"
	elem "map"
		type = MAP
		pos = 0,0
		size = 320x320
		anchor1 = 0,0
		anchor2 = 100,100
)";
        SkinWindow w;
        QVERIFY(w.loadSkin(kTwoSets).isEmpty());
        w.resize(w.designSize());
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        FocusFilter f;
        auto pick = [&] { auto t = w.macros(); f.setMacros(t.sets.at(w.activeMacro().toStdString())); };
        pick();
        QObject::connect(&w, &SkinWindow::macroChanged, &w, pick);
        fugue::skin::MacroSet game, global;
        fugue::skin::editUserMacro(game, "", "F6", "Rest");
        fugue::skin::editUserMacro(game, "", "R", "zap userR");
        f.setUserMacros(game, global);
        qApp->removeEventFilter(filt);  // only this window's filter answers
        qApp->installEventFilter(&f);
        QSignalSpy mac(&f, &FocusFilter::macroFired);
        w.activateWindow();
        w.map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == w.map(); }));
        auto press = [&] { QTest::keyClick(w.map(), Qt::Key_F6); QTest::keyClick(w.map(), Qt::Key_R); };
        press();
        w.live()->winset("mainwindow", "macro=hotkeymode");  // vg/tg's hotkey-mode switch, as the world sends it
        QVERIFY(QTest::qWaitFor([&] { return w.activeMacro() == "hotkeymode"; }));
        press();
        w.live()->winset("mainwindow", "macro=macro");
        QVERIFY(QTest::qWaitFor([&] { return w.activeMacro() == "macro"; }));
        press();
        qApp->removeEventFilter(&f);
        qApp->installEventFilter(filt);
        QStringList got;
        for (const auto& a : mac) got << a.at(1).toString();
        QCOMPARE(got, QStringList({"Rest", "zap userR", "Rest", "zap hotkeyR", "Rest", "zap userR"}));
    }
    // The top-left system menu (shell/macrodialog.h): the window icon in the menu bar's top-left corner, opening
    // "Options & Messages" and Client > Screenshot... / Macros... / Quit, each routed as the client command it names.
    void systemMenuSitsTopLeftAndLeadsToMacros() {
        static const char* kMenuSkin = R"(menu "menu"
	elem
		name = "&File"
		command = ""
	elem
		name = "Say hello"
		command = "hello"
		category = "&File"
window "mainwindow"
	elem "mainwindow"
		type = MAIN
		size = 320x240
		is-default = true
		menu = "menu"
)";
        SkinWindow w;
        QVERIFY(w.loadSkin(kMenuSkin).isEmpty());
        w.resize(w.designSize());
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QToolButton* b = w.systemMenuButton();
        // the button costs the layout nothing: the bar is as tall as a bar with the same menu and no corner widget
        {
            QMenuBar ref;
            ref.addMenu("&File")->addAction("Say hello");
            QCOMPARE(w.menuBar()->sizeHint().height(), ref.sizeHint().height());
        }
        QVERIFY(b);
        QCOMPARE(w.menuBar()->cornerWidget(Qt::TopLeftCorner), static_cast<QWidget*>(b));
        QVERIFY(b->isVisible());
        QPoint at = b->mapTo(&w, QPoint(0, 0));
        QVERIFY2(at.x() < 8 && at.y() < 8, "top-left of the window");
        QAction* file = w.menuBar()->actions().at(0);
        QVERIFY(w.menuBar()->actionGeometry(file).left() >= b->geometry().right());  // left of the skin's own menus
        QVERIFY(!b->icon().isNull());
        QImage px(16, 16, QImage::Format_ARGB32);
        px.fill(Qt::red);
        w.setWindowIcon(QIcon(QPixmap::fromImage(px)));  // a world's icon (setSkinIcon)
        QCOMPARE(b->icon().pixmap(16).toImage().pixelColor(8, 8), QColor(Qt::red));
        QMenu* sys = b->menu();
        QVERIFY(sys);
        QStringList top;
        for (QAction* a : sys->actions()) top << a->text();
        QCOMPARE(top, QStringList({"Options && Messages", "&Client"}));
        QMenu* client = sys->actions().at(1)->menu();
        QVERIFY(client);
        QStringList items;
        for (QAction* a : client->actions()) items << (a->isSeparator() ? QString("-") : a->text());
        QCOMPARE(items, QStringList({"&Screenshot...", "-", "&Macros...", "-", "&Quit"}));
        QSignalSpy cmd(&w, &SkinWindow::menuCommand);
        sys->actions().at(0)->trigger();
        client->actions().at(2)->trigger();
        QCOMPARE(cmd.count(), 2);
        QCOMPARE(int(routeCommand(cmd.at(0).at(0).toString().toStdString(), "", false).kind), int(Route::Options));
        QCOMPARE(int(routeCommand(cmd.at(1).at(0).toString().toStdString(), "", false).kind), int(Route::Macros));
    }
    void optionsWindowLeadsToMacros() {
        OptionsWindow w;
        QSignalSpy cmd(&w, &OptionsWindow::command);
        QMenu* client = w.menuBar()->actions().at(0)->menu();
        QVERIFY(client);
        QAction* macros = nullptr;
        for (QAction* a : client->actions()) if (a->text() == "&Macros...") macros = a;
        QVERIFY(macros);
        macros->trigger();
        QCOMPARE(cmd.count(), 1);
        QCOMPARE(cmd.at(0).at(0).toString(), QString(".macros"));
        QCOMPARE(int(routeCommand(cmd.at(0).at(0).toString().toStdString(), "", false).kind), int(Route::Macros));
    }
    void routing() {
        QFETCH(QString, cmd); QFETCH(bool, up); QFETCH(int, kind); QFETCH(int, dir); QFETCH(bool, down); QFETCH(QString, text);
        Route r = routeCommand(cmd.toStdString(), "K", up);
        QCOMPARE(int(r.kind), kind);
        QCOMPARE(r.dir, dir);
        QCOMPARE(r.down, down);
        QCOMPARE(QString::fromStdString(r.text), text);
    }
};

int main(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    InputTest t;
    return QTest::qExec(&t, argc, argv);
}
#include "input_test.moc"
