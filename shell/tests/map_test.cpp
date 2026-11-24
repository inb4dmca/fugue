#include "tests/appearancejson.h"
#include <QtTest>
#include <QLabel>
#include <QAction>
#include <QWheelEvent>
#include <QMenu>
#include <QBuffer>
#include <QScreen>
#include <QPointer>
#include <QScopeGuard>
#include <QStyleFactory>
#include <QPainter>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include "shell/browsehost.h"
#include <bit>
#include <limits>
#include <map>
#include <memory>
#include "shell/entitymap.h"
#include "shell/tests/iconfixture.h"
#include "shell/tests/vismaterialfixture.h"
#include "shell/tests/attachmenttreefixture.h"
#include "shell/tests/opacityfixture.h"
#include "shell/tests/affinefixture.h"
#include "shell/tests/roottreefixture.h"
#include "shell/tests/planeeffectreference.h"
#include "shell/tests/localrelayfixture.h"
#include "shell/tests/planesourcefixture.h"
#include "shell/tests/masteropacityfixture.h"
#include "shell/tests/hudrelayfixture.h"
#include "shell/tests/mastertreefixture.h"
#include "shell/tests/stockinputfixture.h"
#include "shell/tests/masterplacementfixture.h"
#include "shell/tests/filtertypesfixture.h"
#include "shell/mapwidget.h"
#include "shell/compositionplan.h"
#include "shell/browserpane.h"
#include "shell/skinwindow.h"
#include "shell/inputrouter.h"
#include "shell/screenshot.h"
#include <QTemporaryDir>
using namespace fugue::shell;

static const char* skin = R"(menu "menu"
    elem
        name = "&File"
        command = ""
    elem
        name = "Hello"
        command = "hello"
        category = "&File"
window "main"
    elem "main"
        type = MAIN
        size = 640x480
        is-default = true
        menu = "menu"
    elem "map"
        type = MAP
        size = 480x480
        is-default = true
)";

#ifndef Q_MOC_RUN
#include "shell/tests/displayfixture.h"
#include "shell/tests/nativedesktopfixture.h"
#include "shell/tests/blacknessfixture.h"
static void wireDisplayCommands(SkinWindow& window) {
    QObject::connect(&window, &SkinWindow::menuCommand, &window, [&window](const QString& command) {
        const auto route = routeCommand(command.toStdString(), "", false);
        if (route.kind == Route::Winset) {
            window.live()->winset("", route.winsetBody.empty() ? route.text : route.winsetBody);
            window.syncFromLive();
        }
    });
}
static QAction* displayAction(SkinWindow& window, const QString& name) {
    for (auto* action : window.findChildren<QAction*>()) if (action->text() == name) return action;
    return nullptr;
}

// Pixel-only cases share the native window/compositor, never a MapWidget's
// frame, renderer caches, mailbox, metrics or input callbacks. Keep lifecycle,
// focus, desktop capture and resource-recreation cases on independent windows.
class PixelMapHosts {
    std::map<QRhiWidget::Api, std::unique_ptr<QWidget>> windows_;
public:
    QWidget& get(QRhiWidget::Api api) {
        auto& window = windows_[api];
        if (!window) {
            window = std::make_unique<QWidget>();
            window->setWindowFlag(Qt::FramelessWindowHint);
            window->setObjectName("nativePixelTestHost");
        }
        return *window;
    }
    void hide() { for (auto& [api, window] : windows_) window->hide(); }
};
class HostedPixelMap : public MapWidget {
    PixelMapHosts& hosts_;
public:
    explicit HostedPixelMap(PixelMapHosts& hosts) : hosts_(hosts) {}
    void show() {
        auto& host = hosts_.get(api()); // setApi() has already selected this case's backend
        if (!parentWidget()) setParent(&host);
        host.resize(size()); move(0,0);
        MapWidget::show();
        host.show(); host.raise();
    }
};
#endif

