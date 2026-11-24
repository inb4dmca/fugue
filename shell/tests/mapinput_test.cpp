#include "tests/appearancejson.h"
#include <QtTest>
#include "tests/testmain.h"
#include "shell/mapinput.h"
#include "shell/mapvisibility.h"
#include "shell/mappresentation.h"
#include "model/world.h"
#include "net/input.h"
#include "shell/entitymap.h"
#include "shell/tests/roottreefixture.h"
#include "shell/tests/planesourcefixture.h"
#include "shell/tests/hudrelayfixture.h"
#include "shell/tests/replayfixture.h"
#include "shell/compositionplan.h"
using namespace fugue;
using namespace fugue::shell;
class MapInputTest : public QObject {
    Q_OBJECT
private slots:
    void popupReleaseRouting() {
        MapPointer p;
        for(const auto kind:{MapPointer::Down,MapPointer::Up,MapPointer::Click,MapPointer::DoubleClick,
                            MapPointer::Drag,MapPointer::Drop,MapPointer::Wheel,MapPointer::Hover,MapPointer::Leave}) {
            p.kind=kind;
            for(const auto* button:{"left","middle","right"}) {
                p.click.button=button;
                QCOMPARE(mapPopupAction(p,false),MapPopupAction::None);
                const bool ordinary=p.click.button!="right" || kind==MapPointer::Wheel ||
                    kind==MapPointer::Hover || kind==MapPointer::Leave;
                QCOMPARE(mapPopupAction(p,true),ordinary ? MapPopupAction::None :
                    kind==MapPointer::Click ? MapPopupAction::Open : MapPopupAction::Consume);
            }
        }
        // Stock 516.1684, tests/data/popupmenu-world part A: the flag at the press and at the release.
        p=MapPointer{}; p.click.button="right";
        auto sequence=[&](bool atPress,bool atRelease) {
            QString out; bool sent=false;
            for(const auto kind:{MapPointer::Down,MapPointer::Up,MapPointer::Click}) {
                p.kind=kind;
                const auto action=mapPopupAction(p,kind==MapPointer::Down ? atPress : atRelease,sent);
                if(kind==MapPointer::Down) sent=action==MapPopupAction::None;
                out+=action==MapPopupAction::None ? 'S' : action==MapPopupAction::Open ? 'M' : '-';
            }
            return out;
        };
        QCOMPARE(sequence(true,true),QString("--M"));   // menu, nothing sent
        QCOMPARE(sequence(false,false),QString("SSS")); // DOWN UP CLICK
        QCOMPARE(sequence(false,true),QString("SSS"));  // DOWN UP CLICK, no menu
        QCOMPARE(sequence(true,false),QString("-SS"));  // UP CLICK
        // Part C: the press that closes the verb menu is consumed whatever the flag; its release is a fresh one.
        p.closedPopup=true;
        QCOMPARE(sequence(false,false),QString("-SS"));
        QCOMPARE(sequence(false,true),QString("--M"));
        p=MapPointer{};
        // Menu-only rectangles have no alpha target: opening is only a routing
        // decision, not a replacement for worker candidate/lifetime validation.
        p.kind=MapPointer::Click; p.click.button="right"; p.backgroundHidden=true;
        QCOMPARE(mapPopupAction(p,true),MapPopupAction::Open);
    }
    void opacityAndRenderedStacks() {
        MapSprite bottom; bottom.image=QImage(32,32,QImage::Format_RGBA8888); bottom.image.fill(Qt::red);
        bottom.cell={1,1}; bottom.iconBottom={0,32}; bottom.atom=1; bottom.haveKey=true; bottom.key=9; bottom.targetIdentity=10;
        auto top=bottom; top.atom=2; top.targetIdentity=20; top.plane=1;
        top.image.fill(Qt::transparent);
        MapSprites sprites{bottom,top};
        auto pick=[&] { return *mapPointerAt({8,8},{32,32},{1,1},{0,0,32,32},
            {{0,0,1,1},{1,1},32,0,7},"main.map",true,sprites); };
        QCOMPARE(pick().click.over.ref,1u);
        QCOMPARE(mapPointerStack(pick()).size(),2); // rectangles, not source alpha
        sprites.back().color[19]=1; // compositing cannot turn a source-alpha hole into a hit
        QCOMPARE(pick().click.over.ref,1u);
        sprites.back().mouseOpacity=2;
        QCOMPARE(pick().click.over.ref,2u);
        sprites.back().mouseOpacity=1; sprites.back().image.fill(Qt::green);
        sprites.back().tint[3]=0; sprites.back().color={};
        QCOMPARE(pick().click.over.ref,2u); // alpha/tint/matrix ignored
        const auto retained=pick(); const auto stack=mapPointerStack(retained);
        sprites.back().mouseOpacity=0;
        QCOMPARE(pick().click.over.ref,1u); QCOMPARE(mapPointerStack(pick()).size(),1);
        QCOMPARE(mapPointerStack(retained).size(),2); // immutable last-rendered values
        model::World w; w.bounds.maxX=w.bounds.maxY=1;
        for(uint32_t id:{1u,2u}) {auto& a=w.atoms[id];a.hasPos=true;a.haveKey=true;a.key=9;a.renderIdentity=id*10;}
        QVERIFY(mapPointerCurrent(stack[0],w,7));
        ++w.atoms[2].renderIdentity; QVERIFY(!mapPointerCurrent(stack[0],w,7));
        QVERIFY(mapPointerCurrent(stack[1],w,7)); QVERIFY(!mapPointerCurrent(stack[1],w,8));
        sprites={bottom,top,top}; QCOMPARE(mapPointerStack(pick()).size(),2); // overlays deduplicate
        sprites.back().position={64,0}; QCOMPARE(mapPointerStack(pick()).size(),2); // no union-box gap
        sprites={bottom,top}; sprites.back().footprint=true; sprites.back().image={}; sprites.back().size={32,32};
        QCOMPARE(pick().click.over.ref,1u); QCOMPARE(mapPointerStack(pick()).size(),2);
        sprites.back()=top; sprites.back().kind=MapSprite::Image; sprites.back().screenInteractive=true;
        sprites.back().mouseOpacity=2; sprites.back().image.fill(Qt::red);
        QCOMPARE(pick().click.over.ref,1u); // unverified image-owned opacity-2 footprint refused
        QCOMPARE(mapPointerStack(pick()).size(),2); // menu rectangles are independently established
        sprites.back()=top; sprites.back().mouseOpacity=3;
        QCOMPARE(pick().click.over.ref,1u); // unknown packed mode is not guessed
        sprites.back()=top; sprites.back().maptext=true; sprites.back().mouseTransparent=true;
        QCOMPARE(pick().click.over.ref,1u);
        sprites.back().mouseOpacity=2; QCOMPARE(pick().click.over.ref,2u);
        sprites.back()=top; sprites.back().mouseOpacity=2;
        sprites.back().affine=StaticAffine::make({.8,-.6,0,.6,.8,0},{32,32});
        // Opacity 2 is the turned icon rectangle: a corner of its bounds falls through (stock 516.1689, click-world
        // set b flat-turn-opaque2).
        const auto corner=sprites.back().rect().topLeft()+QPoint(1,1);
        const auto box=mapPointerAt(corner+QPoint(16,16),{64,64},{2,2},{-16,-16,64,64},
            {{0,0,2,2},{2,2},32,0,7},"main.map",true,sprites);
        QVERIFY(box); QVERIFY(box->click.over.ref!=2u);
        sprites={bottom,top}; MapSprite master; master.kind=MapSprite::Master;master.plane=1;master.mouseOpacity=0;
        sprites.push_back(master); QCOMPARE(pick().click.over.ref,1u); QCOMPARE(mapPointerStack(pick()).size(),1);
        master.mouseOpacity=1;master.tint[3]=0;sprites.push_back(master);
        QCOMPARE(mapPointerStack(pick()).size(),2); // last master wins; alpha is not opacity
        sprites.clear();
        for(uint32_t id=1;id<=200;++id) { auto s=bottom;s.atom=id;sprites.push_back(s); }
        QCOMPARE(mapPointerStack(pick()).size(),128);
        auto outside=pick();outside.insideScene=false;QVERIFY(mapPointerStack(outside).empty());
    }
    void affineCoverageAndStockCoordinates_data() {
        QTest::addColumn<bool>("mob");
        QTest::newRow("object") << false; QTest::newRow("mob") << true;
    }
    void affineCoverageAndStockCoordinates() {
        QFETCH(bool,mob);
        MapSprite sprite; sprite.image=QImage(32,32,QImage::Format_RGBA8888); sprite.image.fill(Qt::red);
        sprite.position={32,32}; sprite.iconBottom={32,64}; sprite.cell={2,2}; sprite.atom=42 | (mob ? model::kRegistryA : 0);
        sprite.targetIdentity=123; sprite.haveKey=true; sprite.key=9;
        auto hit=[&](QPoint p) { return mapPointerAt(QPointF(p),{96,96},{3,3},{0,0,96,96},
            {QRect(0,0,3,3),QSize(3,3),32,0,1},"main.map",true,{sprite}); };
        sprite.affine=StaticAffine::make({.8,-.6,0,.6,.8,0},{32,32}); QVERIFY(sprite.affine);
        const QRect bounds=sprite.rect();
        for(QPoint corner:{bounds.topLeft(),bounds.topRight(),bounds.bottomLeft(),bounds.bottomRight()}) {
            const auto p=hit(corner); QVERIFY(p); QCOMPARE(p->click.over.kind,uint8_t(proto::enc::RefTurf));
        }
        QCOMPARE(hit({48,48})->click.over.ref,uint32_t(42));
        sprite.image.setPixelColor(16,16,Qt::transparent);
        QCOMPARE(hit({48,48})->click.over.kind,uint8_t(proto::enc::RefTurf));
        sprite.image.fill(Qt::red);
        struct Case { std::array<double,6> m; QPoint pixel, icon, vis; bool hasVis; };
        // Integer pixel corners reconstructed from the stock fixture screen-loc.
        const Case cases[] = {
            {{0,-1,0,1,0,0},{9,24},{9,23},{10,8},true},
            {{0,1,0,-1,0,0},{11,7},{8,11},{12,25},true}, // paired stock mob/object lying rotation
            {{1,0,9,0,1,5},{16,6},{8,21},{8,21},false},
            {{-1,0,0,0,1,0},{25,9},{8,23},{26,23},true},
            {{1,.5,0,0,1,0},{7,16},{8,16},{16,16},true},
            {{1,.5,0,0,1,0},{7,15},{7,17},{16,17},true}
        };
        for(const auto& item:cases) {
            sprite.affine=StaticAffine::make(item.m,{32,32}); const auto p=hit(sprite.position+item.pixel); QVERIFY(p);
            QCOMPARE(p->click.over.kind,uint8_t(mob ? proto::enc::RefMob : proto::enc::RefObj));
            QCOMPARE(p->click.over.ref,uint32_t(42)); QCOMPARE(p->click.iconX,item.icon.x()); QCOMPARE(p->click.iconY,item.icon.y());
            QCOMPARE(p->click.hasVis,item.hasVis);
            if(item.hasVis) { QCOMPARE(p->click.visX,item.vis.x()); QCOMPARE(p->click.visY,item.vis.y()); }
            QCOMPARE(p->targetIdentity,uint64_t(123)); QCOMPARE(p->atomKey,uint16_t(9));
            model::World w; auto& atom=w.atoms[sprite.atom]; atom.hasPos=true; atom.z=0;
            atom.renderIdentity=123; atom.haveKey=true; atom.key=9;
            QVERIFY(mapPointerCurrent(*p,w,1));
            ++atom.renderIdentity; QVERIFY(!mapPointerCurrent(*p,w,1)); // recycled registry slot
            atom.renderIdentity=123; w.resync.freed.insert(sprite.atom); QVERIFY(!mapPointerCurrent(*p,w,1));
        }
    }
    void imageAffineStockCoordinates_data() {
        QTest::addColumn<bool>("mob"); QTest::newRow("object") << false; QTest::newRow("mob") << true;
    }
    void imageAffineStockCoordinates() {
        QFETCH(bool,mob); const uint32_t target=42u | (mob ? model::kRegistryA : 0u);
        MapSprite image; image.kind=MapSprite::Image; image.screenInteractive=true;
        image.image=QImage(32,32,QImage::Format_RGBA8888); image.image.fill(Qt::transparent);
        for(int y=3;y<9;++y) for(int x=23;x<29;++x) image.image.setPixelColor(x,y,Qt::yellow);
        image.position={32+7+25,32-5-9}; image.iconBottom=image.position+QPoint(0,32);
        image.cell={2,2}; image.atom=target; image.haveKey=true; image.key=9;
        image.imageId=7; image.imageIdentity=20; image.targetIdentity=10;
        MapGeometry geometry{{0,0,4,4},{4,4},32,0,1};
        geometry.visibility=std::make_shared<const VisibilityFrame>();
        auto hit=[&](QPoint p){return *mapPointerAt(image.position+p,{128,128},{4,4},{0,0,128,128},
            geometry,"main.map",true,{image});};
        struct Case {std::array<double,6> m; QPoint pixel,icon,vis; bool hasVis;};
        const Case cases[]={
            {{1,0,0,0,1,0},{25,5},{26,27},{26,27},false},
            {{0,-1,0,1,0,0},{5,5},{28,27},{6,27},true},
            {{.5,0,0,0,.5,0},{20,10},{25,28},{13,14},true},
            {{1,0,9,0,1,5},{34,0},{26,27},{26,27},false},
            {{0,-1,9,1,0,5},{14,0},{28,27},{6,27},true},
            {{.5,0,9,0,.5,5},{29,5},{25,28},{13,14},true}
        };
        model::World w; auto& atom=w.atoms[target]; atom.hasPos=true; atom.haveKey=true; atom.key=9; atom.renderIdentity=10;
        w.atoms[target^model::kRegistryA]=atom; // even identical wire keys/tokens cannot alias registries
        w.imageInstances[7]={2,target,uint8_t(mob?2:1),false,20,10};
        for(const auto& c:cases) {
            image.affine=StaticAffine::make(c.m,{32,32}); image.smooth=c.m[0]==.5;
            const auto p=hit(c.pixel);
            QCOMPARE(p.click.over.kind,uint8_t(mob?proto::enc::RefMob:proto::enc::RefObj)); QCOMPARE(p.click.over.ref,42u);
            QCOMPARE(p.click.loc.kind,uint8_t(proto::enc::RefTurf));
            QCOMPARE(p.click.iconX,c.icon.x()); QCOMPARE(p.click.iconY,c.icon.y());
            QCOMPARE(p.click.hasVis,c.hasVis);
            if(c.hasVis){QCOMPARE(p.click.visX,c.vis.x());QCOMPARE(p.click.visY,c.vis.y());}
            QVERIFY(mapPointerCurrent(p,w,1));
            auto forged=p; forged.click.over.kind=mob?proto::enc::RefObj:proto::enc::RefMob;
            QVERIFY(!mapPointerCurrent(forged,w,1));
            forged.click.over.kind=proto::enc::RefTurf; QVERIFY(!mapPointerCurrent(forged,w,1));
            auto drag=p; drag.kind=MapPointer::Drag; drag.source=std::make_shared<const MapPointer>(p);
            QVERIFY(!mapPointerCurrent(drag,w,1)); drag.kind=MapPointer::Drop; QVERIFY(!mapPointerCurrent(drag,w,1));
            ++w.imageInstances[7].identity; QVERIFY(!mapPointerCurrent(p,w,1)); --w.imageInstances[7].identity;
            ++atom.renderIdentity; QVERIFY(!mapPointerCurrent(p,w,1)); --atom.renderIdentity;
            const auto transparent=image.affine->forward.map(QPointF(16,16)).toPoint();
            QVERIFY(hit(transparent).backgroundHidden); // no phantom-turf fallback
            image.renderTarget="*captured"; QVERIFY(hit(c.pixel).backgroundHidden); image.renderTarget.clear();
        }
        image.affine=StaticAffine::make({.5,0,0,0,.5,0},{32,32}); image.smooth=true;
        QVERIFY(hit({19,9}).backgroundHidden); QVERIFY(hit({19,10}).backgroundHidden);
        QCOMPARE(hit({22,12}).click.iconX,29); QCOMPARE(hit({22,12}).click.iconY,24);
        QCOMPARE(hit({22,10}).click.iconY,28);
    }
    void ordinaryRootTreePicking() {
        auto w=roottreefixture::world(true,true); roottreefixture::pose(w,{0,-1,9,1,0,5});
        TurfMap terrain; EntityMap scene; auto frame=terrain.prepare(w,{},0,false);
        auto sprites=scene.prepare(w,frame,roottreefixture::read,1,nullptr,true); QCOMPARE(sprites.size(),4);
        frame.geometry.session=7; frame.geometry.visibility=std::make_shared<const VisibilityFrame>();
        const QPoint origin=sprites[1].position-frame.source.topLeft();
        auto hit=[&](QPoint p){return *mapPointerAt(origin+p,{160,160},{5,5},frame.source,frame.geometry,"main.map",true,sprites);};
        // Child-only and nested pixels report ROOT coordinates, including negatives.
        const auto child=hit({60,4}); QCOMPARE(child.click.over.kind,uint8_t(proto::enc::RefMob));
        QCOMPARE(child.click.iconX,24); QCOMPARE(child.click.iconY,-19);
        QCOMPARE(child.click.visX,52); QCOMPARE(child.click.visY,23); QVERIFY(mapPointerCurrent(child,w,7));
        const auto nested=hit({58,15}); QCOMPARE(nested.click.iconX,13); QCOMPARE(nested.click.iconY,-17);
        // Captured stock corner-alpha boundary: adjacent visible pixels need not hit.
        QVERIFY(hit({21,12}).backgroundHidden); QVERIFY(hit({0,0}).backgroundHidden);
        const auto underlay=hit({21,13}); QCOMPARE(underlay.click.over.kind,uint8_t(proto::enc::RefMob));
        for(auto& s:sprites) s.renderTarget="*capture";
        QVERIFY(hit({60,4}).backgroundHidden); // no phantom turf behind captured tree
        for(auto& s:sprites) {s.renderTarget.clear();s.mouseTransparent=true;}
        QVERIFY(hit({60,4}).backgroundHidden);
        ++w.atoms.begin()->second.renderIdentity; QVERIFY(!mapPointerCurrent(child,w,7));
        --w.atoms.begin()->second.renderIdentity; QVERIFY(!mapPointerCurrent(child,w,8));
        w.resync.freed.insert(w.atoms.begin()->first); QVERIFY(!mapPointerCurrent(child,w,7));
    }
    void stockHudAnchorsAndPassMouse() {
        const auto rows=test::HudRelayFixture::rows();QCOMPARE(rows.size(),12);
        for(const auto& value:rows) {
            const auto row=value.toObject();test::HudRelayFixture f(row["phase"].toString());
            for(const auto& v:row["clicks"].toArray()) {
                const auto expected=v.toObject();const auto xy=expected["point"].toArray();
                const auto hit=f.at({xy[0].toInt(),xy[1].toInt()});
                const auto error=test::HudRelayFixture::check(hit,expected);
                QVERIFY2(error.isEmpty(),qPrintable(row["phase"].toString()+": "+error));
                QVERIFY(mapPointerCurrent(hit,f.world,17));
            }
            EntityMap::Stats stats;const auto old=f.sprites;f.refresh(&stats);
            QCOMPARE(stats.templateBuilds,uint64_t(0));QCOMPARE(stats.frameDecodes,uint64_t(0));
            QCOMPARE(old.constData(),f.sprites.constData());
        }
    }
    void stockWideMousePackets() {
        QFile file(QString(FUGUE_TEST_DATA_DIR)+"/hud-relay-world/wide-mouse.json");QVERIFY(file.open(QIODevice::ReadOnly));
        const auto rows=QJsonDocument::fromJson(file.readAll()).object()["bodies"].toObject();QCOMPARE(rows.size(),8);
        auto check=[&](const Bytes& narrow,const QString& name) {
            const auto expected=QByteArray::fromHex(rows[name].toString().toLatin1());
            QCOMPARE(proto::enc::mouseWireBody(narrow,true),Bytes(expected.begin(),expected.end()));
            QCOMPARE(proto::enc::mouseWireBody(narrow,false),narrow); // negotiated mode, not ID magnitude
        };
        MapPointer p;p.click.over=proto::enc::objRef(0);p.click.control="main.map";
        p.click.iconX=4;p.click.iconY=18;p.click.screenX=3;p.click.screenY=5;p.click.screenPX=p.click.screenPY=16;
        for(auto [kind,name]:std::initializer_list<std::pair<MapPointer::Kind,const char*>>{
            {MapPointer::Click,"03"},{MapPointer::Down,"06"},{MapPointer::Up,"05"},{MapPointer::Hover,"08"}}) {
            p.kind=kind;check(mapPointerBody(p),name);
        }
        p.kind=MapPointer::DoubleClick;p.click.over=p.click.loc=proto::enc::turfRef(7,2);
        p.click.iconX=p.click.screenPX=9;p.click.iconY=p.click.screenPY=28;p.click.screenX=7;p.click.screenY=2;
        check(mapPointerBody(p),"04");
        proto::enc::WheelCmd wheel;wheel.over=p.click.over;wheel.dy=120;wheel.control="main.map";
        wheel.params="icon-x=9;icon-y=28;screen-loc=map:7:9,2:28";check(proto::enc::encodeWheelCmd(wheel),"wheel");
        proto::enc::DragCmd drag;drag.src=drag.srcLoc=p.click.over;drag.over=drag.overLoc=proto::enc::turfRef(6,2);
        drag.srcControl=drag.overControl="main.map";
        drag.params="icon-x=21;icon-y=18;left=1;button=left;screen-loc=6:21,2:18";
        check(proto::enc::encodeDragCmd(drag),"drag");
        drag.drop=true;drag.over=drag.overLoc=proto::enc::nullRef();drag.overControlNull=true;
        drag.params="icon-x=9;icon-y=28;left=1;button=left;screen-loc=7:9,2:28";
        check(proto::enc::encodeDragCmd(drag),"drop");
        QVERIFY(proto::enc::mouseWireBody({},true).empty());
        QVERIFY(proto::enc::mouseWireBody(proto::enc::encodeVerb(3,0,0,{},true),true).empty());
    }
    void hudPassMouseLifetimeAndBounds() {
        test::HudRelayFixture f("chain");const auto hit=f.at({79,16});
        QVERIFY(hit.planeMousePath);QCOMPARE(hit.planeMousePath->hops.size(),size_t(2));
        QCOMPARE(hit.click.over.ref,1u);QVERIFY(!hit.click.hasVis);
        for(const auto& hop:hit.planeMousePath->hops) {
            QVERIFY(hop.master.renderedScene.empty());QVERIFY(!hop.master.composition);
            QVERIFY(hop.relay.renderedScene.empty());QVERIFY(!hop.relay.composition);
            QVERIFY(!hop.master.planeMousePath);QVERIFY(!hop.relay.planeMousePath);
        }
        QVERIFY(mapPointerCurrent(hit,f.world,17));QVERIFY(!mapPointerCurrent(hit,f.world,18));
        for(uint32_t id:{1,2,3,4,5}) {
            ++f.world.hudIdentity[id].membership;QVERIFY(!mapPointerCurrent(hit,f.world,17));--f.world.hudIdentity[id].membership;
            ++f.world.hudIdentity[id].key;QVERIFY(!mapPointerCurrent(hit,f.world,17));--f.world.hudIdentity[id].key;
            const auto appid=f.world.hudAppid[id];f.world.hudAppid.erase(id);
            QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.hudAppid[id]=appid;
            f.world.appearances[99]=f.world.appearances[appid];f.world.hudAppid[id]=99;
            QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.hudAppid[id]=appid;
        }
        auto hover=hit;hover.kind=MapPointer::Hover;
        QVERIFY(mapPointerCurrent(hover,f.world,17));MapHover state;
        QVERIFY(!state.update(hover,true,true,false).empty());QVERIFY(state.update(hover,true,true,false).empty());
        for(auto kind:{MapPointer::Down,MapPointer::Up,MapPointer::Click}) {
            auto p=hit;p.kind=kind;QVERIFY(mapPointerCurrent(p,f.world,17));
        }
        for(auto kind:{MapPointer::DoubleClick,MapPointer::Wheel,MapPointer::Drag,MapPointer::Drop}) {
            auto p=hit;p.kind=kind;p.source=std::make_shared<const MapPointer>(hit);
            QVERIFY(!mapPointerCurrent(p,f.world,17));
        }
        for(uint32_t id:{3,5}) {
            const auto fields=fieldsJson(f.world.appearances[id]);
            f.change(id,{{"renderSource","*rebound"}});QVERIFY(!mapPointerCurrent(hit,f.world,17));
            setFields(f.world.appearances[id], fields);
        }
        const auto old=f.sprites;f.change(1,{{"screenLoc","TOP:-50,CENTER:-20"}});f.refresh();
        QCOMPARE(f.at({79,16}).click.over.kind,uint8_t(proto::enc::RefTurf));
        const auto retained=mapPointerAt({79,16},{224,160},{7,5},f.frame.source,f.frame.geometry,"main.map",true,old);
        QVERIFY(retained && retained->planeMousePath);QCOMPARE(retained->click.over.ref,1u);
        for(int variant=0;variant<6;++variant) {
            test::HudRelayFixture refused("chain");
            for(auto& s:refused.sprites) if(s.atom==3) {
                if(variant==0) s.iconBottom.rx()+=1; // translated plane relay
                if(variant==1) {auto fx=std::make_shared<PlaneEffects>();fx->filters.push_back({});s.planeEffects=fx;}
                if(variant==2) s.tint[3]=.5f;
                if(variant==3) s.passMouse=false; // mixed ownership chain is not admitted
                if(variant==4) s.renderSource="*middle"; // capture cycle
                if(variant==5) s.imageIdentity=1;
            }
            QVERIFY(!refused.at({79,16}).planeMousePath);
        }
        test::HudRelayFixture duplicate("chain");
        auto master=*std::find_if(duplicate.sprites.begin(),duplicate.sprites.end(),[](const auto& s){return s.atom==2;});
        master.plane=11;duplicate.sprites.push_back(master);
        QVERIFY(!duplicate.at({79,16}).planeMousePath);
    }
    void stockWorldPassMouse() {
        QFile file(QString(FUGUE_TEST_DATA_DIR)+"/plane-pass-world/observations.json");QVERIFY(file.open(QIODevice::ReadOnly));
        const auto rows=QJsonDocument::fromJson(file.readAll()).array();QCOMPARE(rows.size(),13);
        int callbacks=0,empty=0;
        for(const auto& value:rows) {
            const auto row=value.toObject();const auto name=row["case"].toString();
            test::ReplayFixture f;
            QVERIFY2(f.load(row["frames"].toString(),"plane-pass-world","stock-shadow-world"),qPrintable(name));
            // Live MAP publishes unified-scene visibility: never synthesize a
            // flattened terrain hit through a captured turf's alpha hole.
            f.frame.geometry.visibility=std::make_shared<const VisibilityFrame>();
            for(const auto& pointValue:row["points"].toArray()) {
                const auto point=pointValue.toObject();const auto xy=point["point"].toArray();
                const auto picked=mapPointerAt({xy[0].toDouble(),xy[1].toDouble()},{224,224},{7,7},
                    f.frame.source,f.frame.geometry,"main.map",true,f.sprites);
                QVERIFY(picked);
                const auto events=point["events"].toArray();
                if(events.empty()) {
                    ++empty;QVERIFY(picked->backgroundHidden);
                    QCOMPARE(picked->click.over.kind,uint8_t(proto::enc::RefNull));
                }
                for(const auto& eventValue:events) {
                    const auto expected=eventValue.toObject();auto hit=*picked;
                    const auto kind=expected["kind"].toString();
                    hit.kind=kind=="Enter" ? MapPointer::Hover : kind=="Down" ? MapPointer::Down :
                        kind=="Up" ? MapPointer::Up : MapPointer::Click;
                    const auto error=f.checkClick(hit,expected);
                    QVERIFY2(error.isEmpty(),qPrintable(name+" "+kind+": "+error));
                    QVERIFY2(mapPointerCurrent(hit,f.session.world(),0),qPrintable(name+" lifetime"));
                    if(expected["target"].toString()!="floor") {
                        QVERIFY2(hit.planeMousePath,qPrintable(name));
                        QCOMPARE(hit.planeMousePath->hops.size(),name.endsWith("chain") ? size_t(2) : size_t(1));
                    }
                    ++callbacks;
                }
            }
        }
        QCOMPARE(callbacks,150);QCOMPARE(empty,1);
    }
    void worldPassMouseLifetimeAndBounds() {
        test::PlaneSourceFixture f;
        f.relay({{"appearanceFlags",double(0x80000204u)}});
        auto hit=f.at({69,37});
        QVERIFY(hit.planeMousePath);QCOMPARE(hit.planeMousePath->hops.size(),size_t(1));
        QCOMPARE(hit.click.over.kind,uint8_t(proto::enc::RefObj));QCOMPARE(hit.click.over.ref,100u);QVERIFY(!hit.screen);
        QCOMPARE(hit.click.loc.kind,uint8_t(proto::enc::RefTurf));
        QCOMPARE(hit.click.loc.x,uint16_t(5));QCOMPARE(hit.click.loc.y,uint16_t(5));
        QCOMPARE(hit.click.iconX,6);QCOMPARE(hit.click.iconY,27);
        QVERIFY(mapPointerCurrent(hit,f.world,17));QVERIFY(!mapPointerCurrent(hit,f.world,18));
        for(uint32_t id:{200,201}) {
            ++f.world.hudIdentity[id].membership;QVERIFY(!mapPointerCurrent(hit,f.world,17));--f.world.hudIdentity[id].membership;
            ++f.world.hudIdentity[id].key;QVERIFY(!mapPointerCurrent(hit,f.world,17));--f.world.hudIdentity[id].key;
            const auto appid=f.world.hudAppid[id];f.world.hudAppid.erase(id);
            QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.hudAppid[id]=appid;
            ++f.world.hudAppid[id];QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.hudAppid[id]=appid;
        }
        const auto master=fieldsJson(f.world.appearances[2]);
        setFields(f.world.appearances[2], R"({"appearanceFlags":268435972,"plane":60,"renderTarget":"*rebound"})");
        QVERIFY(!mapPointerCurrent(hit,f.world,17));setFields(f.world.appearances[2], master);
        const auto relay=fieldsJson(f.world.appearances[3]);
        f.relay({{"appearanceFlags",double(0x80000204u)},{"renderSource","*rebound"}});
        QVERIFY(!mapPointerCurrent(hit,f.world,17));setFields(f.world.appearances[3], relay);f.refresh();
        auto& atom=f.world.atoms.at(100);
        ++atom.renderIdentity;QVERIFY(!mapPointerCurrent(hit,f.world,17));--atom.renderIdentity;
        ++atom.key;QVERIFY(!mapPointerCurrent(hit,f.world,17));--atom.key;
        ++atom.appid;QVERIFY(!mapPointerCurrent(hit,f.world,17));--atom.appid;
        ++atom.z;QVERIFY(!mapPointerCurrent(hit,f.world,17));--atom.z;
        f.world.resync.freed.insert(100);QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.resync.freed.clear();
        for(auto kind:{MapPointer::Down,MapPointer::Up,MapPointer::Click,MapPointer::Hover}) {
            auto event=hit;event.kind=kind;QVERIFY(mapPointerCurrent(event,f.world,17));
        }
        for(auto kind:{MapPointer::DoubleClick,MapPointer::Wheel,MapPointer::Drag,MapPointer::Drop}) {
            auto event=hit;event.kind=kind;event.source=std::make_shared<const MapPointer>(hit);
            QVERIFY(!mapPointerCurrent(event,f.world,17));
        }
        const auto original=f.sprites;
        for(int variant=0;variant<9;++variant) {
            f.sprites=original;
            for(auto& s:f.sprites) {
                if(s.atom==200 && variant<=2) {
                    auto fx=std::make_shared<PlaneEffects>();
                    if(variant==0) fx->ignoredFilters=1;
                    if(variant==1) fx->supported=false;
                    if(variant==2) fx->matrix=std::array<double,6>{1,0,1,0,1,0};
                    s.planeEffects=fx;
                }
                if(s.atom==100) {
                    if(variant==3) s.targetIdentity=0;
                    if(variant==4) s.haveKey=false;
                    if(variant==5) s.imageIdentity=1;
                    if(variant==6) s.size={64,64};
                    if(variant==7) s.tint[3]=std::numeric_limits<float>::quiet_NaN();
                    if(variant==8) s.image.fill(Qt::transparent);
                }
            }
            QVERIFY(!f.at({69,37}).planeMousePath);
        }
        const auto retained=mapPointerAt({69,37},{160,96},{5,3},hit.renderedSource,f.frame.geometry,"main.map",true,hit.renderedScene);
        QVERIFY(retained && retained->planeMousePath);QVERIFY(sameMapTarget(hit,*retained));
        for(const auto& hop:hit.planeMousePath->hops) {
            QVERIFY(hop.master.renderedScene.empty());QVERIFY(!hop.master.composition);
            QVERIFY(hop.relay.renderedScene.empty());QVERIFY(!hop.relay.composition);
        }
    }
    void worldPassMouseTypedLeaves() {
        for(bool turf:{false,true}) {
            test::PlaneSourceFixture f;f.relay({{"appearanceFlags",double(0x80000204u)}});
            const uint32_t mob=model::kRegistryA|100;
            f.world.atoms[mob]=f.world.atoms.at(100);
            f.world.tiles[{5,5,0}]=1;
            for(auto& s:f.sprites) if(s.atom==100) {
                s.kind=turf ? MapSprite::Turf : MapSprite::Entity;
                s.atom=mob;
            }
            const auto hit=f.at({69,37});QVERIFY(hit.planeMousePath);QVERIFY(!hit.screen);
            QCOMPARE(hit.click.over.kind,uint8_t(turf ? proto::enc::RefTurf : proto::enc::RefMob));
            if(turf) {QCOMPARE(hit.click.over.x,uint16_t(5));QCOMPARE(hit.click.over.y,uint16_t(5));}
            else QCOMPARE(hit.click.over.ref,100u);
            QVERIFY(mapPointerCurrent(hit,f.world,17));
            if(turf) {
                f.world.tiles[{5,5,0}]=2;QVERIFY(!mapPointerCurrent(hit,f.world,17));
                f.world.tiles.erase({5,5,0});QVERIFY(!mapPointerCurrent(hit,f.world,17));
            } else {
                ++f.world.atoms[100].renderIdentity;QVERIFY(mapPointerCurrent(hit,f.world,17)); // other registry
                ++f.world.atoms[mob].appid;QVERIFY(!mapPointerCurrent(hit,f.world,17));
                f.world.atoms.erase(mob);QVERIFY(!mapPointerCurrent(hit,f.world,17));
            }
        }
    }
    void planeSourceLifetimeAndCoverage() {
        for (bool grouped : {false,true}) for (bool blur : {false,true}) {
        test::PlaneSourceFixture f;
        QJsonObject fields;
        if (blur) fields["filters"]=test::PlaneSourceFixture::blur("0000803f");
        if (grouped) { fields["appearanceFlags"]=0x04000204; fields["overlays"]=QJsonArray{4}; }
        f.relay(fields);
        // Stock blur adds visible ink here, but not an interactive source pixel.
        QCOMPARE(f.at({65,40}).click.over.kind,uint8_t(proto::enc::RefTurf));
        QCOMPARE(f.at({79,47}).click.over.kind,uint8_t(proto::enc::RefTurf));
        const auto hit=f.at({69,37});
        QCOMPARE(hit.click.over.ref,201u); QVERIFY(hit.relayHit && hit.relayHit->ink);
        QCOMPARE(hit.relayHit->producer.click.over.ref,200u);
        QCOMPARE(hit.relayHit->ink->click.over.ref,100u);
        QVERIFY(hit.relayHit->producer.renderedScene.empty()); QVERIFY(hit.relayHit->ink->renderedScene.empty());
        QVERIFY(mapPointerCurrent(hit,f.world,17));
        for (auto kind:{MapPointer::Hover,MapPointer::Drag,MapPointer::Drop,MapPointer::DoubleClick}) {
            auto gesture=hit; gesture.kind=kind; QVERIFY(!mapPointerCurrent(gesture,f.world,17));
        }
        QVERIFY(mapPointerStack(hit).empty()); // no invented plane-relay rectangle menu
        ++f.world.hudIdentity[200].membership; QVERIFY(!mapPointerCurrent(hit,f.world,17)); --f.world.hudIdentity[200].membership;
        ++f.world.hudIdentity[201].membership; QVERIFY(!mapPointerCurrent(hit,f.world,17)); --f.world.hudIdentity[201].membership;
        ++f.world.atoms[100].renderIdentity; QVERIFY(!mapPointerCurrent(hit,f.world,17)); --f.world.atoms[100].renderIdentity;
        f.world.resync.freed.insert(100); QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.resync.freed.clear();
        const auto master=f.world.appearances[2];
        setFields(f.world.appearances[2], R"({"appearanceFlags":268435972,"plane":61,"renderTarget":"*relay"})");
        QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.appearances[2]=master;
        f.world.appearances[5]=f.world.appearances[1];f.world.atoms[100].appid=5;
        QVERIFY(!mapPointerCurrent(hit,f.world,17));f.world.atoms[100].appid=1;
        f.relay({{"renderSource","*rebound"}}); QVERIFY(!mapPointerCurrent(hit,f.world,17));
        // Picking remains tied to the retained frame, not the newer worker/model scene.
        const auto old=mapPointerAt({69,37},{160,96},{5,3},hit.renderedSource,f.frame.geometry,"main.map",true,hit.renderedScene);
        QVERIFY(old); QCOMPARE(old->click.over.ref,201u);
        f.relay({});
        for (auto& s:f.sprites) if(s.atom==100) s.image.fill(Qt::transparent);
        QCOMPARE(f.at({69,37}).click.over.kind,uint8_t(proto::enc::RefTurf));
        QCOMPARE(old->renderedScene[0].image.pixelColor(5,5),QColor(255,220,20)); // retained image is immutable
        f.refresh();
        // A second selected plane with the same name is ambiguous, never guessed.
        auto duplicate=*std::find_if(f.sprites.begin(),f.sprites.end(),[](const auto& s){return s.kind==MapSprite::Master;});
        duplicate.plane=61;f.sprites.push_back(duplicate);
        QVERIFY(!f.at({69,37}).relayHit);
        f.refresh();
        for(auto& s:f.sprites) if(s.atom==100) s.renderSource="*chain";
        QVERIFY(!f.at({69,37}).relayHit);
        f.relay({{"appearanceFlags",double(0x80000204u)}}); // source-owned path, not a consumer-owned relayHit
        QVERIFY(!f.at({69,37}).relayHit);
        f.relay({{"overlays",QJsonArray{4}}});
        auto local=f.sprites.front();local.renderTarget="*relay";local.renderSource.clear();
        local.plane=80;local.localGroup=123;f.sprites.push_back(local);
        QVERIFY(!f.at({69,37}).relayHit); // an extended appearance cannot bind a local/plane collision
        f.relay({});
        const auto plan=CompositionPlan::build(f.sprites,f.frame.source,{true});
        const auto* surface=plan->planeSources.find("*relay");QVERIFY(surface);
        uint64_t work=0;QCOMPARE(PlaneSources::ink(*surface,f.sprites,{69,37},work),qsizetype(-1));
        for(auto& s:f.sprites) if(s.atom==100) s.tint[3]=0.5f;
        QVERIFY(f.at({69,37}).relayHit); // source material does not change original-ink coverage
        f.refresh();f.sprites.resize(65537);QVERIFY(!f.at({69,37}).relayHit);
        }
    }
    void groupedPlaneSourceRenderedOwnership() {
        test::PlaneSourceFixture f;
        f.relay({{"appearanceFlags",0x04000204},{"overlays",QJsonArray{4}},
                 {"filters",test::PlaneSourceFixture::blur("0000803f")}});
        const auto source=f.at({69,37});
        const auto hole=f.at({79,47});
        QVERIFY(source.relayHit && source.relayHit->ink);
        QCOMPARE(hole.click.over.kind,uint8_t(proto::enc::RefTurf));
        // A later ungrouped frame admits child ink, but cannot change the old
        // grouped frame's original-source coverage or fabricate child ownership.
        f.relay({{"overlays",QJsonArray{4}}});
        QCOMPARE(f.at({79,47}).click.over.ref,201u);
        const auto old=mapPointerAt({79,47},{160,96},{5,3},hole.renderedSource,f.frame.geometry,"main.map",true,hole.renderedScene);
        QVERIFY(old); QCOMPARE(old->click.over.kind,uint8_t(proto::enc::RefTurf));
        // Server appearance IDs are immutable; replacing the consumer appearance
        // invalidates even a source-only hit retained from the grouped frame.
        f.world.appearances[5]=f.world.appearances[3]; f.world.hudAppid[201]=5;
        QVERIFY(!mapPointerCurrent(source,f.world,17));
    }
    void planeSourceMatrixRenderedOwnership() {
        QFile file(QFINDTESTDATA("../../tests/data/relay-gate-world/matrix.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        test::PlaneSourceFixture f;
        for (const auto& value:test::PlaneSourceFixture::capturePixelObservations(file.readAll(),false)) {
            const auto row=value.toObject();
            if (!row["admitted"].toBool()) continue;
            f.observation(row);
            for (const auto& click:row["clicks"].toArray()) {
                const auto c=click.toArray(); if (!c[2].toBool()) continue;
                const QPoint p(c[0].toInt(),c[1].toInt()); const auto hit=f.at(p);
                QVERIFY(hit.relayHit && hit.relayHit->ink); QVERIFY(mapPointerCurrent(hit,f.world,17));
                const auto scene=hit.renderedScene;
                // A newly prepared hole cannot change the last rendered coverage
                // or the old viewport-space affine/material coordinate envelope.
                f.relay({{"renderSource","*absent"}});
                const auto old=mapPointerAt(p,{160,96},{5,3},f.frame.source,f.frame.geometry,"main.map",true,scene);
                QVERIFY(old); QCOMPARE(old->click.iconX,c[3].toInt()); QCOMPARE(old->click.iconY,c[4].toInt());
                QVERIFY(old->relayHit); QVERIFY(!mapPointerCurrent(hit,f.world,17));
                f.observation(row); QVERIFY(mapPointerCurrent(hit,f.world,17));
                ++f.world.hudIdentity[201].membership; QVERIFY(!mapPointerCurrent(hit,f.world,17)); --f.world.hudIdentity[201].membership;
                ++f.world.hudIdentity[200].membership; QVERIFY(!mapPointerCurrent(hit,f.world,17)); --f.world.hudIdentity[200].membership;
                ++f.world.atoms[100].renderIdentity;
                if (hit.relayHit->ink->click.over.ref==100u) QVERIFY(!mapPointerCurrent(hit,f.world,17));
                --f.world.atoms[100].renderIdentity;
                f.world.hudAppid[201]=5; QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.hudAppid[201]=3;
            }
        }
    }
    void planeSourceCaptureCoverage() {
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
        for (const auto& value:rows) {
            const auto row=value.toObject(); f.sourceObservation(row);
            for (const auto& click:row["clicks"].toArray()) {
                const auto c=click.toArray(); const QPoint p(c[0].toInt(),c[1].toInt()); const auto hit=f.at(p);
                QVERIFY2(bool(hit.relayHit)==c[2].toBool(),qPrintable(row["name"].toString()));
                QCOMPARE(hit.click.iconX,c[3].toInt()); QCOMPARE(hit.click.iconY,c[4].toInt());
                QCOMPARE(hit.click.hasVis,c[5].toInt()!=0);
                if (hit.click.hasVis) { QCOMPARE(hit.click.visX,c[5].toInt()); QCOMPARE(hit.click.visY,c[6].toInt()); }
                QVERIFY(mapPointerCurrent(hit,f.world,17));
                if (!hit.relayHit) continue;
                QCOMPARE(hit.click.over.ref,201u);
                const auto contributor=hit.relayHit->ink->click.over.ref;
                if (row.contains("overlap") && p==QPoint(69,37)) QCOMPARE(contributor,101u);
                const auto atom=f.world.atoms.at(contributor);
                f.world.atoms.erase(contributor); QVERIFY(!mapPointerCurrent(hit,f.world,17));
                f.world.atoms[contributor]=atom; ++f.world.atoms[contributor].renderIdentity;
                QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.atoms[contributor]=atom;
                f.world.atoms[contributor].appid=99; QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.atoms[contributor]=atom;
                for (uint32_t endpoint:{200u,201u}) {
                    const auto identity=f.world.hudIdentity.at(endpoint);
                    f.world.hudIdentity.erase(endpoint); QVERIFY(!mapPointerCurrent(hit,f.world,17));
                    f.world.hudIdentity[endpoint]=identity; ++f.world.hudIdentity[endpoint].membership;
                    QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.hudIdentity[endpoint]=identity;
                    const auto appid=f.world.hudAppid.at(endpoint); f.world.hudAppid[endpoint]=99;
                    QVERIFY(!mapPointerCurrent(hit,f.world,17)); f.world.hudAppid[endpoint]=appid;
                }
                f.relay({{"renderSource","*changed"}}); QVERIFY(!mapPointerCurrent(hit,f.world,17));
                const auto old=mapPointerAt(p,{160,96},{5,3},hit.renderedSource,f.frame.geometry,"main.map",true,hit.renderedScene);
                QVERIFY(old && old->relayHit); QCOMPARE(old->click.iconX,c[3].toInt()); QCOMPARE(old->click.iconY,c[4].toInt());
                f.sourceObservation(row); QVERIFY(mapPointerCurrent(hit,f.world,17));
            }
        }
        const test::PlaneSourceFixture plain;
        const auto original=plain.sprites;
        for (float alpha:{0.f,1.f/255,0.13f,0.5f,1.f}) {
            f.sprites=original;
            for (auto& s:f.sprites) if(s.atom==100 || s.kind==MapSprite::Master) {
                s.tint[3]=alpha; s.color[3]=2; s.color[19]=-1; // finite alpha-changing matrix class, not a value whitelist
            }
            QVERIFY(f.at({69,37}).relayHit); QVERIFY(!f.at({79,47}).relayHit);
        }
        // Whole surface refusal: a second unsupported contributor cannot leave
        // the first one's seemingly valid original mask active.
        for (int refusal=0;refusal<7;++refusal) {
            f.sprites=original; auto extra=f.sprites.front(); extra.position={120,0};
            switch(refusal) {
            case 0: extra.blend=2; break;
            case 1: extra.color[0]=std::numeric_limits<float>::infinity(); break;
            case 2: extra.renderSource="*chain"; break;
            case 3: extra.localGroup=5; break;
            case 4: extra.size={16,16}; break;
            case 5: extra.maptext=true; break;
            case 6: extra.planeEffects=std::make_shared<PlaneEffects>(); break;
            }
            f.sprites.push_back(extra); QVERIFY(!f.at({69,37}).relayHit);
            f.sprites=original; QVERIFY(f.at({69,37}).relayHit);
        }
    }
    void capturedTurfHasNoFallback() {
        MapGeometry geometry{{0,0,1,1},{1,1},32,0,1};
        geometry.visibility=std::make_shared<const VisibilityFrame>(); // immutable unified-scene marker
        MapSprite turf; turf.kind=MapSprite::Turf; turf.cell={1,1}; turf.iconBottom={0,32};
        turf.image=QImage(32,32,QImage::Format_RGBA8888); turf.image.fill(Qt::white);
        turf.image.setPixelColor(0,16,Qt::transparent);
        MapSprite master; master.kind=MapSprite::Master; master.renderTarget="*capture";
        MapSprite source; source.kind=MapSprite::Screen; source.plane=5; source.renderSource="*capture";
        source.mouseTransparent=true;
        auto pick=[&](MapSprites sprites,QPoint p=QPoint(16,16)) {
            return *mapPointerAt(p,{32,32},{1,1},{0,0,32,32},geometry,"main.map",true,sprites);
        };
        QCOMPARE(pick({turf}).click.over.kind,uint8_t(proto::enc::RefTurf));
        QVERIFY(pick({turf},{0,16}).backgroundHidden); // a transparent turf pixel is not a cell hit
        QVERIFY(pick({}).backgroundHidden); // iconless/absent admitted turf
        const auto captured=pick({turf,master,source});
        QVERIFY(captured.backgroundHidden); QCOMPARE(captured.click.over.kind,uint8_t(proto::enc::RefNull));
        QCOMPARE(captured.click.screenX,1); QCOMPARE(captured.click.screenPX,17);
        QCOMPARE(pick({turf,master}).click.over.kind,uint8_t(proto::enc::RefTurf)); // producer without consumer
        master.renderTarget="capture"; source.renderSource="capture";
        QCOMPARE(pick({turf,master,source}).click.over.kind,uint8_t(proto::enc::RefTurf)); // directly shown producer
        master.renderTarget.clear(); master.tint[3]=0;
        QCOMPARE(pick({turf,master}).click.over.kind,uint8_t(proto::enc::RefTurf));
        master.renderTarget="*capture"; master.tint[3]=1; source.renderSource="*capture";
        auto show=master; show.renderTarget.clear();
        QCOMPARE(pick({turf,master,source,show}).click.over.kind,uint8_t(proto::enc::RefTurf));
        QVERIFY(pick({turf,show,source,master}).backgroundHidden); // last-master-wins
        auto object=turf; object.kind=MapSprite::Entity; object.plane=1; object.atom=42; object.targetIdentity=7;
        const auto pressed=pick({turf,master,source,object}); QCOMPARE(pressed.click.over.ref,42u);
        auto hud=object; hud.kind=MapSprite::Screen; hud.screenInteractive=true; hud.atom=50;
        QCOMPARE(pick({turf,master,source,hud}).click.over.ref,50u);
        model::World w; w.bounds.maxX=w.bounds.maxY=1;
        auto& atom=w.atoms[42]; atom.hasPos=true; atom.renderIdentity=7;
        auto hidden=captured; hidden.kind=MapPointer::Hover;
        QVERIFY(!mapPointerCurrent(hidden,w,1));
        MapHover hover; QVERIFY(!hover.update(pressed,true,true,true).empty());
        QCOMPARE(hover.update(hidden,false,true,true),proto::enc::encodeHoverLeave());
        hidden.kind=MapPointer::Drop; hidden.source=std::make_shared<const MapPointer>(pressed);
        QVERIFY(mapPointerCurrent(hidden,w,1)); // null drop keeps its rendered source identity
        ++atom.renderIdentity; QVERIFY(!mapPointerCurrent(hidden,w,1));
        geometry.visibility.reset(); // retained flattened-reference compatibility, not live MAP
        QCOMPARE(pick({}).click.over.kind,uint8_t(proto::enc::RefTurf));
    }
    void presentationGeometry() {
        MapPresentationSettings settings;
        const QSize scene(96, 64), view(3, 2);
        MapGeometry geometry{{0,0,3,2},{20,20},32,0,1};
        auto p = mapPresentation({240,240}, scene, 32, settings);
        QCOMPARE(p.destination, QRectF(0,40,240,160));
        QVERIFY(p.linear); // 2.5x
        auto hit = [&](QPointF point) { return mapPointerAtPresentation(point, p.destination, view,
            {0,0,96,64}, geometry, "main.map", true); };
        QCOMPARE(hit({80.1,80})->click.screenX, 2);
        QCOMPARE(hit({80.1,80})->click.screenPX, 1);
        QCOMPARE(hit({80,20})->click.over.kind, uint8_t(proto::enc::RefNull));
        settings.letterbox = false;
        p = mapPresentation({240,240},scene,32,settings);
        QCOMPARE(p.destination, QRectF(-60,0,360,240));
        QCOMPARE(hit({0,10})->click.screenPX, 17); // centered cropped coverage
        settings.zoom = 2;
        p = mapPresentation({240,240},scene,32,settings);
        QCOMPARE(p.destination, QRectF(24,56,192,128)); QVERIFY(p.linear); QVERIFY(p.sharpen);
        QCOMPARE(hit({88,60})->click.screenX, 2);
        settings.mode = MapPresentationSettings::Blur;
        QVERIFY(mapPresentation({240,240},scene,32,settings).linear);
        QVERIFY(!mapPresentation({240,240},scene,32,settings).sharpen);
        settings.zoom = 1.5; settings.mode = MapPresentationSettings::Distort;
        QVERIFY(!mapPresentation({240,240},scene,32,settings).linear);
        QVERIFY(!mapPresentation({240,240},scene,32,settings).sharpen);
        settings.zoom = 0; settings.iconSize = 48;
        QCOMPARE(mapPresentation({240,240},scene,16,settings).scale, 3.0);
        QCOMPARE(mapPresentation({240,240},scene,32,settings).scale, 1.5);
        settings.zoom = 2; // explicit zoom wins when both defaults are positive
        QCOMPARE(mapPresentation({240,240},scene,16,settings).scale, 2.0);
        settings.mode = MapPresentationSettings::Normal;
        settings.zoom = std::nextafter(2.0, 3.0);
        QVERIFY(mapPresentation({240,240},scene,32,settings).linear);
        settings.zoom = std::nextafter(2.0, 1.0);
        QVERIFY(mapPresentation({240,240},scene,32,settings).linear);
        settings.zoom = 1e300;
        QVERIFY(mapPresentation({240,240},scene,32,settings).destination.isEmpty());
        settings.zoom = 1e-300;
        const auto tiny = mapPresentation({240,240},scene,32,settings);
        QVERIFY(!mapPointerAtPresentation({0,0},tiny.destination,view,{0,0,96,64},geometry,"main.map",true));
        settings.zoom = 0; settings.iconSize = 0; settings.letterbox = true;
        QCOMPARE(mapPresentation({241,160},scene,32,settings).destination, QRectF(0.5,0,240,160));
    }
    void dropAdoptsImplicitServerHover() {
        MapPointer source;source.kind=MapPointer::Hover;source.click.over=proto::enc::objRef(42);
        source.click.control="main.map";
        MapHover hover;QVERIFY(!hover.update(source,true,true,true).empty());
        auto destination=source;destination.click.over=proto::enc::objRef(43);
        destination.kind=MapPointer::Drop;destination.click.button="left";
        destination.source=std::make_shared<MapPointer>(source);
        hover.dropped(destination); // no extra Hover packet/Exit between Drag and Drop
        auto next=destination;next.kind=MapPointer::Hover;next.source.reset();next.click.button.clear();
        QVERIFY(hover.update(next,true,true,true).empty());
        QCOMPARE(hover.update(source,true,true,true),mapPointerBody(source)); // exact return must enter
        hover.dropped(destination);
        next.kind=MapPointer::Leave;
        QCOMPARE(hover.update(next,true,false,false),proto::enc::encodeHoverLeave());
    }
    void hoverTransitionsAndStaleIdentities() {
        MapPointer p; p.kind = MapPointer::Hover; p.session = 7;
        p.click.over = proto::enc::objRef(42); p.click.loc = proto::enc::nullRef();
        p.click.control = "world.map"; p.screen = true; p.haveAtomKey = true; p.atomKey = 9;
        p.click.iconX = 3; p.click.iconY = 4; p.click.screenX = 1; p.click.screenY = 2;
        model::World w; w.hud[42]={}; w.hudIdentity[42] = {2,9};
        w.hudAppid[42]=1; setFields(w.appearances[1], R"({"screenLoc":"1,1"})");
        QVERIFY(mapPointerCurrent(p,w,7));
        QVERIFY(!mapPointerCurrent(p,w,8));
        MapHover hover;
        QVERIFY(hover.update(p,true,false,false).empty());
        QCOMPARE(hover.update(p,true,true,false), mapPointerBody(p));
        QCOMPARE(mapPointerBody(p)[0], proto::enc::kMouseHover);
        auto bytes = mapPointerBody(p);
        QByteArray packet(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
        QVERIFY(packet.contains("icon-x=3;icon-y=4;screen-loc=")); QVERIFY(!packet.contains("button="));
        QVERIFY(hover.update(p,true,true,false).empty());
        p.click.iconX++;
        QVERIFY(hover.update(p,true,true,false).empty()); // enter/exit only
        p.click.iconX++;
        QCOMPARE(hover.update(p,true,true,true), mapPointerBody(p));
        w.hudIdentity[42].key++;
        QVERIFY(!mapPointerCurrent(p,w,7));
        QCOMPARE(hover.update(p,false,true,true), proto::enc::encodeHoverLeave());
        QVERIFY(hover.update(p,false,true,true).empty());
        p.atomKey++; QVERIFY(mapPointerCurrent(p,w,7));
        QVERIFY(!hover.update(p,true,true,false).empty());
        p.kind = MapPointer::Leave;
        QCOMPARE(hover.update(p,true,false,false), proto::enc::encodeHoverLeave());
        QVERIFY(hover.update(p,true,false,false).empty());
        // Image hits route to the target, but both image and target lifetimes are checked.
        p.kind = MapPointer::Hover; p.screen = false; p.atomKey = 9;
        auto& atom = w.atoms[42]; atom.hasPos = atom.haveKey = true; atom.key = 9; atom.renderIdentity = 10;
        w.imageInstances[1] = {2,42,1,false,20,10};
        p.imageId = 1; p.imageIdentity = 20; p.targetIdentity = 10;
        QVERIFY(mapPointerCurrent(p,w,7));
        w.imageInstances[1].identity++;
        QVERIFY(!mapPointerCurrent(p,w,7));
        w.imageInstances[1].identity--; atom.renderIdentity++;
        QVERIFY(!mapPointerCurrent(p,w,7));
        net::InputModel input;
        Bytes flags(20); flags[15] = 0x82; flags[16] = 1;
        input.onFrame(0x003b, flags);
        QVERIFY(input.clientHover()); QVERIFY(input.clientMove());
        flags[15] = 0; input.onFrame(0x003b,flags);
        QVERIFY(!input.clientHover()); QVERIFY(!input.clientMove());
    }
    void pointerTarget() {
        model::World w; w.viewZ = 2; w.bounds.maxX = w.bounds.maxY = 10;
        w.atoms[42].appid = 5; w.atoms[42 | model::kRegistryA].appid = 6; w.hudAppid[42] = 9;
        MapPointer p; p.session = 7; p.z = 2;
        // A vis_contents mob member is the mob, on the map and under a HUD holder alike, never obj or HUD ref 42.
        auto member = std::make_shared<VisHit>(); member->target = 42 | model::kRegistryA;
        p.click.over = proto::enc::mobRef(42); p.visHit = member;
        for (bool screen : {false, true}) {
            p.screen = screen;
            const auto named = mapPointerTarget(p,w);
            QVERIFY(named && named->appid); QCOMPARE(named->kind,MapPointerTarget::Atom);
            QCOMPARE(named->atom,42u | model::kRegistryA); QCOMPARE(*named->appid,6u);
        }
        p.visHit.reset();
        auto named = mapPointerTarget(p,w); QCOMPARE(named->kind,MapPointerTarget::Hud); QCOMPARE(*named->appid,9u);
        p.screen = false; QCOMPARE(*mapPointerTarget(p,w)->appid,6u);
        p.click.over = proto::enc::objRef(42); QCOMPARE(*mapPointerTarget(p,w)->appid,5u);
        p.click.over = proto::enc::objRef(77); named = mapPointerTarget(p,w); QVERIFY(named && !named->appid);
        // A turf on the map is the cell on the map's z; a copied turf is its own cell on its own z.
        p.click.over = proto::enc::turfRef(4,5);
        const uint16_t x = uint16_t(p.click.over.x), y = uint16_t(p.click.over.y);
        w.tiles[{x,y,2}] = 7; w.tiles[{uint16_t(x+1),y,5}] = 8;
        named = mapPointerTarget(p,w); QCOMPARE(named->kind,MapPointerTarget::Turf);
        QCOMPARE(named->cell,(std::array<uint16_t,3>{x,y,2})); QCOMPARE(*named->appid,7u);
        auto copied = std::make_shared<VisHit>(); copied->turf = true; copied->cell = {uint16_t(x+1),y,5};
        p.visHit = copied; named = mapPointerTarget(p,w);
        QCOMPARE(named->cell,copied->cell); QCOMPARE(*named->appid,8u);
        p.visHit.reset(); p.click.over = proto::enc::nullRef(); QVERIFY(!mapPointerTarget(p,w));

    }
    void dragEncodingAndLifetime() {
        MapPointer src; src.session = 7; src.z = 2;
        src.click.over = proto::enc::objRef(42); src.click.loc = proto::enc::turfRef(3,4);
        src.haveAtomKey = true; src.atomKey = 9; src.targetIdentity = 10;
        model::World w; w.viewZ = 2;
        auto& atom = w.atoms[42]; atom.hasPos = atom.haveKey = true;
        atom.key = 9; atom.z = 2; atom.renderIdentity = 10;
        w.bounds.maxX = w.bounds.maxY = 10;
        MapPointer p = src; p.kind = MapPointer::Drag;
        p.click.over = proto::enc::objRef(43); p.click.loc = proto::enc::nullRef();
        p.screen = true; p.atomKey = 11; p.targetIdentity = 20;
        w.hud[43]={}; w.hudIdentity[43] = {2,11,20};
        w.hudAppid[43]=1; setFields(w.appearances[1], R"({"screenLoc":"1,1"})");
        p.source = std::make_shared<const MapPointer>(src);
        p.click.mods = {true,true,true}; p.click.iconX = 5; p.click.iconY = 6;
        QVERIFY(mapPointerCurrent(p,w,7));
        proto::enc::DragCmd expected;
        expected.src = src.click.over; expected.srcLoc = src.click.loc;
        expected.over = p.click.over; expected.overLoc = p.click.loc;
        expected.params = proto::enc::clickParams(p.click);
        QCOMPARE(mapPointerBody(p), proto::enc::encodeDragCmd(expected));
        p.kind = MapPointer::Drop; expected.drop = true;
        QCOMPARE(mapPointerBody(p), proto::enc::encodeDragCmd(expected));
        const auto body = mapPointerBody(p);
        QByteArray golden = QByteArray::fromHex("0700920000ffff00000700012a00000000012b0000000003020003000000ffff0000");
        golden += QByteArrayLiteral("\2mapwindow.map\0\2mapwindow.map\0");
        golden += QByteArrayLiteral("\2icon-x=5;icon-y=6;left=1;button=left;ctrl=1;shift=1;alt=1;screen-loc=1:5,1:6\0");
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(body.data()),qsizetype(body.size())),golden);
        auto hooks = proto::enc::MouseHooks{};
        QVERIFY(!mapPointerWanted(MapPointer::Drag,hooks,false));
        QVERIFY(!mapPointerWanted(MapPointer::Drop,hooks,false));
        hooks.drop = true; QVERIFY(mapPointerWanted(MapPointer::Drop,hooks,false));
        hooks.drag = true; QVERIFY(mapPointerWanted(MapPointer::Drag,hooks,false));
        // Same wire slot/key but a new local lifetime must not receive a gesture.
        atom.renderIdentity++; QVERIFY(!mapPointerCurrent(p,w,7)); atom.renderIdentity--;
        w.resync.freed.insert(42); QVERIFY(!mapPointerCurrent(p,w,7)); w.resync.freed.clear();
        w.hud.erase(43); QVERIFY(!mapPointerCurrent(p,w,7)); w.hud[43]={};
        w.hudIdentity[43].membership++; QVERIFY(!mapPointerCurrent(p,w,7)); w.hudIdentity[43].membership--;
        QVERIFY(!mapPointerCurrent(p,w,8)); w.viewZ++; QVERIFY(!mapPointerCurrent(p,w,7)); w.viewZ--;
        auto imageSource = src; imageSource.imageIdentity = 1;
        p.source = std::make_shared<const MapPointer>(imageSource);
        QVERIFY(!mapPointerCurrent(p,w,7));
        auto visSource = src; visSource.visHit = std::make_shared<VisHit>();
        p.source = std::make_shared<const MapPointer>(visSource);
        QVERIFY(!mapPointerCurrent(p,w,7));
        auto hudSource = p; hudSource.source.reset(); hudSource.kind = MapPointer::Down;
        auto worldDest = src; worldDest.kind = MapPointer::Drop;
        worldDest.source = std::make_shared<const MapPointer>(hudSource);
        QVERIFY(mapPointerCurrent(worldDest,w,7));
        w.hudIdentity[43].membership++; QVERIFY(!mapPointerCurrent(worldDest,w,7)); w.hudIdentity[43].membership--;
        w.hud.erase(43); QVERIFY(!mapPointerCurrent(worldDest,w,7)); w.hud[43]={};
        p.source = std::make_shared<const MapPointer>(src);
        p.outside = true; p.screen = false; p.targetIdentity = 0;
        p.click.over = p.click.loc = proto::enc::nullRef(); p.click.control.clear();
        expected.over = expected.overLoc = proto::enc::nullRef(); expected.overControl.clear();
        expected.overControlNull = expected.overLocBlank = true;
        // Outside every window the drop carries the press's cell without icon-x/icon-y (stock, dragout-world).
        expected.params = proto::enc::clickParams(p.click);
        expected.params.erase(0, expected.params.find(p.click.button + "=1"));
        QVERIFY(mapPointerCurrent(p,w,7));
        QCOMPARE(mapPointerBody(p),proto::enc::encodeDragCmd(expected));
        auto surface = std::make_shared<const uint8_t>(0);
        src.surface = surface; p.source = std::make_shared<const MapPointer>(src);
        QVERIFY(mapPointerCurrent(p,w,7)); surface.reset();
        QVERIFY(!mapPointerCurrent(p,w,7)); // queued gesture after skin/MAP destruction
    }
    void coordinates() {
        MapGeometry geometry{QRect(10, 20, 6, 6), QSize(100, 100), 32, 3, 17};
        // Two tiles in a 200x100 control: 50 px horizontal letterbox on either side.
        auto p = mapPointerAt({50, 0}, {200, 100}, {2, 2}, QRect(64, 64, 64, 64), geometry, "world.map", true);
        QVERIFY(p);
        QCOMPARE(p->click.over.kind, uint8_t(proto::enc::RefTurf));
        QCOMPARE(p->click.over.x, uint16_t(12));
        QCOMPARE(p->click.over.y, uint16_t(23));
        QCOMPARE(p->click.iconX, 1);
        QCOMPARE(p->click.iconY, 32);
        QCOMPARE(p->click.screenX, 1);
        QCOMPARE(p->click.screenY, 2);
        QCOMPARE(p->z, 3);
        QCOMPARE(p->session, uint64_t(17));
        auto glide = mapPointerAt({50, 0}, {200, 100}, {2, 2}, QRect(48, 80, 64, 64), geometry, "world.map", true);
        QCOMPARE(glide->click.over.x, uint16_t(11));
        QCOMPARE(glide->click.over.y, uint16_t(23));
        QCOMPARE(glide->click.iconX, 17);
        QCOMPARE(glide->click.iconY, 16);
        QCOMPARE(glide->click.screenPX, 1); // screen pixel and icon pixel differ while gliding
        QCOMPARE(glide->click.screenPY, 32);
        auto scaled = mapPointerAt({100, 0}, {400, 200}, {2, 2}, QRect(48, 80, 64, 64), geometry, "world.map", true);
        QCOMPARE(proto::enc::clickParams(scaled->click), proto::enc::clickParams(glide->click));
        auto box = mapPointerAt({49, 50}, {200, 100}, {2, 2}, QRect(64, 64, 64, 64), geometry, "world.map", true);
        QCOMPARE(box->click.over.kind, uint8_t(proto::enc::RefNull));
        QVERIFY(!mapPointerAt({}, {}, {}, {}, {}, "world.map", true));
        auto large = mapPointerAt({63, 0}, {64, 64}, {1, 1}, QRect(0, 0, 64, 64),
                                  {QRect(0, 0, 1, 1), QSize(10, 10), 64, 0, 1}, "world.map", true);
        QVERIFY(large);
        QVERIFY(QString::fromStdString(proto::enc::clickParams(large->click)).contains("screen-loc=1:64,1:64"));
    }
    void encodingAndSubscriptions() {
        auto p = mapPointerAt({10, 10}, {64, 64}, {2, 2}, QRect(0, 0, 64, 64),
                              {QRect(0, 0, 2, 2), QSize(10, 10), 32, 0, 1}, "world.camera", false);
        QVERIFY(p);
        QCOMPARE(p->click.control, std::string("world.camera"));
        QCOMPARE(p->click.locStr, std::string("camera"));
        p->click.mods = {true, true, true};
        p->click.button = "right";
        p->kind = MapPointer::Down;
        QCOMPARE(mapPointerBody(*p)[0], proto::enc::kMouseDown);
        p->kind = MapPointer::Up;
        QCOMPARE(mapPointerBody(*p)[0], proto::enc::kMouseUp);
        p->kind = MapPointer::DoubleClick;
        QCOMPARE(mapPointerBody(*p)[0], proto::enc::kMouseDblClick);
        p->kind = MapPointer::Click;
        auto bytes = mapPointerBody(*p);
        QByteArray packet(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
        // A click names a second map by its full id; a press by its short one (stock, secmapmouse-world).
        QVERIFY(packet.contains("right=1;button=right;ctrl=1;shift=1;alt=1;screen-loc=world.camera:"));
        p->kind = MapPointer::Down; bytes = mapPointerBody(*p);
        QVERIFY(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).contains("screen-loc=camera:"));
        p->kind = MapPointer::Click;
        p->kind = MapPointer::Wheel; p->dx = -1; p->dy = 1; p->click.button.clear();
        bytes = mapPointerBody(*p);
        QCOMPARE(bytes[0], proto::enc::kMouseExt);
        QCOMPARE(bytes[2], uint8_t(0x0a));
        packet = QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
        QVERIFY(packet.contains("screen-loc=camera:"));
        QVERIFY(!packet.contains("button="));
        proto::enc::MouseHooks hooks;
        QVERIFY(!mapPointerWanted(MapPointer::Click, hooks, false));
        hooks.click = true;
        QVERIFY(mapPointerWanted(MapPointer::Click, hooks, false));
        QVERIFY(!mapPointerWanted(MapPointer::Down, hooks, false));
        QVERIFY(!mapPointerWanted(MapPointer::Wheel, hooks, false));
        QVERIFY(mapPointerWanted(MapPointer::Wheel, hooks, true));
    }
};
FUGUE_TEST_GUILESS_MAIN(MapInputTest)
#include "mapinput_test.moc"
