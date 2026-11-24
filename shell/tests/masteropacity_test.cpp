// BYOND 516.1688 master opacity: one scenario, three execution profiles.
#include <QtTest>
#include "tests/testmain.h"
#include "shell/tests/replayfixture.h"
#include "shell/tests/gpucomparison.h"
#include "shell/mapwidget.h"
#include <algorithm>

using namespace fugue;
using namespace fugue::shell;
namespace {
struct MasterOpacityFixture : test::ReplayFixture {
    static QString directory() {
        return QString(FUGUE_TEST_DATA_DIR)+"/master-opacity-world/stock/";
    }
    bool load(const QString& phase) {
        return ReplayFixture::load("frames-"+phase+".txt","master-opacity-world/stock");
    }
    MapPointer at(QPoint point, const MapSprites& scene, bool defaultMap = true) const {
        return *mapPointerAt(point,frame.source.size(),{7,7},frame.source,frame.geometry,"main.map",defaultMap,scene);
    }
    // Transcribed from independent popup images, NOT Click ownership.
    static QStringList menu(const QString& phase, QPoint point) {
        return phase != "zero" && point.x() >= 32 ? QStringList{"member","floor"} : QStringList{"floor"};
    }
    QStringList menuNames(const MapPointer& p) const {
        QStringList result;
        for (const auto& hit : mapPointerStack(p)) result.push_back(QString::fromStdString(targetName(hit)));
        return result;
    }
};

// Both GPU readback and native presentation use the same exact stock pixels.
QString pixelError(const QImage& actual, const QString& phase, const QString& profile) {
    const QString expectedPath=MasterOpacityFixture::directory()+phase+"-map.png";
    const QImage expected(expectedPath);
    if (expected.size()!=QSize(224,224)) return "Missing or invalid stock image: "+expectedPath;
    if (actual.size()!=expected.size()) return "Missing or incorrectly sized render for "+phase;
    int different=0;
    QPoint first;
    for (int y=0;y<expected.height();++y) for (int x=0;x<expected.width();++x) {
        if (actual.pixelColor(x,y)!=expected.pixelColor(x,y) && !different++) first={x,y};
    }
    if (!different) return {};
    const QString path=QDir::temp().filePath("fugue-master-opacity-"+profile+'-'+phase+"-actual.png");
    const bool saved=actual.save(path);
    return QString("%1/%2: %3 pixels differ, first at %4,%5; expected: %6; actual: %7")
        .arg(profile,phase).arg(different).arg(first.x()).arg(first.y()).arg(expectedPath,saved ? path : "could not save image");
}
}

