// Asset decoding and recipe regressions through the worker's IconCache.
// Expected pixels/metadata come from DreamDaemon exports and stock DreamSeeker
// captures, not renderer-generated reference output.
#include <QtTest>
#include "tests/testmain.h"
#include <QFile>
#include <QTextStream>
#include <cmath>
#include "model/world.h"
#include "shell/iconcache.h"
#include "shell/tests/iconfixture.h"
#include "assets/gif.h"
using namespace fugue;
using namespace fugue::shell;
namespace {
QByteArray load(const QString& path) {
    QFile file(QString(FUGUE_TEST_DATA_DIR) + '/' + path);
    if (!file.open(QIODevice::ReadOnly)) qFatal("missing fixture %s", qPrintable(file.fileName()));
    return file.readAll();
}
struct Case { QString name; uint32_t id; int x, y; };
QList<Case> cases() {
    const auto bytes = load("ddmiops-world/cases.tsv");
    QTextStream stream(bytes);
    QList<Case> out;
    while (!stream.atEnd()) {
        QString name, id; int x = 0, y = 0;
        stream >> name >> id >> x >> y;
        if (!name.isEmpty()) out.push_back({name,id.toUInt(nullptr,16),x,y});
    }
    return out;
}
}
class AssetRecoveryTest : public QObject {
    Q_OBJECT
    model::Session session_;
    IconCache cache_;
    std::map<uint32_t,QByteArray> resources_;
    void begin() {
        cache_.begin(session_.world().resources, [this](uint32_t id,QByteArray& out) {
            const auto it = resources_.find(id);
            if (it == resources_.end()) return false;
            out = it->second; return true;
        });
    }
    // A recipe gets a 2 ms slice per preparation and is finished by later ones, as the client's frames do.
    IconCache::Result finish(uint32_t id) {
        IconCache::Result result;
        int preparations = 0;
        do { begin(); result = resolve(id); } while (!result.ready && ++preparations < 1000);
        return result;
    }
    IconCache::Result resolve(uint32_t id) {
        const auto& resources = session_.world().resources;
        const auto it = std::find_if(resources.begin(), resources.end(), [id](const auto& r){return r.first == id;});
        return cache_.lookup(uint32_t(it - resources.begin()));
    }
    void compare(const DmiIcon& got, const DmiIcon& want, int allowed = 0) {
        QVERIFY(!got.sheet.isNull()); QVERIFY(!want.sheet.isNull());
        QCOMPARE(got.cell,want.cell);
        const auto g = got.toAsset(), w = want.toAsset();
        QCOMPARE(g.states.size(),w.states.size());
        int bad = 0;
        for (size_t si = 0; si < w.states.size(); ++si) {
            const auto& a = g.states[si]; const auto& b = w.states[si];
            QCOMPARE(a.name,b.name); QCOMPARE(a.dirs,b.dirs); QCOMPARE(a.frames,b.frames);
            QCOMPARE(a.movement,b.movement); QCOMPARE(a.loop,b.loop); QCOMPARE(a.rewind,b.rewind);
            QCOMPARE(a.hotspot,b.hotspot);
            for (int64_t f = 0; f < b.frames; ++f) {
                const double gd = size_t(f) < a.delays.size() ? a.delays[size_t(f)] : 1.;
                const double wd = size_t(f) < b.delays.size() ? b.delays[size_t(f)] : 1.;
                // DD's fcopy omits single-frame delays (even explicit Insert
                // delay=2). Do not erase retained timing to match that omission.
                if (b.frames > 1 || b.hasDelays) QVERIFY(std::abs(gd - wd) < 1e-5);
                for (int64_t dir = 0; dir < b.dirs; ++dir) {
                    std::vector<uint8_t> gp, wp;
                    QVERIFY(g.frame(a,dir,f,gp)); QVERIFY(w.frame(b,dir,f,wp)); QCOMPARE(gp.size(),wp.size());
                    for (size_t p = 0; p < wp.size(); p += 4)
                        if (gp[p+3] != wp[p+3] || (wp[p+3] && !std::equal(gp.begin()+p,gp.begin()+p+3,wp.begin()+p))) ++bad;
                }
            }
        }
        QCOMPARE(bad,allowed);
    }
private slots:
    void initTestCase() {
        for (const auto& line : load("ddmiops-world/frames.txt").split('\n')) {
            if (!line.startsWith("0x")) continue;
            const auto at = line.indexOf(' ',line.indexOf("len="));
            const auto bytes = at < 0 ? QByteArray() : QByteArray::fromHex(line.mid(at+1));
            session_.ingest(line.mid(2,4).toUShort(nullptr,16), Bytes(bytes.begin(),bytes.end()));
        }
        QDir dir(QString(FUGUE_TEST_DATA_DIR) + "/ddmiops-world/res");
        for (const auto& name : dir.entryList(QDir::Files))
            resources_.emplace(name.toUInt(nullptr,16),load("ddmiops-world/res/"+name));
        QVERIFY(!session_.world().resources.empty());
        // Known-good raw source establishes resource-list/replay adapter correctness.
        begin(); const auto control = cache_.lookup(1); QVERIFY(control.ready);
        QCOMPARE(cache_.icon(control.id).cell,QSize(32,32));
        QVERIFY(cache_.icon(control.id).state("grad"));
    }
    void dreamDaemonRecipes_data() {
        QTest::addColumn<QString>("name"); QTest::addColumn<uint32_t>("id");
        for (const auto& c : cases()) QTest::newRow(qPrintable(c.name)) << c.name << c.id;
    }
    void dreamDaemonRecipes() {
        QFETCH(QString,name); QFETCH(uint32_t,id);
        const auto result = finish(id);
        QVERIFY2(result.ready,IconCache::statusName(result.status));
        QCOMPARE(result.status,IconCache::Status::Ready);
        // 45-degree exact ties differ in DD's x87 vs DS's float32 arithmetic.
        // The stock-map test below independently checks the DS result.
        compare(cache_.icon(id),DmiIcon::decode(load("ddmiops-world/dmi/"+name+".dmi")),name=="c_turn45"?2:0);
        const auto retained = cache_.icon(id).sheet;
        begin(); QCOMPARE(resolve(id),result); QCOMPARE(cache_.reads(),uint64_t(0));
        QCOMPARE(cache_.icon(id).sheet.cacheKey(),retained.cacheKey());
    }
    void stockMapAssetPixels() {
        // Asset-only counterpart of the old map test: no scene assembler restored.
        const auto stock = QImage::fromData(load("ddmiops-world/dreamseeker.png"));
        QVERIFY(!stock.isNull());
        const QSet<QString> animated{"d_insanim","e_insdelay","e_insframe3","f_walkboth","f_walkmv","f_walkstill","f_wholemov"};
        int compared = 0;
        for (const auto& c : cases()) {
            if (animated.contains(c.name)) continue;
            QVERIFY(finish(c.id).ready);
            const auto& icon = cache_.icon(c.id);
            const auto frame = icon.frame(c.name == "c_insert" ? "ins" : c.name == "d_insfull" ? "x" : "",2);
            const int x0 = (c.x-1)*32, y0 = stock.height()-(c.y+1)*32;
            int bad = 0;
            for (int y=0;y<64;++y) for (int x=0;x<64;++x) {
                const int fy = y - (64-icon.cell.height());
                const auto pixel = x<frame.width() && fy>=0 && fy<frame.height() ? frame.pixelColor(x,fy) : QColor(Qt::transparent);
                const auto ref = stock.pixelColor(x0+x,y0+y);
                const int alpha = pixel.alpha();
                const int r=(pixel.red()*alpha+255*(255-alpha)+127)/255;
                const int g=(pixel.green()*alpha+127)/255;
                const int b=(pixel.blue()*alpha+255*(255-alpha)+127)/255;
                if (std::max({std::abs(r-ref.red()),std::abs(g-ref.green()),std::abs(b-ref.blue())})>1) ++bad;
            }
            QVERIFY2(!bad,qPrintable(c.name+": "+QString::number(bad)+" differing pixels")); ++compared;
        }
        QCOMPARE(compared,111);
    }
    void imageDecoders_data() {
        QTest::addColumn<QString>("source"); QTest::addColumn<QString>("reference");
        for (const auto* name : {"a","c","e","l2","d","d2","d3","d4"})
            QTest::newRow(name) << QString("gifanim/%1.gif").arg(name) << QString("gifanim/%1.dd.dmi").arg(name);
        for (const auto* name : {"base","prog"})
            QTest::newRow(name) << QString("jpeg/%1.jpg").arg(name) << QString("jpeg/%1.dd.dmi").arg(name);
    }
    void imageDecoders() {
        QFETCH(QString,source); QFETCH(QString,reference);
        compare(DmiIcon::decode(load(source)),DmiIcon::decode(load(reference)));
    }
    void singleFrameMetadata() {
        // Original gif_anim_test.cpp still-image control, through the current API.
        const auto gif = QByteArray::fromHex("47494638396101000100800000ff00000000002c00000000010001000002024401003b");
        const auto icon = DmiIcon::decode(gif);
        QCOMPARE(icon.cell,QSize(1,1)); QVERIFY(icon.state("")); QCOMPARE(icon.state("")->frames,1);
        QCOMPARE(icon.sheet.pixelColor(0,0),QColor(Qt::red));
        QCOMPARE(icon.state("")->flickFrameAt(100000000,0),-1);
        for (const auto& c : cases()) if (c.name == "e_insmoving") {
            QVERIFY(finish(c.id).ready);
            const auto& generated = cache_.icon(c.id);
            QVERIFY(generated.metadata); QCOMPARE(generated.metadata->size(),size_t(1));
            const auto& state=generated.metadata->front();
            QVERIFY(state.movement); QCOMPARE(state.frames,int64_t(1)); QCOMPARE(state.delays,std::vector<double>{2.});
            QVERIFY(!generated.states.count("m")); // movement state retained, not an ordinary state
        }
    }
    void gifPlaybackAndGrayscale() {
        const auto icon = DmiIcon::decode(load("gifanim/a.gif")); const auto* s=icon.state(""); QVERIFY(s);
        for (auto [time,frame] : {std::pair{0.,0},{4.9,0},{5.,1},{14.9,1},{15.,2},{15.9,2},{16.,3},{30.9,3},{31.,0},{36.,1}})
            QCOMPARE(s->frameAt(int64_t(time*100000000),0),frame);
        const auto finite = DmiIcon::decode(load("gifanim/c.gif")); QVERIFY(finite.state(""));
        QCOMPARE(finite.state("")->loop,1); QCOMPARE(finite.state("")->frameAt(4000000000,0),3);
        const auto gray = DmiIcon::decode(load("jpeg/gray.jpg")); QVERIFY(!gray.sheet.isNull());
        for(int y=0;y<gray.sheet.height();++y) for(int x=0;x<gray.sheet.width();++x) {
            const auto p=gray.sheet.pixelColor(x,y); QCOMPARE(p.red(),p.green()); QCOMPARE(p.red(),p.blue()); QCOMPARE(p.alpha(),255);
        }
    }
    void cacheDecodeBudgetAndEviction() {
        IconCache cache; IconCache::Resources resources{{10,3},{20,3}};
        const auto gif = load("gifanim/a.gif");
        auto read = [&](uint32_t,QByteArray& out) { out=gif; return true; };
        cache.begin(resources,read,4096);
        const auto first=cache.lookup(0); QVERIFY(first.ready);
        const auto retained=cache.icon(10);
        QVERIFY(!cache.lookup(1).ready); QCOMPARE(cache.lookup(1).status,IconCache::Status::PixelLimit);
        cache.begin(resources,read,4096); QVERIFY(cache.lookup(1).ready);
        cache.trim(0,0); cache.begin(resources,read,4096);
        QVERIFY(cache.lookup(0).ready); QVERIFY(cache.lookup(0).revision != first.revision);
        compare(cache.icon(10),retained);
        // An insufficient budget refuses the entire animation, never frame zero.
        cache.trim(0,0); cache.begin(resources,read,1024);
        QVERIFY(!cache.lookup(0).ready); QVERIFY(cache.icon(10).sheet.isNull());
    }
    void legacyV4Structure() {
        // Structural regression for the recovered parser, not a new stock capture:
        // two frames, the second a vertical-flip back-reference; R,B,G palette.
        QByteArray bytes=QByteArray::fromHex("04444d49010200000000ff4020010001000200030000010000070000000000");
        bytes += QByteArray(64,'\xff') + QByteArray(64,'\0');
        bytes += QByteArray::fromHex("0700040000");
        const auto icon=DmiIcon::decode(bytes); QVERIFY(!icon.sheet.isNull());
        QVERIFY(icon.state("")); QCOMPARE(icon.state("")->frames,2);
        QCOMPARE(icon.frame("",2,0).pixelColor(0,0),QColor(255,32,64));
        QCOMPARE(icon.frame("",2,1).pixelColor(0,0).alpha(),0);
        QCOMPARE(icon.frame("",2,1).pixelColor(0,31),QColor(255,32,64));
        QCOMPARE(icon.state("")->frameAt(300000000,0),1);
        auto unknown=bytes; unknown[unknown.size()-3]=char(3); QVERIFY(DmiIcon::decode(unknown).sheet.isNull());
        auto flags=bytes; flags[21]=char(1); QVERIFY(DmiIcon::decode(flags).sheet.isNull());
        QVERIFY(DmiIcon::decode(bytes.first(30)).sheet.isNull());
        bool limited=false; QVERIFY(DmiIcon::decode(bytes,1024,&limited).sheet.isNull()); QVERIFY(limited);
    }
    void decoderLimitsAndTruncation() {
        const auto gif=load("gifanim/a.gif");
        for (int n : {0,6,13,32,int(gif.size()-1)}) QVERIFY(DmiIcon::decode(gif.first(n)).sheet.isNull());
        auto huge=gif; huge[6]='\xff'; huge[7]='\xff'; huge[8]='\xff'; huge[9]='\xff';
        QVERIFY(DmiIcon::decode(huge).sheet.isNull());
        assets::GifAnimation anim; std::string error;
        QVERIFY(!assets::decodeGifAnimation(reinterpret_cast<const uint8_t*>(gif.constData()),size_t(gif.size()),anim,error,1024));
        auto jpeg=load("jpeg/base.jpg"); QVERIFY(DmiIcon::decode(jpeg.first(12)).sheet.isNull());
    }
};
FUGUE_TEST_MAIN(AssetRecoveryTest)
#include "assetrecovery_test.moc"