class MapTest : public QObject {
    Q_OBJECT
    PixelMapHosts pixelHosts_;
private slots:
    void liveOpenGLOverride() {
        const bool existed=qEnvironmentVariableIsSet("FUGUE_MAP_OPENGL");
        const auto original=qgetenv("FUGUE_MAP_OPENGL");
        const auto restore=qScopeGuard([&] {
            if(existed) qputenv("FUGUE_MAP_OPENGL",original);else qunsetenv("FUGUE_MAP_OPENGL");
        });
        qunsetenv("FUGUE_MAP_OPENGL");MapWidget ordinary;
        qputenv("FUGUE_MAP_OPENGL","1");MapWidget forced;
        QCOMPARE(forced.api(),QRhiWidget::Api::OpenGL);
        qputenv("FUGUE_MAP_OPENGL","0");MapWidget disabled;
        QCOMPARE(disabled.api(),ordinary.api());
        // Explicit per-widget selection remains available to the native tests.
        forced.setApi(ordinary.api());QCOMPARE(forced.api(),ordinary.api());
    }
    void screenshotNativeComposition_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void screenshotNativeComposition() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires native QRhi readback");
        QFETCH(bool, openGL);
        MapWidget map;
        if (openGL) map.setApi(QRhiWidget::Api::OpenGL);
        QString error;
        QVERIFY(map.screenshot(&error).isNull()); QVERIFY(!error.isEmpty());
        map.resize(300, 220); map.setViewSize({3, 2});
        auto metrics = std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox = std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        const QRect source(7, 9, 48, 32); // exclude padded terrain from the PNG
        QImage terrain(64, 56, QImage::Format_RGBA8888); terrain.fill(Qt::green);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                terrain.setPixelColor(source.x() + x, source.y() + y, QColor(x * 5, y * 7, 20));
        MapSprite object;
        object.image = QImage(6, 5, QImage::Format_RGBA8888); object.image.fill(Qt::red);
        object.position = source.topLeft() + QPoint(10, 7);
        auto group = std::make_shared<AppearanceGroup>(); group->tint[3] = .5f;
        object.appearanceGroup = group; object.groupOccurrence = 1;
        MapSprite hud; hud.kind = MapSprite::Screen; hud.plane = 10;
        hud.image = QImage(4, 3, QImage::Format_RGBA8888); hud.image.fill(Qt::blue);
        hud.position = source.topLeft() + QPoint(40, 26);
        const MapSprites sprites{object, hud};
        auto publish = [&](const QImage& image, QRect rect) {
            mailbox->publish(image, rect, 0, 0, {{0, 0, 3, 2}, {3, 2}, 16, 0, 1}, sprites);
        };
        publish(terrain, source);
        QSignalSpy submitted(&map, &QRhiWidget::frameSubmitted);
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        QTRY_VERIFY_WITH_TIMEOUT(!submitted.empty(), 5000);
        const QImage first = map.screenshot(&error);
        QVERIFY2(!first.isNull(), qPrintable(error)); QVERIFY(error.isEmpty());
        QCOMPARE(first.size(), source.size()); QCOMPARE(first.devicePixelRatio(), 1.0);
        QCOMPARE(first.pixelColor(0, 0), terrain.pixelColor(source.topLeft()));
        QCOMPARE(first.pixelColor(47, 31), terrain.pixelColor(source.bottomRight()));
        QCOMPARE(first.pixelColor(41, 27), QColor(Qt::blue));
        const auto blend = first.pixelColor(12, 9);
        QVERIFY(std::abs(blend.red() - 158) <= 1);
        QVERIFY(std::abs(blend.green() - 31) <= 1);
        QVERIFY(std::abs(blend.blue() - 10) <= 1);
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        const auto path = tmp.filePath("native.png");
        QVERIFY2(saveScreenshot(first, path, &error), qPrintable(error));
        QCOMPARE(QImage(path).convertToFormat(QImage::Format_RGB32), first.convertToFormat(QImage::Format_RGB32));
        // Publication without a draw must not replace what is being captured.
        QImage next = terrain; next.fill(Qt::cyan); publish(next, source);
        const auto count = submitted.size();
        const auto now = MapMetrics::nowNs(); metrics->reset(now);
        QCOMPARE(map.screenshot(), first); QCOMPARE(map.screenshot(), first);
        QCOMPARE(submitted.size(), count);
        const auto stats = metrics->snapshot(now + 1'000'000'000);
        QCOMPARE(stats.submittedPerSecond, 0.0); QCOMPARE(stats.uploadMBPerSecond, 0.0);
        // Repaints change presentation/overlay, not native screenshot pixels.
        for (auto mode : {MapPresentationSettings::Distort, MapPresentationSettings::Normal, MapPresentationSettings::Blur}) {
            MapPresentationSettings settings; settings.mode = mode; settings.zoom = 3;
            map.setPresentationSettings(settings); map.setMetricsVisible(true);
            map.resize(360, 280); QVERIFY(!map.grabFramebuffer().isNull());
            const auto shot = map.screenshot(&error); QVERIFY2(!shot.isNull(), qPrintable(error));
            QCOMPARE(shot.size(), source.size());
            QCOMPARE(shot.pixelColor(0, 0), QColor(Qt::cyan));
            QCOMPARE(shot.pixelColor(41, 27), QColor(Qt::blue));
        }
        // An invalid new composition must not return the old native texture.
        publish(next, {}); map.grabFramebuffer();
        QVERIFY(map.screenshot(&error).isNull()); QVERIFY(!error.isEmpty());
        publish(terrain, source); map.grabFramebuffer(); QCOMPARE(map.screenshot(), first);
        // Native render-target resizing must change readback extent and not
        // retain a result backed by the old texture's storage.
        const QRect smaller(8, 10, 32, 24);
        publish(terrain, smaller); map.grabFramebuffer();
        QCOMPARE(map.screenshot().size(), smaller.size());
        QCOMPARE(map.screenshot().pixelColor(0, 0), terrain.pixelColor(smaller.topLeft()));
        publish(terrain, source); map.grabFramebuffer(); QCOMPARE(map.screenshot(), first);
        map.setMailbox(std::make_shared<MapMailbox>(metrics));
        QVERIFY(map.screenshot().isNull()); // session replacement invalidates capture
    }
    void displaySettingsRoute() {
        // The checked menu item is not a MAP property. An omitted MAP mode
        // still resolves to Normal; exercise ingestion and command routing.
        SkinWindow fresh;
        std::string unset=displaySkin;
        for (const std::string line : {"        zoom = 2\n", "        zoom-mode = normal\n", "        letterbox = true\n"})
            unset.erase(unset.find(line),line.size());
        QVERIFY(fresh.loadSkin(unset).isEmpty()); wireDisplayCommands(fresh);
        auto* defaultMap=dynamic_cast<MapWidget*>(fresh.map()); QVERIFY(defaultMap);
        QCOMPARE(defaultMap->presentationSettings(),MapPresentationSettings{});
        displayAction(fresh,"&Blur")->trigger();
        QCOMPARE(defaultMap->presentationSettings().mode,MapPresentationSettings::Blur);
        displayAction(fresh,"&Normal")->trigger();
        QCOMPARE(defaultMap->presentationSettings(),MapPresentationSettings{});
        SkinWindow window;
        window.setMapViewSize(3,3);
        QVERIFY(window.loadSkin(displaySkin).isEmpty()); wireDisplayCommands(window);
        auto* map = dynamic_cast<MapWidget*>(window.map()); QVERIFY(map);
        QCOMPARE(map->presentationSettings().zoom, 2.0);
        window.live()->winset("mapwindow.secondary","is-visible=true"); window.syncFromLive();
        MapWidget* secondary=nullptr;
        for (auto* child : window.findChildren<QWidget*>())
            if (auto* other=dynamic_cast<MapWidget*>(child); other && other!=map) secondary=other;
        QVERIFY(secondary);
        QCOMPARE(secondary->presentationSettings().iconSize,16.0);
        for (const auto& name : {"&Distort", "&Normal", "&Blur"}) {
            auto* action = displayAction(window, name); QVERIFY(action); action->trigger();
            QCOMPARE(int(map->presentationSettings().mode), name == QString("&Blur") ? 2 : name == QString("&Normal") ? 1 : 0);
        }
        QCOMPARE(secondary->presentationSettings().mode,MapPresentationSettings::Distort);
        QCOMPARE(secondary->presentationSettings().iconSize,16.0);
        window.live()->winset("mapwindow.map", "icon-size=48"); window.syncFromLive();
        QCOMPARE(map->presentationSettings().zoom, 0.0); // overrides the skin's positive zoom
        QCOMPARE(map->presentationSettings().iconSize, 48.0);
        window.live()->winset("mapwindow.map", "zoom=1.5;letterbox=false"); window.syncFromLive();
        QCOMPARE(map->presentationSettings().iconSize, 0.0);
        QCOMPARE(map->presentationSettings().zoom, 1.5); QVERIFY(!map->presentationSettings().letterbox);
        window.live()->winset("mapwindow.map", "icon-size=0"); window.syncFromLive();
        QCOMPARE(map->presentationSettings().zoom, 0.0);
        QCOMPARE(map->presentationSettings().iconSize, 0.0);
    }
    void namedMapRoutingPixels_data() {
        QTest::addColumn<bool>("gl");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void namedMapRoutingPixels() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native separate MAP submission");
        QFETCH(bool,gl);
        const auto old=qgetenv("FUGUE_MAP_OPENGL");
        const bool existed=qEnvironmentVariableIsSet("FUGUE_MAP_OPENGL");
        const auto restore=qScopeGuard([&] { if(existed) qputenv("FUGUE_MAP_OPENGL",old);else qunsetenv("FUGUE_MAP_OPENGL"); });
        qputenv("FUGUE_MAP_OPENGL",gl ? "1" : "0");
        SkinWindow window; window.setMapViewSize(1,1);
        QVERIFY(window.loadSkin(R"(window "main"
    elem "main"
        type = MAIN
        size = 160x80
        is-default = true
        statusbar = false
    elem "map"
        type = MAP
        size = 80x80
        is-default = true
)").isEmpty());
        auto* main=dynamic_cast<MapWidget*>(window.map()); QVERIFY(main);
        QImage red(32,32,QImage::Format_RGBA8888); red.fill(Qt::red);
        window.mapMailbox()->publish(red,red.rect(),0,0,{{0,0,1,1},{},32,0,17});
        window.resize(160,80);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto baseline=main->grabFramebuffer();QVERIFY(!baseline.isNull());
        const auto metricsNow=MapMetrics::nowNs();window.mapMetrics()->reset(metricsNow+60'000'000'000);
        window.live()->winset("preview","parent=main;type=map;pos=80,0;size=80x80");
        window.syncFromLive();
        QPointer<MapWidget> preview=dynamic_cast<MapWidget*>(window.controlWidget("main.preview"));QVERIFY(preview);
        auto routes=window.mapRoutes()->snapshot();QCOMPARE(routes.size(),size_t(1));
        auto mailbox=routes[0].mailbox; const auto oldRoute=routes[0].id;routes.clear();
        MapSprite green;green.kind=MapSprite::Screen;green.screenInteractive=true;
        green.image=QImage(32,32,QImage::Format_RGBA8888);green.image.fill(Qt::green);
        green.atom=42;green.key=9;green.haveKey=true;green.mouseOpacity=1;green.iconBottom={0,32};
        QImage black(32,32,QImage::Format_RGBA8888);black.fill(Qt::black);
        mailbox->publish(black,black.rect(),0,0,{{0,0,1,1},{},32,0,17},{green});
        window.mapFrameAvailable();
        auto image=preview->grabFramebuffer();QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(image.width()/4,image.height()/2),QColor(Qt::green));
        mailbox->metrics()->reset(metricsNow+60'000'000'000);
        QCOMPARE(main->grabFramebuffer(),baseline);
        QSignalSpy events(&window,&SkinWindow::mapPointer);
        const QPoint point(preview->width()/4,preview->height()/2);
        auto click=[&] {
            QMouseEvent down(QEvent::MouseButtonPress,point,preview->mapToGlobal(point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease,point,preview->mapToGlobal(point),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(preview,&down);QApplication::sendEvent(preview,&up);
        };
        click();QVERIFY(!events.empty());
        const auto prior=qvariant_cast<MapPointer>(events.back()[0]);QCOMPARE(prior.click.over.ref,uint32_t(42));
        QCOMPARE(prior.click.screenLocMap,std::string("preview"));
        green.position={16,0};
        mailbox->publish(black,black.rect(),0,0,{{0,0,1,1},{},32,0,17},{green});
        click();QCOMPARE(qvariant_cast<MapPointer>(events.back()[0]).click.over.ref,uint32_t(42));
        image=preview->grabFramebuffer();
        QCOMPARE(image.pixelColor(image.width()/4,image.height()/2),QColor(Qt::black));
        click();QCOMPARE(qvariant_cast<MapPointer>(events.back()[0]).click.over.kind,uint8_t(fugue::proto::enc::RefNull));
        QCOMPARE(main->grabFramebuffer(),baseline);
        window.live()->winset("preview","parent=");window.syncFromLive();QVERIFY(preview.isNull());
        QVERIFY(prior.surface && prior.surface->expired());
        QCOMPARE(window.mapMetrics()->snapshot(metricsNow+61'000'000'000).uploadMBPerSecond,0.0);
        QCOMPARE(mailbox->metrics()->snapshot(metricsNow+61'000'000'000).uploadMBPerSecond,0.0);
        mailbox.reset();
        QVERIFY(window.mapRoutes()->snapshot().empty());
        window.live()->winset("preview","parent=main;type=map;pos=80,0;size=80x80");
        window.syncFromLive();routes=window.mapRoutes()->snapshot();QCOMPARE(routes.size(),size_t(1));
        QVERIFY(routes[0].id!=oldRoute);QVERIFY(!routes[0].mailbox->forRender(0));
        QCOMPARE(main->grabFramebuffer(),baseline);
    }
    void displayPixels_data() {
        QTest::addColumn<bool>("gl");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void displayPixels() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires native MAP final-resampling pixels");
        QFETCH(bool, gl);
        SkinWindow window;
        window.setMapViewSize(1,1);
        QVERIFY(window.loadSkin(displaySkin).isEmpty()); wireDisplayCommands(window);
        auto* map = dynamic_cast<MapWidget*>(window.map()); QVERIFY(map);
        if (gl) map->setApi(QRhiWidget::Api::OpenGL);
        auto metrics = std::make_shared<MapMetrics>(); auto mailbox = std::make_shared<MapMailbox>(metrics);
        map->setMetrics(metrics); map->setMailbox(mailbox);
        QImage terrain(32,32,QImage::Format_RGBA8888); terrain.fill(Qt::black);
        MapSprite lower, upper;
        lower.image = QImage(32,32,QImage::Format_RGBA8888); lower.image.fill(Qt::transparent);
        upper.image = lower.image;
        for (int y=0;y<32;++y) for(int x=0;x<16;++x) {
            lower.image.setPixelColor(x,y,QColor(255,255,255,128));
            upper.image.setPixelColor(x,y,QColor(0,0,255,128));
        }
        lower.atom=42; lower.cell={1,1}; lower.iconBottom={0,32};
        upper.kind=MapSprite::Screen; upper.plane=1;
        MapSprite master; master.kind=MapSprite::Master; master.plane=1; master.tint[0]=0.5f;
        // Nonidentity master forces supported plane composition; HUD is included in final resampling.
        MapGeometry geometry{{0,0,1,1},{20,20},32,0,1};
        mailbox->publish(terrain,terrain.rect(),0,0,geometry,{lower,upper,master});
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QSignalSpy submitted(map,&QRhiWidget::frameSubmitted);
        QTRY_VERIFY_WITH_TIMEOUT(submitted.size()>0,5000);
        const auto initial=map->grabFramebuffer(); QVERIFY(!initial.isNull());
        qInfo() << "MAP presentation logical/physical/DPR" << map->size() << initial.size() << map->devicePixelRatioF();
        const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        QSignalSpy events(&window,&SkinWindow::mapPointer);
        const auto click = [&](QPoint point) {
            // Do not pump Qt's render/update queue between setting changes and input.
            QMouseEvent down(QEvent::MouseButtonPress,point,map->mapToGlobal(point),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QMouseEvent up(QEvent::MouseButtonRelease,point,map->mapToGlobal(point),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(map,&down); QApplication::sendEvent(map,&up);
        };
        const auto settings = [&](const char* text) { window.live()->winset("mapwindow.map",text); window.syncFromLive(); };
        for (double zoom : {2.0,1.5}) {
            settings(zoom == 2 ? "zoom=2" : "zoom=1.5");
            for (const auto& name : {"&Distort","&Normal","&Blur"}) {
                auto* action=displayAction(window,name); QVERIFY(action);
                const auto count=submitted.size(); action->trigger();
                QTRY_VERIFY_WITH_TIMEOUT(submitted.size()>count,5000); // unchanged mailbox, normal MAP submission
                const auto image=map->grabFramebuffer(); QVERIFY(!image.isNull());
                const auto place=mapPresentation(map->size(),{32,32},32,map->presentationSettings());
                const double dpr=double(image.width())/map->width();
                const int y=image.height()/2;
                bool interpolated=false;
                for(int x=0;x<image.width();++x) {
                    const double native=((x+0.5)/dpr-place.destination.x())/place.scale;
                    double weight=0;
                    if(native>=0 && native<32) {
                        const double slope = name == QString("&Normal") && zoom*dpr >= 1 ? zoom*dpr/0.75 : 1;
                        weight = name == QString("&Distort") ? (native<16 ? 1 : 0) : std::clamp(0.5+(16-native)*slope,0.0,1.0);
                    }
                    const auto pixel=image.pixelColor(x,y);
                    // Blend at native resolution FIRST: white/2 then blue/2 = (64,64,192).
                    QVERIFY2(std::abs(pixel.red()-qRound(64*weight))<=2,qPrintable(QString("%1 zoom %2 x %3 red %4 weight %5").arg(name).arg(zoom).arg(x).arg(pixel.red()).arg(weight)));
                    QVERIFY(std::abs(pixel.green()-qRound(64*weight))<=2);
                    QVERIFY(std::abs(pixel.blue()-qRound(192*weight))<=2);
                    if(pixel.blue()>2 && pixel.blue()<190) interpolated=true;
                }
                if (name == QString("&Distort")) QVERIFY(!interpolated);
                if (name == QString("&Blur")) QVERIFY(interpolated);
                std::string size; QVERIFY(window.live()->winget("mapwindow.map","view-size",size));
                QCOMPARE(QString::fromStdString(size),zoom==2 ? QString("64x64") : QString("48x48"));
            }
        }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        // Last-rendered destination survives both settings and viewport changes until render.
        const QPoint point(map->width()/2-20,map->height()/2);
        settings("zoom=2"); QVERIFY(!map->grabFramebuffer().isNull());
        click(point);
        const auto before=qvariant_cast<MapPointer>(events.back().at(0)); QCOMPARE(before.click.over.ref,uint32_t(42));
        displayAction(window,"&Normal")->trigger(); // mode and size are both pending
        settings("zoom=1");
        click(point);
        QCOMPARE(qvariant_cast<MapPointer>(events.back().at(0)).click.iconX,before.click.iconX);
        QVERIFY(!map->grabFramebuffer().isNull());
        click(point);
        QCOMPARE(qvariant_cast<MapPointer>(events.back().at(0)).click.over.kind,uint8_t(fugue::proto::enc::RefNull));
        // Qt may render synchronously inside resize (without frameSubmitted).
        // Check the resulting pixels/transform rather than treating it as pending.
        settings("zoom=2;size=240x160"); QVERIFY(!map->grabFramebuffer().isNull());
        click(point);
        QCOMPARE(qvariant_cast<MapPointer>(events.back().at(0)).click.over.kind,uint8_t(fugue::proto::enc::RefNull));
        // Automatic fit and crop use the same transform for input and reported extent.
        for (bool crop : {false,true}) {
            settings(crop ? "zoom=0;letterbox=false" : "zoom=0;letterbox=true");
            QVERIFY(!map->grabFramebuffer().isNull());
            QCOMPARE(map->size(),QSize(240,160));
            const auto place=mapPresentation(map->size(),{32,32},32,map->presentationSettings());
            QCOMPARE(place.destination,crop ? QRectF(0,-40,240,240) : QRectF(40,0,160,160));
            click({80,80});
            const auto hit=qvariant_cast<MapPointer>(events.back().at(0));
            QCOMPARE(hit.click.iconX,int(std::floor((80-place.destination.x())/place.scale))+1);
            QCOMPARE(map->presentedViewSize(),place.destination.size().toSize());
        }
        // Requested server view changes cannot move input ahead of the immutable scene.
        map->setViewSize({2,2}); QVERIFY(!map->grabFramebuffer().isNull());
        click({80,80});
        QCOMPARE(qvariant_cast<MapPointer>(events.back().at(0)).click.screenY,1);
        // icon-size uses the actual tile size, not a hard-coded 32. Keep the
        // identical resident images while changing only immutable frame geometry.
        geometry.tile=16; geometry.region={0,0,2,2}; map->setViewSize({2,2});
        mailbox->publish(terrain,terrain.rect(),0,0,geometry,{lower,upper,master});
        settings("icon-size=32"); QVERIFY(!map->grabFramebuffer().isNull());
        QCOMPARE(map->presentedViewSize(),QSize(64,64));
        QCOMPARE(metrics->snapshot(now+62'000'000'000).uploadMBPerSecond,0.0);
        // A recreated MAP consumes the retained scene and its own presentation settings.
        const auto retained=map->grabFramebuffer();
        MapWidget replay; if (gl) replay.setApi(QRhiWidget::Api::OpenGL);
        replay.resize(map->size()); replay.setViewSize({2,2}); replay.setMailbox(mailbox);
        replay.setPresentationSettings(map->presentationSettings()); replay.show();
        QVERIFY(QTest::qWaitForWindowExposed(&replay));
        QCOMPARE(replay.grabFramebuffer(),retained);
    }
    void normalStockEdges_data() { displayPixels_data(); }
    void normalStockEdges() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires native stock-edge pixels");
        QFETCH(bool, gl);
        SkinWindow window; window.setMapViewSize(3,3);
        QVERIFY(window.loadSkin(displaySkin).isEmpty()); wireDisplayCommands(window);
        auto* map = dynamic_cast<MapWidget*>(window.map()); QVERIFY(map);
        if (gl) map->setApi(QRhiWidget::Api::OpenGL);
        auto metrics = std::make_shared<MapMetrics>(); auto mailbox = std::make_shared<MapMailbox>(metrics);
        map->setMetrics(metrics); map->setMailbox(mailbox);
        QImage scene(96,96,QImage::Format_RGBA8888);
        for (int y=0;y<96;++y) for (int x=0;x<96;++x)
            scene.setPixelColor(x,y, ((x+(y%32>=8 ? y : 0))%2) ? Qt::white : Qt::black);
        mailbox->publish(scene,scene.rect(),0,0,{{0,0,3,3},{20,20},32,0,1},{});
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!map->grabFramebuffer().isNull());
        const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        displayAction(window,"&Normal")->trigger();
        for (const auto* props : {"zoom=1.125", "icon-size=40", "zoom=1.5", "zoom=1.875", "zoom=2",
                                  "zoom=0;size=240x154;letterbox=true", "zoom=0;size=240x180;letterbox=false",
                                  "zoom=0.5;letterbox=true"}) {
            window.live()->winset("mapwindow.map",props); window.syncFromLive();
            const auto image=map->grabFramebuffer(); QVERIFY(!image.isNull());
            const auto p=mapPresentation(map->size(),scene.size(),32,map->presentationSettings());
            const double dx=double(image.width())/map->width(), dy=double(image.height())/map->height();
            qInfo() << props << "logical/framebuffer/DPR" << map->size() << image.size() << map->devicePixelRatioF();
            if (QString(props)=="zoom=1.125" && dx==1 && dy==1) {
                // Raw stock 516.1688 run, not computed from our sampling helper:
                // first nine stripe pixels at 1.125x (RENDERING.md evidence).
                const int stock[] = {0,255,42,170,127,85,212,0,255};
                for (int i=0;i<9;++i)
                    QVERIFY(std::abs(image.pixelColor(qRound(p.destination.x())+i,
                        qRound(p.destination.y())+4).red()-stock[i])<=1);
            }
            // Independent stock observation: Normal's transition occupies .75
            // physical pixels, not one source texel and not one logical pixel.
            const auto coordinate=[](double u,double scale) {
                if (scale<1) return u-0.5;
                const double cell=std::floor(u), f=u-cell;
                if (f<0.5) return cell-0.5+std::min(0.5,f*scale/0.75);
                return cell+0.5-std::min(0.5,(1-f)*scale/0.75);
            };
            for (int y=0;y<image.height();++y) for (int x=0;x<image.width();++x) {
                const double u=((x+0.5)/dx-p.destination.x())/p.scale;
                const double v=((y+0.5)/dy-p.destination.y())/p.scale;
                // A fragment center exactly on the outer cutoff is sensitive
                // to GPU float interpolation. Test interior filtering, not the
                // tie-breaking of that zero-area rectangle boundary.
                if (std::abs(u)<1e-9 || std::abs(u-96)<1e-9 || std::abs(v)<1e-9 || std::abs(v-96)<1e-9) continue;
                double expected=0;
                if (u>=0 && u<96 && v>=0 && v<96) {
                    const double a=coordinate(u,p.scale*dx), b=coordinate(v,p.scale*dy);
                    const int ix=int(std::floor(a)), iy=int(std::floor(b));
                    const double fx=a-ix, fy=b-iy;
                    for (int j=0;j<2;++j) for (int i=0;i<2;++i)
                        expected+=scene.pixelColor(std::clamp(ix+i,0,95),std::clamp(iy+j,0,95)).red() *
                            (i ? fx : 1-fx)*(j ? fy : 1-fy);
                }
                QVERIFY2(std::abs(image.pixelColor(x,y).red()-qRound(expected))<=2,
                    qPrintable(QString("%1 at %2,%3 actual %4 expected %5").arg(props).arg(x).arg(y).arg(image.pixelColor(x,y).red()).arg(expected)));
            }
        }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
    }
    void mapAddedToVisibleRasterWindow() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        SkinWindow window;
        window.resize(320,240);window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.loadSkin(skin).isEmpty());
        MapWidget* map=nullptr;
        for(auto* child:window.findChildren<QWidget*>()) if(auto* candidate=dynamic_cast<MapWidget*>(child)) map=candidate;
        QVERIFY(map);
        QSignalSpy submitted(map,&QRhiWidget::frameSubmitted);
        map->setViewSize({3,3});map->update();
        // No grabFramebuffer(): that could conceal a missing normal submission path.
        QTRY_VERIFY_WITH_TIMEOUT(submitted.size()>0,5000);
    }
    void nativeDesktopCaptureOwnership() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native desktop ownership");
        MapWidget map; map.resize(320,240); map.setViewSize({1,1});
        auto mailbox=std::make_shared<MapMailbox>(std::make_shared<MapMetrics>()); map.setMailbox(mailbox);
        QImage green(32,32,QImage::Format_RGBA8888); green.fill(Qt::green);
        mailbox->publish(green,green.rect(),0,0,{{0,0,1,1},{},32,0,1});
        QSignalSpy submitted(&map,&QRhiWidget::frameSubmitted);
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        map.raise(); map.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&map));
        const QPoint local(160,120),sample=map.mapToGlobal(local);
        QTRY_VERIFY(nativedesktopfixture::ownsPoint(map,sample));
        map.requestMapFrame(); QTRY_VERIFY(!submitted.empty());
        nativedesktopfixture::Capture capture(map,map,{local},[&]{return submitted.size();});
        auto color=[&]{const auto pixels=capture.grab();return capture.color(pixels,local);};
        QTRY_COMPARE(color(),QColor(Qt::green));

        // A nonactivating owned cover leaves Qt AND native foreground on MAP.
        // Submission/activation alone therefore cannot validate a desktop pixel.
        QWidget cover(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus);
        cover.setAttribute(Qt::WA_ShowWithoutActivating);
        cover.setStyleSheet("background:#ef5fff");
        cover.setGeometry(QRect(sample-QPoint(40,40),QSize(80,80)));
        cover.show(); QVERIFY(QTest::qWaitForWindowExposed(&cover));
        QTRY_COMPARE(QGuiApplication::topLevelAt(sample),cover.windowHandle());
        QCOMPARE(QApplication::activeWindow(),&map);
        QVERIFY(!nativedesktopfixture::ownsPoint(map,sample));
        capture.grab(); QVERIFY(!capture.valid()); QVERIFY(!color().isValid());
        cover.hide();
        QTRY_VERIFY(nativedesktopfixture::ownsPoint(map,sample));
        QVERIFY(!color().isValid()); // never recover a contaminated observation
        nativedesktopfixture::Capture fresh(map,map,{local},[&]{return submitted.size();});
        auto freshColor=[&]{const auto pixels=fresh.grab();return fresh.color(pixels,local);};
        QTRY_COMPARE(freshColor(),QColor(Qt::green));
    }
    void disabledBrowserAboveMap() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires native QRhi and browser composition");
        // WebEngine's QtQuick graphics API is process-global. Exercise OpenGL
        // in a separate process with QSG_RHI_BACKEND=opengl, not a second row.
        const bool openGL = qEnvironmentVariable("QSG_RHI_BACKEND") == "opengl";
        QVector<MapPointer> events;
        SkinWindow window; window.resize(640,480); QVERIFY(window.loadSkin(skin).isEmpty());
        auto* map=dynamic_cast<MapWidget*>(window.controlWidget("main.map")); QVERIFY(map);
        if(openGL) map->setApi(QRhiWidget::Api::OpenGL);
        window.setMapViewSize(2,2);
        auto metrics=std::make_shared<MapMetrics>(); auto mailbox=std::make_shared<MapMailbox>(metrics);
        map->setMailbox(mailbox);
        QImage green(64,64,QImage::Format_RGBA8888); green.fill(Qt::green);
        MapSprite sprite; sprite.image=green; sprite.kind=MapSprite::Screen; sprite.atom=42; sprite.haveKey=true;
        sprite.key=7; sprite.screenInteractive=true; sprite.iconBottom={0,64};
        MapGeometry geometry{QRect(0,0,2,2),QSize(10,10),32,0,9};
        mailbox->publish({},QRect(0,0,64,64),0,0,geometry,{sprite});
        map->setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        QSignalSpy submissions(map,&QRhiWidget::frameSubmitted);
        QPoint diagnosticSample(90,90);
        const auto diagnostic=qScopeGuard([&] {
            if(QTest::currentTestFailed()) nativedesktopfixture::diagnose(window,*map,diagnosticSample,submissions.size());
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        // Activate after native exposure; activation alone is not proof that
        // the desktop compositor's sample belongs to this window.
        window.raise(); window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        map->requestMapFrame();
        QTRY_VERIFY_WITH_TIMEOUT(!submissions.empty(),5000);
        map->setFocus();
        auto live=window.live();
        live->winset("floating","parent=main;type=browser;size=1x1;is-visible=false;is-disabled=true");
        window.showBrowse("floating","window=floating",R"(<style>html,body{margin:0;background:rgb(255,0,0)}</style><b>Overlay</b><script>
          window.overlay={show(){BYOND.winset('floating',{isVisible:true,pos:(60*devicePixelRatio)+','+(60*devicePixelRatio),size:(100*devicePixelRatio)+'x'+(100*devicePixelRatio)})},hide(){BYOND.winset('floating',{isVisible:false})}};
        </script>)");
        auto* pane=window.browseHost()->paneFor("floating"); QVERIFY(pane);
        QTRY_VERIFY_WITH_TIMEOUT(pane->ready(),8000); QVERIFY(pane->isHidden());
        window.pageCall("floating","overlay.show",{});
        QTRY_VERIFY_WITH_TIMEOUT(pane->isVisible() && pane->size()==QSize(100,100),5000);
        const auto sample=pane->mapToGlobal(QPoint(30,30));
        diagnosticSample=map->mapFromGlobal(sample);
        QTRY_VERIFY_WITH_TIMEOUT(nativedesktopfixture::ownsPoint(window,sample),5000);
        QTRY_COMPARE(QApplication::focusWidget(),map);
        QCOMPARE(QApplication::widgetAt(sample),map); // disabled overlay is pointer transparent
        // Capture the desktop compositor, not HWND GetDC (which can return a
        // stale redirected surface on Windows), JS, or a forced MAP readback.
        nativedesktopfixture::Capture desktop(window,*map,{diagnosticSample},[&]{return submissions.size();});
        auto color=[&] { const auto pixels=desktop.grab(); return desktop.color(pixels,diagnosticSample); };
        QTRY_COMPARE_WITH_TIMEOUT(color(),QColor(Qt::red),5000);
        QTest::mouseClick(QApplication::widgetAt(sample),Qt::LeftButton,Qt::NoModifier,map->mapFromGlobal(sample));
        QVERIFY(!events.empty()); QCOMPARE(events.back().click.over.ref,42u);
        // A newer pending frame cannot change this hover hit (HUD -> image).
        auto next=sprite; next.kind=MapSprite::Image; next.imageId=3; next.imageIdentity=20; next.targetIdentity=30; next.key=8;
        mailbox->publish({},QRect(0,0,64,64),0,0,geometry,{next});
        QMouseEvent motion(QEvent::MouseMove,map->mapFromGlobal(sample),sample,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QCursor::setPos(sample); QApplication::sendEvent(map,&motion);
        QVERIFY(events.back().screen); QCOMPARE(events.back().atomKey,uint16_t(7));
        map->requestMapFrame();
        QTRY_VERIFY_WITH_TIMEOUT(!events.empty() && events.back().imageIdentity==20,5000); // stationary re-pick after render
        window.pageCall("floating","overlay.hide",{}); QTRY_VERIFY(pane->isHidden());
        QTRY_COMPARE_WITH_TIMEOUT(color(),QColor(Qt::green),5000);
        const auto loads=window.browseHost()->origin()->stats().pages;
        window.pageCall("floating","overlay.show",{}); QTRY_VERIFY(pane->isVisible());
        QCOMPARE(window.browseHost()->origin()->stats().pages,loads);
        // TG-style CSS alpha must blend with the live MAP, not an opaque page
        // backing store. Observe actual desktop pixels on both native backends.
        pane->page()->runJavaScript("document.documentElement.style.background='transparent';"
                                   "document.body.style.cssText='margin:0;height:100vh;background:rgba(0,0,0,0.65)'");
        auto near = [](QColor actual, QColor expected) {
            return std::abs(actual.red()-expected.red()) <= 2 &&
                std::abs(actual.green()-expected.green()) <= 2 &&
                std::abs(actual.blue()-expected.blue()) <= 2;
        };
        QTRY_VERIFY_WITH_TIMEOUT(near(color(), QColor(89,89,89)),5000); // ordinary opaque browser
        auto changed=live->winset("floating","inner-background-color=transparent;is-disabled=false");
        QVERIFY(changed.unhandled.empty()); window.syncFromLive();
        QTRY_VERIFY_WITH_TIMEOUT(near(color(), QColor(0,89,0)),5000);
        auto* hit=QApplication::widgetAt(sample);
        QVERIFY(hit && (hit==pane || pane->isAncestorOf(hit))); // enabled menu still takes input
        window.pageCall("floating","overlay.hide",{}); QTRY_VERIFY(pane->isHidden());
        QTRY_COMPARE_WITH_TIMEOUT(color(),QColor(Qt::green),5000);
        window.pageCall("floating","overlay.show",{}); QTRY_VERIFY(pane->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(near(color(), QColor(0,89,0)),5000);
        QCOMPARE(window.browseHost()->origin()->stats().pages,loads);
        live->winset("floating","inner-background-color=none"); window.syncFromLive();
        QTRY_VERIFY_WITH_TIMEOUT(near(color(), QColor(89,89,89)),5000);
        QPointer<BrowserPane> old=pane; live->clearRuntimeBrowsers(); window.syncFromLive(); QVERIFY(old.isNull());
        QVERIFY(!window.browseHost()->paneFor("floating"));
        QVERIFY(desktop.valid());
        map->setInputHandler("main.map",true,{}); // callback does not outlive the test's vector
    }
    void hudPixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void hudPixels() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool,openGL);
        MapWidget map;if(openGL)map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(320,240);map.setViewSize({3,3});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        const auto initialMetricTime=MapMetrics::nowNs()+60'000'000'000;metrics->reset(initialMetricTime);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        fugue::model::World w;w.bounds={true,20,20,0,3,3,3,3};
        w.resync.haveOrigin=true;w.resync.originX=w.resync.originY=4;w.resources={{123,0}};
        w.appearances[1].fields.iconResID=0;setFields(w.appearances[1], R"({"screenLoc":"1,1"})");
        w.hudOrder={42};w.hudAppid[42]=1;w.hudIdentity[42]={2,99};w.hud[42]={};
        TurfMap terrain;EntityMap scene;
        auto read=[](uint32_t,QByteArray& out){out=iconfixture::animation();return true;};
        auto frame=terrain.prepare(w,{},0,false);
        auto publish=[&](int64_t now){
            auto sprites=scene.prepare(w,frame,read,now,nullptr,true);
            mailbox->publish(frame.image,frame.source,0,0,frame.geometry,sprites);
        };
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        publish(1);auto image=map.grabFramebuffer();QVERIFY(!image.isNull());
        auto pixel=[](const QImage& im,int x,int y){return im.pixelColor(x*im.width()/320,y*im.height()/240);};
        QCOMPARE(pixel(image,80,200),QColor(50,30,20));
        QCOMPARE(pixel(image,20,200),QColor(Qt::black)); // letterbox
        QVERIFY(metrics->snapshot(initialMetricTime+1'000'000'000).uploadMBPerSecond>0);
        QTest::mouseClick(&map,Qt::LeftButton,Qt::ControlModifier|Qt::ShiftModifier,{80,200});
        QVERIFY(!events.isEmpty());auto hit=events.back();QVERIFY(hit.screen);
        QCOMPARE(hit.click.over.kind,fugue::proto::enc::RefObj);QCOMPARE(hit.click.over.ref,42u);
        QCOMPARE(hit.click.loc.kind,fugue::proto::enc::RefNull);QCOMPARE(hit.atomKey,uint16_t(99));
        QCOMPARE(hit.click.iconX,17);QCOMPARE(hit.click.iconY,16);
        QCOMPARE(hit.click.screenX,1);QCOMPARE(hit.click.screenY,1);
        QVERIFY(hit.click.mods.ctrl);QVERIFY(hit.click.mods.shift);
        // Geometry and identity in a newer pending frame must not leak into picking.
        setFields(w.appearances[1], R"({"screenLoc":"EAST,NORTH","overlays":[2]})");
        w.appearances[2]=w.appearances[1];setFields(w.appearances[2], R"({"pixelX":-32})");
        w.hudIdentity[42].key=100;publish(1);
        QTest::mouseClick(&map,Qt::MiddleButton,Qt::AltModifier,{80,200});
        QVERIFY(events.back().screen);QCOMPARE(events.back().atomKey,uint16_t(99));
        image=map.grabFramebuffer();QCOMPARE(pixel(image,80,200),QColor(Qt::black));
        QTest::mouseClick(&map,Qt::RightButton,Qt::NoModifier,{160,40});
        QVERIFY(events.back().screen);QCOMPARE(events.back().atomKey,uint16_t(100));
        QCOMPARE(events.back().click.iconX,-15);QCOMPARE(events.back().click.iconY,16);
        // Warm every selected animation cell; subsequent cycles and camera motion reuse textures.
        for(int64_t t:{1LL,50000000LL,200000000LL}){publish(t);map.grabFramebuffer();}
        const auto metricTime=MapMetrics::nowNs()+60'000'000'000;
        metrics->reset(metricTime);
        for(int i=0;i<12;++i){frame.source.translate(i%2?1:-1,0);publish(400000000LL*(i/3+1)+(i%3==0?1:i%3==1?50000000:200000000));map.grabFramebuffer();}
        QCOMPARE(metrics->snapshot(metricTime+1'000'000'000).uploadMBPerSecond,0.0);
        // Verified mouse_opacity=0 wire bits make the screen tree click through.
        setFields(w.appearances[1], R"({"screenLoc":"EAST,NORTH","overlays":[2],"appearanceFlags":12288})");
        publish(1);map.grabFramebuffer();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{240,40});
        QVERIFY(!events.back().screen);QCOMPARE(events.back().click.over.kind,fugue::proto::enc::RefTurf);
    }
    void screenOffsetRangePixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void screenOffsetRangePixels() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native MAP submission and desktop pixels");
        QFETCH(bool,openGL);
        MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(480,224); map.setViewSize({15,7});
        auto mailbox=std::make_shared<MapMailbox>(std::make_shared<MapMetrics>()); map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        fugue::model::World w; w.bounds={true,20,20,0,15,7,15,7};
        w.resync.haveOrigin=true; w.resync.originX=w.resync.originY=1; w.resources={{123,0},{456,0}};
        w.appearances[1].fields.iconResID=0; w.appearances[1].fields.layerBits=std::bit_cast<uint32_t>(1.f);
        setFields(w.appearances[1], R"({"screenLoc":"4:16,2:16 to 10:16,2:16"})");
        w.appearances[2].fields.iconResID=1; w.appearances[2].fields.layerBits=std::bit_cast<uint32_t>(2.f);
        setFields(w.appearances[2], R"({"screenLoc":"4:16,2:16"})");
        w.hudAppid={{42,1},{43,2}}; w.hudIdentity={{42,{2,99}},{43,{2,100}}};
        auto read=[](uint32_t id,QByteArray& out){
            QImage image(32,32,QImage::Format_RGBA8888); image.fill(id==123?Qt::blue:Qt::transparent);
            if(id==456) for(int y=10;y<22;++y) for(int x=10;x<22;++x) image.setPixelColor(x,y,Qt::green);
            QBuffer buffer(&out); buffer.open(QIODevice::WriteOnly); return image.save(&buffer,"PNG");
        };
        TurfMap terrain; EntityMap scene; auto frame=terrain.prepare(w,{},0,false);
        QSignalSpy submitted(&map,&QRhiWidget::frameSubmitted);
        const auto diagnostic=qScopeGuard([&] {
            if(QTest::currentTestFailed()) nativedesktopfixture::diagnose(map,map,{128,160},submitted.size());
        });
        auto publish=[&]{
            auto sprites=scene.prepare(w,frame,read,1,nullptr,true);
            mailbox->publish(frame.image,frame.source,0,0,frame.geometry,sprites);
            map.requestMapFrame();
        };
        map.show();
        QVERIFY(QTest::qWaitForWindowExposed(&map));
        map.raise(); map.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&map));
        QList<QPoint> samples{{128,160},{108,172},{340,172}};
        for(int x=116;x<=308;x+=32) samples.push_back({x,172});
        for(const auto& point:samples)
            QTRY_VERIFY_WITH_TIMEOUT(nativedesktopfixture::ownsPoint(map,map.mapToGlobal(point)),5000);
        // Observe the desktop after normal submission, never grabFramebuffer().
        // Poll a fresh capture for readiness, then check all pixels in that same
        // desktop snapshot instead of recapturing the entire screen per pixel.
        nativedesktopfixture::Capture capture(map,map,samples,[&]{return submitted.size();});
        auto desktop=[&]{return capture.grab();};
        auto color=[&](const QImage& image,QPoint point){return capture.color(image,point);};
        for(bool occupied:{false,true}) {
            w.hudOrder={42}; w.hud={{42,fugue::proto::Hud::Record{}}};
            if(occupied) {w.hudOrder.push_back(43);w.hud[43]={};}
            for(int reopen=0;reopen<2;++reopen) {
                const int before=submitted.size(); publish();
                QTRY_VERIFY_WITH_TIMEOUT(submitted.size()>before,5000);
                QImage pixels;
                QTRY_COMPARE_WITH_TIMEOUT(color(pixels=desktop(),{128,160}),QColor(occupied?Qt::green:Qt::blue),5000);
                for(int x=116;x<=308;x+=32) QCOMPARE(color(pixels,{x,172}),QColor(Qt::blue));
                QCOMPARE(color(pixels,{108,172}),QColor(Qt::black)); QCOMPARE(color(pixels,{340,172}),QColor(Qt::black));
                QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{128,160});
                QVERIFY(!events.empty()); QVERIFY(events.back().screen);
                QCOMPARE(events.back().click.over.ref,occupied?43u:42u);
                QVERIFY(w.hudObjectCurrent(events.back().click.over.ref,events.back().atomKey));
                // Removal invalidates worker-side input before a new frame is painted.
                w.hudOrder.clear(); w.hud.clear();
                QVERIFY(!w.hudObjectCurrent(events.back().click.over.ref,events.back().atomKey));
                const int closing=submitted.size(); publish();
                QTRY_VERIFY_WITH_TIMEOUT(submitted.size()>closing,5000);
                QTRY_COMPARE_WITH_TIMEOUT(color(pixels=desktop(),{128,160}),QColor(Qt::black),5000);
                QCOMPARE(color(pixels,{308,172}),QColor(Qt::black));
                QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{128,160});
                QVERIFY(!events.back().screen); QCOMPARE(events.back().click.over.kind,fugue::proto::enc::RefTurf);
                w.hudOrder={42};w.hud={{42,fugue::proto::Hud::Record{}}};
                if(occupied) {w.hudOrder.push_back(43);w.hud[43]={};}
            }
        }
        QVERIFY(capture.valid());
        map.setInputHandler("main.map",true,{});
    }
    void maptextPixels_data() { visPixels_data(); }
    void maptextPixels() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool,openGL); MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(320,240); map.setViewSize({3,3});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        fugue::model::World w; w.bounds={true,20,20,0,3,3,3,3}; w.resync.haveOrigin=true;
        w.resync.originX=w.resync.originY=4;
        auto& atom=w.atoms[42]; atom.hasPos=true; atom.x=atom.y=6; atom.appid=1;
        atom.key=99; atom.haveKey=true; atom.renderIdentity=1;
        auto& ap=w.appearances[1]; ap.fields.iconResID=UINT32_MAX;
        setFields(ap, R"({"maptext":"<span class='caption'><span style='text-align:center;vertical-align:middle'>Hi</span></span>","maptextW":32,"maptextH":32})");
        TurfMap terrain; EntityMap scene; const auto frame=terrain.prepare(w,{},0,false);
        scene.setMapTextStyle({".caption { font-size:16px; color:red; line-height:1.0; -dm-text-outline:1px black }","Arial",6});
        auto publish=[&] { mailbox->publish(frame.image,frame.source,0,0,frame.geometry,scene.prepare(w,frame,{})); };
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map)); QSignalSpy submits(&map,&QRhiWidget::frameSubmitted);
        publish(); map.requestMapFrame(); QTRY_VERIFY_WITH_TIMEOUT(!submits.empty(),5000);
        auto image=map.grabFramebuffer(); QVERIFY(!image.isNull());
        int red=0;
        for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
            const auto pixel=image.pixelColor(x,y);
            if(pixel.red()>30 && pixel.green()==0 && pixel.blue()==0) ++red;
        }
        QVERIFY(red>50); // worker raster reached the normal GPU MAP path
        const auto warm=MapMetrics::nowNs()+60'000'000'000; metrics->reset(warm);
        for(int i=0;i<12;++i) { atom.pixelX=i%3; publish(); QVERIFY(!map.grabFramebuffer().isNull()); }
        QCOMPARE(metrics->snapshot(warm+1'000'000'000).uploadMBPerSecond,0.0);
        scene.setMapTextStyle({".caption { font-size:16px; color:lime; line-height:1.0; -dm-text-outline:1px black }","Arial",6});
        const auto retained=map.grabFramebuffer();
        publish(); QCOMPARE(map.grabFramebuffer(),retained); // stock style-only update retains old text
        setFields(ap, R"({"maptext":"<span class='caption'><span style='text-align:center;vertical-align:middle'>Hi</span></span> ","maptextW":32,"maptextH":32})");
        publish(); image=map.grabFramebuffer();
        int green=0;
        for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
            const auto pixel=image.pixelColor(x,y);
            if(pixel.green()>30 && pixel.red()==0 && pixel.blue()==0) ++green;
        }
        QVERIFY(green>50); // new text uses the updated stylesheet through normal MAP submission
        setFields(ap, "{}"); publish(); image=map.grabFramebuffer();
        for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
            QCOMPARE(image.pixelColor(x,y),QColor(Qt::black));
        // Image-owned runechat alpha changes geometry/uniforms, not its resident raster.
        w.appearances[2]=ap; setFields(w.appearances[2], R"({"maptext":"<span style='font-size:16pt;color:red'>Hi</span>","maptextW":32,"maptextH":32})");
        w.appearances[3]=w.appearances[2];
        setFields(w.appearances[3], R"({"maptext":"<span style='font-size:16pt;color:red'>Hi</span>","maptextW":32,"maptextH":32,"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":0})");
        w.imageInstances[7]={2,42,1,false,20,1};
        w.imageChains[7]={1000,100,{{3,2,std::bit_cast<uint32_t>(10.f),0,0,1,1000}}};
        auto textFrame=[&](int64_t now) { const auto sprites=scene.prepare(w,frame,{},now,nullptr,true);
            mailbox->publish(frame.image,frame.source,0,0,frame.geometry,sprites); return sprites; };
        const auto half=textFrame(1500); QVERIFY(!half.empty()); image=map.grabFramebuffer();
        int maximum=0;for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)maximum=std::max(maximum,image.pixelColor(x,y).red());
        QVERIFY(maximum>50 && maximum<=129);
        metrics->reset(warm);
        for(int i=1;i<10;++i){textFrame(1000+i*100);map.grabFramebuffer();}
        QCOMPARE(metrics->snapshot(warm+1'000'000'000).uploadMBPerSecond,0.0);
        QCOMPARE(half[0].tint[3],128.f/255);
    }
    void visPixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void attachmentTreePixels_data() { visPixels_data(); }
    void attachmentTreePixels() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native attachment composition");
        QFETCH(bool,openGL);MapWidget map;if(openGL)map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,448);map.setViewSize({7,7});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        test::AttachmentTreeFixture f;QVERIFY(f.load());
        QVector<MapPointer> events;map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        const auto points=f.evidence["points"].toArray();const auto phases=f.evidence["phases"].toObject();
        auto publish=[&] {mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);return map.grabFramebuffer();};
        for (auto phase=phases.begin();phase!=phases.end();++phase) {
            f.phase(phase.key());const auto pixels=publish();QVERIFY(!pixels.isNull());
            const auto expected=phase.value().toArray();
            for (int i=0;i<points.size();++i) {
                const auto xy=points[i].toArray(),rgb=expected[i].toArray();
                const auto color=pixels.pixelColor((2*xy[0].toInt()+1)*pixels.width()/448,
                    (2*xy[1].toInt()+1)*pixels.height()/448);
                const int error=std::max({std::abs(color.red()-rgb[0].toInt()),std::abs(color.green()-rgb[1].toInt()),
                    std::abs(color.blue()-rgb[2].toInt())});
                QVERIFY2(error<=1,qPrintable(phase.key()+QString(" sample %1: %2,%3,%4").arg(i).arg(color.red()).arg(color.green()).arg(color.blue())));
            }
            const auto warm=MapMetrics::nowNs()+60'000'000'000;metrics->reset(warm);
            for (int i=0;i<3;++i) {f.world.atoms[43].pixelX=i;f.refresh();publish();}
            QCOMPARE(metrics->snapshot(warm+1'000'000'000).uploadMBPerSecond,0.0);
            f.world.atoms[43].pixelX=0;
        }
        f.phase("input");publish();
        for(const auto& row:f.evidence["clicks"].toArray()) {
            const auto expected=row.toObject(),params=expected["params"].toObject();
            const auto xy=expected["point"].toArray();
            QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{2*xy[0].toInt()+1,2*xy[1].toInt()+1});
            QVERIFY(!events.empty());const auto hit=events.back();QVERIFY(hit.visHit);QVERIFY(hit.screen);
            QCOMPARE(hit.click.over.ref,43u);QCOMPARE(hit.click.loc.kind,fugue::proto::enc::RefNull);
            QCOMPARE(hit.click.iconX,params["icon-x"].toString().toInt());QCOMPARE(hit.click.iconY,params["icon-y"].toString().toInt());
            QVERIFY(mapPointerCurrent(hit,f.world,0));
        }
        const auto old=events.back();f.world.hud.erase(42);f.world.hudOrder.clear();f.refresh();
        mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{161,281});
        QCOMPARE(events.back().visHit,old.visHit);QVERIFY(!mapPointerCurrent(events.back(),f.world,0));
        publish();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{161,281});QVERIFY(!events.back().visHit);
        f.phase("group-half");publish();
        f.world.appearances[7]=f.world.appearances[3];
        auto endpoint=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(f.world.appearances[7]))).object();
        endpoint["pixelX"]=16;endpoint["colorA"]=64;
        setFields(f.world.appearances[7], QJsonDocument(endpoint).toJson(QJsonDocument::Compact).toStdString());
        f.world.appearances[7].fields.hasPixelOffsets=true;
        f.world.atoms[43].appid=7;
        f.world.atoms[43].chain={1000,100,{{3,7,std::bit_cast<uint32_t>(10.f),0,0,1,1000}}};
        const auto warm=MapMetrics::nowNs()+60'000'000'000;metrics->reset(warm);
        for(int64_t now:{1000,1250,1500,1750,2100}) {f.refresh(now);publish();}
        QCOMPARE(metrics->snapshot(warm+1'000'000'000).uploadMBPerSecond,0.0);
    }
    void visMaterialPixels_data() { visPixels_data(); }
    void visMaterialPixels() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native material surfaces");
        QFETCH(bool,openGL);
        HostedPixelMap map(pixelHosts_); if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,448);map.setViewSize({7,7});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        test::VisMaterialFixture f;QVERIFY(f.load());
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        for(const auto& value:f.evidence["phases"].toArray()) {
            const auto row=value.toObject();const auto name=row["name"].toString();f.phase(name);
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            const auto pixels=map.grabFramebuffer();QVERIFY(!pixels.isNull());
            const auto points=f.evidence["sourceSamplePoints"].toArray();
            const auto expected=row["samples"].toArray();
            for(int i=0;i<points.size();++i) {
                const auto xy=points[i].toArray(),rgb=expected[i].toArray();
                const auto color=pixels.pixelColor((2*xy[0].toInt()+1)*pixels.width()/448,
                    (2*xy[1].toInt()+1)*pixels.height()/448);
                const int error=std::max({std::abs(color.red()-rgb[0].toInt()),std::abs(color.green()-rgb[1].toInt()),
                    std::abs(color.blue()-rgb[2].toInt())});
                QVERIFY2(error<=1,qPrintable(name+QString(" sample %1: %2,%3,%4").arg(i).arg(color.red()).arg(color.green()).arg(color.blue())));
            }
            const auto warm=MapMetrics::nowNs()+60'000'000'000;metrics->reset(warm);
            for(int i=0;i<3;++i) {
                f.world.atoms[43].pixelX=i;f.refresh();
                mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);map.grabFramebuffer();
            }
            QCOMPARE(metrics->snapshot(warm+1'000'000'000).uploadMBPerSecond,0.0);
            f.world.atoms[43].pixelX=0;
        }
    }
    void visPixels() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool,openGL); MapWidget map; if(openGL)map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(320,240);map.setViewSize({3,3});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        QVector<MapPointer> events;map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        fugue::model::World w;w.bounds={true,20,20,0,3,3,3,3};w.resync.haveOrigin=true;
        w.resync.originX=w.resync.originY=4;w.resources={{123,0},{456,0}};
        auto& root=w.atoms[42];root.hasPos=true;root.x=root.y=6;root.appid=1;root.key=99;root.haveKey=true;root.renderIdentity=1;
        setFields(w.appearances[1], "{}");w.appearances[1].fields.layerBits=std::bit_cast<uint32_t>(3.f);
        w.appearances[2]=w.appearances[1];w.appearances[2].fields.iconResID=0;
        auto& child=w.atoms[43];child.appid=2;child.pixelX=16;child.renderIdentity=2;child.key=100;child.haveKey=true;
        auto bind=[&](bool present){w.vis.replace(42,1,present?std::vector<fugue::model::VisReference>{{2,43}}:std::vector<fugue::model::VisReference>{},
            [&](uint32_t id){return w.atoms.at(id).renderIdentity;});};bind(true);
        TurfMap terrain;EntityMap scene;auto frame=terrain.prepare(w,{},0,false);
        auto read=[](uint32_t id,QByteArray& out){
            if(id==123)out=iconfixture::animation();
            else {QImage image(32,32,QImage::Format_RGBA8888);image.fill(Qt::blue);out=iconfixture::png(image);}return true;};
        auto publish=[&](int64_t now){auto sprites=scene.prepare(w,frame,read,now,nullptr,true);
            mailbox->publish(frame.image,frame.source,0,0,frame.geometry,sprites);};
        const auto initial=MapMetrics::nowNs()+60'000'000'000;metrics->reset(initial);
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));QSignalSpy submits(&map,&QRhiWidget::frameSubmitted);
        publish(1);map.requestMapFrame();QTRY_VERIFY_WITH_TIMEOUT(!submits.empty(),5000);
        auto image=map.grabFramebuffer();QVERIFY(!image.isNull());
        auto pixel=[](const QImage& im,int x,int y){return im.pixelColor(x*im.width()/320,y*im.height()/240);};
        QCOMPARE(pixel(image,220,120),QColor(50,30,20));
        QVERIFY(metrics->snapshot(initial+1'000'000'000).uploadMBPerSecond>0);
        QTest::mouseClick(&map,Qt::LeftButton,Qt::ControlModifier|Qt::ShiftModifier,{220,120});
        QVERIFY(!events.empty());auto hit=events.back();QVERIFY(hit.visHit);
        QCOMPARE(hit.click.over.ref,43u);QCOMPARE(hit.click.iconX,25);QCOMPARE(hit.click.iconY,16);
        QVERIFY(mapPointerCurrent(hit,w,0));QVERIFY(hit.click.mods.ctrl&&hit.click.mods.shift);
        // New pending edge/geometry must not change picking until a real render.
        bind(false);child.pixelX=-16;bind(true);publish(1);
        QTest::mouseClick(&map,Qt::MiddleButton,Qt::AltModifier,{220,120});
        QCOMPARE(events.back().visHit,hit.visHit);QVERIFY(!mapPointerCurrent(events.back(),w,0));
        image=map.grabFramebuffer();QCOMPARE(pixel(image,220,120),QColor(Qt::black));
        QCOMPARE(pixel(image,120,120),QColor(50,30,20));
        setFields(w.appearances[2], R"({"appearanceFlags":12288,"vars":[{"bit":8192,"data":"2000"}]})"); // inherited ID uses holder mouse opacity
        publish(1);map.grabFramebuffer();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{120,120});
        QCOMPARE(events.back().click.over.ref,42u);QCOMPARE(events.back().click.iconX,1);
        setFields(w.appearances[2], "{}");
        w.atoms[44]=child;w.atoms[44].renderIdentity=4;w.atoms[44].pixelX=0;w.atoms[44].pixelY=32;
        w.vis.replace(43,2,{{2,44}},[&](uint32_t id){return w.atoms.at(id).renderIdentity;});
        publish(1);image=map.grabFramebuffer();QCOMPARE(pixel(image,120,40),QColor(50,30,20));
        for(int64_t t:{1LL,50000000LL,200000000LL}){publish(t);map.grabFramebuffer();}
        const auto warm=MapMetrics::nowNs()+60'000'000'000;metrics->reset(warm);
        for(int i=0;i<12;++i){root.pixelX=i%2;frame.source.translate(i%2?1:-1,0);
            publish(400000000LL*(i/3+1)+(i%3==0?1:i%3==1?50000000:200000000));map.grabFramebuffer();}
        QCOMPARE(metrics->snapshot(warm+1'000'000'000).uploadMBPerSecond,0.0);
        root.pixelX=0;w.appearances[1].fields.iconResID=1;
        setFields(w.appearances[1], R"({"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":128})");
        publish(1);image=map.grabFramebuffer();QCOMPARE(pixel(image,100,120),QColor(25,15,10));
        QCOMPARE(pixel(image,140,120),QColor(25,15,74));
        root.x=3;child.pixelX=96;publish(1);image=map.grabFramebuffer();
        QCOMPARE(pixel(image,160,120),QColor(25,15,10)); // off-view holder, visible extended child bounds
        QCOMPARE(pixel(image,160,40),QColor(25,15,10)); // nested copy outside the holder tile
        child.renderIdentity=3;publish(1);QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{160,120});
        QVERIFY(events.back().visHit);QVERIFY(!mapPointerCurrent(events.back(),w,0));
        // Gas-style nullspace child attached to a turf, not an ordinary object holder.
        w.vis.erase(42);w.vis.erase(43);child.renderIdentity=2;child.pixelX=16;
        w.tiles[{5,5,0}]=1;w.appearances[1].fields.iconResID=UINT32_MAX;setFields(w.appearances[1], "{}");
        const auto turf=fugue::model::VisContents::turf(5,5,0);
        w.vis.replace(turf,0,{{2,43}},[&](uint32_t id){return w.atoms.at(id).renderIdentity;});
        publish(1);image=map.grabFramebuffer();QCOMPARE(pixel(image,220,120),QColor(50,30,20));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{220,120});QVERIFY(events.back().visHit);
        QCOMPARE(events.back().visHit->path[0].holder,turf);QVERIFY(mapPointerCurrent(events.back(),w,0));
        w.vis.erase(turf);QVERIFY(!mapPointerCurrent(events.back(),w,0));
    }
    void imagePixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;QTest::newRow("OpenGL") << true;
    }
    void imagePixels() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool,openGL);MapWidget map;if(openGL)map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(320,240);map.setViewSize({3,3});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        const auto initial=MapMetrics::nowNs()+60'000'000'000;metrics->reset(initial);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        QVector<MapPointer> events;map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        fugue::model::World w;w.bounds={true,20,20,0,3,3,3,3};w.resync.haveOrigin=true;
        w.resync.originX=w.resync.originY=4;w.resources={{123,0},{456,0}};
        auto& target=w.atoms[42];target.hasPos=true;target.x=target.y=6;target.appid=1;
        target.key=99;target.haveKey=true;target.renderIdentity=10;
        setFields(w.appearances[1], "{}");w.appearances[1].fields.layerBits=std::bit_cast<uint32_t>(3.f);
        w.appearances[2]=w.appearances[1];w.appearances[2].fields.iconResID=0;
        setFields(w.appearances[2], R"({"pixelX":16,"hasPlane":true,"plane":-32767})");
        w.imageInstances[7]={2,42,1,false,20,10};
        TurfMap terrain;EntityMap scene;auto frame=terrain.prepare(w,{},0,false);
        auto read=[](uint32_t id,QByteArray& out){
            if(id==123)out=iconfixture::animation();
            else {QImage blue(32,32,QImage::Format_RGBA8888);blue.fill(Qt::blue);out=iconfixture::png(blue);}return true;};
        auto publish=[&](int64_t now){auto sprites=scene.prepare(w,frame,read,now,nullptr,true);
            mailbox->publish(frame.image,frame.source,0,0,frame.geometry,sprites);};
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));QSignalSpy submits(&map,&QRhiWidget::frameSubmitted);
        publish(1);map.requestMapFrame();QTRY_VERIFY_WITH_TIMEOUT(!submits.empty(),5000);
        auto image=map.grabFramebuffer();QVERIFY(!image.isNull());
        auto pixel=[](const QImage& im,int x,int y){return im.pixelColor(x*im.width()/320,y*im.height()/240);};
        QCOMPARE(pixel(image,220,120),QColor(50,30,20));QCOMPARE(pixel(image,140,120),QColor(Qt::black));
        QVERIFY(metrics->snapshot(initial+1'000'000'000).uploadMBPerSecond>0);
        QTest::mouseClick(&map,Qt::LeftButton,Qt::ControlModifier|Qt::ShiftModifier,{220,120});
        QVERIFY(!events.empty());const auto hit=events.back();
        QCOMPARE(hit.click.over.kind,fugue::proto::enc::RefObj);QCOMPARE(hit.click.over.ref,42u);
        QCOMPARE(hit.click.loc.kind,fugue::proto::enc::RefTurf);QCOMPARE(hit.click.iconX,25);QCOMPARE(hit.click.iconY,16);
        QCOMPARE(hit.click.screenX,3);QCOMPARE(hit.click.screenY,2);QVERIFY(hit.click.mods.ctrl&&hit.click.mods.shift);
        QCOMPARE(hit.imageId,7u);QCOMPARE(hit.imageIdentity,uint64_t(20));QCOMPARE(hit.targetIdentity,uint64_t(10));
        QVERIFY(w.imageTargetCurrent(hit.imageId,hit.imageIdentity,42,hit.targetIdentity));
        target.dir=4;publish(1);image=map.grabFramebuffer();QCOMPARE(pixel(image,220,120),QColor(50,70,20));
        setFields(w.appearances[2], R"({"pixelX":16,"dir":1,"appearanceFlags":512})");
        publish(1);image=map.grabFramebuffer();QCOMPARE(pixel(image,220,120),QColor(50,50,20));
        target.dir=0;
        setFields(w.appearances[2], R"({"pixelX":-16,"overlays":[3]})");
        w.appearances[3]=w.appearances[2];setFields(w.appearances[3], R"({"pixelY":32})");
        w.imageInstances[7].identity=21;publish(1);
        QTest::mouseClick(&map,Qt::MiddleButton,Qt::AltModifier,{220,120});
        QCOMPARE(events.back().imageIdentity,uint64_t(20));QVERIFY(!w.imageTargetCurrent(7,20,42,10));
        image=map.grabFramebuffer();QCOMPARE(pixel(image,220,120),QColor(Qt::black));
        QCOMPARE(pixel(image,120,40),QColor(50,30,20));
        QTest::mouseClick(&map,Qt::RightButton,Qt::NoModifier,{120,40});
        QCOMPARE(events.back().imageIdentity,uint64_t(21));QCOMPARE(events.back().click.iconY,48);
        for(int64_t t:{1LL,50000000LL,200000000LL}){publish(t);map.grabFramebuffer();}
        const auto warm=MapMetrics::nowNs()+60'000'000'000;metrics->reset(warm);
        for(int i=0;i<12;++i){target.pixelX=i%2;frame.source.translate(i%2?1:-1,0);
            publish(400000000LL*(i/3+1)+(i%3==0?1:i%3==1?50000000:200000000));map.grabFramebuffer();}
        QCOMPARE(metrics->snapshot(warm+1'000'000'000).uploadMBPerSecond,0.0);
        target.pixelX=0;setFields(w.appearances[1], R"({"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":128})");
        w.appearances[1].fields.iconResID=1;
        publish(1);image=map.grabFramebuffer();QCOMPARE(pixel(image,100,120),QColor(25,15,10));
        QCOMPARE(pixel(image,140,120),QColor(25,15,74)); // inherited alpha applies per part, not a flattened target+image group
        w.appearances[1].fields.iconResID=UINT32_MAX;
        setFields(w.appearances[1], R"({"appearanceFlags":12288})");publish(1);map.grabFramebuffer();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{120,120});QCOMPARE(events.back().imageIdentity,uint64_t(0));
        setFields(w.appearances[1], "{}");setFields(w.appearances[2], R"({"appearanceFlags":262144})");
        publish(1);image=map.grabFramebuffer();QCOMPARE(pixel(image,160,120),QColor(50,30,20));
        // HUD attachment uses screen placement and both image + HUD lifetime fences.
        w.hudOrder={42};w.hudAppid[42]=1;w.hud[42]={};w.hudIdentity[42]={2,99,88};
        target.hasPos=false;setFields(w.appearances[1], R"({"screenLoc":"2,2"})");
        publish(1);image=map.grabFramebuffer();QCOMPARE(pixel(image,160,120),QColor(50,30,20));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{160,120});
        QVERIFY(events.back().screen);QCOMPARE(events.back().imageId,7u);QCOMPARE(events.back().hudIdentity,uint64_t(88));
        QVERIFY(mapPointerCurrent(events.back(),w,0));w.hudIdentity[42].membership=89;QVERIFY(!mapPointerCurrent(events.back(),w,0));
    }
    void imageTargetTransformPixels_data() {
        QTest::addColumn<bool>("openGL"); QTest::addColumn<bool>("mob");
        QTest::newRow("native-object") << false << false; QTest::newRow("OpenGL-object") << true << false;
        QTest::newRow("native-mob-tree") << false << true; QTest::newRow("OpenGL-mob-tree") << true << true;
    }
    void imageTargetTransformPixels() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool,openGL); QFETCH(bool,mob);
        const uint32_t targetKey=42u | (mob ? fugue::model::kRegistryA : 0u);
        MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(160,160); map.setViewSize({5,5});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        QVector<MapPointer> events; map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        fugue::model::World w; w.bounds={true,20,20,0,5,5,5,5}; w.resync.haveOrigin=true;
        w.resync.originX=w.resync.originY=4; w.resources={{123,0},{456,0}};
        auto& target=w.atoms[targetKey]; target.hasPos=true; target.x=target.y=7; target.appid=1;
        target.key=99; target.haveKey=true; target.renderIdentity=10;
        setFields(w.appearances[1], R"({"pixelX":7,"pixelY":5})"); w.appearances[1].fields.iconResID=0;
        w.appearances[1].fields.layerBits=std::bit_cast<uint32_t>(3.f);
        w.appearances[2]=w.appearances[1]; w.appearances[2].fields.iconResID=1;
        setFields(w.appearances[2], R"({"pixelX":25,"pixelY":9})");
        const std::string targetFields=mob ? ",\"pixelX\":7,\"pixelY\":5,\"overlays\":[3]" : ",\"pixelX\":7,\"pixelY\":5";
        if(mob) {w.appearances[3]=w.appearances[1]; setFields(w.appearances[3], "{}");
            setFields(w.appearances[1], affinefixture::fields({1,0,0,0,1,0},targetFields));}
        w.imageInstances[7]={2,targetKey,uint8_t(mob?2:1),false,20,10};
        TurfMap terrain; EntityMap scene; const auto frame=terrain.prepare(w,{},0,false);
        auto read=[](uint32_t id,QByteArray& out){
            QImage im(32,32,QImage::Format_RGBA8888); im.fill(Qt::transparent);
            if(id==456) for(int y=3;y<9;++y) for(int x=23;x<29;++x) im.setPixelColor(x,y,QColor(255,220,0));
            out=iconfixture::png(im); return true;
        };
        MapSprites sprites;
        auto publish=[&]{sprites=scene.prepare(w,frame,read,1,nullptr,true);
            mailbox->publish(frame.image,frame.source,0,0,frame.geometry,sprites);};
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map)); publish(); map.grabFramebuffer();
        QCOMPARE(sprites.size(),mob?3:2); const QPoint origin=sprites.back().position-frame.source.topLeft();
        auto click=[&](QPoint p){QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,origin+p);};
        click({25,5}); QVERIFY(!events.empty()); const auto original=events.back(); QCOMPARE(original.imageIdentity,uint64_t(20));
        struct Case {std::array<float,6> m; QRect yellow; QPoint hit,icon; bool half;};
        const Case cases[]={
            {{1,0,0,0,1,0},{23,3,6,6},{25,5},{26,27},false},
            {{0,-1,0,1,0,0},{3,3,6,6},{5,5},{28,27},false},
            {{0,1,0,-1,0,0},{23,23,6,6},{25,25},{26,25},false},
            {{.5f,0,0,0,.5f,0},{19,9,4,4},{20,10},{25,28},true},
            {{1,0,9,0,1,5},{32,-2,6,6},{34,0},{26,27},false},
            {{0,-1,9,1,0,5},{12,-2,6,6},{14,0},{28,27},false},
            {{.5f,0,9,0,.5f,5},{28,4,4,4},{29,5},{25,28},true},
            {{1,0,0,0,1,0},{23,3,6,6},{25,5},{26,27},false}
        };
        const auto warm=MapMetrics::nowNs()+60'000'000'000; metrics->reset(warm);
        QPoint previousPoint=origin+QPoint(25,5);
        for(int cycle=0;cycle<3;++cycle) for(const auto& c:cases) {
            const auto previous=events.back();
            setFields(w.appearances[1], affinefixture::fields(c.m,targetFields));
            ++w.imageInstances[7].identity; publish();
            // Pending geometry/identity cannot change input until a real submission.
            QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,previousPoint);
            QCOMPARE(events.back().imageIdentity,previous.imageIdentity);
            QVERIFY(!mapPointerCurrent(previous,w,frame.geometry.session));
            const auto image=map.grabFramebuffer(); QVERIFY(!image.isNull());
            for(int y=-4;y<34;++y) for(int x=0;x<40;++x) {
                QColor expected(Qt::black);
                if(c.yellow.contains(x,y)) {
                    int edges=0;
                    if(c.half) {edges+=(x==c.yellow.left()||x==c.yellow.right()); edges+=(y==c.yellow.top()||y==c.yellow.bottom());}
                    expected=edges==2?QColor(16,14,0):edges==1?QColor(63,55,0):QColor(255,220,0);
                }
                const QPoint p=origin+QPoint(x,y);
                // Sample the native texel's interior, not its boundary when
                // physical DPR is fractional (final Normal now blends edges).
                const auto actual=image.pixelColor(int((p.x()+0.5)*image.width()/160),int((p.y()+0.5)*image.height()/160));
                QVERIFY2(qAbs(actual.red()-expected.red())<=1 && qAbs(actual.green()-expected.green())<=1 && actual.blue()==0,
                    qPrintable(QString("pixel %1,%2 expected %3 actual %4").arg(x).arg(y).arg(expected.name()).arg(actual.name())));
            }
            click(c.hit); previousPoint=origin+c.hit;
            QCOMPARE(events.back().imageIdentity,w.imageInstances[7].identity);
            QCOMPARE(events.back().click.iconX,c.icon.x()); QCOMPARE(events.back().click.iconY,c.icon.y());
            QCOMPARE(events.back().targetIdentity,uint64_t(10));
            QCOMPARE(events.back().click.over.kind,mob?fugue::proto::enc::RefMob:fugue::proto::enc::RefObj);
            QCOMPARE(events.back().click.over.ref,42u);
            QVERIFY(mapPointerCurrent(events.back(),w,frame.geometry.session));
            ++target.renderIdentity; QVERIFY(!mapPointerCurrent(events.back(),w,frame.geometry.session)); --target.renderIdentity;
        }
        for(int i=0;i<12;++i) {target.pixelX=i%2; publish(); map.grabFramebuffer();}
        QCOMPARE(metrics->snapshot(warm+1'000'000'000).uploadMBPerSecond,0.0);
        target.pixelX=0; setFields(w.appearances[1], affinefixture::fields({0,-1,0,1,0,0},targetFields));
        publish();
        MapSprite black; black.kind=MapSprite::Blackness; black.position=sprites.back().rect().topLeft(); black.size=sprites.back().rect().size();
        sprites.push_back(black); mailbox->publish(frame.image,frame.source,0,0,frame.geometry,sprites);
        const auto covered=map.grabFramebuffer(); const QPoint p=origin+QPoint(5,5);
        QCOMPARE(covered.pixelColor(p.x()*covered.width()/160,p.y()*covered.height()/160),QColor(Qt::black));
        click({5,5}); QCOMPARE(events.back().imageIdentity,w.imageInstances[7].identity); // stock blackness is presentation-only
    }
    void ordinaryRootTreePixels_data() {
        QTest::addColumn<bool>("openGL"); QTest::addColumn<bool>("mob");
        QTest::newRow("native-object") << false << false; QTest::newRow("OpenGL-object") << true << false;
        QTest::newRow("native-mob") << false << true; QTest::newRow("OpenGL-mob") << true << true;
    }
    void ordinaryRootTreePixels() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native root-tree pixels/input");
        QFETCH(bool,openGL); QFETCH(bool,mob);
        MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.setWindowFlag(Qt::FramelessWindowHint); map.resize(160,160); map.setViewSize({5,5});
        auto metrics=std::make_shared<MapMetrics>(); auto mailbox=std::make_shared<MapMailbox>(metrics);
        map.setMetrics(metrics); map.setMailbox(mailbox);
        QVector<MapPointer> events; map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        auto w=roottreefixture::world(mob,true); TurfMap terrain; EntityMap scene;
        const auto frame=terrain.prepare(w,{},0,false);
        auto publish=[&]{auto sprites=scene.prepare(w,frame,roottreefixture::read,1,nullptr,true);
            mailbox->publish(frame.image,frame.source,0,0,frame.geometry,sprites);};
        QFile observations(QFINDTESTDATA("../../tests/data/root-transform-world/stock-observations.json"));
        QVERIFY(observations.open(QIODevice::ReadOnly));
        const auto clicks=QJsonDocument::fromJson(observations.readAll()).object()["clicks"].toArray();
        const QString directory=QFileInfo(observations.fileName()).absolutePath();
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map)); publish(); map.grabFramebuffer();
        const auto warm=MapMetrics::nowNs()+60'000'000'000; metrics->reset(warm);
        struct Case {const char* name; std::array<float,6> matrix; bool nested=false,wide=false;};
        const Case cases[]={
            {"absent",{1,0,0,0,1,0}}, {"identity",{1,0,0,0,1,0}},
            {"rotate",{0,-1,9,1,0,5}}, {"reverse",{0,1,9,-1,0,5}},
            {"shrink",{.5f,0,9,0,.5f,5}}, {"translate",{1,0,9,0,1,5}},
            {"nested",{0,-1,9,1,0,5},true},
            {"wide-root",{0,-1,9,1,0,5},false,true}};
        std::optional<MapPointer> previous; QPoint previousPoint;
        uint64_t identity=10;
        for(int cycle=0;cycle<3;++cycle) for(const auto& c:cases) {
            w=roottreefixture::world(mob,c.nested,c.wide);
            if(QString(c.name)!="absent") roottreefixture::pose(w,c.matrix);
            w.atoms.begin()->second.renderIdentity=++identity; publish();
            if(previous) {
                QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,previousPoint);
                QCOMPARE(events.back().targetIdentity,previous->targetIdentity);
                QCOMPARE(events.back().click.iconX,previous->click.iconX);
                QCOMPARE(events.back().click.iconY,previous->click.iconY);
                QVERIFY(!mapPointerCurrent(*previous,w,frame.geometry.session));
            }
            const auto image=map.grabFramebuffer(); QVERIFY(!image.isNull());
            const QImage expected(directory+"/stock-"+c.name+".png"); QVERIFY(!expected.isNull());
            for(int y=0;y<104;++y) for(int x=0;x<120;++x) {
                const auto actual=image.pixelColor(int((32+x+0.5)*image.width()/160),int((24+y+0.5)*image.height()/160));
                const auto want=expected.pixelColor(x,y);
                QVERIFY2(qAbs(actual.red()-want.red())<=1 && qAbs(actual.green()-want.green())<=1 && qAbs(actual.blue()-want.blue())<=1,
                    qPrintable(QString("%1 pixel %2,%3 expected %4 actual %5").arg(c.name).arg(x).arg(y).arg(want.name()).arg(actual.name())));
            }
            for(const auto& value:clicks) {
                const auto row=value.toObject(); QString name=row["case"].toString(); name.replace(' ','-');
                if(name!=c.name) continue;
                const auto local=row["rootLocal"].toArray(); const QPoint p=QPoint(71,59)+QPoint(local[0].toInt(),local[1].toInt());
                QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,p); QVERIFY(!events.empty());
                const auto hit=events.back();
                if(row["callback"].isNull()) {QVERIFY(hit.click.over.kind!=fugue::proto::enc::RefObj && hit.click.over.kind!=fugue::proto::enc::RefMob); continue;}
                const auto callback=row["callback"].toObject();
                QCOMPARE(hit.click.over.kind,uint8_t(mob ? fugue::proto::enc::RefMob : fugue::proto::enc::RefObj));
                QCOMPARE(hit.click.over.ref,42u); QCOMPARE(hit.targetIdentity,identity);
                QCOMPARE(hit.click.iconX,callback["icon-x"].toInt()); QCOMPARE(hit.click.iconY,callback["icon-y"].toInt());
                QCOMPARE(hit.click.hasVis,callback.contains("vis-x"));
                if(hit.click.hasVis) {QCOMPARE(hit.click.visX,callback["vis-x"].toInt()); QCOMPARE(hit.click.visY,callback["vis-y"].toInt());}
                QVERIFY(mapPointerCurrent(hit,w,frame.geometry.session));
                QVERIFY(!mapPointerCurrent(hit,w,frame.geometry.session+1));
                previous=hit; previousPoint=p;
            }
        }
        for(int i=0;i<12;++i) {w.atoms.begin()->second.pixelX=i%2; publish(); map.grabFramebuffer();}
        QCOMPARE(metrics->snapshot(warm+1'000'000'000).uploadMBPerSecond,0.0);
    }
    void dimensions() {
        MapWidget map;
        QCOMPARE(map.viewSize(), QSize(0, 0));
        map.setViewSize(QSize(19, 13));
        QCOMPARE(map.viewSize(), QSize(19, 13));
        map.setViewSize(QSize(0, 13));
        QCOMPARE(map.viewSize(), QSize(0, 0));
    }
    void metricsOverlay() {
        MapWidget map;
        map.resize(640, 480);
        QVERIFY(!map.metricsVisible());
        map.setMetricsVisible(true);
        auto* label = map.findChild<QLabel*>("mapPerformanceOverlay");
        QVERIFY(label);
        QVERIFY(map.metricsVisible());
        QVERIFY(label->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(label->focusPolicy(), Qt::NoFocus);
        QVERIFY(label->text().contains("Target"));
        QVERIFY(label->text().contains("Update latency"));
        QVERIFY(label->text().contains("Upload"));
        QVERIFY(label->text().contains("GUI queue"));
        QVERIFY(label->text().contains("Render wait"));
        QVERIFY(label->text().contains("Render to submit"));
        QVERIFY(label->text().contains("Command recording"));
        QVERIFY(label->text().contains("Qt submission wait"));
        QVERIFY(label->text().contains("Display"));
        QVERIFY(label->text().contains("TIME (ms)"));
        QVERIFY(label->text().contains("AVG"));
        QVERIFY(label->text().contains("P95"));
        QVERIFY(!label->text().contains("(reported)"));
        QVERIFY(!label->text().contains("/ p95"));
        QCOMPARE(label->textFormat(),Qt::RichText);
        QVERIFY(label->text().contains("\nCOMPOSITION\n"));
        QVERIFY(label->text().contains("Generated passes"));
        QVERIFY(label->text().contains("Storage pixels"));
        QVERIFY(label->text().contains("-- / 4096"));
        const auto lines = label->text().split('\n');
        QCOMPARE(lines.at(4).indexOf("AVG") + 3, lines.at(5).indexOf("--") + 2);
        QVERIFY(label->text().contains("Pending"));
        QVERIFY(map.rect().contains(label->geometry()));
        map.setMetricsVisible(false);
        QVERIFY(label->isHidden());
    }
    void layerBandsAndMouseOpacity_data() { visPixels_data(); }
    void layerBandsAndMouseOpacity() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native QRhi composition and input");
        QFETCH(bool,openGL); MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(192,192); map.setViewSize({3,3});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        QVector<MapPointer> events;map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        fugue::model::World w;w.bounds={true,20,20,0,3,3,3,3};w.resync.haveOrigin=true;
        w.resync.originX=w.resync.originY=4;w.resources={{123,0},{456,0}};
        auto& atom=w.atoms[42];atom.hasPos=true;atom.x=atom.y=6;atom.appid=1;atom.key=9;atom.haveKey=true;atom.renderIdentity=10;
        w.appearances[1].fields.iconResID=0;setFields(w.appearances[1], R"({"name":"world"})");
        w.appearances[1].fields.layerBits=std::bit_cast<uint32_t>(100.f);
        w.appearances[2].fields.iconResID=1;setFields(w.appearances[2], R"({"screenLoc":"2,2","name":"screen"})");
        w.appearances[2].fields.layerBits=std::bit_cast<uint32_t>(3.f);
        w.hudOrder={7};w.hudAppid[7]=2;w.hudIdentity[7]={2,99,20};w.hud[7]={};
        QImage red(32,32,QImage::Format_RGBA8888);red.fill(Qt::red);
        for(int y=0;y<8;++y)for(int x=0;x<8;++x)red.setPixelColor(x,y,Qt::transparent);
        QImage blue(32,32,QImage::Format_RGBA8888);blue.fill(Qt::blue);
        const auto redBytes=iconfixture::png(red),blueBytes=iconfixture::png(blue);
        auto read=[&](uint32_t id,QByteArray& out){out=id==123 ? redBytes : blueBytes;return true;};
        TurfMap terrain;EntityMap scene;const auto frame=terrain.prepare(w,{},0,false);
        auto publish=[&]{mailbox->publish(frame.image,frame.source,0,0,frame.geometry,scene.prepare(w,frame,read,0,nullptr,true));};
        auto color=[](const QImage& im,int x,int y){return im.pixelColor(x*im.width()/96,y*im.height()/96);};
        auto click=[&](QPoint point){events.clear();QTest::mouseClick(&map,Qt::RightButton,Qt::NoModifier,point*2);return events.isEmpty() ? MapPointer() : events.back();};
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));QSignalSpy submits(&map,&QRhiWidget::frameSubmitted);
        const auto initial=scene.prepare(w,frame,read,0,nullptr,true);
        QCOMPARE(initial.size(),2); QCOMPARE(initial.back().kind,MapSprite::Screen);
        QCOMPARE(initial.back().position-frame.source.topLeft(),QPoint(32,32));
        publish();map.requestMapFrame();QTRY_VERIFY_WITH_TIMEOUT(!submits.empty(),5000);
        QCOMPARE(color(map.grabFramebuffer(),48,48),QColor(Qt::blue)); // screen above any regular world layer
        auto hit=click({48,48});QCOMPARE(hit.click.over.ref,7u);
        auto stack=mapPointerStack(hit);QCOMPARE(stack.size(),2);QCOMPARE(stack[0].click.over.ref,7u);
        w.appearances[1].fields.layerBits=std::bit_cast<uint32_t>(5001.f);publish();
        QCOMPARE(click({48,48}).click.over.ref,7u); // pending order never leaks into input
        QCOMPARE(color(map.grabFramebuffer(),48,48),QColor(Qt::red));
        hit=click({48,48});QCOMPARE(hit.click.over.ref,42u);QVERIFY(mapPointerCurrent(hit,w,0));
        QCOMPARE(click({34,34}).click.over.ref,7u); // source-alpha hole
        const auto warm=MapMetrics::nowNs()+60'000'000'000;metrics->reset(warm);
        setFields(w.appearances[1], R"({"name":"world","appearanceFlags":4096,"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":0})");
        publish();QCOMPARE(color(map.grabFramebuffer(),48,48),QColor(Qt::blue));
        hit=click({34,34});QCOMPARE(hit.click.over.ref,42u); // invisible bounding rectangle still owns input
        stack=mapPointerStack(hit);QCOMPARE(stack[0].click.over.ref,42u);QCOMPARE(stack[1].click.over.ref,7u);
        const auto retained=stack[0];++atom.renderIdentity;QVERIFY(!mapPointerCurrent(retained,w,0));
        setFields(w.appearances[1], R"({"name":"world","appearanceFlags":12288})");publish();map.grabFramebuffer();
        QCOMPARE(click({48,48}).click.over.ref,7u);
        for(int i=0;i<8;++i){atom.pixelX=i%2;publish();map.grabFramebuffer();}
        QCOMPARE(metrics->snapshot(warm+1'000'000'000).uploadMBPerSecond,0.0);
        // No alpha target: native right release must still deliver the rendered
        // iconless menu footprint, without manufacturing ordinary left input.
        w.hudOrder.clear();w.appearances[1].fields.iconResID=UINT32_MAX;
        setFields(w.appearances[1], R"({"name":"iconless"})");
        auto geometry=frame.geometry;geometry.visibility=std::make_shared<const VisibilityFrame>();
        mailbox->publish(frame.image,frame.source,0,0,geometry,scene.prepare(w,frame,read,0,nullptr,true));map.grabFramebuffer();
        hit=click({48,48});QCOMPARE(hit.kind,MapPointer::Click);QVERIFY(hit.backgroundHidden);
        stack=mapPointerStack(hit);QCOMPARE(stack.size(),1);QCOMPARE(stack[0].click.over.ref,42u);
        QVERIFY(mapPointerCurrent(stack[0],w,0));
        events.clear();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{96,96});QVERIFY(events.empty());
    }
    void masterOpacityPixelsAndInput_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void masterOpacityPixelsAndInput() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native master opacity input");
        QFETCH(bool,openGL);
        MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,448); map.setViewSize({7,7});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        int receipts=0,menus=0;
        for(const auto& value:test::MasterOpacityFixture::rows()) {
            const auto row=value.toObject(); const auto name=row["case"].toString();
            test::MasterOpacityFixture f; QVERIFY(f.load(name));
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            const auto pixels=map.grabFramebuffer(); QVERIFY(!pixels.isNull());
            const QImage stock(QString(FUGUE_TEST_DATA_DIR)+"/master-opacity-world/stock/"+name+"-map.png");
            QCOMPARE(stock.size(),QSize(224,224));
            for(int y=0;y<224;++y) for(int x=0;x<224;++x)
                QCOMPARE(pixels.pixelColor((2*x+1)*pixels.width()/448,(2*y+1)*pixels.height()/448),stock.pixelColor(x,y));
            // A newer pending empty scene cannot replace the rendered master/menus.
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,{});
            for(const auto& av:row["actions"].toArray()) {
                const auto action=av.toObject(); const auto xy=action["point"].toArray();
                const QPoint point(xy[0].toInt(),xy[1].toInt());
                events.clear(); QTest::mouseClick(&map,action["kind"]=="menu" ? Qt::RightButton : Qt::LeftButton,Qt::NoModifier,point*2+QPoint(1,1));
                if(action["kind"]=="menu") {
                    const auto click=std::find_if(events.begin(),events.end(),[](const auto& p){return p.kind==MapPointer::Click;});
                    QVERIFY(click!=events.end()); QCOMPARE(f.menuNames(*click),f.menu(name,point)); ++menus;
                    for(const auto& entry:mapPointerStack(*click)) QVERIFY(mapPointerCurrent(entry,f.session.world(),0));
                } else {
                    int i=0;
                    for(const auto& event:events) {
                        if(event.kind==MapPointer::Hover || event.kind==MapPointer::Leave) continue;
                        const auto expected=action["callbacks"].toArray(); QVERIFY(i<expected.size());
                        const auto c=expected[i++].toObject();
                        QCOMPARE(event.kind,c["kind"]=="Down" ? MapPointer::Down : c["kind"]=="Up" ? MapPointer::Up : MapPointer::Click);
                        const auto error=f.checkClick(event,c); QVERIFY2(error.isEmpty(),qPrintable(name+": "+error));
                        QVERIFY(mapPointerCurrent(event,f.session.world(),0)); ++receipts;
                    }
                    QCOMPARE(i,3);
                }
            }
            // Only an actually rendered replacement may retire the old target.
            map.grabFramebuffer(); events.clear();
            QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{121,337});
            QVERIFY(std::none_of(events.begin(),events.end(),[](const auto& p){return p.masterFallback;}));
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites); map.grabFramebuffer();
            const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
            map.grabFramebuffer(); QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        }
        QCOMPARE(receipts,45); QCOMPARE(menus,9);
    }
    void stockOutlinePrecisionAndWorkload_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void stockOutlinePrecisionAndWorkload() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native outline precision/workload checks");
        QFETCH(bool,openGL);
        MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,448); map.setViewSize({7,7});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        // Independent D3D9 readbacks of recovered 516.1688 OutlineVS/PS on
        // A8R8G8B8 (build/fractional-outline): binary/partial 16px patch, 224px
        // source, growing surfaces. These are not renderer-generated expectations.
        QImage black(224,224,QImage::Format_RGBA8888);black.fill(Qt::black);
        for(const auto& v:std::vector<std::array<double,5>>{{255,1,95,95,127},{128,1,95,95,64},
                {255,2.75,94,94,180},{128,2.75,94,94,140},{255,4.25,91,98,157},{128,4.25,91,98,157}}) {
            MapSprite s;s.image=QImage(16,16,QImage::Format_RGBA8888);
            s.image.fill(QColor(255,255,255,int(v[0])));s.position={96,96};s.plane=1;
            MapSprite m;m.kind=MapSprite::Master;m.plane=1;
            PlaneFilter o;o.kind=PlaneFilter::Outline;o.stock=true;o.size=v[1];o.color={1,0,1,1};
            auto e=std::make_shared<PlaneEffects>();e->filters={o};m.planeEffects=e;
            mailbox->publish(black,black.rect(),0,0,{}, {s,m});const auto image=map.grabFramebuffer();
            const auto c=image.pixelColor((2*int(v[2])+1)*image.width()/448,(2*int(v[3])+1)*image.height()/448);
            QVERIFY(std::abs(c.red()-v[4])<=1 && c.green()==0 && std::abs(c.blue()-v[4])<=1);
        }
        // Fresh stock standalone control: same retained source, removing only
        // blur. Generated ink outside the viewport must survive later ladder steps.
        test::StockInputFixture edgeOutline;
        QVERIFY(edgeOutline.load("frames-edge-outline.txt","master-tree-world"));
        for(auto& s:edgeOutline.sprites) if(s.kind==MapSprite::Master && s.planeEffects) {
            auto e=std::make_shared<PlaneEffects>(*s.planeEffects);
            std::erase_if(e->filters,[](const auto& f){return f.kind==PlaneFilter::Blur;});s.planeEffects=e;
        }
        mailbox->publish(edgeOutline.frame.image,edgeOutline.frame.source,0,0,edgeOutline.frame.geometry,edgeOutline.sprites);
        const auto edgePixels=map.grabFramebuffer();
        for(QPoint p:{QPoint(12,0),QPoint(0,12)}) {
            const auto c=edgePixels.pixelColor((2*p.x()+1)*edgePixels.width()/448,(2*p.y()+1)*edgePixels.height()/448);
            QVERIFY(std::abs(c.red()-164)<=1 && std::abs(c.green()-14)<=1 && std::abs(c.blue()-164)<=1);
        }
        // Independent stock 516.1688 samples, same retained partial/edge source;
        // build/outline-range retains full captures. Alpha-only recovery fails
        // the 512/1024 and repeat8 samples by two LSB. No tolerance enlargement.
        struct OutlineSample { double size; int alpha,repeats,x,y,r,g,b,flags=0,rgb=0xc04080; };
        for(const auto& v:std::array<OutlineSample,18>{{{4.25,0,1,95,95,35,35,35},{4.25,1,1,95,95,37,35,35},
                {64.5,128,1,12,0,185,63,125},{512,16,1,132,79,48,35,43},{512,16,1,118,99,69,42,54},
                {1024,16,1,112,86,60,39,51},{4.25,128,4,11,1,192,64,127},
                // Stock captures in build/outline-flags: zero-size color, sharp
                // prefix, square ladder and repeated partial-alpha operations.
                {0,128,1,0,0,87,130,146},{1.75,255,1,95,94,255,0,255,1,0xff00ff},
                {4.25,128,1,11,0,113,49,81,1},{33.25,128,1,30,3,153,57,105,1},
                {4.25,128,1,99,91,64,40,52,2},{33.25,128,1,69,67,123,51,87,2},
                {4.25,128,1,12,2,35,35,35,3},{33.25,128,1,30,22,153,57,105,3},
                {4.25,128,4,20,0,183,63,122,1},{4.25,128,4,28,3,55,38,47,2},{4.25,128,4,24,2,35,35,35,3}}}) {
            auto sprites=edgeOutline.sprites;
            for(auto& s:sprites) if(s.kind==MapSprite::Master && s.planeEffects) {
                auto e=std::make_shared<PlaneEffects>(*s.planeEffects);e->filters.clear();
                PlaneFilter o;o.kind=PlaneFilter::Outline;o.stock=true;o.size=v.size;o.flags=v.flags;
                o.color={float((v.rgb>>16)&255)/255,float((v.rgb>>8)&255)/255,float(v.rgb&255)/255,v.alpha/255.f};
                PlaneFilter b;b.stock=true;b.size=.5;
                for(int i=0;i<v.repeats;++i) {if(v.repeats>1)e->filters.push_back(b);e->filters.push_back(o);}
                s.planeEffects=e;
            }
            mailbox->publish(edgeOutline.frame.image,edgeOutline.frame.source,0,0,edgeOutline.frame.geometry,sprites);
            const auto image=map.grabFramebuffer();const auto c=image.pixelColor((2*v.x+1)*image.width()/448,(2*v.y+1)*image.height()/448);
            QVERIFY(std::abs(c.red()-v.r)<=1 && std::abs(c.green()-v.g)<=1 && std::abs(c.blue()-v.b)<=1);
        }
        // Ordered outline(2)->blur(1): capture, float copy, boundary, two
        // outline steps, two blur axes = seven passes per selected plane.
        // Workload thresholds warn without dropping pixels or input.
        test::StockInputFixture chain;QVERIFY(chain.load("frames-filtered-input.txt","master-tree-world"));
        const auto leaf=std::find_if(chain.sprites.begin(),chain.sprites.end(),[](const auto& s){return s.kind==MapSprite::Screen && !s.image.isNull();});
        const auto master=std::find_if(chain.sprites.begin(),chain.sprites.end(),[](const auto& s){return s.kind==MapSprite::Master;});
        QVERIFY(leaf!=chain.sprites.end() && master!=chain.sprites.end());
        MapSprites many;
        auto budgetFrame=[&](QRect bounds) {
            // Fixture sprites use the captured world-space viewport origin.
            if(bounds.size()==black.size()) bounds=chain.frame.source;
            mailbox->publish(black,bounds,0,0,chain.frame.geometry,many);return map.grabFramebuffer();
        };
        auto ink=[](const QImage& image) {return image.pixelColor(81*image.width()/448,337*image.height()/448);};
        for(int plane=0;plane<36;++plane) {auto member=*leaf,command=*master;member.plane=command.plane=plane;many.push_back(member);many.push_back(command);}
        QVERIFY(ink(budgetFrame(black.rect()))!=QColor(Qt::black));
        auto member=*leaf,command=*master;member.plane=command.plane=36;many.push_back(member);many.push_back(command);
        QVERIFY(ink(budgetFrame(black.rect()))!=QColor(Qt::black)); // 259 passes now admitted
        for(int plane=37;plane<74;++plane) {member.plane=command.plane=plane;many.push_back(member);many.push_back(command);}
        QVERIFY(ink(budgetFrame(black.rect()))!=QColor(Qt::black));
        events.clear();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{81,337});
        QVERIFY(std::any_of(events.begin(),events.end(),[](const auto& e){return e.kind==MapPointer::Click;}));
        many={*leaf,*master};
        auto enormous=std::make_shared<PlaneEffects>(*master->planeEffects);enormous->filters[0].size=8192;enormous->filters[0].flags=1;
        QVERIFY(enormous->boundedTreeFilters());many[1].planeEffects=enormous;
        // Exercise enormous work in the CPU plan only; do not allocate gigabytes
        // in a native regression now that the workload cap is advisory.
        const auto enormousPlan=CompositionPlan::build(many,chain.frame.source,{true});
        QVERIFY(enormousPlan->supported);
        QVERIFY(enormousPlan->metrics.workPixels > CompositionMetrics::pixelCap);
        auto resident=std::make_shared<PlaneEffects>(*enormous);
        resident->filters[0].size=512;resident->filters[0].color[3]=16.f/255;
        many[1].planeEffects=resident;QVERIFY(ink(budgetFrame(black.rect()))!=QColor(Qt::black));
        const auto warm=MapMetrics::nowNs();metrics->reset(warm+60'000'000'000);
        many[0].position.rx()+=1;budgetFrame(black.rect());many[0].position.rx()-=1;budgetFrame(black.rect());
        QCOMPARE(metrics->snapshot(warm+61'000'000'000).uploadMBPerSecond,0.0);
    }
    void sharedPixelHostIsolation_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void sharedPixelHostIsolation() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native shared pixel hosts");
        QFETCH(bool,openGL);
        WId nativeWindow = 0;
        QPointer<QWidget> host;
        for (const QColor color : {QColor(Qt::red),QColor(Qt::green)}) {
            QPointer<MapWidget> retired;
            {
                HostedPixelMap map(pixelHosts_);
                retired = &map;
                if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
                QCOMPARE(map.viewSize(),QSize(0,0));
                QVERIFY(map.presentationSettings()==MapPresentationSettings{});
                map.resize(96,96); map.setViewSize({1,1});
                auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
                auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
                QImage image(32,32,QImage::Format_RGBA8888); image.fill(color);
                mailbox->publish(image,image.rect(),0,0,{},{});
                map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
                if (host) QCOMPARE(map.window(),host.data());
                host = map.window();
                if (nativeWindow) QCOMPARE(host->winId(),nativeWindow);
                nativeWindow = host->winId();
                const auto pixels=map.grabFramebuffer(); QVERIFY(!pixels.isNull());
                QCOMPARE(pixels.pixelColor(pixels.width()/2,pixels.height()/2),color);
                map.setViewSize({9,7});
                auto settings=map.presentationSettings(); settings.mode=MapPresentationSettings::Blur;
                map.setPresentationSettings(settings);
            }
            QVERIFY(retired.isNull()); // fresh scene/input/resource ownership on the next iteration
            QVERIFY(host && host->isVisible()); // native host survives the child
            QCOMPARE(host->winId(),nativeWindow);
        }
    }
    void hudRelayPixelsAndInput_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;QTest::newRow("OpenGL") << true;
    }
    void hudRelayPixelsAndInput() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native HUD relay pixels/input");
        QFETCH(bool,openGL);
        HostedPixelMap map(pixelHosts_);if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,320);map.setViewSize({7,5});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        QVector<MapPointer> events;map.setInputHandler("main.map",true,[&](const auto& p){events.push_back(p);});
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        const auto rows=test::HudRelayFixture::rows();QCOMPARE(rows.size(),12);
        for(const auto& value:rows) {
            const auto row=value.toObject();test::HudRelayFixture f(row["phase"].toString());
            auto publish=[&] {
                mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
                return map.grabFramebuffer();
            };
            const auto image=publish();QVERIFY(!image.isNull());
            QRect ink;
            for(int y=0;y<160;++y) for(int x=0;x<224;++x)
                if(image.pixelColor((2*x+1)*image.width()/448,(2*y+1)*image.height()/320)==QColor(255,220,20))
                    ink=ink.united(QRect(x,y,1,1));
            const auto bounds=row["bounds"].toArray();
            QCOMPARE(ink,QRect(QPoint(bounds[0].toInt(),bounds[1].toInt()),QPoint(bounds[2].toInt()-1,bounds[3].toInt()-1)));
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,{}); // pending != rendered
            for(const auto& v:row["clicks"].toArray()) {
                const auto expected=v.toObject();const auto xy=expected["point"].toArray();events.clear();
                QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,QPoint(xy[0].toInt(),xy[1].toInt())*2+QPoint(1,1));
                const auto hit=std::find_if(events.begin(),events.end(),[](const auto& p){return p.kind==MapPointer::Click;});
                QVERIFY(hit!=events.end());const auto error=test::HudRelayFixture::check(*hit,expected);
                QVERIFY2(error.isEmpty(),qPrintable(row["phase"].toString()+": "+error));
                QVERIFY(mapPointerCurrent(*hit,f.world,17));
            }
            publish();const auto now=MapMetrics::nowNs();metrics->reset(now+60'000'000'000);
            for(int x:{1,0}) {
                f.change(1,{{"pixelX",x}});f.refresh();publish();
            }
            QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        }
    }
    void positionedMasterPixelsAndInput_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;QTest::newRow("OpenGL") << true;
    }
    void positionedMasterPixelsAndInput() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native master placement and input");
        QFETCH(bool,openGL);
        HostedPixelMap map(pixelHosts_);if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,448);map.setViewSize({7,7});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        QVector<MapPointer> events;map.setInputHandler("main.map",true,[&](const auto& p){events.push_back(p);});
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        const auto rows=test::MasterPlacementFixture::rows();QCOMPARE(rows.size(),27);
        for(const auto& value:rows) {
            const auto row=value.toObject();test::MasterPlacementFixture f;QVERIFY(f.load(row));
            auto publish=[&] {
                mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
                return map.grabFramebuffer();
            };
            const auto image=publish();QVERIFY(!image.isNull());
            const QImage stock(QString(FUGUE_TEST_DATA_DIR)+"/master-tree-world/"+test::MasterTreeFixture::goldenFile(row["golden"].toString()));
            QCOMPARE(stock.size(),QSize(224,224));int errors=0;
            for(int y=0;y<224;++y) for(int x=0;x<224;++x) {
                const auto a=image.pixelColor((2*x+1)*image.width()/448,(2*y+1)*image.height()/448);
                const auto b=stock.pixelColor(x,y);
                if(std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())})>1) ++errors;
            }
            QVERIFY2(errors==0,qPrintable(row["kind"].toString()+"/"+row["placement"].toString()+": mismatched pixels "+QString::number(errors)));
            // Pending placement must not move the last rendered hit surfaces.
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,{});
            const auto points=row["points"].toArray(),callbacks=row["callbacks"].toArray();
            for(int i=0;i<points.size();++i) {
                const auto xy=points[i].toArray();events.clear();
                QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,QPoint(xy[0].toInt(),xy[1].toInt())*2+QPoint(1,1));
                const auto hit=std::find_if(events.begin(),events.end(),[](const auto& p){return p.kind==MapPointer::Click;});
                QVERIFY(hit!=events.end());const auto error=f.checkClick(*hit,callbacks[i].toObject());
                QVERIFY2(error.isEmpty(),qPrintable(row["kind"].toString()+"/"+row["placement"].toString()+": "+error));
                QVERIFY(mapPointerCurrent(*hit,f.session.world(),0));
            }
            publish();const auto now=MapMetrics::nowNs();metrics->reset(now+60'000'000'000);
            for(const auto* location:{"CENTER,CENTER","2:7,2:5","1,1"}) {f.setLocation(location);publish();}
            QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        }
    }
    void masterMaterialPixelsAndInput_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void masterMaterialPixelsAndInput() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native master material surfaces");
        QFETCH(bool,openGL);
        HostedPixelMap map(pixelHosts_); if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,448);map.setViewSize({7,7});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("map",true,[&](const MapPointer& p){events.push_back(p);});
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        const QString dir=QString(FUGUE_TEST_DATA_DIR)+"/master-material-world/";
        QFile observations(dir+"stock.json");QVERIFY(observations.open(QIODevice::ReadOnly));
        const auto rows=QJsonDocument::fromJson(observations.readAll()).object()["phases"].toArray();
        QCOMPARE(rows.size(),23);
        for(const auto& value:rows) {
            const auto row=value.toObject();const auto label=row["label"].toString();
            test::StockInputFixture f;
            QVERIFY(f.load("frames-"+label+".txt","master-material-world","local-relay-world"));
            const QImage stock(dir+row["golden"].toString());QCOMPARE(stock.size(),QSize(224,224));
            int64_t start=0;
            for(const auto& [id,atom]:f.session.world().atoms) start=std::max(start,atom.chain.startNs);
            // Stock's no-op 10-second parallax tween must retain the same
            // compositing at the start, midpoint and after it settles.
            const QList<int64_t> times=row["case"].toString()=="parallax" ? QList<int64_t>{0,5'000'000'000,11'000'000'000} : QList<int64_t>{0};
            for(const auto elapsed:times) {
                auto reader=[&](uint32_t id,QByteArray& out){return f.read(id,out);};
                f.sprites=f.entities.prepare(f.session.world(),f.frame,reader,start+elapsed,nullptr,true);
                mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
                const auto pixels=map.grabFramebuffer();QVERIFY(!pixels.isNull());
                int errors=0;
                for(int y=0;y<224;++y) for(int x=0;x<224;++x) {
                    const auto a=pixels.pixelColor((2*x+1)*pixels.width()/448,(2*y+1)*pixels.height()/448);
                    const auto b=stock.pixelColor(x,y);
                    if(std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())})>1) {
                        if(errors<3) qInfo("material %s (%d,%d): %d,%d,%d expected %d,%d,%d",qPrintable(label),x,y,a.red(),a.green(),a.blue(),b.red(),b.green(),b.blue());
                        ++errors;
                    }
                }
                QVERIFY2(errors==0,qPrintable(label+": mismatched stock pixels "+QString::number(errors)));
            }
            // Picking must still use the last rendered publication.
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,{});
            for(const auto& click:row["clicks"].toArray()) {
                const auto entry=click.toObject();const auto xy=entry["point"].toArray();
                events.clear();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,QPoint(xy[0].toInt(),xy[1].toInt())*2+QPoint(1,1));
                const auto hit=std::find_if(events.begin(),events.end(),[](const auto& p){return p.kind==MapPointer::Click;});
                QVERIFY(hit!=events.end());const auto error=f.checkClick(*hit,entry["expected"].toObject());
                QVERIFY2(error.isEmpty(),qPrintable(label+": "+error));QVERIFY(mapPointerCurrent(*hit,f.session.world(),0));
            }
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);map.grabFramebuffer();
            const auto now=MapMetrics::nowNs();metrics->reset(now+60'000'000'000);
            for(int i=0;i<3;++i) {
                auto changed=f.sprites;
                for(auto& sprite:changed) {
                    if(sprite.kind==MapSprite::Master) sprite.color[16]+=.01f*i;
                    else sprite.position.rx()+=i;
                }
                mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,changed);map.grabFramebuffer();
            }
            QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        }
    }
    void masterTreePixelsAndInput_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void masterTreePixelsAndInput() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native master surfaces and input");
        QFETCH(bool,openGL);
        HostedPixelMap map(pixelHosts_); if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,448);map.setViewSize({7,7});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("map",true,[&](const MapPointer& p){events.push_back(p);});
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        for(const auto& phase:test::MasterTreeFixture::phases()) {
            test::MasterTreeFixture f;QVERIFY(f.load(phase));
            auto publish=[&](const MapSprites& sprites) {
                mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,sprites);
                return map.grabFramebuffer();
            };
            const auto pixels=publish(f.sprites);QVERIFY(!pixels.isNull());
            const QImage stock(QString(FUGUE_TEST_DATA_DIR)+"/master-tree-world/"+test::MasterTreeFixture::goldenFile("stock-"+phase+".png"));
            QCOMPARE(stock.size(),QSize(224,224));
            int errors=0;
            for(int y=0;y<224;++y) for(int x=0;x<224;++x) {
                const auto a=pixels.pixelColor((2*x+1)*pixels.width()/448,(2*y+1)*pixels.height()/448);
                const auto b=stock.pixelColor(x,y);
                if(std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())})>1) {
                    if(errors<8) qInfo("stock mismatch %s (%d,%d): got %d,%d,%d expected %d,%d,%d",qPrintable(phase),x,y,a.red(),a.green(),a.blue(),b.red(),b.green(),b.blue());
                    ++errors;
                }
            }
            QVERIFY2(errors==0,qPrintable(phase+": mismatched stock pixels "+QString::number(errors)));
            // Pending removal must not change either source-alpha picking or ownership.
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,{});
            for(int i=0;i<f.callbacks.size();++i) {
                events.clear();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,f.clickPoint(i)*2+QPoint(1,1));
                const auto hit=std::find_if(events.begin(),events.end(),[](const auto& p){return p.kind==MapPointer::Click;});
                QVERIFY(hit!=events.end());const auto error=f.checkClick(*hit,i);
                QVERIFY2(error.isEmpty(),qPrintable(phase+": "+error));
                QVERIFY(mapPointerCurrent(*hit,f.session.world(),0));
                if(hit->screen) {
                    auto& world=f.session.worldMut();const auto id=hit->click.over.ref;
                    ++world.hudIdentity.at(id).membership;QVERIFY(!mapPointerCurrent(*hit,world,0));--world.hudIdentity.at(id).membership;
                }
            }
            publish(f.sprites);const auto now=MapMetrics::nowNs();metrics->reset(now+60'000'000'000);
            for(int i=0;i<3;++i) {
                auto moved=f.sprites;
                for(auto& s:moved) if(s.kind==MapSprite::Screen || s.kind==MapSprite::Entity) {s.position.rx()+=i;s.iconBottom.rx()+=i;}
                publish(moved);
            }
            QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        }
        // A pending regroup cannot remove a rendered escape or retarget its
        // callback. After submission the discarded local leaf has no hit/menu.
        for (const QString escapePhase : {QString("o-overlay-group-apart-outline"),
                QString("q-overlay-group-apart-outline-size2"),QString("w-overlay-group-apart-outline-radius32")}) {
        auto groupPhase=escapePhase;groupPhase.remove("-apart");
        test::MasterTreeFixture escaped,grouped;
        QVERIFY(escaped.load(escapePhase));QVERIFY(grouped.load(groupPhase));
        mailbox->publish(escaped.frame.image,escaped.frame.source,0,0,escaped.frame.geometry,escaped.sprites);
        map.grabFramebuffer();
        mailbox->publish(grouped.frame.image,grouped.frame.source,0,0,grouped.frame.geometry,grouped.sprites);
        events.clear();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,escaped.clickPoint(2)*2+QPoint(1,1));
        auto click=std::find_if(events.begin(),events.end(),[](const auto& p){return p.kind==MapPointer::Click;});
        QVERIFY(click!=events.end());QVERIFY(escaped.checkClick(*click,2).isEmpty());
        map.grabFramebuffer();
        events.clear();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,grouped.clickPoint(2)*2+QPoint(1,1));
        click=std::find_if(events.begin(),events.end(),[](const auto& p){return p.kind==MapPointer::Click;});
        QVERIFY(click!=events.end());QVERIFY(grouped.checkClick(*click,2).isEmpty());
        for(const auto& entry:mapPointerStack(*click)) QVERIFY(!entry.screen);
        }
        // Replicate commands across the surface warning threshold.
        // This is a workload regression, not stock multi-root selection evidence.
        test::MasterTreeFixture budget;QVERIFY(budget.load("g-overlay-under-group-apart-tint"));
        auto leaf=std::find_if(budget.sprites.begin(),budget.sprites.end(),[](const auto& s){return s.kind==MapSprite::Screen;});
        auto master=std::find_if(budget.sprites.begin(),budget.sprites.end(),[](const auto& s){return s.kind==MapSprite::Master;});
        QVERIFY(leaf!=budget.sprites.end() && master!=budget.sprites.end());
        MapSprites many;
        auto budgetFrame=[&](QRect source) {
            mailbox->publish(budget.frame.image,source,0,0,budget.frame.geometry,many);return map.grabFramebuffer();
        };
        auto sample=[&](const QImage& image) {return image.pixelColor(81*image.width()/448,337*image.height()/448);};
        for(int plane=0;plane<62;++plane) {
            auto l=*leaf,m=*master;l.plane=m.plane=plane;many.push_back(l);many.push_back(m);
        }
        const auto admitted=sample(budgetFrame(budget.frame.source));QCOMPARE(admitted,QColor(15,53,53));
        auto l=*leaf,m=*master;l.plane=m.plane=62;many.push_back(l);many.push_back(m);
        QCOMPARE(sample(budgetFrame(budget.frame.source)),admitted);
        many={*leaf,*master};
        QCOMPARE(sample(budgetFrame(budget.frame.source)),admitted);
        const auto warm=MapMetrics::nowNs();metrics->reset(warm+60'000'000'000);
        budgetFrame(budget.frame.source);QCOMPARE(metrics->snapshot(warm+61'000'000'000).uploadMBPerSecond,0.0);
        // Sizes 1, 2, 4.25 and 32 use respectively 3, 4, 5 and 8 passes.
        // Pinned outputs and scratch slots may exceed the surface warning threshold.
        for (const QString phase : {QString("o-overlay-group-apart-outline"),QString("q-overlay-group-apart-outline-size2"),
                QString("w-overlay-group-apart-outline-radius4.25"),QString("w-overlay-group-apart-outline-radius32")}) {
        test::MasterTreeFixture outlined;QVERIFY(outlined.load(phase));
        const auto outlineLeaf=std::find_if(outlined.sprites.begin(),outlined.sprites.end(),[](const auto& s){return s.kind==MapSprite::Screen;});
        const auto outlineMaster=std::find_if(outlined.sprites.begin(),outlined.sprites.end(),[](const auto& s){return s.kind==MapSprite::Master;});
        QVERIFY(outlineLeaf!=outlined.sprites.end() && outlineMaster!=outlined.sprites.end());
        many.clear();
        const int admittedPlanes=61;
        for(int plane=0;plane<admittedPlanes;++plane) {
            auto member=*outlineLeaf,command=*outlineMaster;member.plane=command.plane=plane;
            many.push_back(member);many.push_back(command);
        }
        QCOMPARE(sample(budgetFrame(budget.frame.source)),QColor(60,210,210));
        auto member=*outlineLeaf,command=*outlineMaster;member.plane=command.plane=admittedPlanes;
        many.push_back(member);many.push_back(command);
        QCOMPARE(sample(budgetFrame(budget.frame.source)),QColor(60,210,210));
        events.clear();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{81,337});
        QVERIFY(std::any_of(events.begin(),events.end(),[](const auto& e){return e.kind==MapPointer::Click;}));
        many={*outlineLeaf,*outlineMaster};
        QCOMPARE(sample(budgetFrame(budget.frame.source)),QColor(60,210,210));
        const auto outlineWarm=MapMetrics::nowNs();metrics->reset(outlineWarm+60'000'000'000);
        budgetFrame(budget.frame.source);
        QCOMPARE(metrics->snapshot(outlineWarm+61'000'000'000).uploadMBPerSecond,0.0);
        }
        // Grouping plus a tree transform remains refused, including both known
        // planes, pixels AND input. It must not expose unprocessed children.
        test::MasterTreeFixture refused;QVERIFY(refused.load("overlay-nested-group-tint"));
        auto& world=refused.session.worldMut();
        auto& root=world.appearances.at(world.hudAppid.begin()->second);
        auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(root))).object();
        fields["hasTransform"]=true;fields["transform"]=QJsonArray{};
        setFields(root, QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());refused.refresh();
        mailbox->publish(refused.frame.image,refused.frame.source,0,0,refused.frame.geometry,refused.sprites);
        const auto pixels=map.grabFramebuffer();
        for(int i=0;i<4;++i) {
            const auto p=test::MasterTreeFixture::point(i);
            QCOMPARE(pixels.pixelColor((2*p.x()+1)*pixels.width()/448,(2*p.y()+1)*pixels.height()/448),QColor(35,35,35));
            events.clear();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,p*2+QPoint(1,1));
            const auto hit=std::find_if(events.begin(),events.end(),[](const auto& e){return e.kind==MapPointer::Click;});
            QVERIFY(hit!=events.end());QCOMPARE(hit->click.over.kind,fugue::proto::enc::RefTurf);
        }
    }
    void planeSourceBlurNative() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native relay effects and input");
        test::PlaneSourceFixture f;
        HostedPixelMap map(pixelHosts_); map.resize(320,192); map.setViewSize({5,3});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto publish=[&] {
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            return map.grabFramebuffer();
        };
        auto pixel=[](const QImage& image,QPoint p) {
            return image.pixelColor((2*p.x()+1)*image.width()/320,(2*p.y()+1)*image.height()/192);
        };
        // Independent retained 516.1688 blur-one source pixels. This is not a
        // second CPU blur implementation or an expected image made by our GPU.
        const std::pair<QPoint,QColor> stock[] = {
            {{69,37},{255,220,20}},{{65,40},{89,98,70}},{{79,47},{20,70,90}},
            {{66,34},{134,144,56}},{{87,55},{239,30,30}},{{90,59},{66,61,77}},
            {{77,44},{95,56,69}},{{30,30},{20,70,90}},{{68,32},{34,79,86}},
            {{67,32},{33,78,86}},{{83,47},{86,58,72}},{{84,47},{174,42,48}}
        };
        QImage blurred;
        for (bool overlay : {false,true}) {
            QJsonObject fields{{"filters",test::PlaneSourceFixture::blur("0000803f")}};
            if (overlay) fields["overlays"]=QJsonArray{4};
            f.relay(fields); const auto image=publish(); QVERIFY(!image.isNull());
            for (const auto& [p,color] : stock) {
                const bool child=overlay && QRect(68,32,16,16).contains(p);
                const auto expected=child ? QColor(30,220,240) : color;
                const auto got=pixel(image,p);
                QVERIFY2(std::max({std::abs(got.red()-expected.red()),std::abs(got.green()-expected.green()),
                    std::abs(got.blue()-expected.blue())}) <= (child ? 0 : 1),qPrintable(QString("stock pixel %1,%2: %3 vs %4")
                    .arg(p.x()).arg(p.y()).arg(got.name()).arg(expected.name())));
            }
            if (!overlay) blurred=image;
            else for (int y=4;y<64;++y) for (int x=4;x<144;++x) {
                // The one discriminating stock phase has exactly 256 sharp cyan
                // pixels; everywhere else in this ROI equals retained blur-only.
                const QPoint p(x,y);
                QCOMPARE(pixel(image,p),QRect(68,32,16,16).contains(p) ? QColor(30,220,240) : pixel(blurred,p));
            }
            for (const QPoint p : {QPoint(69,37),QPoint(65,40),QPoint(79,47)}) {
                events.clear(); QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,p*2+QPoint(1,1));
                const auto hit=std::find_if(events.begin(),events.end(),[](const auto& e){return e.kind==MapPointer::Click;});
                QVERIFY(hit!=events.end()); QVERIFY(mapPointerCurrent(*hit,f.world,17));
                const bool relay=p.x()==69 || (overlay && p.x()==79);
                QCOMPARE(hit->click.over.kind,uint8_t(relay ? fugue::proto::enc::RefObj : fugue::proto::enc::RefTurf));
                if (relay) { QCOMPARE(hit->click.over.ref,201u); QCOMPARE(hit->click.iconX,p.x()+1); QCOMPARE(hit->click.iconY,96-p.y()); }
            }
        }
        // Resolve a later producer, and a nonzero capture origin, without using
        // ordinary-icon coordinates or stretching the viewport surface.
        const auto atOrigin=publish();
        for (auto& s : f.sprites) if (s.kind==MapSprite::Master || s.atom==100) s.plane=90;
        QCOMPARE(publish(),atOrigin);
        f.frame.image=QImage(224,160,QImage::Format_RGBA8888); f.frame.image.fill(QColor(20,70,90));
        f.frame.source.translate(32,32);
        for (auto& s : f.sprites) { s.position+=QPoint(32,32); s.iconBottom+=QPoint(32,32); }
        QCOMPARE(publish(),atOrigin);
        const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        f.relay({{"filters",test::PlaneSourceFixture::blur("0000803f")},{"underlays",QJsonArray{4}}}); publish();
        f.relay({{"filters",test::PlaneSourceFixture::blur("0000803f")},{"overlays",QJsonArray{4}}}); publish();
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        // Missing/ambiguous bindings omit the children too, and recover next frame.
        auto saved=f.sprites;
        auto duplicate=*std::find_if(f.sprites.begin(),f.sprites.end(),[](const auto& s){return s.kind==MapSprite::Master;});
        duplicate.plane=61; f.sprites.push_back(duplicate);
        auto absent=publish(); QCOMPARE(pixel(absent,{79,47}),QColor(20,70,90));
        f.sprites=saved; QVERIFY(pixel(publish(),{79,47})!=QColor(20,70,90));
        for (auto& s : f.sprites) if (s.kind==MapSprite::Master) s.renderTarget="*other";
        absent=publish(); QCOMPARE(pixel(absent,{79,47}),QColor(20,70,90));
        f.sprites=saved;
        const auto recovered=publish();
        MapSprites many;
        for (int n=0;n<65;++n) for (auto s : saved) {
            s.plane=s.kind==MapSprite::Master || s.atom==100 ? n : 1000+n;
            const auto name=QString("*budget%1").arg(n);
            if (!s.renderTarget.isEmpty()) s.renderTarget=name;
            if (!s.renderSource.isEmpty()) s.renderSource=name;
            if (!s.planeSourceDependency.isEmpty()) s.planeSourceDependency=name;
            many.push_back(s);
        }
        f.sprites=many; absent=publish(); // pinned outputs exceed the surface warning threshold
        QVERIFY(pixel(absent,{79,47})!=QColor(Qt::black));
        f.sprites=saved; QCOMPARE(publish(),recovered);
        // Identical consumers reuse one resolved effect rather than paying for
        // hundreds of duplicate surfaces (ordinary draw limits still apply).
        const auto relay=*std::find_if(saved.begin(),saved.end(),[](const auto& s){return !s.renderSource.isEmpty();});
        for (int n=0;n<300;++n) f.sprites.push_back(relay);
        QCOMPARE(pixel(publish(),{79,47}),QColor(30,220,240));
    }
    void planeSourceMatrixNative() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native relay matrix");
        QFile file(QFINDTESTDATA("../../tests/data/relay-gate-world/matrix.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto rows=test::PlaneSourceFixture::capturePixelObservations(file.readAll(),false);
        test::PlaneSourceFixture f;
        HostedPixelMap map(pixelHosts_); map.resize(320,192); map.setViewSize({5,3});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto publish=[&] {
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            return map.grabFramebuffer();
        };
        f.relay({{"overlays",QJsonArray{4}}}); publish(); // warm both resident images
        const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        for (const auto& value:rows) {
            const auto row=value.toObject();
            if (!row["admitted"].toBool()) continue; // recorded unresolved quarter-turns
            const auto label=row["name"].toString();
            f.observation(row); const auto image=publish(); QVERIFY(!image.isNull());
            for (const auto& sample:row["pixels"].toArray()) {
                const auto p=sample.toArray();
                const auto got=image.pixelColor((2*p[0].toInt()+1)*image.width()/320,(2*p[1].toInt()+1)*image.height()/192);
                const QColor expected(p[2].toInt(),p[3].toInt(),p[4].toInt());
                QVERIFY2(std::max({std::abs(got.red()-expected.red()),std::abs(got.green()-expected.green()),
                    std::abs(got.blue()-expected.blue())})<=1,qPrintable(QString("%1 pixel %2,%3: %4 vs %5")
                    .arg(label).arg(p[0].toInt()).arg(p[1].toInt()).arg(got.name()).arg(expected.name())));
            }
            for (const auto& click:row["clicks"].toArray()) {
                const auto c=click.toArray(); const QPoint p(c[0].toInt(),c[1].toInt());
                events.clear(); QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,p*2+QPoint(1,1));
                const auto hit=std::find_if(events.begin(),events.end(),[](const auto& e){return e.kind==MapPointer::Click;});
                QVERIFY(hit!=events.end()); QVERIFY(mapPointerCurrent(*hit,f.world,17));
                QVERIFY2(hit->click.over.kind==uint8_t(c[2].toBool() ? fugue::proto::enc::RefObj : fugue::proto::enc::RefTurf),qPrintable(label));
                QCOMPARE(hit->click.iconX,c[3].toInt()); QCOMPARE(hit->click.iconY,c[4].toInt());
                QCOMPARE(hit->click.hasVis,c[5].toInt()!=0);
                if (hit->click.hasVis) { QCOMPARE(hit->click.visX,c[5].toInt()); QCOMPARE(hit->click.visY,c[6].toInt()); }
            }
        }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        // A shared producer with DIFFERENT operations must not alias an effect
        // cache entry. The upper opaque outline replaces the lower blur edge.
        const auto named=[&](const char* name) {
            for (const auto& v:rows) if (v.toObject()["name"].toString()==name) return v.toObject();
            return QJsonObject{};
        };
        f.observation(named("m-scale-blur-group")); const auto atOrigin=publish();
        const auto originalFrame=f.frame; const auto originalSprites=f.sprites;
        f.frame.image=QImage(224,160,QImage::Format_RGBA8888); f.frame.image.fill(QColor(20,70,90));
        f.frame.source.translate(32,32);
        for (auto& s:f.sprites) { s.position+=QPoint(32,32); s.iconBottom+=QPoint(32,32); }
        QCOMPARE(publish(),atOrigin); // the placement basis is capture-local, not global MAP coordinates
        f.frame=originalFrame; f.sprites=originalSprites; publish();
        // The deliberate terrain resize above is not warm resident playback.
        metrics->reset(now+60'000'000'000);
        f.observation(named("m-blur-group")); const auto blurred=f.sprites; const auto blurOnly=publish();
        f.observation(named("m-outline-group"));
        auto upper=*std::find_if(f.sprites.cbegin(),f.sprites.cend(),[](const auto& s){return s.atom==201u;});
        upper.plane=71; f.sprites=blurred; f.sprites.push_back(upper);
        auto image=publish();
        auto pixel=[&](const QImage& im,QPoint p) {return im.pixelColor((2*p.x()+1)*im.width()/320,(2*p.y()+1)*im.height()/192);};
        QCOMPARE(pixel(image,{65,40}),QColor(64,255,128));
        const auto saved=f.sprites; const auto recovered=image;
        auto master=*std::find_if(saved.cbegin(),saved.cend(),[](const auto& s){return s.kind==MapSprite::Master;});
        master.plane=61; f.sprites.push_back(master);
        QCOMPARE(pixel(publish(),{65,40}),QColor(20,70,90)); // ambiguous typed binding, all parts omitted
        f.sprites=saved; QCOMPARE(publish(),recovered);
        auto recursive=upper; recursive.plane=60; f.sprites.push_back(recursive);
        QCOMPARE(publish(),blurOnly); // cyclic edge is transparent, never a prior-frame texture
        f.sprites=saved; QCOMPARE(publish(),recovered);
        // Wide blur reuses bounded cascade passes, not a relay-specific kernel.
        f.relay({{"appearanceFlags",0x04000204},{"overlays",QJsonArray{4}},
                 {"filters",test::PlaneSourceFixture::blur("0000c842")}}); // 100
        const auto wide=f.sprites; const auto wideImage=publish();
        MapSprites many;
        for (int n=0;n<65;++n) for (auto s:wide) {
            s.plane=s.kind==MapSprite::Master || s.atom==100 ? n : 1000+n;
            const auto name=QString("*wide%1").arg(n);
            if (!s.renderTarget.isEmpty()) s.renderTarget=name;
            if (!s.renderSource.isEmpty()) s.renderSource=name;
            if (!s.planeSourceDependency.isEmpty()) s.planeSourceDependency=name;
            many.push_back(s);
        }
        f.sprites=many; QVERIFY(pixel(publish(),{65,40})!=QColor(Qt::black)); // 65 overlapping captures remain admitted
        f.sprites=wide; QCOMPARE(publish(),wideImage);
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
    }
    void planeSourceCaptureCoverageNative() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native source-capture coverage");
        pixelHosts_.hide(); // this case also checks independent resource recreation
        QFile file(QFINDTESTDATA("../../tests/data/relay-gate-world/source-coverage.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto rows=test::PlaneSourceFixture::capturePixelObservations(file.readAll());
        QFile pixels(QFINDTESTDATA("../../tests/data/relay-gate-world/capture-pixels.json"));
        QVERIFY(pixels.open(QIODevice::ReadOnly));
        for (const auto& row:test::PlaneSourceFixture::capturePixelObservations(pixels.readAll())) rows.append(row);
        QFile precision(QFINDTESTDATA("../../tests/data/relay-gate-world/precision-model.json"));
        QVERIFY(precision.open(QIODevice::ReadOnly));
        for (const auto& row:test::PlaneSourceFixture::capturePixelObservations(precision.readAll())) rows.append(row);
        QFile arithmetic(QFINDTESTDATA("../../tests/data/relay-gate-world/master-arithmetic.json"));
        QVERIFY(arithmetic.open(QIODevice::ReadOnly));
        for (const auto& row:test::PlaneSourceFixture::capturePixelObservations(arithmetic.readAll())) rows.append(row);
        test::PlaneSourceFixture f;
        MapWidget map; map.resize(320,192); map.setViewSize({5,3});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto publish=[&] {
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            return map.grabFramebuffer();
        };
        // Warm every independent resident raster. Material, target-format and
        // topology changes must not create new image or input-mask uploads.
        for (const auto& v:rows) {f.sourceObservation(v.toObject());publish();}
        const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        int pixelErrors=0, verifiedPixels=0, counterexamplePixels=0, counterexampleDifferences=0, clickCount=0;
        for (const auto& value:rows) {
            const auto row=value.toObject(); f.sourceObservation(row);
            const auto image=publish(); QVERIFY(!image.isNull());
            for (const auto& sample:row["pixels"].toArray()) {
                const auto p=sample.toArray();
                const auto got=image.pixelColor((2*p[0].toInt()+1)*image.width()/320,(2*p[1].toInt()+1)*image.height()/192);
                const QColor expected(p[2].toInt(),p[3].toInt(),p[4].toInt());
                const bool different=std::max({std::abs(got.red()-expected.red()),std::abs(got.green()-expected.green()),std::abs(got.blue()-expected.blue())})>1;
                if (row["pixelParity"].toBool()) {
                    ++verifiedPixels;
                    if (different) { ++pixelErrors; qInfo().noquote()<<row["name"].toString()<<p[0].toInt()<<p[1].toInt()<<got.name()<<expected.name(); }
                } else {
                    // Independent counterexamples are measured, not converted to
                    // renderer-generated goldens or passed by a larger tolerance.
                    ++counterexamplePixels; counterexampleDifferences+=different;
                    if (different) qInfo().noquote()<<"counterexample"<<row["name"].toString()<<p[0].toInt()<<p[1].toInt()<<got.name()<<expected.name();
                }
            }
            for (const auto& click:row["clicks"].toArray()) {
                const auto c=click.toArray(); const QPoint p(c[0].toInt(),c[1].toInt());
                events.clear(); QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,p*2+QPoint(1,1));
                const auto hit=std::find_if(events.cbegin(),events.cend(),[](const auto& e){return e.kind==MapPointer::Click;});
                QVERIFY(hit!=events.cend()); QVERIFY(mapPointerCurrent(*hit,f.world,17));
                ++clickCount;
                QVERIFY2(bool(hit->relayHit)==c[2].toBool(),qPrintable(row["name"].toString()));
                if (hit->relayHit) QCOMPARE(hit->click.over.ref,201u);
                QCOMPARE(hit->click.iconX,c[3].toInt()); QCOMPARE(hit->click.iconY,c[4].toInt());
                QCOMPARE(hit->click.hasVis,c[5].toInt()!=0);
                if (hit->click.hasVis) {QCOMPARE(hit->click.visX,c[5].toInt());QCOMPARE(hit->click.visY,c[6].toInt());}
            }
        }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        qInfo()<<"source capture replay:"<<clickCount<<"Clicks;"<<verifiedPixels<<"verified pixels;"
               <<counterexamplePixels<<"counterexample pixels (not parity),"<<counterexampleDifferences<<"differences >1 LSB";
        QCOMPARE(clickCount,574); QCOMPARE(verifiedPixels,580); QCOMPARE(counterexamplePixels,0);
        QCOMPARE(pixelErrors,0);
        // Source effects retain pixels across advisory work thresholds and reuse resident
        // rasters, not an input-specific capture or extra uploaded coverage mask.
        f.sourceObservation(rows[8].toObject());
        for (auto& s:f.sprites) if (s.kind==MapSprite::Master) {
            auto effects=std::make_shared<PlaneEffects>(*s.planeEffects); effects->filters[0].size=100; s.planeEffects=effects;
        }
        const auto wide=f.sprites; const auto recovered=publish();
        MapSprites many;
        for (int n=0;n<65;++n) for (auto s:wide) {
            s.plane=s.kind==MapSprite::Master || s.atom==100 ? n : 1000+n;
            const auto name=QString("*source%1").arg(n);
            if (!s.renderTarget.isEmpty()) s.renderTarget=name;
            if (!s.renderSource.isEmpty()) s.renderSource=name;
            if (!s.planeSourceDependency.isEmpty()) s.planeSourceDependency=name;
            many.push_back(s);
        }
        f.sprites=many; const auto admitted=publish(); QVERIFY(!admitted.isNull());
        QVERIFY(admitted.pixelColor(admitted.width()/2,admitted.height()/2)!=QColor(Qt::black));
        f.sprites=wide; QCOMPARE(publish(),recovered);
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
    }
    void planeSourceGroupedNative() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native grouped plane-source replacement");
        test::PlaneSourceFixture f;
        HostedPixelMap map(pixelHosts_); map.resize(320,192); map.setViewSize({5,3});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto publish=[&] {
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            return map.grabFramebuffer();
        };
        auto pixel=[](const QImage& image,QPoint p) {
            return image.pixelColor((2*p.x()+1)*image.width()/320,(2*p.y()+1)*image.height()/192);
        };
        const auto plain=publish();
        f.relay({{"filters",test::PlaneSourceFixture::blur("0000803f")}});
        const auto blurred=publish();
        QVERIFY(!plain.isNull()); QVERIFY(!blurred.isNull());
        const auto child=f.world.appearances[4];
        const QVector<QPoint> full{{69,37},{79,47},{79,48},{79,31},{87,55},{65,40}};
        struct Case { const char* name; bool blur, underlay, origin, baseIcon; QVector<QPoint> clicks; };
        const Case cases[]={
            {"group-overlay",false,false,false,false,{{69,37},{79,47}}},
            {"group-blur-overlay",true,false,false,false,full},
            {"group-blur-origin",true,false,true,false,{{8,72},{16,72},{79,47},{69,37}}},
            {"group-blur-baseicon",true,false,false,true,{{69,37},{79,47}}},
            {"group-blur-underlay",true,true,false,false,full}
        };
        const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        for (const auto& c:cases) {
            f.world.appearances[4]=child;
            if(c.origin) setFields(f.world.appearances[4], R"({"hasPlane":true,"plane":-32767})");
            f.world.appearances[3].fields.iconResID=c.baseIcon ? 0 : 0xffff;
            QJsonObject fields{{"appearanceFlags",0x04000204},{c.underlay ? "underlays" : "overlays",QJsonArray{4}}};
            if(c.blur) fields["filters"]=test::PlaneSourceFixture::blur("0000803f");
            f.relay(fields); QCOMPARE(f.sprites.size(),3);
            const auto image=publish(); QVERIFY(!image.isNull());
            // Independent stock relation: the entire ROI equals the retained
            // no-child source control. No generated expected group composition.
            for(int y=4;y<64;++y) for(int x=4;x<144;++x)
                QVERIFY2(pixel(image,{x,y})==pixel(c.blur ? blurred : plain,{x,y}),c.name);
            QCOMPARE(pixel(image,{69,37}),QColor(255,220,20));
            QCOMPARE(pixel(image,{79,47}),QColor(20,70,90));
            QCOMPARE(pixel(image,{8,72}),QColor(20,70,90));
            QCOMPARE(pixel(image,{16,72}),QColor(20,70,90));
            const auto edge=pixel(image,{65,40});
            const auto expected=c.blur ? QColor(89,98,70) : QColor(20,70,90);
            QVERIFY2(std::max({std::abs(edge.red()-expected.red()),std::abs(edge.green()-expected.green()),
                std::abs(edge.blue()-expected.blue())}) <= (c.blur ? 1 : 0),c.name);
            for(const QPoint p:c.clicks) {
                events.clear(); QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,p*2+QPoint(1,1));
                const auto hit=std::find_if(events.begin(),events.end(),[](const auto& e){return e.kind==MapPointer::Click;});
                QVERIFY(hit!=events.end()); QVERIFY(mapPointerCurrent(*hit,f.world,17));
                const bool relay=p==QPoint(69,37) || p==QPoint(87,55);
                QCOMPARE(hit->click.over.kind,uint8_t(relay ? fugue::proto::enc::RefObj : fugue::proto::enc::RefTurf));
                QCOMPARE(hit->click.iconX,relay ? p.x()+1 : p.x()%32+1);
                QCOMPARE(hit->click.iconY,relay ? 96-p.y() : 32-p.y()%32);
                if(relay) { QCOMPARE(hit->click.over.ref,201u); QVERIFY(hit->relayHit && hit->relayHit->ink); }
            }
        }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        const auto saved=f.sprites;
        const auto recovered=publish();
        auto master=*std::find_if(saved.begin(),saved.end(),[](const auto& s){return s.kind==MapSprite::Master;});
        master.plane=61; f.sprites.push_back(master);
        QCOMPARE(pixel(publish(),{69,37}),QColor(20,70,90)); // ambiguous binding omits source
        f.sprites=saved;
        auto local=f.sprites.front(); local.renderTarget="*relay"; local.renderSource.clear();
        local.plane=80; local.localGroup=123; f.sprites.push_back(local);
        QCOMPARE(pixel(publish(),{69,37}),QColor(20,70,90)); // no local-target redirection
        f.sprites=saved;
        for(auto& s:f.sprites) if(s.kind==MapSprite::Master) s.renderTarget="*missing";
        QCOMPARE(pixel(publish(),{69,37}),QColor(20,70,90));
        f.sprites=saved; QCOMPARE(publish(),recovered);
    }
    void unsupportedPlaneFilterPixelsAndInput_data() {
        QTest::addColumn<bool>("openGL");QTest::addColumn<QString>("phase");
        for(const bool gl:{false,true}) for(const QString phase:{QString("overlay-nested-tint"),QString("g-overlay-nested-group-apart-overlap-alpha")})
            QTest::newRow(qPrintable((gl?"OpenGL-":"native-default-")+phase)) << gl << phase;
    }
    void unsupportedPlaneFilterPixelsAndInput() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native fallback pixels/input");
        QFETCH(bool,openGL);QFETCH(QString,phase);
        test::MasterTreeFixture f;QVERIFY(f.load(phase));
        auto& world=f.session.worldMut();QCOMPARE(world.hudAppid.size(),size_t(1));
        const auto root=world.hudAppid.begin()->second;
        auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(world.appearances.at(root)))).object();
        HostedPixelMap map(pixelHosts_);if(openGL) map.setApi(QRhiWidget::Api::OpenGL);map.resize(448,448);map.setViewSize({7,7});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        QVector<MapPointer> events;map.setInputHandler("map",true,[&](const MapPointer& p){events.push_back(p);});
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto frame=[&](QJsonArray filters) {
            fields["filters"]=filters;
            setFields(world.appearances.at(root), QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());f.refresh();
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            return map.grabFramebuffer();
        };
        const auto baseline=frame({});QVERIFY(!baseline.isNull());
        const QJsonObject unknown{{"type",99}},malformed{{"type",1},{"flags",1},{"paramMask",1},{"params","bad hex"}};
        const QJsonObject outline{{"type",2},{"flags",1},{"paramMask",0x23},{"params","00000040ff00ffff0000"}};
        const QJsonObject negative{{"type",1},{"flags",1},{"paramMask",1},{"params","000080bf"}};
        QJsonArray tooMany;for(int i=0;i<9;++i) tooMany.append(outline);
        for(const QJsonArray filters:{QJsonArray{unknown},QJsonArray{malformed},QJsonArray{negative},tooMany}) {
            QVERIFY2(frame(filters)==baseline,qPrintable(QJsonDocument(filters).toJson(QJsonDocument::Compact)));
            QVERIFY(std::any_of(f.sprites.begin(),f.sprites.end(),[](const auto& s){
                return s.kind==MapSprite::Master && s.planeEffects && s.planeEffects->supported && s.planeEffects->ignoredFilters;
            }));
            // Pending removal cannot replace the rendered fallback's ownership.
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,{});
            events.clear();QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,test::MasterTreeFixture::point(2)*2+QPoint(1,1));
            const auto hit=std::find_if(events.begin(),events.end(),[](const auto& p){return p.kind==MapPointer::Click;});
            QVERIFY(hit!=events.end());QVERIFY(hit->screen && !hit->filteredPlane);
            QVERIFY(f.checkClick(*hit,2).isEmpty());QVERIFY(mapPointerCurrent(*hit,world,0));
        }
        const auto filtered=frame({outline});QVERIFY(filtered!=baseline);
        // Repeats are supported, not fallback cases. The second outline must
        // affect pixels too, not merely survive decoding. Independent stock outline
        // samples remain covered by stockOutlinePrecisionAndWorkload.
        const auto repeated=frame({outline,outline});
        QVERIFY(repeated!=baseline);QVERIFY(repeated!=filtered);
        QVERIFY(std::any_of(f.sprites.begin(),f.sprites.end(),[](const auto& s){
            return s.kind==MapSprite::Master && s.planeEffects && s.planeEffects->supported &&
                s.planeEffects->filters.size()==2 && s.planeEffects->ignoredFilters==0;
        }));
        QCOMPARE(frame({unknown,outline,malformed}),filtered); // keep supported neighbor, including its order/material
        QCOMPARE(map.grabFramebuffer(),filtered);
        const auto now=MapMetrics::nowNs();metrics->reset(now+60'000'000'000);
        QCOMPARE(frame({unknown}),baseline);QCOMPARE(frame({outline}),filtered);
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        fields["hasTransform"]=true;fields["transform"]=QJsonArray{};
        const auto refused=frame({});QVERIFY(refused!=baseline);
        QCOMPARE(frame({unknown}),refused); // unrelated structural refusal is not bypassed
    }
    // Wave, ripple and bloom pixels against stock samples (tests/data/filter-types-world, 516.1688): the atom's
    // filter surface over the fixture's floor, one case at a time.
    void filterTypesPixelsLikeStock_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void filterTypesPixelsLikeStock() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native filter surfaces");
        QFETCH(bool,openGL);
        HostedPixelMap map(pixelHosts_);if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(160,160);map.setViewSize({5,5});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        MapGeometry geometry;geometry.region={0,0,5,5};geometry.world={5,5};geometry.tile=32;
        QImage floor(160,160,QImage::Format_RGBA8888);floor.fill(QColor(16,16,16));
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        int checked=0;
        for(const auto& value:fugue::test::filtertypes::recordRows()) {
            const auto row=value.toObject();
            if(!row.contains("records")) continue;
            mailbox->publish(floor,floor.rect(),0,0,geometry,{fugue::test::filtertypes::sprite(row,{64,64})});
            const auto got=map.grabFramebuffer();QVERIFY(!got.isNull());
            for(const auto& sample:row["samples"].toArray()) {
                const auto s=sample.toArray();
                const int x=64+s[0].toInt(),y=64+s[1].toInt();
                const auto a=got.pixelColor((2*x+1)*got.width()/320,(2*y+1)*got.height()/320);
                const int d=std::max({std::abs(a.red()-s[2].toInt()),std::abs(a.green()-s[3].toInt()),std::abs(a.blue()-s[4].toInt())});
                QVERIFY2(d<=2,qPrintable(QString("%1 (%2,%3): %4,%5,%6 expected %7,%8,%9").arg(row["case"].toString())
                    .arg(s[0].toInt()).arg(s[1].toInt()).arg(a.red()).arg(a.green()).arg(a.blue())
                    .arg(s[2].toInt()).arg(s[3].toInt()).arg(s[4].toInt())));
            }
            ++checked;
        }
        QCOMPARE(checked,21);
    }
    void standaloneBlurPixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void standaloneBlurPixels() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native large blur surfaces");
        QFETCH(bool,openGL);
        HostedPixelMap map(pixelHosts_);if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(992,672);map.setViewSize({31,21});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        MapGeometry geometry;geometry.region={0,0,31,21};geometry.world={31,21};geometry.tile=32;
        QImage black(992,672,QImage::Format_RGBA8888);black.fill(Qt::black);
        MapSprite markers;markers.plane=10;markers.image=black;markers.image.fill(Qt::transparent);
        { QPainter p(&markers.image);
          p.fillRect(0,0,4,4,QColor(255,0,255));p.fillRect(988,0,4,4,QColor(0,255,255));
          p.fillRect(0,668,4,4,QColor(255,128,0));p.fillRect(988,668,4,4,QColor(0,255,0)); }
        MapSprite master;master.kind=MapSprite::Master;master.plane=1;
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        QJsonArray report;
        for(const QString shape:{QString("square"),QString("edge")}) {
            MapSprite pattern;pattern.plane=1;pattern.image=black;pattern.image.fill(Qt::transparent);
            { QPainter p(&pattern.image);p.fillRect(shape=="square" ? QRect(368,208,256,256) : QRect(0,0,496,672),Qt::white); }
            auto frame=[&](double size) {
                auto effects=std::make_shared<PlaneEffects>();PlaneFilter f;f.stock=true;f.size=size;effects->filters={f};master.planeEffects=effects;
                mailbox->publish(black,black.rect(),0,0,geometry,{pattern,master,markers});
                const auto got=map.grabFramebuffer();
                QImage source(black.size(),QImage::Format_RGB32);
                if(got.isNull()) return QImage();
                for(int y=0;y<672;++y) for(int x=0;x<992;++x)
                    source.setPixel(x,y,got.pixel((2*x+1)*got.width()/1984,(2*y+1)*got.height()/1344));
                return source;
            };
            QImage baseline=black;{QPainter p(&baseline);p.drawImage(0,0,pattern.image);p.drawImage(0,0,markers.image);}
            QCOMPARE(frame(0),baseline.convertToFormat(QImage::Format_RGB32));
            QImage at100,at96,at99;
            for(const double size:{4.0,6.25,8.0,12.5,16.0,96.0,99.0,99.5,100.0}) {
                const auto got=frame(size);QVERIFY(!got.isNull());
                const QImage stock(QString(FUGUE_TEST_DATA_DIR)+"/blur-range-world/stock-"+shape+"-"+QString::number(size)+".png");
                QCOMPARE(stock.size(),black.size());
                int overOne=0,maximum=0,differing=0;
                for(int y=0;y<672;++y) for(int x=0;x<992;++x) {
                    const auto a=got.pixelColor(x,y),b=stock.pixelColor(x,y);
                    const int d=std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
                    overOne+=d>1;differing+=d>0;maximum=std::max(maximum,d);
                }
                QJsonObject row{{"shape",shape},{"size",size},{"pixelsOverOne",overOne},{"differingPixels",differing},{"maxChannelDifference",maximum}};
                report.push_back(row);qInfo().noquote()<<"standalone-blur"<<QJsonDocument(row).toJson(QJsonDocument::Compact);
                const auto dump=qEnvironmentVariable("FUGUE_BLUR_REPORT_DIR");
                if(!dump.isEmpty()) {QVERIFY(QDir().mkpath(dump));QVERIFY(got.save(dump+"/"+shape+"-"+QString::number(size)+(openGL?"-opengl.png":"-default.png")));}
                QCOMPARE(overOne,0); // unchanged independent-stock one-LSB tolerance
                if(size==96) at96=got;if(size==99) at99=got;if(size==100) at100=got;
            }
            QVERIFY(at96!=at99);QVERIFY(at99!=at100);
            for(const double size:{101.0,104.0,128.0,256.0,8193.0,std::numeric_limits<double>::max()})
                QCOMPARE(frame(size),at100);
            if(shape=="square") {
                // All six size-100 planes render; work thresholds are advisory.
                MapSprites many;
                for(int plane=1;plane<=6;++plane) {
                    auto member=pattern,command=master;member.plane=command.plane=plane;
                    many.push_back(member);many.push_back(command);
                    if(plane>=4) {
                        mailbox->publish(black,black.rect(),0,0,geometry,many);
                        const auto got=map.grabFramebuffer();QVERIFY(!got.isNull());
                        QVERIFY(got.pixelColor(got.width()/2,got.height()/2)!=QColor(Qt::black));
                    }
                }
                QCOMPARE(frame(100),at100);
            }
            const auto now=MapMetrics::nowNs();metrics->reset(now+60'000'000'000);
            pattern.position.rx()+=1;frame(100);pattern.position.rx()-=1;QCOMPARE(frame(100),at100);
            map.grabFramebuffer();
            QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        }
        const auto directory=qEnvironmentVariable("FUGUE_BLUR_REPORT_DIR");
        if(!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));QFile file(directory+(openGL?"/native-opengl.json":"/native-default.json"));
            QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(report).toJson());
        }
    }
    void stockShadowPixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void stockShadowPixels() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native stock shadow surfaces");
        QFETCH(bool,openGL);
        const QString dir=qEnvironmentVariable("FUGUE_SHADOW_FIXTURE",QString(FUGUE_TEST_DATA_DIR)+"/stock-shadow-world");
        QFile observations(dir+"/observations.json");QVERIFY(observations.open(QIODevice::ReadOnly));
        const auto rows=QJsonDocument::fromJson(observations.readAll()).array();QVERIFY(!rows.empty());
        HostedPixelMap map(pixelHosts_);if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,448);map.setViewSize({7,7});
        auto metrics=std::make_shared<MapMetrics>();map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics);map.setMailbox(mailbox);
        QImage floor(224,224,QImage::Format_RGBA8888);floor.fill(QColor(35,35,35));
        MapGeometry geometry;geometry.region={0,0,7,7};geometry.world={7,7};geometry.tile=32;
        map.show();QVERIFY(QTest::qWaitForWindowExposed(&map));
        int totalErrors=0;
        for(const auto& value:rows) {
            const auto row=value.toObject();const QString name=row["case"].toString();
            auto effects=std::make_shared<PlaneEffects>(decodePlaneEffects(fugue::model::appearanceOf(
                QJsonDocument(row["effects"].toObject()).toJson(QJsonDocument::Compact).toStdString())));
            QCOMPARE(effects->ignoredFilters,size_t(0));QVERIFY(effects->boundedTreeFilters());
            effects->treeSourceAlphaInput=true;
            MapSprites sprites;
            for(int i=0;i<2;++i) {
                MapSprite s;s.image=QImage(32,32,QImage::Format_RGBA8888);s.image.fill(Qt::transparent);
                const QColor color=i?QColor(230,210,30,row["alpha"].toInt()):QColor(240,30,30,row["alpha"].toInt());
                for(int y=0;y<16;++y) for(int x=0;x<16;++x) s.image.setPixelColor(x,y,color);
                s.position={96+32*i,96};if(!i && row["edge"].toBool())s.position+=QPoint(-100,-100);
                s.iconBottom=s.position+QPoint(0,32);s.plane=i+1;s.atom=i+1;s.cell={4+i,4};
                s.haveKey=true;s.key=7;s.targetIdentity=9;s.mouseOpacity=1;sprites.push_back(s);
            }
            MapSprite master;master.kind=MapSprite::Master;master.plane=1;master.planeEffects=effects;
            if(row["masterTint"].isDouble()) {
                const float tint=float(row["masterTint"].toInt())/255;
                master.tint={tint,tint,tint,1};
            }
            if(row["target"].toBool())master.renderTarget="*shadow";
            sprites.push_back(master);
            if(row["target"].toBool()) {
                MapSprite relay;relay.kind=MapSprite::Screen;relay.plane=3;relay.renderSource="*shadow";
                sprites.push_back(relay);
            }
            auto raster=floor;auto domain=floor.rect();auto coordinates=geometry;
            std::unique_ptr<test::StockInputFixture> wire;
            if(row.contains("frames")) {
                wire=std::make_unique<test::StockInputFixture>();
                const auto fixture=QDir(QString(FUGUE_TEST_DATA_DIR)).relativeFilePath(dir);
                QVERIFY(wire->LocalRelayFixture::load(row["frames"].toString(),fixture));
                raster=wire->frame.image;domain=wire->frame.source;coordinates=wire->frame.geometry;sprites=wire->sprites;
                size_t shadows=0;
                for(const auto& s:sprites) if(s.kind==MapSprite::Master && s.planeEffects) {
                    QCOMPARE(s.planeEffects->ignoredFilters,size_t(0));
                    for(const auto& f:s.planeEffects->filters) shadows+=f.kind==PlaneFilter::Shadow;
                }
                QVERIFY(shadows>0);
            }
            mailbox->publish(raster,domain,0,0,coordinates,sprites);
            const auto got=map.grabFramebuffer();QVERIFY(!got.isNull());
            const QImage expected(dir+"/"+row["golden"].toString());QCOMPARE(expected.size(),floor.size());
            int errors=0,maximum=0;
            for(int y=0;y<224;++y)for(int x=0;x<224;++x) {
                const auto a=got.pixelColor((2*x+1)*got.width()/448,(2*y+1)*got.height()/448),b=expected.pixelColor(x,y);
                const int delta=std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
                maximum=std::max(maximum,delta);
                if(delta>1) {if(errors<3)qWarning("shadow %s (%d,%d) got %d,%d,%d expected %d,%d,%d",qPrintable(name),x,y,a.red(),a.green(),a.blue(),b.red(),b.green(),b.blue());++errors;}
            }
            qInfo().noquote()<<"stock-shadow"<<name<<"pixels-over-one"<<errors<<"max"<<maximum;
            const auto report=qEnvironmentVariable("FUGUE_SHADOW_REPORT");
            if(!report.isEmpty()) {QDir().mkpath(report);got.save(report+"/"+name+(openGL?"-gl.png":"-default.png"));}
            totalErrors+=errors;
            if(!row["edge"].toBool() && (!row["target"].toBool() || wire)) {
                const auto hit=mapPointerAt({104,104},{224,224},{7,7},domain,coordinates,"map",true,sprites);
                const auto halo=mapPointerAt({70,104},{224,224},{7,7},domain,coordinates,"map",true,sprites);
                QVERIFY(hit);QVERIFY(halo);
                if(wire) {
                    QVERIFY(row["inputSupported"].toBool());
                    QCOMPARE(wire->targetName(*hit),std::string("red"));
                    if(row["target"].toBool()) {
                        QVERIFY(hit->planeMousePath);
                        QCOMPARE(hit->planeMousePath->hops.size(),size_t(1));
                    }
                    for(const auto& receipt:row["clicks"].toArray()) {
                        const auto click=receipt.toObject();const auto point=click["point"].toArray();
                        const auto picked=mapPointerAt({point[0].toDouble(),point[1].toDouble()},{224,224},{7,7},domain,coordinates,"map",true,sprites);
                        QVERIFY(picked);
                        const auto error=wire->checkClick(*picked,click["expected"].toObject());
                        QVERIFY2(error.isEmpty(),qPrintable(name+": "+error));
                    }
                    QVERIFY(mapPointerCurrent(*hit,wire->session.world(),0));
                    auto stale=wire->session.world();
                    ++stale.atoms.at(hit->click.over.ref).renderIdentity;
                    QVERIFY(!mapPointerCurrent(*hit,stale,0));
                    QCOMPARE(wire->targetName(*halo),std::string("floor"));
                } else {
                    QCOMPARE(hit->click.over.ref,1u);
                    QVERIFY(halo->click.over.kind!=fugue::proto::enc::RefObj || halo->click.over.ref!=1u);
                }
            }
            const auto now=MapMetrics::nowNs();metrics->reset(now+60'000'000'000);
            mailbox->publish(raster,domain,0,0,coordinates,sprites);map.grabFramebuffer();
            QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        }
        QCOMPARE(totalErrors,0);
    }
    void planeCompositingPixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void planeCompositingPixels() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        pixelHosts_.hide(); // leave cold-start/recreation and subsequent window tests isolated
        QFETCH(bool, openGL);
        class TestMap : public MapWidget {
        public:
            bool recreate = false;
            void render(QRhiCommandBuffer* cb) override {
                if (recreate) { releaseResources(); initialize(cb); recreate = false; }
                MapWidget::render(cb);
            }
        } map;
        if (openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(96,96); map.setViewSize({1,1});
        auto metrics = std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox = std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        QImage black(32,32,QImage::Format_RGBA8888); black.fill(Qt::black);
        auto sprite = [](QColor color, int plane) {
            MapSprite s; s.image = QImage(32,32,QImage::Format_RGBA8888); s.image.fill(color); s.plane = plane; return s;
        };
        auto master = [](int plane, int blend = 0) {
            MapSprite s; s.kind = MapSprite::Master; s.plane = plane; s.blend = blend; return s;
        };
        auto frame = [&](MapSprites sprites) {
            mailbox->publish(black,{0,0,32,32},0,0,{},sprites); return map.grabFramebuffer();
        };
        auto center = [](const QImage& image) { return image.pixelColor(image.width()/2,image.height()/2); };
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto floor = sprite(QColor(200,160,120),0);
        auto light = sprite(Qt::red,15);
        light.color = {}; light.color[0] = .5f; light.color[1] = .25f; light.color[2] = 1; light.color[19] = 1;
        auto lighting = master(15,4);
        // Real server pattern: color-matrix tiles, then a multiply plane master
        // whose matrix forces the plane alpha to one, including uncovered pixels.
        lighting.color[15] = 0; lighting.color[19] = 1;
        auto lit = frame({floor,light,lighting}); QVERIFY(!lit.isNull());
        auto c = center(lit);
        QVERIFY(qAbs(c.red()-100) <= 1); QVERIFY(qAbs(c.green()-40) <= 1); QVERIFY(qAbs(c.blue()-120) <= 1);
        light.position = {16,0};
        auto moved = frame({floor,light,lighting});
        QCOMPARE(moved.pixelColor(12,48), QColor(Qt::black));
        // A plane alpha applies once to the composed group, not to each child.
        auto red = sprite(Qt::red,2), blue = sprite(Qt::blue,2), white = sprite(Qt::white,0);
        auto half = master(2); half.tint[3] = .5f;
        c = center(frame({white,red,blue,half}));
        QVERIFY(qAbs(c.red()-127) <= 1); QVERIFY(qAbs(c.green()-127) <= 1); QCOMPARE(c.blue(),255);
        half.tint[3] = 0;
        QCOMPARE(center(frame({white,red,blue,half})), QColor(Qt::white));
        auto stripes = red;
        for (int y = 16; y < 32; ++y) for (int x = 0; x < 32; ++x) stripes.image.setPixelColor(x,y,Qt::blue);
        half.tint[3] = .5f;
        const auto oriented = frame({white,stripes,half});
        QCOMPARE(oriented.pixelColor(oriented.width()/2,oriented.height()/4).red(),255);
        QCOMPARE(oriented.pixelColor(oriented.width()/2,3*oriented.height()/4).blue(),255);
        auto gray = sprite(QColor(80,80,80),0), add = sprite(QColor(40,20,10),2);
        add.blend = 2;
        QCOMPARE(center(frame({gray,add})),QColor(120,100,90));
        add.blend = 3;
        QCOMPARE(center(frame({gray,add})),QColor(40,60,70));
        // A source can precede its producer in plane order; *targets do not also
        // draw in the final scene. Their own color matrix is included exactly once.
        auto capture = master(10); capture.renderTarget = "*later";
        capture.color[0] = .5f;
        red.plane = 10;
        MapSprite source; source.kind = MapSprite::Screen; source.plane = -1; source.renderSource = "*later";
        QCOMPARE(center(frame({red,capture})), QColor(Qt::black));
        c = center(frame({source,red,capture}));
        QVERIFY(qAbs(c.red()-128) <= 1); QCOMPARE(c.green(),0); QCOMPARE(c.blue(),0);
        // Identity uniforms for straight icons and premultiplied surfaces are
        // distinct, even in consecutive runs with the same matrix. Repeated
        // surface sampling must not multiply the alpha into RGB a second time.
        auto translucent = sprite(QColor(200,100,50,128),10);
        auto identityCapture = capture; identityCapture.color = identityColor;
        c = center(frame({source,translucent,identityCapture}));
        QVERIFY(qAbs(c.red()-100)<=1); QVERIFY(qAbs(c.green()-50)<=1); QVERIFY(qAbs(c.blue()-25)<=1);
        // Shared producer survives both consumers, including intervening
        // scratch reuse. Final outputs stay pinned through unchanged replay.
        auto firstSource=source,lastSource=source;firstSource.plane=-1;lastSource.plane=11;
        auto firstMaster=master(-1),lastMaster=master(11);
        firstMaster.tint={1,0,0,1};lastMaster.tint={0,0,1,1};
        auto middle=sprite(Qt::green,0),middleMaster=master(0);
        auto middleEffect=std::make_shared<PlaneEffects>();PlaneFilter middleBlur;middleBlur.size=1;
        middleEffect->filters={middleBlur};middleMaster.planeEffects=middleEffect;middleMaster.tint[3]=.25f;
        auto back=white;back.plane=-20;
        const auto forked=frame({back,firstSource,firstMaster,middle,middleMaster,translucent,identityCapture,lastSource,lastMaster});
        const auto expectedFork=test::overReference(QColor(0,0,50,128),test::overReference(QColor(0,255,0,64),
            test::overReference(QColor(200,0,0,128),Qt::white)));
        c=center(forked);
        QVERIFY(std::abs(c.red()-expectedFork.red())<=2);
        QVERIFY(std::abs(c.green()-expectedFork.green())<=2);
        QVERIFY(std::abs(c.blue()-expectedFork.blue())<=2);
        QCOMPARE(map.grabFramebuffer(),forked);
        // Sources missing after removal are transparent, not stale GPU content.
        QCOMPARE(center(frame({source})), QColor(Qt::black));
        auto cycle = master(10); cycle.renderTarget = "*later";
        auto observer = source;
        source.plane = 10;
        QCOMPARE(center(frame({observer,source,cycle})), QColor(Qt::black));
        // Unconsumed hidden targets do not contribute to the image or GPU surface budget. Real skins
        // can install dozens of plane masters up front; allocating them all used to black out the frame.
        MapSprites manyUnused{white};
        for (int i=0; i<80; ++i) {
            auto unused = master(100+i); unused.renderTarget = QString("*unused%1").arg(i);
            manyUnused.push_back(unused);
        }
        QCOMPARE(center(frame(manyUnused)), QColor(Qt::white));
        auto liveSource = observer;
        auto producer = identityCapture;
        manyUnused.push_back(translucent); manyUnused.push_back(producer); manyUnused.push_back(liveSource);
        // The consumer is on plane -1, before the producer; the white plane 0 covers it here.
        QCOMPARE(center(frame(manyUnused)), QColor(Qt::white));
        manyUnused[0].plane = -2;
        c = center(frame(manyUnused));
        QVERIFY(qAbs(c.red()-227)<=1); QVERIFY(qAbs(c.green()-177)<=1); QVERIFY(qAbs(c.blue()-152)<=1);
        // Changing pixels that are only in an unreachable target must not upload texture bytes.
        auto unusedPixels = sprite(Qt::red, 100); manyUnused.push_back(unusedPixels);
        frame(manyUnused);
        const auto idleNow = MapMetrics::nowNs(); metrics->reset(idleNow + 60'000'000'000);
        manyUnused.back().image.fill(Qt::blue); frame(manyUnused);
        QCOMPARE(metrics->snapshot(idleNow + 61'000'000'000).uploadMBPerSecond, 0.0);
        // Identity captures alias the raw plane, rather than allocating a redundant identity-copy surface.
        MapSprites identityTargets{white};
        for (int i=0; i<32; ++i) {
            auto visibleTarget = master(200+i); visibleTarget.renderTarget = QString("identity%1").arg(i);
            identityTargets.push_back(visibleTarget);
        }
        QCOMPARE(center(frame(identityTargets)), QColor(Qt::white));
        // A reachable graph above the surface threshold renders and warns in the overlay.
        MapSprites oversized{white};
        for (int i=0; i<64; ++i) {
            auto visibleTarget = master(200+i); visibleTarget.renderTarget = QString("visible%1").arg(i);
            oversized.push_back(visibleTarget); // non-* targets are also final outputs, even when empty
        }
        QCOMPARE(center(frame(oversized)), QColor(Qt::white));
        map.setMetricsVisible(true);
        auto* overlay=map.findChild<QLabel*>("mapPerformanceOverlay");QVERIFY(overlay);
        QVERIFY(overlay->text().contains("65 / 64 !"));QVERIFY(overlay->text().contains("#ffbf69"));
        map.setMetricsVisible(false);
        // Repeated resident motion must update vertices/uniforms only.
        frame({floor,light,lighting});
        map.setMetricsVisible(true);
        QVERIFY(!overlay->text().contains("#ffbf69")); // previous warnings do not stick
        map.setMetricsVisible(false);
        const auto now = MapMetrics::nowNs(); metrics->reset(now + 60'000'000'000);
        for (int x = 0; x < 8; ++x) { light.position = {x,0}; frame({floor,light,lighting}); }
        QCOMPARE(metrics->snapshot(now + 61'000'000'000).uploadMBPerSecond, 0.0);
        const auto before = map.grabFramebuffer(); map.recreate = true;
        QCOMPARE(map.grabFramebuffer(), before);
        MapWidget replay; if (openGL) replay.setApi(QRhiWidget::Api::OpenGL);
        replay.resize(map.size()); replay.setViewSize({1,1}); replay.setMailbox(mailbox);
        replay.show(); QVERIFY(QTest::qWaitForWindowExposed(&replay));
        QCOMPARE(replay.grabFramebuffer(), before);
    }
    void rgbaResetPixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void rgbaResetPixels() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool, openGL);
        fugue::model::World w;
        w.bounds = {true,20,20,0,3,3,3,3}; w.resync.haveOrigin = true;
        w.resources = {{123,0}};
        auto& root = w.appearances[1]; root.fields.iconResID = 0; root.fields.layerBits = std::bit_cast<uint32_t>(3.f);
        setFields(root, R"({"hasColor":true,"colorR":128,"colorG":64,"colorB":32,"colorA":128,"underlays":[2],"overlays":[3]})");
        for (uint32_t id : {2u,3u}) { w.appearances[id] = root; w.appearances[id].fields.layerBits = std::bit_cast<uint32_t>(-1.f); }
        auto& atom = w.atoms[42]; atom.appid = 1; atom.hasPos = true; atom.x = atom.y = 2;
        atom.haveKey = true; atom.key = 7;
        QImage icon(32,32,QImage::Format_RGBA8888); icon.fill(Qt::white);
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); QVERIFY(icon.save(&buffer,"PNG"));
        auto read = [&](uint32_t, QByteArray& out) { out = bytes; return true; };
        TurfMap terrain; EntityMap scene;
        auto geometry = terrain.prepare(w,{},0,false);
        MapWidget map; if (openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(192,192); map.setViewSize({3,3});
        auto metrics = std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox = std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        std::vector<MapPointer> events;
        map.setInputHandler("map",true,[&](const auto& p) { events.push_back(p); });
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto publish = [&] {
            const auto sprites = scene.prepare(w,geometry,read,0,nullptr,true);
            mailbox->publish(geometry.image,geometry.source,0,0,geometry.geometry,sprites);
        };
        auto pixel = [](const QImage& image, int x) { return image.pixelColor(image.width()*x/96,image.height()/2); };
        auto near = [](QColor a, QColor b) { return qAbs(a.red()-b.red())<=1 && qAbs(a.green()-b.green())<=1 && qAbs(a.blue()-b.blue())<=1; };
        for (uint32_t flags : {0u,0x400000u,0x800000u,0xc00000u}) {
            for (uint32_t id : {2u,3u})
                setFields(w.appearances[id], "{\"appearanceFlags\":" + std::to_string(flags) +
                    ",\"pixelX\":" + std::to_string(id==2 ? -32 : 32) +
                    R"(,"hasColor":true,"colorR":64,"colorG":128,"colorB":255,"colorA":64})");
            publish(); const auto image = map.grabFramebuffer(); QVERIFY(!image.isNull());
            const float alpha = (flags & 0x800000 ? 1.f : 128.f/255) * 64.f/255;
            const QColor expected(qRound(64 * (flags & 0x400000 ? 1.f : 128.f/255) * alpha),
                qRound(128 * (flags & 0x400000 ? 1.f : 64.f/255) * alpha),
                qRound(255 * (flags & 0x400000 ? 1.f : 32.f/255) * alpha));
            QVERIFY(near(pixel(image,16),expected)); QVERIFY(near(pixel(image,80),expected));
            QVERIFY(near(pixel(image,48),QColor(64,32,16)));
        }
        // Inheritance-only group boundary: these non-overlapping parts do not
        // assert unsupported KEEP_TOGETHER overlap/composited-alpha parity.
        setFields(root, fieldsJson(root).insert(1,"\"appearanceFlags\":67108864,"));
        publish(); auto grouped = map.grabFramebuffer();
        QVERIFY(near(pixel(grouped,80),QColor(4,4,4)));
        setFields(w.appearances[3], R"({"appearanceFlags":146800640,"pixelX":32,"hasColor":true,"colorR":64,"colorG":128,"colorB":255,"colorA":64})");
        publish(); auto apart = map.grabFramebuffer();
        QVERIFY(near(pixel(apart,80),QColor(16,32,64)));
        QVERIFY(near(pixel(apart,16),QColor(4,4,4)));
        // Alpha-zero ancestry must not cull a reset descendant or leave picking
        // ahead of the last rendered frame. Underlay is hidden; overlay revives.
        setFields(root, R"({"hasColor":true,"colorR":255,"colorG":255,"colorB":255,"colorA":0,"underlays":[2],"overlays":[3]})");
        setFields(w.appearances[2], R"({"pixelX":-32})");
        setFields(w.appearances[3], R"({"appearanceFlags":8388608,"pixelX":32})");
        publish(); auto image = map.grabFramebuffer();
        QCOMPARE(pixel(image,16),QColor(Qt::black)); QCOMPARE(pixel(image,48),QColor(Qt::black));
        QCOMPARE(pixel(image,80),QColor(Qt::white));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{160,96});
        QCOMPARE(events.back().click.over.ref,uint32_t(42));
        const auto now = MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        for (int x=1; x<=4; ++x) { atom.pixelX = int16_t(-x); publish(); QVERIFY(!map.grabFramebuffer().isNull()); }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        setFields(w.appearances[3], R"({"pixelX":32})"); publish();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{160,96});
        QCOMPARE(events.back().click.over.ref,uint32_t(42)); // pending invisible scene not used for input
        image = map.grabFramebuffer(); QCOMPARE(pixel(image,80),QColor(Qt::black));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{160,96});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefObj));
        QCOMPARE(events.back().click.over.ref,42u); // root alpha never erases source-alpha coverage
    }
    void flickPixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void flickPixels() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool,openGL);
        fugue::model::World w;
        w.bounds={true,20,20,0,3,3,3,3}; w.resync.haveOrigin=true; w.resources={{123,0},{456,0}};
        auto& ap=w.appearances[1]; ap.fields.iconResID=0; setFields(ap, "{}");
        auto& atom=w.atoms[42]; atom.appid=1; atom.hasPos=true; atom.x=atom.y=2; atom.dir=4;
        atom.key=7; atom.haveKey=true;
        auto read=[](uint32_t id,QByteArray& out){out=iconfixture::flickAsset(id==456?64:32);return true;};
        TurfMap terrain; EntityMap scene; const auto geometry=terrain.prepare(w,{},0,false);
        MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(192,192); map.setViewSize({3,3});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        std::vector<MapPointer> events; map.setInputHandler("map",true,[&](const auto& p){events.push_back(p);});
        auto publish=[&](int64_t ns){mailbox->publish(geometry.image,geometry.source,0,0,geometry.geometry,scene.prepare(w,geometry,read,ns));};
        auto pixel=[](const QImage& im,int x,int y){return im.pixelColor(x*im.width()/192,y*im.height()/192);};
        publish(1000000000); map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto image=map.grabFramebuffer(); QCOMPARE(pixel(image,160,32),QColor(Qt::black));
        w.flicks[42]={"pulse",1,true,1000000000,7,true};
        publish(1000000000); image=map.grabFramebuffer(); QCOMPARE(pixel(image,160,32),QColor(50,70,20));
        const auto first=scene.prepare(w,geometry,read,1000000000);
        publish(1050000000); // pending frame's alpha hole must not affect picking yet
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{72,32});
        QCOMPARE(events.back().click.over.ref,uint32_t(42));
        image=map.grabFramebuffer(); QCOMPARE(pixel(image,72,32),QColor(Qt::black));
        QCOMPARE(pixel(image,160,32),QColor(130,70,20));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{72,32});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
        auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        publish(1200000000); image=map.grabFramebuffer(); QCOMPARE(pixel(image,160,32),QColor(210,70,20));
        QVERIFY(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond>0); // first-use cell
        publish(1400000000); // completed bounds are pending
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{160,32});
        QCOMPARE(events.back().click.over.ref,uint32_t(42));
        image=map.grabFramebuffer(); QCOMPARE(pixel(image,160,32),QColor(Qt::black));
        QCOMPARE(pixel(image,96,96),QColor(180,180,180));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{160,32});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
        now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        for(int cycle=0;cycle<3;++cycle) {
            w.flicks[42].startNs=2000000000+cycle*500000000LL;
            for(int64_t phase:{0LL,50000000LL,200000000LL,400000000LL}) {
                atom.pixelX=int16_t(cycle); publish(w.flicks[42].startNs+phase);
                QVERIFY(!map.grabFramebuffer().isNull());
            }
        }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        QCOMPARE(first[0].image.pixelColor(32,0),QColor(50,70,20));
    }
    void animationPixels_data() {
        QTest::addColumn<bool>("openGL"); QTest::addColumn<bool>("opacityMob");
        QTest::newRow("native-default") << false << false; QTest::newRow("OpenGL") << true << false;
        QTest::newRow("native-default-opacity-mob") << false << true; QTest::newRow("OpenGL-opacity-mob") << true << true;
    }
    void animationPixels() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool,openGL);
        fugue::model::World w;
        w.bounds={true,20,20,0,1,1,1,1}; w.resync.haveOrigin=true; w.resources={{123,0}};
        QFETCH(bool,opacityMob);
        w.resources.push_back({456,12});
        auto& ap=w.appearances[1]; ap.fields.iconResID=opacityMob ? 1 : 0; ap.fields.iconState="absent"; setFields(ap, "{}");
        auto& atom=w.atoms[42 | (opacityMob ? fugue::model::kRegistryA : 0)]; atom.appid=1; atom.hasPos=true; atom.x=atom.y=1; atom.dir=4;
        bool ready=false;
        auto read=[&](uint32_t id,QByteArray& out){out=id==456 ? opacityfixture::recipe(0) : iconfixture::animation();return ready;};
        TurfMap terrain; EntityMap scene;
        const auto geometry=terrain.prepare(w,{},0,false);
        QVERIFY(scene.prepare(w,geometry,read,50000000).isEmpty()); ready=true;
        const auto first=scene.prepare(w,geometry,read,1);
        MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        // At forced DPR=1 on a 150% Windows desktop, the native title bar forces
        // a 176px minimum width. These pixel fixtures require their exact extent.
        map.setWindowFlag(Qt::FramelessWindowHint);
        map.resize(96,96); map.setViewSize({1,1});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        std::vector<MapPointer> events; map.setInputHandler("map",true,[&](const auto& p){events.push_back(p);});
        auto publish=[&](int64_t ns){mailbox->publish(geometry.image,geometry.source,0,0,geometry.geometry,scene.prepare(w,geometry,read,ns));};
        auto pixel=[](const QImage& im,int x){return im.pixelColor(x*im.width()/96,im.height()/2);};
        publish(1); map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto image=map.grabFramebuffer(); QCOMPARE(pixel(image,12),QColor(50,70,20));
        publish(50000000); // newer transparent cell is pending, not rendered
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{12,48});
        QCOMPARE(events.back().click.over.ref,uint32_t(42));
        QCOMPARE(events.back().click.over.kind,uint8_t(opacityMob ? fugue::proto::enc::RefMob : fugue::proto::enc::RefObj));
        image=map.grabFramebuffer(); QCOMPARE(pixel(image,12),QColor(Qt::black));
        QCOMPARE(pixel(image,48),QColor(130,70,20));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{12,48});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
        auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        publish(200000000); image=map.grabFramebuffer(); QCOMPARE(pixel(image,48),QColor(210,70,20));
        QVERIFY(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond>0); // legitimate first use
        now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        for(int cycle=1;cycle<4;++cycle) for(int64_t phase:{1LL,50000000LL,200000000LL}) {
            atom.pixelX=int16_t(cycle); publish(cycle*400000000LL+phase);
            QVERIFY(!map.grabFramebuffer().isNull());
        }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        QCOMPARE(first[0].image.pixelColor(16,0),QColor(50,70,20));
        EntityMap::Stats stats;
        auto stable=scene.prepare(w,geometry,read,1800000000,&stats);
        QCOMPARE(scene.prepare(w,geometry,read,1810000000,&stats).constData(),stable.constData());
        QCOMPARE(stats.templateBuilds,uint64_t(0)); QCOMPARE(stats.sortedSprites,uint64_t(0));
        // Recovered animate() changes instance geometry/RGBA, not resident image
        // contents. Exercise the normal mailbox and both native QRhi backends.
        w.appearances[2]=ap;
        setFields(w.appearances[2], affinefixture::fields({1.5f,0,0,0,1.5f,0},
            ",\"pixelX\":8,\"hasColor\":true,\"colorR\":255,\"colorG\":255,\"colorB\":255,\"colorA\":128"));
        w.appearances[2].fields.hasPixelOffsets=true;
        atom.appid=2; atom.pixelX=8; atom.animating=true;
        atom.chain={2'000'000'000,100'000'000,{{1,2,std::bit_cast<uint32_t>(10.f),0,0,1,2'000'000'000}}};
        now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        publish(2'000'000'000); image=map.grabFramebuffer();
        QCOMPARE(pixel(image,48),QColor(50,70,20));
        publish(2'400'000'000); image=map.grabFramebuffer();
        const auto faded=pixel(image,48);
        QVERIFY(faded.red()<50 && faded.red()>30); QVERIFY(faded.green()<70 && faded.green()>45);
        for(int i=5;i<10;++i) { publish(2'000'000'000+i*100'000'000LL); QVERIFY(!map.grabFramebuffer().isNull()); }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        QCOMPARE(first[0].image.pixelColor(16,0),QColor(50,70,20));
    }
    void generatedIconPixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void generatedIconPixels() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool,openGL);
        fugue::model::World w;
        w.bounds = {true,20,20,0,1,1,1,1}; w.resync.haveOrigin = true;
        w.resources = {{100,12},{200,3}};
        auto& ap = w.appearances[1]; ap.fields.iconResID = 0; ap.fields.iconState = "old-state";
        setFields(ap, "{}"); ap.fields.layerBits = std::bit_cast<uint32_t>(5.f);
        auto& atom = w.atoms[42]; atom.appid = 1; atom.hasPos = true; atom.x = atom.y = 1; atom.dir = 8;
        std::map<uint32_t,QByteArray> data{{100,iconfixture::masked(1)}};
        auto read = [&](uint32_t id,QByteArray& out) { if(!data.count(id)) return false; out=data.at(id); return true; };
        TurfMap terrain; EntityMap scene;
        const auto geometry = terrain.prepare(w,{},0,false);
        QVERIFY(scene.prepare(w,geometry,read).isEmpty());
        data[200] = iconfixture::sheet();
        const auto first=scene.prepare(w,geometry,read); QCOMPARE(first.size(),1);
        QCOMPARE(scene.prepare(w,geometry,read).constData(),first.constData());
        MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.setWindowFlag(Qt::FramelessWindowHint); // exact extent, independent of native title-bar minimum
        map.resize(96,96); map.setViewSize({1,1});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        std::vector<MapPointer> events;
        map.setInputHandler("map",true,[&](const auto& p){events.push_back(p);});
        auto publish=[&]{mailbox->publish(geometry.image,geometry.source,0,0,geometry.geometry,scene.prepare(w,geometry,read));};
        publish(); map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto image=map.grabFramebuffer(); QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(image.width()/4,image.height()/2),QColor(Qt::black));
        QCOMPARE(image.pixelColor(image.width()*3/4,image.height()/2),QColor(Qt::red));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{24,48});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{72,48});
        QCOMPARE(events.back().click.over.ref,uint32_t(42));
        const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        for(int x=1;x<=4;++x) {atom.pixelX=int16_t(x); publish(); QVERIFY(!map.grabFramebuffer().isNull());}
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        // New source mapping is pending; picking still uses the last rendered recipe.
        w.resources[1]={300,3}; publish();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{72,48});
        QCOMPARE(events.back().click.over.ref,uint32_t(42));
        image=map.grabFramebuffer(); QCOMPARE(image.pixelColor(image.width()*3/4,image.height()/2),QColor(Qt::black));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{72,48});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
        data[300]=iconfixture::sheet(Qt::blue); publish(); image=map.grabFramebuffer();
        QCOMPARE(image.pixelColor(image.width()*3/4,image.height()/2),QColor(Qt::blue));
        QCOMPARE(first[0].image.pixelColor(20,0),QColor(Qt::red));
    }
    void defaultStatePixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void defaultStatePixels() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool,openGL);
        fugue::model::World w;
        w.bounds = {true,20,20,0,1,1,1,1}; w.resync.haveOrigin = true;
        w.resources = {{123,0}};
        auto& ap = w.appearances[1]; ap.fields.iconResID = 0; ap.fields.iconState = "old-state-name";
        setFields(ap, "{}"); ap.fields.layerBits = std::bit_cast<uint32_t>(4.f);
        const uint32_t key = fugue::model::kRegistryA | 42;
        auto& atom = w.atoms[key]; atom.appid = 1; atom.hasPos = true; atom.x = atom.y = 1; atom.dir = 4;
        QImage sheet(128,32,QImage::Format_RGBA8888);
        const QColor colors[] = {Qt::red,Qt::green,Qt::blue,Qt::yellow};
        for (int y=0; y<32; ++y) for (int x=0; x<128; ++x) sheet.setPixelColor(x,y,colors[x/32]);
        sheet.setText("Description","width = 32\nheight = 32\nstate = \"\"\ndirs = 4\nframes = 1\n");
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); QVERIFY(sheet.save(&buffer,"PNG"));
        bool ready = false;
        auto read = [&](uint32_t, QByteArray& out) { out = bytes; return ready; };
        TurfMap terrain; EntityMap scene;
        const auto geometry = terrain.prepare(w,{},0,false);
        QVERIFY(scene.prepare(w,geometry,read).isEmpty());
        ready = true;
        const auto first = scene.prepare(w,geometry,read);
        QCOMPARE(first.size(),1); QCOMPARE(first[0].image.pixelColor(16,16),QColor(Qt::blue));
        QCOMPARE(scene.prepare(w,geometry,{}).constData(),first.constData());
        MapWidget map; if (openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(96,96); map.setViewSize({1,1});
        auto metrics = std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox = std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        std::vector<MapPointer> events;
        map.setInputHandler("map",true,[&](const auto& p) { events.push_back(p); });
        auto publish = [&] { mailbox->publish(geometry.image,geometry.source,0,0,geometry.geometry,scene.prepare(w,geometry,read)); };
        publish(); map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto image = map.grabFramebuffer(); QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(image.width()/2,image.height()/2),QColor(Qt::blue));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{48,48});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefMob));
        QCOMPARE(events.back().click.over.ref,uint32_t(42));
        const auto now = MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        for (int x=1; x<=4; ++x) { atom.pixelX = int16_t(x); publish(); QVERIFY(!map.grabFramebuffer().isNull()); }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        atom.dir = 1; publish(); image = map.grabFramebuffer();
        QCOMPARE(image.pixelColor(image.width()/2,image.height()/2),QColor(Qt::green));
    }
    void overlayDirectionPixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void overlayDirectionPixels() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        QFETCH(bool,openGL);
        fugue::model::World w;
        w.bounds = {true,20,20,0,3,3,3,3}; w.resync.haveOrigin = true;
        w.resources = {{123,0}};
        auto& ap = w.appearances[1]; ap.fields.iconResID = 0; ap.fields.layerBits = std::bit_cast<uint32_t>(4.f);
        setFields(ap, R"({"dir":2,"underlays":[2],"overlays":[3]})");
        for (uint32_t id : {2u,3u,4u}) { w.appearances[id] = ap; w.appearances[id].fields.layerBits = std::bit_cast<uint32_t>(-1.f); }
        setFields(w.appearances[2], R"({"dir":2,"pixelX":-32})");
        setFields(w.appearances[3], R"({"dir":1,"appearanceFlags":131588,"pixelX":32,"overlays":[4]})");
        setFields(w.appearances[4], R"({"dir":2,"pixelY":32})");
        auto& atom = w.atoms[fugue::model::kRegistryA | 42];
        atom.appid = 1; atom.hasPos = true; atom.x = atom.y = 2; atom.dir = 2;
        QImage sheet(128,32,QImage::Format_RGBA8888);
        const QColor colors[] = {Qt::red,Qt::green,Qt::blue,Qt::yellow};
        for (int y=0; y<32; ++y) for (int x=0; x<128; ++x) sheet.setPixelColor(x,y,colors[x/32]);
        sheet.setPixelColor(32+4,16,Qt::transparent); // a direction-specific picking hole in NORTH
        sheet.setText("Description","width = 32\nheight = 32\nstate = \"\"\ndirs = 4\nframes = 1\n");
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); QVERIFY(sheet.save(&buffer,"PNG"));
        auto read = [&](uint32_t, QByteArray& out) { out = bytes; return true; };
        TurfMap terrain; EntityMap scene;
        const auto geometry = terrain.prepare(w,{},0,false);
        MapWidget map; if (openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(192,192); map.setViewSize({3,3});
        auto metrics = std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox = std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        std::vector<MapPointer> events;
        map.setInputHandler("map",true,[&](const auto& p) { events.push_back(p); });
        auto publish = [&] { mailbox->publish(geometry.image,geometry.source,0,0,geometry.geometry,scene.prepare(w,geometry,read)); };
        auto pixel = [](const QImage& image,int x,int y) { return image.pixelColor(x*image.width()/96,y*image.height()/96); };
        publish(); map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        const int dirs[] = {2,1,4,8};
        for (int i=0; i<4; ++i) {
            atom.dir = dirs[i]; publish(); const auto image = map.grabFramebuffer(); QVERIFY(!image.isNull());
            QCOMPARE(pixel(image,48,48),colors[i]); // root uses atom direction
            QCOMPARE(pixel(image,16,48),colors[i]); // non-fixed SOUTH follows root
            QCOMPARE(pixel(image,80,48),QColor(Qt::green)); // fixed NORTH does not turn
            QCOMPARE(pixel(image,80,16),QColor(Qt::green)); // nested child follows its immediate parent
        }
        atom.dir = 2; publish(); QVERIFY(!map.grabFramebuffer().isNull());
        atom.dir = 1; publish();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{8,96});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefMob)); // old SOUTH frame
        const auto north = map.grabFramebuffer(); QCOMPARE(pixel(north,4,48),QColor(Qt::black));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{8,96});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf)); // new overlay alpha hole
        const auto now = MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        for (int x=1; x<=4; ++x) { atom.pixelX = int16_t(x); publish(); QVERIFY(!map.grabFramebuffer().isNull()); }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        atom.pixelX = 0;
        setFields(w.appearances[3], R"({"dir":2,"appearanceFlags":131588,"pixelX":32,"overlays":[4]})");
        publish(); const auto fixedSouth = map.grabFramebuffer();
        QCOMPARE(pixel(fixedSouth,48,48),QColor(Qt::green));
        QCOMPARE(pixel(fixedSouth,80,48),QColor(Qt::red)); QCOMPARE(pixel(fixedSouth,80,16),QColor(Qt::red));
    }
    void blacknessStockPixels_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void blacknessStockPixels() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native blackness pixels/input");
        QFETCH(bool,openGL);
        QFile evidence(QFINDTESTDATA("../../tests/data/darkness-world/blackness-observations.json"));
        QVERIFY(evidence.open(QIODevice::ReadOnly));
        auto cases=QJsonDocument::fromJson(evidence.readAll()).object()["cases"].toArray();
        QCOMPARE(cases.size(),42);
        QFile sourceEvidence(QFINDTESTDATA("../../tests/data/darkness-world/source-input-observations.json"));
        QVERIFY(sourceEvidence.open(QIODevice::ReadOnly));
        const auto sourceCases=QJsonDocument::fromJson(sourceEvidence.readAll()).object()["cases"].toArray();
        QCOMPARE(sourceCases.size(),14);
        for (const auto& c:sourceCases) cases.append(c);
        MapWidget map; if (openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.setWindowFlag(Qt::FramelessWindowHint); map.resize(288,288); map.setViewSize({9,9});
        auto metrics=std::make_shared<MapMetrics>(); auto mailbox=std::make_shared<MapMailbox>(metrics);
        map.setMetrics(metrics); map.setMailbox(mailbox);
        TurfMap terrain; EntityMap scene; MapVisibility visibility;
        const auto read=test::blacknessReader();
        auto w=test::blacknessWorld("ambient=1");
        MapSprites sprites;
        auto publish=[&] {
            auto f=terrain.prepare(w,{},0,false);
            f.geometry.visibility=visibility.prepare(w,MapVisibility::displayedCells(f.geometry.region,f.geometry.tile,f.source));
            f.geometry.session=1; sprites=scene.prepare(w,f,read,0,nullptr,true);
            mailbox->publish(f.image,f.source,0,0,f.geometry,sprites);
        };
        auto pixel=[](const QImage& f,int x,int y) {
            return f.pixelColor(int((x+.5)*f.width()/288),int((y+.5)*f.height()/288));
        };
        std::vector<MapPointer> clicks;
        map.setInputHandler("main.map",true,[&](const MapPointer& p) { if (p.kind==MapPointer::Click) clicks.push_back(p); });
        publish(); map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        QVERIFY(!map.grabFramebuffer().isNull());
        for (const auto& value:cases) {
            const auto c=value.toObject(); const auto name=c["name"].toString();
            w=test::blacknessWorld(c["config"].toString()); publish(); const auto image=map.grabFramebuffer();
            QVERIFY(!image.isNull());
            for (const auto& v:c["samples"].toArray()) {
                const auto s=v.toArray(); const auto actual=pixel(image,s[0].toInt(),s[1].toInt());
                const QColor expected(s[2].toInt(),s[3].toInt(),s[4].toInt());
                // Existing RGBA8 backend rounding tolerance; native production
                // captures additionally compare every normalized MAP pixel exactly.
                QVERIFY2(qAbs(actual.red()-expected.red())<=1 && qAbs(actual.green()-expected.green())<=1 &&
                    qAbs(actual.blue()-expected.blue())<=1,qPrintable(name+QString(" @%1,%2 expected %3 actual %4")
                        .arg(s[0].toInt()).arg(s[1].toInt()).arg(expected.name()).arg(actual.name())));
            }
            for (const auto& sprite:sprites) if (sprite.kind==MapSprite::Blackness) {
                QVERIFY(sprite.image.isNull()); QCOMPARE(sprite.atom,0u); QVERIFY(sprite.mouseTransparent);
            }
            for (const auto& v:c["clicks"].toArray()) {
                const auto observation=v.toObject(); const auto point=observation["point"].toArray();
                const auto expected=observation["events"].toString(); const auto before=clicks.size();
                QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{point[0].toInt(),point[1].toInt()});
                if (expected.isEmpty()) { QVERIFY2(clicks.size()==before,qPrintable(name)); continue; }
                QVERIFY2(clicks.size()==before+1,qPrintable(name));
                const auto& hit=clicks.back(); QVERIFY(mapPointerCurrent(hit,w,1));
                if (expected.contains("/obj/target")) QCOMPARE(hit.click.over.ref,42u);
                else if (expected.contains("/turf/floor")) QCOMPARE(hit.click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
                else if (expected.contains("/mob")) QCOMPARE(hit.click.over.kind,uint8_t(fugue::proto::enc::RefMob));
                else { QCOMPARE(hit.click.over.ref,50u); QVERIFY(hit.screen); }
            }
        }
        // A pending capture cannot change the last rendered turf hit. Once the
        // hidden producer is presented, only independent world/HUD targets hit.
        w=test::blacknessWorld("ambient=1&targetx=6&plane=1&hudplane=10");
        publish(); map.grabFramebuffer();
        auto captureClicks=clicks.size();
        w=test::blacknessWorld("ambient=1&master=target&targetx=6&plane=1&hudplane=10"); publish();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{176,112}); QCOMPARE(clicks.size(),captureClicks+1);
        map.grabFramebuffer();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{176,112}); QCOMPARE(clicks.size(),captureClicks+1);
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{176,144}); QCOMPARE(clicks.back().click.over.ref,42u);
        QVERIFY(mapPointerCurrent(clicks.back(),w,1));
        ++w.atoms[42].renderIdentity; QVERIFY(!mapPointerCurrent(clicks.back(),w,1)); --w.atoms[42].renderIdentity;
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{272,16}); QCOMPARE(clicks.back().click.over.ref,50u);
        captureClicks=clicks.size();
        w=test::blacknessWorld("ambient=1&targetx=6&plane=1&hudplane=10"); publish();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{176,112}); QCOMPARE(clicks.size(),captureClicks);
        map.grabFramebuffer();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{176,112}); QCOMPARE(clicks.size(),captureClicks+1);
        const QString base="ambient=0&see=2&targetx=6&wide=1&backdrop=1&black=1";
        w=test::blacknessWorld(base); publish(); map.grabFramebuffer();
        auto before=clicks.size();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{200,144}); QCOMPARE(clicks.size(),before+1);
        QVERIFY(mapPointerCurrent(clicks.back(),w,1));
        ++w.atoms[42].renderIdentity; QVERIFY(!mapPointerCurrent(clicks.back(),w,1)); --w.atoms[42].renderIdentity;
        before=clicks.size();
        w.sight|=16; publish(); // pending category admission cannot change the rendered background
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{240,144}); QCOMPARE(clicks.size(),before);
        QCOMPARE(pixel(map.grabFramebuffer(),240,144),QColor(Qt::black));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{240,144}); QCOMPARE(clicks.size(),before+1);
        QCOMPARE(clicks.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
        w.sight=0x400; publish();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{240,144}); QCOMPARE(clicks.size(),before+2);
        map.grabFramebuffer();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{240,144}); QCOMPARE(clicks.size(),before+2);
        // Lighting, whole-cell movement, offset movement and master passes reuse
        // resident icon bytes. Coverage is bounded row geometry, never a mask image.
        w=test::blacknessWorld(base+"&master=alpha"); publish(); map.grabFramebuffer();
        const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        for (int i=0;i<8;++i) {
            w=test::blacknessWorld(base+"&master=alpha"); w.seeInDark=uint8_t(1+i%3);
            setLuminosity(w.appearances[100], uint8_t(i%2));
            w.cellObjs.clear(); auto& a=w.atoms[42]; a.x=5+i%2; a.pixelX=i;
            w.cellObjs[{a.x,a.y,0}]={42}; w.cellObjs[{3,5,0}].push_back(43);
            publish(); map.grabFramebuffer();
        }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
    }
    void visibilitySceneAndRenderedInput() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native visibility composition and input");
        fugue::model::World w;
        w.bounds={true,7,7,0,5,5,5,5,5,5}; w.resync.haveOrigin=true;
        w.haveEye=w.haveSight=w.haveSeeInDark=true; w.eyeX=w.eyeY=3;
        w.resources={{1,0},{2,0},{3,0},{4,0}};
        for (uint32_t id=1;id<=4;++id) {
            auto& a=w.appearances[id]; setFields(a, "{}"); setOpaque(a, id==2);
            a.fields.iconResID=id-1; a.fields.layerBits=std::bit_cast<uint32_t>(id<=2?2.f:3.f);
        }
        setFields(w.appearances[4], R"({"screenLoc":"NORTHEAST","hasPlane":true,"plane":20})");
        w.hudOrder={50}; w.hudAppid[50]=4; w.hudIdentity[50]={2,50};
        setLuminosity(w.appearances[100], 1); w.turfBinds[0]=100;
        for (int y=0;y<7;++y) for (int x=0;x<7;++x) {
            const std::array<uint16_t,3> cell{uint16_t(x),uint16_t(y),0};
            w.tiles[cell]=x==3?2:1; w.areaCells[cell]=0;
        }
        auto& a=w.atoms[42]; a.hasPos=true; a.x=5; a.y=3; a.appid=3; a.haveKey=true; a.key=7; a.renderIdentity=10;
        w.cellObjs[{5,3,0}]={42};
        auto read=[](uint32_t id,QByteArray& out) {
            QImage image(32,32,QImage::Format_RGBA8888);
            image.fill(id==1?Qt::blue:id==2?Qt::white:id==3?Qt::red:Qt::green);
            QBuffer b(&out); b.open(QIODevice::WriteOnly); return image.save(&b,"PNG");
        };
        MapWidget map; map.setWindowFlag(Qt::FramelessWindowHint); map.resize(320,320); map.setViewSize({5,5});
        auto metrics=std::make_shared<MapMetrics>(); auto mailbox=std::make_shared<MapMailbox>(metrics);
        map.setMetrics(metrics); map.setMailbox(mailbox);
        TurfMap terrain; EntityMap scene; MapVisibility visibility;
        auto publish=[&] {
            auto f=terrain.prepare(w,{},0,false); f.geometry.visibility=visibility.prepare(w); f.geometry.session=1;
            const auto sprites=scene.prepare(w,f,read,0,nullptr,true);
            mailbox->publish(f.image,f.source,0,0,f.geometry,sprites);
        };
        std::vector<MapPointer> clicks, gestures;
        map.setInputHandler("main.map",true,[&](const MapPointer& p) {
            if(p.kind==MapPointer::Click) clicks.push_back(p);
            if(p.kind==MapPointer::Drag || p.kind==MapPointer::Drop) gestures.push_back(p);
        });
        auto pixel=[](const QImage& f,int x,int y) { return f.pixelColor(x*f.width()/320,y*f.height()/320); };
        publish(); map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto image=map.grabFramebuffer(); QVERIFY(!image.isNull());
        QCOMPARE(pixel(image,288,160),QColor(Qt::black)); QCOMPARE(pixel(image,224,160),QColor(Qt::white));
        QCOMPARE(pixel(image,160,160),QColor(Qt::blue)); QCOMPARE(pixel(image,288,32),QColor(Qt::green));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{288,160}); QVERIFY(clicks.empty());
        w.sight=8; publish(); // pending reveal must not change picking
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{288,160}); QVERIFY(clicks.empty());
        image=map.grabFramebuffer(); QCOMPARE(pixel(image,288,160),QColor(Qt::red));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{288,160}); QCOMPARE(clicks.size(),size_t(1));
        QCOMPARE(clicks.back().click.over.ref,uint32_t(42));
        QVERIFY(mapPointerCurrent(clicks.back(),w,1)); a.renderIdentity=11; QVERIFY(!mapPointerCurrent(clicks.back(),w,1)); a.renderIdentity=10;
        w.sight=0; publish(); QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{288,160}); QCOMPARE(clicks.size(),size_t(2));
        image=map.grabFramebuffer(); QCOMPARE(pixel(image,288,160),QColor(Qt::black));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{288,160}); QCOMPARE(clicks.size(),size_t(2));
        const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        for (int i=0;i<6;++i) { w.tiles[{3,2,0}]=i%2?2:1; publish(); map.grabFramebuffer(); }
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        QTest::mousePress(&map,Qt::LeftButton,Qt::NoModifier,{160,160});
        QMouseEvent motion(QEvent::MouseMove,QPointF(288,160),QPointF(map.mapToGlobal(QPoint(288,160))),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&map,&motion);
        QTest::mouseRelease(&map,Qt::LeftButton,Qt::NoModifier,{288,160});
        QCOMPARE(gestures.size(),size_t(2));
        QCOMPARE(gestures[0].kind,MapPointer::Drag); QCOMPARE(gestures[1].kind,MapPointer::Drop);
        for (const auto& gesture:gestures) {
            QVERIFY(gesture.backgroundHidden); QCOMPARE(gesture.click.over.kind,uint8_t(fugue::proto::enc::RefNull));
            QCOMPARE(gesture.click.screenX,5); QVERIFY(gesture.source);
        }
        // The same upstream admission applies before offscreen plane targets and consumers.
        setFields(w.appearances[3], R"({"hasPlane":true,"plane":1})");
        setFields(w.appearances[5], R"({"appearanceFlags":268435456,"hasPlane":true,"plane":1,"renderTarget":"*visibility"})");
        setFields(w.appearances[6], R"({"screenLoc":"SOUTHWEST","hasPlane":true,"plane":2,"renderSource":"*visibility"})");
        w.hudOrder={50,51,52}; w.hudAppid[51]=5; w.hudAppid[52]=6;
        publish(); image=map.grabFramebuffer(); QCOMPARE(pixel(image,288,160),QColor(Qt::black));
        w.sight=8; publish(); image=map.grabFramebuffer(); QCOMPARE(pixel(image,288,160),QColor(Qt::red));
        w.sight=0; publish(); image=map.grabFramebuffer(); QCOMPARE(pixel(image,288,160),QColor(Qt::black));
        QCOMPARE(pixel(image,288,32),QColor(Qt::green));
        // Ambient/dark-vision transitions use the same immutable rendered-frame
        // picking contract; warmed illumination changes upload no texture bytes.
        w.hudOrder={50}; setFields(w.appearances[3], "{}");
        for (auto& [cell,app]:w.tiles) app=1;
        setLuminosity(w.appearances[100], 0); w.seeInDark=1;
        publish(); image=map.grabFramebuffer();
        QCOMPARE(pixel(image,160,160),QColor(Qt::blue)); QCOMPARE(pixel(image,288,160),QColor(Qt::black));
        QCOMPARE(pixel(image,288,32),QColor(Qt::green));
        auto previousClicks=clicks.size();
        setLuminosity(w.appearances[100], 1); publish();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{288,160}); QCOMPARE(clicks.size(),previousClicks);
        image=map.grabFramebuffer(); QCOMPARE(pixel(image,288,160),QColor(Qt::red));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{288,160}); QCOMPARE(clicks.size(),previousClicks+1);
        setLuminosity(w.appearances[100], 0); publish();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{288,160}); QCOMPARE(clicks.size(),previousClicks+2);
        image=map.grabFramebuffer(); QCOMPARE(pixel(image,288,160),QColor(Qt::black));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{288,160}); QCOMPARE(clicks.size(),previousClicks+2);
        const auto lightNow=MapMetrics::nowNs(); metrics->reset(lightNow+60'000'000'000);
        for (int i=0;i<6;++i) { setLuminosity(w.appearances[100], uint8_t(i%2)); publish(); map.grabFramebuffer(); }
        QCOMPARE(metrics->snapshot(lightNow+61'000'000'000).uploadMBPerSecond,0.0);
    }
    void sceneLastRenderedPicking() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        MapWidget map; map.resize(96,96); map.setViewSize({1,1});
        auto mailbox = std::make_shared<MapMailbox>(std::make_shared<MapMetrics>()); map.setMailbox(mailbox);
        QImage black(32,32,QImage::Format_RGBA8888); black.fill(Qt::black);
        MapSprite entity; entity.image = black; entity.image.fill(Qt::red); entity.atom = 42; entity.cell = {1,1};
        entity.iconBottom = {0,32};
        MapSprite turf = entity; turf.image = black; turf.image.fill(Qt::green); turf.kind = MapSprite::Turf; turf.plane = 1;
        MapGeometry geometry{{0,0,1,1},{20,20},32,0,1};
        std::vector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p) { events.push_back(p); });
        mailbox->publish(black,black.rect(),0,0,geometry,{entity,turf});
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map)); QVERIFY(!map.grabFramebuffer().isNull());
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{48,48});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
        MapSprite hide; hide.kind = MapSprite::Master; hide.plane = 1; hide.tint[3] = 0;
        mailbox->publish(black,black.rect(),0,0,geometry,{entity,turf,hide});
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{48,48});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
        const auto frame = map.grabFramebuffer(); QCOMPARE(frame.pixelColor(frame.width()/2,frame.height()/2), QColor(Qt::red));
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{48,48});
        // Stock master alpha affects pixels, not the group's original hit tree.
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
        auto show = hide; show.tint[3] = 1;
        mailbox->publish(black,black.rect(),0,0,geometry,{entity,turf,hide,show});
        QVERIFY(!map.grabFramebuffer().isNull());
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{48,48});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
    }
    void cameraOnlyFramesDoNotUpload() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        MapWidget map;
        map.resize(96, 96);
        map.setViewSize(QSize(1, 1));
        QImage terrain(96, 32, QImage::Format_RGBA8888);
        terrain.fill(Qt::red);
        for (int y = 0; y < 32; ++y) for (int x = 32; x < 96; ++x)
            terrain.setPixelColor(x, y, x < 64 ? Qt::green : Qt::blue);
        auto metrics = std::make_shared<MapMetrics>();
        map.setMetrics(metrics);
        auto mailbox = std::make_shared<MapMailbox>(metrics);
        map.setMailbox(mailbox);
        QVERIFY(mailbox->publish(terrain, QRect(0, 0, 32, 32), 0, 0));
        mailbox->acknowledge();
        map.requestMapFrame();
        map.show();
        QVERIFY(QTest::qWaitForWindowExposed(&map));
        QImage initial = map.grabFramebuffer();
        QVERIFY(!initial.isNull());
        QCOMPARE(initial.pixelColor(initial.width() / 2, initial.height() / 2), QColor(Qt::red));
        const int64_t now = MapMetrics::nowNs();
        metrics->reset(now + 60'000'000'000); // avoid the real-time overlay timer consuming this test window
        for (int x = 1; x <= 32; ++x) {
            mailbox->publish(terrain, QRect(x, 0, 32, 32), 0, 0);
            // Deliberately do not handle GUI notifications: render must pull the latest rectangle.
            QImage frame = map.grabFramebuffer();
            QVERIFY(!frame.isNull());
        }
        QImage final = map.grabFramebuffer();
        QCOMPARE(final.pixelColor(final.width() / 2, final.height() / 2), QColor(Qt::green));
        auto s = metrics->snapshot(now + 61'000'000'000);
        QCOMPARE(s.uploadMBPerSecond, 0.0); // 32 glide frames: only the uniform buffer changed
        // grabFramebuffer performs a GPU render/readback without a presentation signal.
        // A terrain replacement still reaches the GPU.
        terrain.fill(Qt::blue);
        mailbox->publish(terrain, QRect(32, 0, 32, 32), 0, 0);
        final = map.grabFramebuffer();
        QCOMPARE(final.pixelColor(final.width() / 2, final.height() / 2), QColor(Qt::blue));
        QVERIFY(metrics->snapshot(now + 62'000'000'000).uploadMBPerSecond > 0);
    }
    void staticAffineSprites_data() {
        QTest::addColumn<bool>("gl"); QTest::addColumn<bool>("mob");
        QTest::newRow("native-default-object") << false << false; QTest::newRow("OpenGL-object") << true << false;
        QTest::newRow("native-default-mob") << false << true; QTest::newRow("OpenGL-mob") << true << true;
    }
    void staticAffineSprites() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires native affine quad pixels");
        QFETCH(bool, gl); QFETCH(bool, mob);
        MapWidget map; if (gl) map.setApi(QRhiWidget::Api::OpenGL);
        map.setWindowFlag(Qt::FramelessWindowHint); map.resize(96,96); map.setViewSize({3,3});
        auto metrics=std::make_shared<MapMetrics>(); auto mailbox=std::make_shared<MapMailbox>(metrics);
        map.setMetrics(metrics); map.setMailbox(mailbox);
        QImage terrain(96,96,QImage::Format_RGBA8888); terrain.fill(Qt::white);
        MapGeometry geometry{QRect(0,0,3,3),QSize(3,3),32,0,1};
        MapSprite sprite; sprite.image=QImage(32,32,QImage::Format_RGBA8888); sprite.image.fill(Qt::red);
        for(int y=0;y<32;++y) for(int x=16;x<32;++x) sprite.image.setPixelColor(x,y,Qt::blue);
        sprite.position={32,32}; sprite.iconBottom={32,64}; sprite.atom=42 | (mob ? 0x80000000u : 0); sprite.cell={2,2}; sprite.targetIdentity=71;
        auto publish=[&] { mailbox->publish(terrain,terrain.rect(),0,0,geometry,{sprite}); };
        std::vector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p) { events.push_back(p); });
        publish(); map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map)); QVERIFY(!map.grabFramebuffer().isNull());
        const int64_t now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
        sprite.affine=StaticAffine::make({1,0,40,0,1,0},{32,32}); publish();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{40,40});
        QVERIFY(!events.empty()); QCOMPARE(events.back().click.over.ref,uint32_t(42)); // not rendered yet
        QCOMPARE(events.back().click.over.kind,uint8_t(mob ? fugue::proto::enc::RefMob : fugue::proto::enc::RefObj));
        map.grabFramebuffer(); QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{40,40});
        QCOMPARE(events.back().click.over.kind,uint8_t(fugue::proto::enc::RefTurf));
        const std::array<std::array<double,6>,7> matrices{{
            {1,0,0,0,1,0}, {1.5,0,0,0,.75,0}, {0,-1,0,1,0,0}, {1,0,9,0,1,5},
            {-1,0,0,0,1,0}, {1,.5,0,0,1,0}, {.8,-.6,0,.6,.8,0}}};
        int checked=0;
        for(const auto& matrix:matrices) {
            sprite.affine=StaticAffine::make(matrix,{32,32}); QVERIFY(sprite.affine); publish();
            const auto frame=map.grabFramebuffer(); QVERIFY(!frame.isNull());
            for(int y=16;y<80;++y) for(int x=16;x<80;++x) {
                const int px=x*frame.width()/map.width(), py=y*frame.height()/map.height();
                const QPointF sample((px+.5)*map.width()/frame.width()-32,(py+.5)*map.height()/frame.height()-32);
                const auto src=sprite.affine->inverse.map(sample);
                // Exclude raster/sampling edge ties, not bounding-box corners.
                if(std::min({std::abs(src.x()),std::abs(src.x()-16),std::abs(src.x()-32),std::abs(src.y()),std::abs(src.y()-32)})<1) continue;
                const QColor expected=src.x()<0 || src.y()<0 || src.x()>=32 || src.y()>=32 ? Qt::white : src.x()<16 ? Qt::red : Qt::blue;
                QCOMPARE(frame.pixelColor(px,py),expected); ++checked;
            }
        }
        QVERIFY(checked>1000);
        QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
    }
    void entitySprites() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        class TestMap : public MapWidget {
        public:
            bool recreate = false;
            void render(QRhiCommandBuffer* cb) override {
                if (recreate) { releaseResources(); initialize(cb); recreate = false; }
                MapWidget::render(cb);
            }
        } map;
        map.setWindowFlag(Qt::FramelessWindowHint); // exact extent at forced DPR=1 too
        map.resize(160, 96); map.setViewSize({3, 3});
        auto metrics = std::make_shared<MapMetrics>();
        auto mailbox = std::make_shared<MapMailbox>(metrics);
        map.setMetrics(metrics); map.setMailbox(mailbox);
        QImage terrain(96, 96, QImage::Format_RGBA8888); terrain.fill(Qt::white);
        MapGeometry geometry{QRect(0, 0, 3, 3), QSize(20, 20), 32, 0, 1};
        MapSprite sprite;
        sprite.image = QImage(32, 32, QImage::Format_RGBA8888); sprite.image.fill(Qt::red);
        for (int y = 16; y < 32; ++y) for (int x = 0; x < 32; ++x) sprite.image.setPixelColor(x, y, Qt::blue);
        sprite.image.setPixelColor(0, 0, Qt::transparent);
        sprite.position = {32, 32}; sprite.iconBottom = {32, 64}; sprite.cell = {2, 2}; sprite.atom = 42;
        MapSprites sprites{sprite};
        mailbox->publish(terrain, terrain.rect(), 0, 0, geometry, sprites);
        std::vector<MapPointer> events;
        map.setInputHandler("main.map", true, [&](const MapPointer& p) { events.push_back(p); });
        map.show();
        QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto pixel = [&](const QImage& frame, int x, int y) {
            return frame.pixelColor(x * frame.width() / map.width(), y * frame.height() / map.height());
        };
        auto frame = map.grabFramebuffer();
        QVERIFY(!frame.isNull());
        QCOMPARE(pixel(frame, 70, 38), QColor(Qt::red));
        QCOMPARE(pixel(frame, 70, 56), QColor(Qt::blue));
        QCOMPARE(pixel(frame, 64, 32), QColor(Qt::white));
        QCOMPARE(pixel(frame, 1, 40), QColor(Qt::black));
        QTest::mouseClick(&map, Qt::LeftButton, Qt::NoModifier, {70, 38});
        QCOMPARE(events.back().click.over.kind, uint8_t(fugue::proto::enc::RefObj));
        QCOMPARE(events.back().click.over.ref, uint32_t(42));
        // Publishing without rendering must not move the input geometry ahead of the pixels.
        sprites[0].position += QPoint(8, 0); sprites[0].iconBottom += QPoint(8, 0);
        mailbox->publish(terrain, terrain.rect(), 0, 0, geometry, sprites);
        QTest::mouseClick(&map, Qt::LeftButton, Qt::NoModifier, {70, 38});
        QCOMPARE(events.back().click.over.ref, uint32_t(42));
        const int64_t now = MapMetrics::nowNs();
        metrics->reset(now + 60'000'000'000);
        frame = map.grabFramebuffer();
        QCOMPARE(pixel(frame, 70, 38), QColor(Qt::white));
        for (int i = 0; i < 8; ++i) {
            sprites[0].position += QPoint(1, 0); sprites[0].iconBottom += QPoint(1, 0);
            mailbox->publish(terrain, terrain.rect(), 0, 0, geometry, sprites);
            frame = map.grabFramebuffer();
        }
        QCOMPARE(metrics->snapshot(now + 61'000'000'000).uploadMBPerSecond, 0.0);
        QCOMPARE(pixel(frame, 90, 38), QColor(Qt::red));
        // Alpha/tint are draw data too, with premultiplied output over opaque terrain.
        sprites[0].tint[3] = 0.5f;
        mailbox->publish(terrain, terrain.rect(), 0, 0, geometry, sprites);
        frame = map.grabFramebuffer();
        auto blended = pixel(frame, 90, 38);
        QCOMPARE(blended.red(), 255);
        QVERIFY(std::abs(blended.green() - 128) <= 1);
        QVERIFY(std::abs(blended.blue() - 128) <= 1);
        MapSprite cover = sprites[0];
        cover.image = QImage(32, 32, QImage::Format_RGBA8888); cover.image.fill(Qt::green);
        cover.tint = {1, 1, 1, 1};
        sprites.push_back(cover);
        mailbox->publish(terrain, terrain.rect(), 0, 0, geometry, sprites);
        frame = map.grabFramebuffer();
        QCOMPARE(pixel(frame, 90, 38), QColor(Qt::green));
        std::swap(sprites[0], sprites[1]);
        mailbox->publish(terrain, terrain.rect(), 0, 0, geometry, sprites);
        frame = map.grabFramebuffer();
        blended = pixel(frame, 90, 38);
        QVERIFY(std::abs(blended.red() - 128) <= 1);
        QVERIFY(std::abs(blended.green() - 128) <= 1);
        QCOMPARE(blended.blue(), 0);
        // GPU recreation reuploads the current retained sprites, without a new worker frame.
        map.recreate = true;
        auto restored = map.grabFramebuffer();
        QCOMPARE(pixel(restored, 90, 38), blended);
        {
            MapWidget replay;
            replay.resize(map.size()); replay.setViewSize({3, 3}); replay.setMailbox(mailbox);
            replay.show(); QVERIFY(QTest::qWaitForWindowExposed(&replay));
            auto retained = replay.grabFramebuffer();
            QCOMPARE(pixel(retained, 90, 38), blended);
        }
        sprites = {sprites.back()};
        // A sprite crossing the viewport edge must not draw into the letterbox.
        sprites[0].position = {-16, 32};
        mailbox->publish(terrain, terrain.rect(), 0, 0, geometry, sprites);
        frame = map.grabFramebuffer();
        QCOMPARE(pixel(frame, 25, 38), QColor(Qt::black));
        QVERIFY(pixel(frame, 35, 38) != QColor(Qt::white));
        mailbox->publish(terrain, terrain.rect(), 0, 0, geometry);
        frame = map.grabFramebuffer();
        QCOMPARE(pixel(frame, 35, 38), QColor(Qt::white));
    }
    void gestures_data() {
        QTest::addColumn<bool>("gl");
        QTest::newRow("native-default") << false;
        QTest::newRow("OpenGL") << true;
    }
    void gestures() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires native MAP submission");
        QFETCH(bool, gl);
        MapWidget map;
        if (gl) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(256,256); map.setViewSize({4,4});
        auto mailbox = std::make_shared<MapMailbox>(std::make_shared<MapMetrics>());
        map.setMailbox(mailbox);
        QImage terrain(128,128,QImage::Format_RGBA8888); terrain.fill(Qt::gray);
        QImage icon(32,32,QImage::Format_RGBA8888); icon.fill(Qt::red);
        MapGeometry geometry{QRect(0,0,4,4),QSize(10,10),32,0,7};
        MapSprite a; a.image=icon; a.position={0,0}; a.iconBottom={0,32}; a.cell={1,4};
        a.atom=42; a.haveKey=true; a.key=9; a.targetIdentity=10;
        MapSprite b=a; b.position={64,0}; b.iconBottom={64,32}; b.cell={3,4}; b.atom=43; b.targetIdentity=20;
        MapSprites sprites{a,b};
        std::vector<MapPointer> events;
        map.setInputHandler("world.map",true,[&](const MapPointer& p) {
            if (p.kind!=MapPointer::Hover && p.kind!=MapPointer::Leave) events.push_back(p);
        });
        QSignalSpy submitted(&map,&QRhiWidget::frameSubmitted);
        auto publish = [&] {
            const auto before=submitted.size();
            mailbox->publish(terrain,terrain.rect(),0,0,geometry,sprites); map.requestMapFrame();
            QTRY_VERIFY_WITH_TIMEOUT(submitted.size()>before,5000);
        };
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map)); publish();
        auto move = [&](QPoint pos, Qt::KeyboardModifiers mods=Qt::NoModifier) {
            QMouseEvent e(QEvent::MouseMove,QPointF(pos),QPointF(map.mapToGlobal(pos)),
                          Qt::NoButton,Qt::LeftButton,mods);
            QApplication::sendEvent(&map,&e);
        };
        // Both modifiers and all ordinary buttons survive the native MAP route.
        for (auto button : {Qt::LeftButton,Qt::MiddleButton,Qt::RightButton}) {
            events.clear(); QTest::mouseClick(&map,button,Qt::ControlModifier|Qt::ShiftModifier|Qt::AltModifier,{16,16});
            QCOMPARE(events.size(),size_t(3)); QCOMPARE(events.back().kind,MapPointer::Click);
            QVERIFY(events.back().click.mods.ctrl && events.back().click.mods.shift && events.back().click.mods.alt);
            QCOMPARE(events.back().click.over.ref,42u);
        }
        // A visible mouse-opacity=0 world root must not intercept the mob/object
        // below it (the live lighting-overlay failure), nor prevent turf fallback.
        MapSprite blocker=a; blocker.atom=99; blocker.plane=100; blocker.mouseTransparent=true;
        sprites.push_back(blocker); publish(); events.clear();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{16,16});
        QCOMPARE(events.back().click.over.ref,42u);
        sprites[0].atom=0x8000002au; publish(); events.clear();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::AltModifier,{16,16});
        QCOMPARE(events.back().click.over.kind,fugue::proto::enc::RefMob);
        sprites[0].mouseTransparent=true; publish(); events.clear();
        QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{16,16});
        QCOMPARE(events.back().click.over.kind,fugue::proto::enc::RefTurf);
        sprites[0]=a; sprites.removeLast(); publish();
        // World/HUD endpoints all use the same state machine and frozen source.
        for (bool sourceHud : {false,true}) for (bool destHud : {false,true}) {
            sprites[0].kind=sourceHud?MapSprite::Screen:MapSprite::Entity; sprites[0].screenInteractive=sourceHud;
            sprites[1].kind=destHud?MapSprite::Screen:MapSprite::Entity; sprites[1].screenInteractive=destHud;
            publish(); events.clear();
            QTest::mousePress(&map,Qt::LeftButton,Qt::AltModifier,{16,16});
            move({150,16},Qt::ControlModifier);
            move({160,20},Qt::ControlModifier); // same target: no extra motion work
            QTest::mouseRelease(&map,Qt::LeftButton,Qt::ShiftModifier,{150,16});
            QCOMPARE(events.size(),size_t(4));
            QCOMPARE(events[0].kind,MapPointer::Down); QCOMPARE(events[1].kind,MapPointer::Drag);
            QCOMPARE(events[2].kind,MapPointer::Up); QCOMPARE(events[3].kind,MapPointer::Drop);
            QVERIFY(events[1].click.mods.ctrl); QVERIFY(events[3].click.mods.shift);
            const auto& drop=events.back(); QVERIFY(drop.source);
            QCOMPARE(drop.source->click.over.ref,42u); QCOMPARE(drop.click.over.ref,43u);
            QCOMPARE(drop.source->screen,sourceHud); QCOMPARE(drop.screen,destHud);
            QCOMPARE(drop.source->click.loc.kind,sourceHud?fugue::proto::enc::RefNull:fugue::proto::enc::RefTurf);
            QCOMPARE(drop.click.loc.kind,destHud?fugue::proto::enc::RefNull:fugue::proto::enc::RefTurf);
        }
        events.clear(); QTest::mousePress(&map,Qt::LeftButton,Qt::NoModifier,{63,16});
        move({65,16}); QTest::mouseRelease(&map,Qt::LeftButton,Qt::NoModifier,{65,16});
        QCOMPARE(events.size(),size_t(4)); QCOMPARE(events.back().kind,MapPointer::Drop); // 2px target boundary
        events.clear(); QTest::mouseDClick(&map,Qt::LeftButton,Qt::AltModifier,{16,16});
        QTest::mouseRelease(&map,Qt::LeftButton,Qt::AltModifier,{16,16});
        QCOMPARE(events.back().kind,MapPointer::DoubleClick); QVERIFY(events.back().click.mods.alt);
        events.clear(); QTest::mousePress(&map,Qt::LeftButton,Qt::NoModifier,{2,2});
        move({60,60}); QTest::mouseRelease(&map,Qt::LeftButton,Qt::NoModifier,{60,60});
        QCOMPARE(events.size(),size_t(3)); QCOMPARE(events.back().kind,MapPointer::Click); // no distance threshold
        events.clear(); QTest::mousePress(&map,Qt::LeftButton,Qt::NoModifier,{16,16});
        move({150,16}); move({16,16}); QTest::keyClick(&map,Qt::Key_Escape); // stock Escape does not cancel
        QTest::mouseRelease(&map,Qt::LeftButton,Qt::NoModifier,{16,16});
        QCOMPARE(events.size(),size_t(5)); QCOMPARE(events.back().kind,MapPointer::Drop); // never click on return
        // A newer pending destination does not replace the rendered destination.
        events.clear(); QTest::mousePress(&map,Qt::LeftButton,Qt::NoModifier,{16,16});
        sprites[1].atom=44; sprites[1].targetIdentity=21;
        mailbox->publish(terrain,terrain.rect(),0,0,geometry,sprites);
        move({150,16}); QTest::mouseRelease(&map,Qt::LeftButton,Qt::NoModifier,{150,16});
        QCOMPARE(events.back().click.over.ref,43u); QCOMPARE(events.back().source->click.over.ref,42u);
        publish();
        // A newly rendered source occupant cannot retarget the original press.
        events.clear(); QTest::mousePress(&map,Qt::LeftButton,Qt::NoModifier,{16,16});
        sprites[0].atom=45; sprites[0].targetIdentity=22; publish();
        move({150,16}); QTest::mouseRelease(&map,Qt::LeftButton,Qt::NoModifier,{150,16});
        QCOMPARE(events.back().source->click.over.ref,42u); QCOMPARE(events.back().click.over.ref,44u);
        events.clear(); QTest::mousePress(&map,Qt::LeftButton,Qt::NoModifier,{16,16});
        move({270,16}); QTest::mouseRelease(&map,Qt::LeftButton,Qt::NoModifier,{270,16});
        QCOMPARE(events.back().kind,MapPointer::Drop); QVERIFY(events.back().outside);
        QCOMPARE(events.back().click.over.kind,fugue::proto::enc::RefNull);
        // Focus loss/hide/session changes end local gestures without Click/Drop.
        for (int cancel=0;cancel<4;++cancel) {
            events.clear(); QTest::mousePress(&map,Qt::LeftButton,Qt::NoModifier,{16,16}); move({150,16});
            if (cancel==0) { QFocusEvent e(QEvent::FocusOut); QApplication::sendEvent(&map,&e); }
            else if (cancel==1) { map.hide(); map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map)); }
            else if (cancel==2) { ++geometry.session; publish(); }
            else { map.setMailbox(mailbox); publish(); } // replaced/rebuilt input surface
            QTest::mouseRelease(&map,Qt::LeftButton,Qt::NoModifier,{150,16});
            QCOMPARE(events.back().kind,MapPointer::Up);
            for (const auto& p:events) QVERIFY(p.kind!=MapPointer::Click && p.kind!=MapPointer::Drop);
        }
        events.clear(); QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{16,16});
        QCOMPARE(events.size(),size_t(3)); QCOMPARE(events.back().kind,MapPointer::Click);
    }
    void pointerEvents() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("Requires a real QRhi backend");
        MapWidget map;
        map.setWindowFlag(Qt::FramelessWindowHint); // exact extent at forced DPR=1 too
        map.resize(128, 128); map.setViewSize({2, 2});
        auto metrics = std::make_shared<MapMetrics>();
        auto mailbox = std::make_shared<MapMailbox>(metrics);
        map.setMailbox(mailbox);
        QImage terrain(64, 64, QImage::Format_RGBA8888); terrain.fill(Qt::green);
        mailbox->publish(terrain, QRect(0, 0, 64, 64), 0, 0, {QRect(10, 20, 2, 2), QSize(100, 100), 32, 2, 7});
        std::vector<MapPointer> events;
        map.setInputHandler("world.map", true, [&](const MapPointer& p) { events.push_back(p); });
        map.show();
        QVERIFY(QTest::qWaitForWindowExposed(&map));
        QVERIFY(!map.grabFramebuffer().isNull());
        QTest::mouseClick(&map, Qt::LeftButton, Qt::ControlModifier, QPoint(16, 16));
        QCOMPARE(events.size(), size_t(3));
        QCOMPARE(events[0].kind, MapPointer::Down);
        QCOMPARE(events[1].kind, MapPointer::Up);
        QCOMPARE(events[2].kind, MapPointer::Click);
        QCOMPARE(events[2].click.over.x, uint16_t(10));
        QCOMPARE(events[2].click.over.y, uint16_t(21));
        QVERIFY(events[2].click.mods.ctrl);
        QCOMPARE(events[2].session, uint64_t(7));
        events.clear();
        QTest::mouseClick(&map, Qt::RightButton, Qt::NoModifier, QPoint(16, 16));
        QCOMPARE(events.back().click.button, std::string("right"));
        events.clear();
        for (int i = 0; i < 4; ++i) {
            QWheelEvent wheel(QPointF(16, 16), map.mapToGlobal(QPoint(16, 16)), QPoint(), QPoint(0, 30),
                              Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&map, &wheel);
        }
        QCOMPARE(events.size(), size_t(1));
        QCOMPARE(events[0].kind, MapPointer::Wheel);
        QCOMPARE(events[0].dy, 120.0f); // one notch, as stock reports it
        QVERIFY(events[0].click.mods.shift);
    }
    void popupReleaseTiming_data() {
        QTest::addColumn<bool>("gl");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void popupReleaseTiming() {
        if(QGuiApplication::platformName()=="offscreen") QSKIP("Requires native MAP and popup presentation");
        QFETCH(bool,gl);
        test::StockInputFixture f; QVERIFY(f.load("frames-filtered-input.txt","master-tree-world"));
        SkinWindow window; window.resize(448,448);
        MapWidget map(&window); if(gl) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,448); map.setViewSize({7,7});
        auto mailbox=std::make_shared<MapMailbox>(std::make_shared<MapMetrics>()); map.setMailbox(mailbox);
        QVector<MapPointer> requests,events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p) {
            events.push_back(p);
            if(mapPopupAction(p,true)==MapPopupAction::Open) requests.push_back(p);
        });
        QSignalSpy submitted(&map,&QRhiWidget::frameSubmitted);
        QSignalSpy picked(&window,&SkinWindow::mapMenuPicked);
        mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(!submitted.empty(),5000);
        // Stock's original-rectangle menu includes the master at this alpha hole.
        // A pending empty publication must not replace the last-rendered scene.
        const QPoint point(121,337); QCursor::setPos(map.mapToGlobal(point)); QTest::qWait(10);
        events.clear(); mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,{});
        QTest::mousePress(&map,Qt::RightButton,Qt::NoModifier,point);
        QCOMPARE(requests.size(),0); // independently observed stock held-button state
        QTest::mouseRelease(&map,Qt::RightButton,Qt::NoModifier,point);
        QCOMPARE(requests.size(),1); QCOMPARE(requests.front().kind,MapPointer::Click);
        const auto request=requests.front(); QCOMPARE(f.targetName(request),std::string("floor"));
        const auto candidates=mapPointerStack(request); QCOMPARE(candidates.size(),2);
        QCOMPARE(f.targetName(candidates[0]),std::string("master"));
        QCOMPARE(f.targetName(candidates[1]),std::string("floor"));
        for(const auto& p:candidates) QVERIFY(mapPointerCurrent(p,f.session.world(),0));
        // Exercise the real popup presenter using the production worker routing
        // predicate. Verb transport is not mocked as an end-to-end server check.
        MapMenu menu; menu.token=42; menu.global=request.global;
        for(int i=0;i<candidates.size();++i)
            menu.items.push_back({i,QString::fromStdString(f.targetName(candidates[i])),"Inspect"});
        window.showMapMenu(menu);
        QPointer<QMenu> popup=window.findChild<QMenu*>("mapContextMenu"); QVERIFY(popup);
        QTRY_VERIFY(popup->isVisible()); // opens only after the release
        const auto before=submitted.size(); map.requestMapFrame();
        QTRY_VERIFY_WITH_TIMEOUT(submitted.size()>before,5000);
        QCOMPARE(mapPointerStack(request).size(),2); // new rendered frame cannot alter the request
        QCOMPARE(requests.size(),1);
        QVERIFY(popup && popup->isVisible()); QCOMPARE(picked.size(),0);
        // The OS context-menu event and an unmatched release cannot duplicate
        // the worker request or dispatch ordinary right mouse hooks.
        QContextMenuEvent context(QContextMenuEvent::Mouse,point,request.global);
        QApplication::sendEvent(&map,&context);
        QTest::mouseRelease(&map,Qt::RightButton,Qt::NoModifier,point);
        QCOMPARE(requests.size(),1);
        for(const auto& p:events) QVERIFY(mapPopupAction(p,true)!=MapPopupAction::None);
        // Worker menu candidates retain separate HUD membership and surface fences
        // both before construction and when the selected verb returns later.
        auto& membership=f.session.worldMut().hudIdentity.at(candidates[0].click.over.ref).membership;
        ++membership; QVERIFY(!mapPointerCurrent(candidates[0],f.session.world(),0)); --membership;
        QVERIFY(mapPointerCurrent(candidates[0],f.session.world(),0));
        popup->actions()[0]->menu()->actions()[0]->trigger();
        QCOMPARE(picked.size(),1); QCOMPARE(picked[0][0].toULongLong(),quint64(42));
        QCOMPARE(picked[0][1].toInt(),0);
        window.showMapMenu({});
        map.setInputHandler("main.map",true,{});
        for(const auto& p:candidates) QVERIFY(!mapPointerCurrent(p,f.session.world(),0));
    }
    void contextMenuSelection() {
        SkinWindow window;
        QSignalSpy picked(&window, &SkinWindow::mapMenuPicked);
        MapMenu menu;
        menu.token = 42; menu.global = QPoint(20, 20);
        menu.items = {{0, "Floor & tile", "Examine"}, {1, "Object", "Use"}};
        window.showMapMenu(menu);
        auto* popup = window.findChild<QMenu*>("mapContextMenu");
        QVERIFY(popup);
        QCOMPARE(popup->actions().size(), 2);
        auto* group = popup->actions().at(1)->menu();
        QVERIFY(group);
        group->actions().at(0)->trigger();
        QCOMPARE(picked.size(), 1);
        QCOMPARE(picked.at(0).at(0).toULongLong(), quint64(42));
        QCOMPARE(picked.at(0).at(1).toInt(), 1);
        window.showMapMenu({});
        QVERIFY(!popup->isVisible());
    }
    void metricsMenu() {
        SkinWindow window;
        QVERIFY(window.loadSkin(skin).isEmpty());
        auto* action = window.findChild<QAction*>("mapPerformanceAction");
        QVERIFY(action);
        auto* map = dynamic_cast<MapWidget*>(window.map());
        QVERIFY(map);
        QVERIFY(action->isCheckable());
        QVERIFY(!action->isChecked());
        QSignalSpy commands(&window, &SkinWindow::menuCommand);
        action->trigger();
        QVERIFY(action->isChecked());
        QVERIFY(map->metricsVisible());
        QCOMPARE(commands.count(), 0);
        QVERIFY(window.loadSkin(skin).isEmpty());
        map = dynamic_cast<MapWidget*>(window.map());
        QVERIFY(map && map->metricsVisible());
        window.setMapMetricsVisible(false);
        QVERIFY(!action->isChecked());
        QVERIFY(!map->metricsVisible());
    }
    void skinRebuild() {
        SkinWindow window;
        window.setMapViewSize(19, 13); // bounds may precede the skin
        window.setMapMetricsVisible(true);
        QVERIFY(window.loadSkin(skin).isEmpty());
        auto* map = dynamic_cast<MapWidget*>(window.map());
        QVERIFY(map);
        QCOMPARE(map->viewSize(), QSize(19, 13));
        window.setMapViewSize(11, 17);
        QCOMPARE(map->viewSize(), QSize(11, 17));
        QVERIFY(window.loadSkin(skin).isEmpty());
        map = dynamic_cast<MapWidget*>(window.map());
        QVERIFY(map);
        QCOMPARE(map->viewSize(), QSize(11, 17));
        QVERIFY(map->metricsVisible()); // toggle survives skin reconstruction
        window.setMapViewSize(0, 0); // reconnect clears old bounds
        QCOMPARE(map->viewSize(), QSize(0, 0));
    }
};
int main(int argc, char** argv) {
    registerSchemes();
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    MapTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "map_test.moc"