class MasterOpacityTest : public QObject {
    Q_OBJECT
    QJsonArray phases_;
private slots:
    void initTestCase() {
        QFile file(MasterOpacityFixture::directory()+"result.json");
        QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(file.fileName()));
        QJsonParseError error;
        const auto record=QJsonDocument::fromJson(file.readAll(),&error).object();
        QCOMPARE(error.error,QJsonParseError::NoError);
        QVERIFY(record["complete"].toBool());
        phases_=record["phases"].toArray();
        QStringList names;
        for (const auto& value:phases_) {
            const auto row=value.toObject(); names.push_back(row["case"].toString());
            int clicks=0, menus=0;
            for (const auto& av:row["actions"].toArray()) {
                const auto action=av.toObject(); const auto point=action["point"].toArray();
                QCOMPARE(point.size(),2);
                for (const auto& coordinate:point) QVERIFY(coordinate.isDouble() && coordinate.toInt()>=0 && coordinate.toInt()<224);
                QStringList kinds;
                for (const auto& callback:action["callbacks"].toArray()) kinds.push_back(callback.toObject()["kind"].toString());
                if (action["kind"]=="click") {
                    QCOMPARE(kinds,(QStringList{"Down","Up","Click"})); ++clicks;
                } else {
                    QCOMPARE(action["kind"].toString(),QString("menu"));
                    QCOMPARE(kinds,(QStringList{"Menu"})); ++menus;
                }
            }
            QCOMPARE(clicks,5); QCOMPARE(menus,3);
        }
        QCOMPARE(names,(QStringList{"control","two","zero"}));
    }
    void portable() {
        int clicks=0, menus=0;
        for (const auto& value:phases_) {
            const auto row=value.toObject(); const auto name=row["case"].toString();
            MasterOpacityFixture f; QVERIFY2(f.load(name),qPrintable(f.error));
            for (const auto& av:row["actions"].toArray()) {
                const auto action=av.toObject(); const auto xy=action["point"].toArray();
                const QPoint point(xy[0].toInt(),xy[1].toInt());
                auto hit=f.at(point,f.sprites);
                QVERIFY(mapPointerCurrent(hit,f.session.world(),0));
                if (action["kind"]=="menu") {
                    QCOMPARE(f.menuNames(hit),f.menu(name,point)); ++menus;
                    for (const auto& entry:mapPointerStack(hit)) {
                        QVERIFY(!entry.masterFallback); QVERIFY(mapPointerCurrent(entry,f.session.world(),0));
                    }
                } else for (const auto& cv:action["callbacks"].toArray()) {
                    const auto c=cv.toObject();
                    hit.kind=c["kind"]=="Down" ? MapPointer::Down : c["kind"]=="Up" ? MapPointer::Up : MapPointer::Click;
                    const auto error=f.checkClick(hit,c); QVERIFY2(error.isEmpty(),qPrintable(name+": "+error));
                    QVERIFY(mapPointerCurrent(hit,f.session.world(),0)); ++clicks;
                }
            }
        }
        QCOMPARE(clicks,45); QCOMPARE(menus,9);
    }
    void lifetimes() {
        MasterOpacityFixture f; QVERIFY(f.load("two"));
        auto& w=f.session.worldMut(); const auto original=f.sprites;
        const auto hit=f.at({60,168},original); QVERIFY(hit.masterFallback && hit.screen);
        const auto ref=hit.click.over.ref; const auto id=w.hudAppid.at(ref);
        QVERIFY(mapPointerCurrent(hit,w,0));
        ++w.hudIdentity.at(ref).membership; QVERIFY(!mapPointerCurrent(hit,w,0)); --w.hudIdentity.at(ref).membership;
        ++w.hudIdentity.at(ref).key; QVERIFY(!mapPointerCurrent(hit,w,0)); --w.hudIdentity.at(ref).key;
        const auto hud=w.hud.at(ref); w.hud.erase(ref); QVERIFY(!mapPointerCurrent(hit,w,0)); w.hud[ref]=hud;
        QVERIFY(!mapPointerCurrent(hit,w,1));
        auto stale=hit; auto surface=std::make_shared<const uint8_t>(0); stale.surface=surface;
        QVERIFY(mapPointerCurrent(stale,w,0)); surface.reset(); QVERIFY(!mapPointerCurrent(stale,w,0));
        for (auto kind:{MapPointer::Hover,MapPointer::Drag,MapPointer::Drop,MapPointer::Wheel,MapPointer::DoubleClick}) {
            auto gesture=hit; gesture.kind=kind; QVERIFY(!mapPointerCurrent(gesture,w,0));
        }
        auto drag=f.at({40,168},original); drag.kind=MapPointer::Drag;
        drag.source=std::make_shared<MapPointer>(hit); QVERIFY(!mapPointerCurrent(drag,w,0));
        QVERIFY(!f.at({60,168},original,false).masterFallback);
        const auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(w.appearances.at(id)))).object();
        const auto saved=w.appearances.at(id);
        for (const QString key:{"filter","transform","target","source","tree","text","offset","placement","alpha","color","group","icon"}) {
            auto changed=fields;
            if(key=="filter") changed["filters"]=QJsonArray{QJsonObject{{"type",99}}};
            if(key=="transform") {changed["hasTransform"]=true;changed["transform"]=QJsonArray{};}
            if(key=="target") changed["renderTarget"]="*plane";
            if(key=="source") changed["renderSource"]="*plane";
            if(key=="tree") changed["overlays"]=QJsonArray{int(id)};
            if(key=="text") changed["maptext"]="unsupported";
            if(key=="offset") changed["pixelX"]=7;
            if(key=="placement") changed["screenLoc"]="2,1";
            if(key=="alpha") {changed["hasColor"]=true;changed["colorA"]=128;}
            if(key=="color") {changed["hasColor"]=true;changed["colorR"]=128;}
            if(key=="group") changed["appearanceFlags"]=double(uint32_t(fields["appearanceFlags"].toDouble()) | 0x04000000u);
            w.appearances.at(id)=saved;
            if(key=="icon") w.appearances.at(id).fields.iconResID=1;
            setFields(w.appearances.at(id), QJsonDocument(changed).toJson(QJsonDocument::Compact).toStdString());
            f.refresh();
            QCOMPARE(f.at({60,168},f.sprites).masterFallback,key=="filter");
            QVERIFY(f.at({60,168},original).masterFallback); // published evidence stays immutable
        }
        w.appearances.at(id)=saved; f.refresh(); QVERIFY(f.at({60,168},f.sprites).masterFallback);
        // No attachment/affine/local relay member silently enables a plane fallback.
        for (int variant=0;variant<8;++variant) {
            auto scene=original;
            auto member=std::find_if(scene.begin(),scene.end(),[](const auto& s){return s.kind==MapSprite::Entity && s.plane==1;});
            QVERIFY(member!=scene.end());
            if(variant==0) member->imageIdentity=1;
            if(variant==1) member->kind=MapSprite::Vis;
            if(variant==2) member->renderSource="*local";
            if(variant==3) member->maptext=true;
            if(variant==4) member->affine=StaticAffine::make({1,0,0,0,1,0},{32,32});
            if(variant==5) member->mouseOpacity=2;
            if(variant==6) member->tint[3]=.5f;
            if(variant==7) member->passMouse=true;
            QVERIFY(!f.at({60,168},scene).masterFallback);
        }
        // Last selected master wins; no borrowing from an earlier admitted master.
        auto scene=original; auto master=*std::find_if(scene.begin(),scene.end(),[](const auto& s){return s.kind==MapSprite::Master;});
        master.masterFallback=false; scene.push_back(master); QVERIFY(!f.at({60,168},scene).masterFallback);
        // Higher-plane ordinary ink remains above the fallback.
        scene=original; auto member=*std::find_if(scene.begin(),scene.end(),[](const auto& s){return s.kind==MapSprite::Entity && s.plane==1;});
        member.plane=2; member.position+=QPoint(20,0); scene.push_back(member);
        QVERIFY(!f.at({60,168},scene).masterFallback);
        // Geometry translated with a nonzero rendered viewport still reports viewport coordinates.
        scene=original; const QPoint delta(64,96);
        for(auto& s:scene) {s.position+=delta;s.iconBottom+=delta;}
        const auto shifted=mapPointerAt(QPoint(60,168),f.frame.source.size(),{7,7},f.frame.source.translated(delta),f.frame.geometry,"main.map",true,scene);
        QVERIFY(shifted && shifted->masterFallback); QCOMPARE(shifted->click.iconX,61); QCOMPARE(shifted->click.iconY,56);
    }
    void gpu_data() {
        QTest::addColumn<QString>("phase");
        for (const auto& value:phases_) {
            const auto phase=value.toObject()["case"].toString();
            QTest::newRow(qPrintable(phase)) << phase;
        }
    }
    void gpu() {
        QFETCH(QString,phase);
        MasterOpacityFixture f; QVERIFY2(f.load(phase),qPrintable(f.error));
        test::GpuComparison gpu;
#ifdef Q_OS_WIN
        const bool openGL=false;
#else
        const bool openGL=true;
#endif
        QVERIFY2(gpu.initialize(openGL,f.frame.source.size(),true),
                 "GPU profile requires a working QRhi device (Windows: software D3D11; elsewhere: OpenGL)");
        const auto error=pixelError(gpu.render(gpu.bounded,f.sprites,f.frame.source,Qt::black),phase,"gpu");
        QVERIFY2(error.isEmpty(),qPrintable(error));
    }
    void native_data() {
        QTest::addColumn<bool>("openGL");
        QTest::newRow("native-default") << false; QTest::newRow("OpenGL") << true;
    }
    void native() {
        QVERIFY2(QGuiApplication::platformName()!="offscreen" && QGuiApplication::platformName()!="minimal",
                 "Native profile requires a window system and a working QRhi backend");
        QFETCH(bool,openGL);
        MapWidget map; if(openGL) map.setApi(QRhiWidget::Api::OpenGL);
        map.resize(448,448); map.setViewSize({7,7});
        auto metrics=std::make_shared<MapMetrics>(); map.setMetrics(metrics);
        auto mailbox=std::make_shared<MapMailbox>(metrics); map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        QSignalSpy submitted(&map,&QRhiWidget::frameSubmitted);
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        int receipts=0,menus=0;
        for(const auto& value:phases_) {
            const auto row=value.toObject(); const auto name=row["case"].toString();
            MasterOpacityFixture f; QVERIFY2(f.load(name),qPrintable(f.error));
            submitted.clear();
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            map.update();
            QTRY_VERIFY_WITH_TIMEOUT(!submitted.empty(),5000);
            const auto pixels=map.grabFramebuffer(); QVERIFY(!pixels.isNull());
            QImage sampled(224,224,QImage::Format_RGBA8888);
            for(int y=0;y<224;++y) for(int x=0;x<224;++x)
                sampled.setPixelColor(x,y,pixels.pixelColor((2*x+1)*pixels.width()/448,(2*y+1)*pixels.height()/448));
            const auto pixelsError=pixelError(sampled,name,openGL ? "native-opengl" : "native-default");
            QVERIFY2(pixelsError.isEmpty(),qPrintable(pixelsError));
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
            QVERIFY(!map.grabFramebuffer().isNull()); events.clear();
            QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{121,337});
            QVERIFY(!events.empty());
            QVERIFY(std::none_of(events.begin(),events.end(),[](const auto& p){return p.masterFallback;}));
            mailbox->publish(f.frame.image,f.frame.source,0,0,f.frame.geometry,f.sprites);
            QVERIFY(!map.grabFramebuffer().isNull());
            const auto now=MapMetrics::nowNs(); metrics->reset(now+60'000'000'000);
            QVERIFY(!map.grabFramebuffer().isNull());
            QCOMPARE(metrics->snapshot(now+61'000'000'000).uploadMBPerSecond,0.0);
        }
        QCOMPARE(receipts,45); QCOMPARE(menus,9);
    }
};
FUGUE_TEST_MAIN(MasterOpacityTest)
#include "masteropacity_test.moc"
