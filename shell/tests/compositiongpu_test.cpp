// Independent stock parity and exact renderer-equivalence checks use GPU readback.
#include <QtTest>
#include "tests/testmain.h"
#include "shell/compositionplan.h"
#include "shell/tests/compositionfixture.h"
#include "shell/tests/gpucomparison.h"
#include "shell/tests/replayfixture.h"
#include <QDir>
#include <QFile>

using namespace fugue::shell;
using test::GpuComparison;
using test::contributorScene;

class CompositionGpuTest : public QObject {
    Q_OBJECT
    QStringList stockSets_;
private slots:
    void contributorMaterialGpu_data() {
        QTest::addColumn<bool>("gl");
#ifdef Q_OS_WIN
        QTest::newRow("d3d11")<<false;
#endif
        QTest::newRow("opengl")<<true;
    }
    void contributorMaterialGpu() {
        QFETCH(bool,gl);GpuComparison gpu;const QRect source(11,-7,193,137);
        QVERIFY2(gpu.initialize(gl,source.size(),true), "Required GPU backend could not be initialized");
        int comparisons=0;
        for (int variant=0;variant<9;++variant) {
            auto scene=contributorScene(source,73,variant);
            QImage original;
            for (int phase=0;phase<4;++phase) {
                // Replay, shrink/move to force pool reuse, then restore old content.
                auto current=scene;
                if (phase==2) {current.remove(9,47);for (auto& s:current) if(s.kind!=MapSprite::Master) s.position+=QPoint(5,-3);}
                const auto a=gpu.render(gpu.reference,current,source);QVERIFY(!a.isNull());
                if (phase>0) QCOMPARE(gpu.uploaded,uint64_t(0));
                const auto b=gpu.render(gpu.bounded,current,source);QVERIFY(!b.isNull());
                if (phase>0) QCOMPARE(gpu.uploaded,uint64_t(0));
                QVERIFY(!gpu.reference.preparedPlan()->backend.boundedMaterials);
                QVERIFY(gpu.bounded.preparedPlan()->backend.boundedMaterials);
                QVERIFY(gpu.bounded.preparedPlan()->storage.workPixels < gpu.reference.preparedPlan()->storage.workPixels);
                if (phase==0) original=a;else if(phase!=2) QCOMPARE(a,original);
                QVERIFY2(a==b,qPrintable(QString("exact RGBA comparison variant=%1 phase=%2").arg(variant).arg(phase)));
                ++comparisons;
            }
        }
        for (int blend=0;blend<5;++blend) {
            const auto grouped=contributorScene(source,70+blend,7);
            const auto a=gpu.render(gpu.reference,grouped,source);
            const auto b=gpu.render(gpu.bounded,grouped,source);
            QVERIFY(!a.isNull() && !b.isNull());
            QVERIFY(gpu.bounded.preparedPlan()->storage.workPixels < gpu.reference.preparedPlan()->storage.workPixels);
            QCOMPARE(a,b);++comparisons;
        }
        GpuComparison large;const QRect largeSource(-23,17,832,704);
        QVERIFY2(large.initialize(gl,largeSource.size(),true), "Required GPU backend could not be initialized");
        const auto dense=contributorScene(largeSource,1024);
        const auto a=large.render(large.reference,dense,largeSource);
        const auto b=large.render(large.bounded,dense,largeSource);
        QVERIFY(!a.isNull() && !b.isNull());
        QVERIFY(large.bounded.preparedPlan()->backend.boundedMaterials);
        QVERIFY(large.bounded.preparedPlan()->storage.workPixels < large.reference.preparedPlan()->storage.workPixels);
        QCOMPARE(a,b);++comparisons;
        // Device resource recreation must not reset the candidate selection.
        QVERIFY(large.bounded.initialize(large.rhi.get(),large.descriptor.get(),1));
        QCOMPARE(large.render(large.bounded,dense,largeSource),a);++comparisons;
        QVERIFY(large.bounded.preparedPlan()->backend.boundedMaterials);
        QVERIFY(large.bounded.preparedPlan()->storage.workPixels < large.reference.preparedPlan()->storage.workPixels);
        qInfo()<<comparisons<<"exact full RGBA GPU comparisons; not presentation/timing evidence";
    }
    // Sets of tests/data/stock-parity-world, replayed without a server: replay/<set>.txt is what DreamDaemon 516.1689
    // sent this client for the set (the app's recorder, FUGUE_RECORD_DIR), replay/res its resources, and
    // replay/<set>.png the map stock DreamSeeker 516.1689 drew for the same world, grabbed six seconds after login
    // (tools/stockprobe/stockprobe.py record writes all three). No channel of any pixel may be more than 8 levels from
    // stock's.
    // Drawn by Windows' software Direct3D, elsewhere by OpenGL (Mesa's llvmpipe passes: LIBGL_ALWAYS_SOFTWARE=1 under
    // xvfb-run with QT_QPA_PLATFORM=xcb; the offscreen platform has no OpenGL). A missing device fails this GPU check.
private:
    std::unique_ptr<GpuComparison> replayGpu_;   // one device for every set
private slots:
    void cleanupTestCase() { replayGpu_.reset(); }
    void initTestCase() {
        const QDir directory(QString(FUGUE_TEST_DATA_DIR)+"/stock-parity-world/replay");
        QFile recorded(directory.filePath("recorded.tsv"));
        QVERIFY2(recorded.open(QIODevice::ReadOnly),qPrintable(recorded.fileName()));
        QStringList sets;
        for (const auto& raw:recorded.readAll().split('\n')) {
            const auto line=QString::fromUtf8(raw).trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            const auto set=line.section('\t',0,0);
            QVERIFY2(!set.isEmpty() && !sets.contains(set),qPrintable("Invalid or duplicate set: "+set));
            sets.push_back(set);
        }
        QVERIFY2(!sets.empty(),"recorded.tsv contains no stock replay sets");
        sets.sort();
        // The recording inventory, wire and images must agree, so a removed PNG
        // cannot silently remove its regression from this data-driven test.
        for (const auto& suffix:{QString(".png"),QString(".txt")}) {
            QStringList expected;
            for (const auto& set:sets) expected.push_back(set+suffix);
            expected.sort();
            QCOMPARE(directory.entryList({"*"+suffix},QDir::Files),expected);
        }
        QVERIFY2(!QDir(directory.filePath("res")).entryList(QDir::Files).empty(),"Missing stock replay resources");
        stockSets_=sets;
    }
    void stockParityReplay_data() {
        QTest::addColumn<QString>("set");
        for (const auto& set:stockSets_) QTest::newRow(qPrintable(set)) << set;
    }
    void stockParityReplay() {
        QFETCH(QString, set);
        const QString dir = QString(FUGUE_TEST_DATA_DIR) + "/stock-parity-world/replay/";
        const int64_t login = 1'000'000'000'000'000'000, grabbed = login + 6'000'000'000;
        test::ReplayFixture fixture;
        fixture.session.setIngestClockNs(login);
        QVERIFY2(fixture.load(set + ".txt", "stock-parity-world/replay", {}, grabbed), qPrintable(fixture.error));
        const auto& frame = fixture.frame;
        const auto& sprites = fixture.sprites;
        QImage stock(dir + set + ".png"); stock = stock.convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(frame.source.size(), stock.size());
        auto& gpu = replayGpu_;
        if (!gpu) {
            gpu = std::make_unique<GpuComparison>();
#ifdef Q_OS_WIN
            const bool gl = false;
#else
            const bool gl = true;
#endif
            if (!gpu->initialize(gl, stock.size(), true)) {
                gpu.reset();
                QFAIL("Required GPU backend could not be initialized");
            }
        }
        const QImage drawn = gpu->render(gpu->bounded, sprites, frame.source, Qt::black);
        QCOMPARE(drawn.size(), stock.size());
        int off = 0; QPoint first;
        for (int y = 0; y < stock.height(); ++y) for (int x = 0; x < stock.width(); ++x) {
            const QRgb a = drawn.pixel(x, y), b = stock.pixel(x, y);
            if (std::abs(qRed(a) - qRed(b)) > 8 || std::abs(qGreen(a) - qGreen(b)) > 8 || std::abs(qBlue(a) - qBlue(b)) > 8)
                if (!off++) first = {x, y};
        }
        if (!off) return;
        const QString saved = QDir::temp().filePath("fugue-parity-" + set + ".png");   // to look at beside stock's
        drawn.save(saved);
        QFAIL(qPrintable(QString("%1 pixels differ from stock, the first at %2,%3; drawn: %4")
                             .arg(off).arg(first.x()).arg(first.y()).arg(saved)));
    }
};
FUGUE_TEST_MAIN(CompositionGpuTest)
#include "compositiongpu_test.moc"
