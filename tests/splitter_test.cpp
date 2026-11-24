// Stock nested CHILD evidence: tests/data/splitter-world/README.md.
#include <QtTest>
#include "tests/testmain.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include "skin/liveskin.h"

using namespace fugue::skin;

static std::string splitSkin(int percent, bool vertical, bool visible) {
    return QString(R"(window "main"
    elem "main"
        type = MAIN
        size = 1000x1000
        is-default = true
    elem "split"
        type = CHILD
        pos = 0,0
        size = 1000x1000
        anchor1 = 0,0
        anchor2 = 100,100
        left = "first"
        right = "second"
        splitter = %1
        is-vert = %2
        show-splitter = %3
window "first"
    elem "first"
        type = MAIN
        size = 1000x1000
        is-pane = true
    elem "content"
        type = LABEL
        pos = 0,0
        size = 1000x1000
        anchor1 = 0,0
        anchor2 = 100,100
window "second"
    elem "second"
        type = MAIN
        size = 1000x1000
        is-pane = true
    elem "content"
        type = LABEL
        pos = 0,0
        size = 1000x1000
        anchor1 = 0,0
        anchor2 = 100,100
)").arg(percent).arg(vertical ? "true" : "false").arg(visible ? "true" : "false").toStdString();
}

class SplitterTest : public QObject {
    Q_OBJECT
private slots:
    void recordedHiddenHeights() {
        QFile file(QStringLiteral(FUGUE_TEST_DATA_DIR "/splitter-world/skin.dmf"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto source = file.readAll();
        QFile evidence(QStringLiteral(FUGUE_TEST_DATA_DIR "/splitter-world/dreamseeker-observations.json"));
        QVERIFY(evidence.open(QIODevice::ReadOnly));
        const auto corpus = QJsonDocument::fromJson(evidence.readAll()).object();
        int checked = 0;
        for (const auto& group : {QString("base"), QString("tall_can_resize_false")}) {
            auto skin = source;
            if (group != "base") skin.replace("676x30", "676x60");
            LiveSkin live;
            QCOMPARE(live.load(skin.toStdString()), std::string());
            for (const auto& value : corpus.value(group).toArray()) {
                const auto row = value.toObject();
                const auto split = row.value("nested.split").toObject();
                if (split.value("show-splitter").toString() != "false") continue;
                const auto extent = row.value("main").toObject().value("size").toString().split('x');
                QCOMPARE(extent.size(), 2);
                live.setClient(extent[0].toInt(), extent[1].toInt());
                live.winset("nested.split", "splitter=" + split.value("splitter").toString().toStdString());
                const auto snapshot = live.snapshot();
                const auto* button = snapshot.tree.byName("row.menu");
                QVERIFY(button);
                const auto height = [&](const char* id) {
                    return row.value(id).toObject().value("size").toString().section('x', 1).toInt();
                };
                QCOMPARE(button->box.dy(), height("row"));
                QCOMPARE(button->full.dy(), height("row.menu"));
                QCOMPARE(button->rect, button->full.intersect(button->box));
                ++checked;
            }
        }
        QCOMPARE(checked, 32);
    }
    void stockButtonHeight() {
        QFile file(QStringLiteral(FUGUE_TEST_DATA_DIR "/splitter-world/skin.dmf"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto source = file.readAll();
        for (int designHeight : {30, 60}) {
            LiveSkin live;
            auto skin = source;
            skin.replace("676x30", QByteArray("676x") + QByteArray::number(designHeight));
            QCOMPARE(live.load(skin.toStdString()), std::string());
            // Hidden divider: no platform-specific native divider width involved.
            for (int height : {240, 440, 880}) {
                live.setClient(640, height);
                for (int percent : {0, 1, 2, 3, 5, 10, 50, 98, 100}) {
                    live.winset("nested.split", "splitter=" + std::to_string(percent));
                    const auto snapshot = live.snapshot();
                    const auto* button = snapshot.tree.byName("row.menu");
                    QVERIFY(button);
                    const int paneHeight = int(std::round((height - 3) * percent / 100.0));
                    QCOMPARE(button->box.dy(), paneHeight); // design height never becomes a pane minimum
                    QCOMPARE(button->full.y0 - button->box.y0, 5);
                    const int buttonHeight = std::max(0, paneHeight + 27 - designHeight);
                    QCOMPARE(button->full.dy(), buttonHeight);
                    QCOMPARE(button->rect, button->full.intersect(button->box));
                    QCOMPARE(button->offPane, paneHeight <= 5 || buttonHeight == 0);
                    std::string size;
                    QVERIFY(live.winget("row.menu", "size", size));
                    QCOMPARE(size, std::to_string(button->full.dx()) + "x" + std::to_string(buttonHeight));
                }
            }
        }
    }
    // Broader implementation invariants, not all stock-observed combinations.
    void fullExtent_data() {
        QTest::addColumn<QString>("source");
        QTest::addColumn<int>("percent");
        QTest::addColumn<bool>("vertical");
        QTest::addColumn<bool>("visible");
        for (const QString& source : {QString("file"), QString("winset"), QString("drag")})
            for (int percent : {-10, 0, 2, 50, 98, 100, 120})
                for (bool vertical : {false, true})
                    for (bool visible : {false, true}) {
                        if (source == "drag" && !visible) continue;
                        const auto name = QString("%1-%2-%3-%4").arg(source).arg(percent).arg(vertical).arg(visible).toUtf8();
                        QTest::newRow(name.constData()) << source << percent << vertical << visible;
                    }
    }
    void fullExtent() {
        QFETCH(QString, source);
        QFETCH(int, percent);
        QFETCH(bool, vertical);
        QFETCH(bool, visible);
        LiveSkin live;
        QCOMPARE(live.load(splitSkin(source == "file" ? percent : 50, vertical, visible)), std::string());
        live.setClient(1000, 1000);
        if (source == "winset") {
            const auto result = live.winset("main.split", "splitter=" + std::to_string(percent));
            QVERIFY(result.unhandled.empty());
        } else if (source == "drag") {
            QVERIFY(live.setSplitter("main.split", percent));
        }
        std::string reported;
        QVERIFY(live.winget("main.split", "splitter", reported));
        QCOMPARE(reported, std::to_string(percent)); // keep raw reporting even outside the pane bounds
        for (int extent : {1000, 500}) {
            live.setClient(extent, extent);
            const auto snapshot = live.snapshot();
            const auto* first = snapshot.tree.byName("first.content");
            const auto* second = snapshot.tree.byName("second.content");
            QVERIFY(first && second);
            const int available = extent - (visible ? kSplitW : 0);
            const int cut = int(std::round(available * std::clamp(percent, 0, 100) / 100.0));
            QCOMPARE(vertical ? first->rect.dx() : first->rect.dy(), cut);
            QCOMPARE(vertical ? second->rect.dx() : second->rect.dy(), available - cut);
        }
    }
};
FUGUE_TEST_GUILESS_MAIN(SplitterTest)
#include "splitter_test.moc"
