// Wire-backed runechat placement, fade, deletion and MeasureText regressions.
// Uses Qt glyph rasterization; does not establish stock GDI glyph/metric parity.
#include <QtTest>
#include "tests/testmain.h"
#include <QFontDatabase>
#include <fstream>
#include "shell/entitymap.h"
#include "net/measuretext.h"
#include "net/textmacro.h"
using namespace fugue;
namespace {
constexpr int64_t t0=1'000'000'000'000'000'000, tick=100'000'000;
const QSize textSize{98,18}; // the runechat's 96x16 box and its 1px outline, which reaches a pixel past it on every side
std::string slurp(const std::string& path) { std::ifstream f(path,std::ios::binary); return {(std::istreambuf_iterator<char>(f)),{}}; }
}
class Runechat : public QObject {
    Q_OBJECT
    std::vector<std::pair<uint16_t,Bytes>> frames;
    std::map<uint32_t,QByteArray> resources;
    QString sheet;
    shell::TurfMap terrain;
    shell::EntityMap entities;
    void ingest(model::Session& s,size_t last) {
        s.setIngestClockNs(t0);
        for(size_t i=0;i<frames.size() && i+2<=last;++i) s.ingest(frames[i].first,frames[i].second);
        for(const auto& [id,b]:resources) s.noteResource(id,Bytes(b.begin(),b.end()),"");
    }
    shell::MapSprites scene(const model::Session& s,int64_t now) {
        auto read=[&](uint32_t id,QByteArray& out) { auto it=resources.find(id); if(it==resources.end()) return false; out=it->second; return true; };
        const auto frame=terrain.prepare(s.world(),read,now,false);
        entities.setMapTextStyle({sheet,{},0});
        auto sprites=entities.prepare(s.world(),frame,read,now,nullptr,true);
        // Translate to world coordinates so camera recentering is not the oracle.
        for(auto& sprite:sprites) sprite.position-=QPoint(-frame.geometry.region.x()*32,(frame.geometry.region.y()+frame.geometry.region.height())*32);
        return sprites;
    }
    static const shell::MapSprite* plane(const shell::MapSprites& sprites,int p,QSize size) {
        for(const auto& sprite:sprites) if(sprite.plane==p && sprite.image.size()==size) return &sprite;
        return nullptr;
    }
private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName()=="offscreen") {
            const auto fonts=qEnvironmentVariable("WINDIR")+"/Fonts"; qputenv("QT_QPA_FONTDIR",fonts.toUtf8());
            QVERIFY(QFontDatabase::addApplicationFont(fonts+"/arial.ttf")>=0);
        }
#endif
        const std::string dir=std::string(FUGUE_TEST_DATA_DIR)+"/runechat-world";
        for(const char* name:{"4c895ab0","5c894aea","6075a274"}) resources[uint32_t(std::stoul(name,nullptr,16))]=QByteArray::fromStdString(slurp(dir+"/res/"+name));
        const auto skin=resources.at(0x6075a274); const auto at=skin.indexOf("style = \".center"); QVERIFY(at>=0);
        const auto first=skin.indexOf('"',at), last=skin.indexOf('"',first+1); sheet=QString::fromUtf8(skin.mid(first+1,last-first-1));
        std::ifstream f(dir+"/frames.txt"); QVERIFY(f); std::string line; std::getline(f,line);
        while(std::getline(f,line)) {
            const auto split=line.find(' ',line.find("len="));
            const auto b=split==std::string::npos ? QByteArray{} : QByteArray::fromHex(QByteArray::fromStdString(line.substr(split+1)));
            frames.push_back({uint16_t(std::stoul(line.substr(2,4),nullptr,16)),Bytes(b.begin(),b.end())});
        }
        QCOMPARE(frames.size(),size_t(89));
    }
    void bubbleAndRunechatAreDrawn() {
        model::Session s; ingest(s,46); QCOMPARE(s.world().images.size(),size_t(2));
        const auto sprites=scene(s,t0+10'000'000'000LL);
        const auto* bubble=plane(sprites,16,{32,32}); QVERIFY(bubble);
        QCOMPARE(bubble->position,QPoint(12*32,-10*32)); // known-good icon control
        const auto* text=plane(sprites,25,textSize); QVERIFY(text); QVERIFY(text->maptext); QVERIFY(text->mouseTransparent);
        QCOMPARE(text->position,bubble->position+QPoint(-32-1,32-30-16-1)); QCOMPARE(text->tint[3],1.f);
        int gold=0,black=0;
        for(int y=0;y<text->image.height();++y) for(int x=0;x<text->image.width();++x) { const auto c=text->image.pixelColor(x,y); if(!c.alpha()) continue;
            if(c.red()>200 && c.green()>150 && c.blue()<50) ++gold;
            if(c.red()==0 && c.green()==0 && c.blue()==0) ++black;
        }
        QVERIFY(gold>0); QVERIFY(black>0);
    }
    void runechatFadesInWithoutNewRasters() {
        model::Session s; ingest(s,45);
        const auto before=scene(s,t0+tick/2); const auto* hidden=plane(before,25,textSize); QVERIFY(!hidden || hidden->tint[3]==0);
        const auto mid=scene(s,t0+2*tick); const auto* text=plane(mid,25,textSize); QVERIFY(text);
        QVERIFY(text->tint[3]>.39f && text->tint[3]<.63f);
        const auto later=scene(s,t0+tick*5/2); const auto* next=plane(later,25,textSize); QVERIFY(next);
        QCOMPARE(text->image.cacheKey(),next->image.cacheKey()); QVERIFY(next->tint[3]>text->tint[3]);
    }
    void bubbleLeavesWithItsDelete() {
        model::Session s; ingest(s,48); QCOMPARE(s.world().images.size(),size_t(1));
        const auto sprites=scene(s,t0+10'000'000'000LL); QVERIFY(!plane(sprites,16,{32,32})); QVERIFY(plane(sprites,25,textSize));
    }
    void markerIsNotDrawn() {
        model::Session plain,marked; ingest(plain,46); const auto a=scene(plain,t0+10'000'000'000LL);
        const std::string hello="Hello there 1";
        for(auto& [type,body]:frames) if(type==0x0026) {
            const auto at=std::search(body.begin(),body.end(),hello.begin(),hello.end()); if(at!=body.end()) body.insert(at,{0xff,0x16});
        }
        ingest(marked,46); const auto b=scene(marked,t0+10'000'000'000LL);
        const auto* x=plane(a,25,textSize); const auto* y=plane(b,25,textSize); QVERIFY(x && y); QCOMPARE(x->image,y->image);
    }
    void recordedMeasureRequestUsesDrawingLayout() {
        shell::MapTextCache cache; cache.setStyle({sheet,{},0});
        for(const auto& [type,body]:frames) if(type==0x00fc) {
            const auto reply=net::measureTextReply(body,[&](const std::string& text,const std::string& style,uint16_t width) {
                const auto size=cache.measure(QString::fromStdString(net::plainText(text)),QString::fromStdString(style),width);
                return std::pair<uint16_t,uint16_t>{uint16_t(size.width()),uint16_t(size.height())};
            });
            QVERIFY(reply); QCOMPARE(reply->size(),size_t(8)); QCOMPARE((*reply)[0],uint8_t(96)); QVERIFY((*reply)[2]>0 && (*reply)[2]<32);
            return;
        }
        QFAIL("missing historical MeasureText request");
    }
};
FUGUE_TEST_MAIN(Runechat)
#include "runechat_test.moc"
