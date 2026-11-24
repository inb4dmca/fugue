// Wire-backed vis_contents animation regressions: playback starts, state changes,
// holder changes, shared clocks and list ordering. Uses the flat-sprite CPU
// fixture, not GPU presentation.
#include <QtTest>
#include "tests/testmain.h"
#include <fstream>
#include <set>
#include "shell/tests/animationfixture.h"
using namespace fugue;
namespace {
constexpr int64_t ds = 100'000'000, t0 = 1'000'000'000'000'000'000;
constexpr int64_t phase[] = {t0,t0+100*ds,t0+160*ds,t0+220*ds,t0+280*ds};
constexpr size_t phaseLine[] = {0,31,41,44,50};
constexpr uint32_t res = 0x3b42c8e8;
std::string slurp(const std::string& p) { std::ifstream f(p,std::ios::binary); return {(std::istreambuf_iterator<char>(f)),{}}; }
}
class VisAnim : public QObject {
    Q_OBJECT
    std::vector<std::pair<uint16_t,Bytes>> frames;
    std::string bytes;
    void ingest(model::Session& s, size_t last) {
        for (size_t i=0;i<frames.size() && i+2<=last;++i) {
            int ph=0; while(ph<4 && i+2>=phaseLine[ph+1]) ++ph;
            s.setIngestClockNs(phase[ph]); s.ingest(frames[i].first,frames[i].second);
        }
        s.noteResource(res,Bytes(bytes.begin(),bytes.end()),"");
    }
    QImage map(const model::Session& s,int64_t clock) {
        AnimationFixture fixture; return fixture.image(s.world(),res,bytes,clock);
    }
    static uint32_t tile(const QImage& f,int x,int y) {
        if(f.width()!=288 || f.height()!=288) return 0;
        return f.pixelColor((x-1)*32+16,f.height()-y*32+16).rgb()&0xffffff;
    }
private slots:
    void initTestCase() {
        const std::string dir=std::string(FUGUE_TEST_DATA_DIR)+"/visanim-world";
        bytes=slurp(dir+"/res/3b42c8e8"); QVERIFY(!bytes.empty());
        std::ifstream f(dir+"/frames.txt"); QVERIFY(f);
        std::string line; std::getline(f,line);
        while(std::getline(f,line)) {
            const auto at=line.find(' ',line.find("len="));
            const auto b=at==std::string::npos ? QByteArray{} : QByteArray::fromHex(QByteArray::fromStdString(line.substr(at+1)));
            frames.push_back({uint16_t(std::stoul(line.substr(2,4),nullptr,16)),Bytes(b.begin(),b.end())});
        }
        QCOMPARE(frames.size(),size_t(53));
    }
    void finiteStatePlaysFromFirstSent() {
        model::Session s; ingest(s,40);
        const uint32_t want[]={0xff0000,0x00ff00,0x0000ff,0x0000ff};
        const int at[]={5,15,25,55};
        for(int k=0;k<4;++k) {
            const auto f=map(s,phase[1]+at[k]*ds); QCOMPARE(f.size(),QSize(288,288));
            QCOMPARE(tile(f,4,5),want[k]); QCOMPARE(tile(f,5,6),want[k]); QCOMPARE(tile(f,6,5),0xffffffu);
        }
    }
    void iconStateChangeRestarts() {
        model::Session s; ingest(s,43);
        const uint32_t want[]={0xff0000,0x00ff00,0x0000ff};
        for(int k=0;k<3;++k) {
            const auto f=map(s,phase[2]+(5+10*k)*ds);
            QCOMPARE(tile(f,6,5),want[k]); QCOMPARE(tile(f,4,5),0x0000ffu);
        }
    }
    void holderAndSecondHolderDoNotRestart() {
        model::Session s; ingest(s,49);
        auto f=map(s,phase[3]+5*ds); QCOMPARE(tile(f,5,6),0x0000ffu); QCOMPARE(tile(f,4,7),0x0000ffu);
        model::Session s4; ingest(s4,54); f=map(s4,phase[4]+5*ds);
        QCOMPARE(tile(f,6,7),0x0000ffu); QCOMPARE(tile(f,4,7),0x0000ffu);
    }
    void loopingStateOnTheSharedClock() {
        model::Session s; ingest(s,40); std::set<uint32_t> seen;
        for(int k=0;k<3;++k) {
            const auto f=map(s,phase[1]+(3+7*k)*ds);
            QCOMPARE(tile(f,7,3),tile(f,3,3)); seen.insert(tile(f,7,3));
        }
        QCOMPARE(seen.size(),size_t(3));
    }
    void listOrderStacks() {
        model::Session s; ingest(s,40); const auto f=map(s,phase[1]+5*ds);
        QCOMPARE(tile(f,8,5),0x8000ffu); QCOMPARE(tile(f,8,6),0xff8000u);
    }
};
FUGUE_TEST_APPLESS_MAIN(VisAnim)
#include "visanim_test.moc"
