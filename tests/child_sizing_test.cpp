// Independent stock CHILD geometry: tests/data/child-sizing-world/README.md.
#include <QtTest>
#include "tests/testmain.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "skin/liveskin.h"

using namespace fugue::skin;

static QByteArray fixture() {
    QFile file(QStringLiteral(FUGUE_TEST_DATA_DIR "/child-sizing-world/skin.dmf"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

static void phase(LiveSkin& live, const QString& name) {
    if (name == "reset" || name == "clear") {
        live.winset("main.leftonly", "left=first;right=\"\"");
        live.winset("main.rightonly", "left=\"\";right=second");
    } else if (name == "both") {
        live.winset("main.leftonly", "right=extra_right");
        live.winset("main.rightonly", "left=extra_left");
    } else if (name == "swap") {
        live.winset("main.leftonly", "left=\"\";right=first");
        live.winset("main.rightonly", "right=\"\";left=second");
    } else if (name == "fraction-winset") {
        live.winset("main.fraction", "splitter=97.5");
    }
}

class ChildSizingTest : public QObject {
    Q_OBJECT
private slots:
    void stockGeometry_data() {
        QTest::addColumn<QJsonArray>("history");
        QFile file(QStringLiteral(FUGUE_TEST_DATA_DIR "/child-sizing-world/stock.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto rows = QJsonDocument::fromJson(file.readAll()).object().value("rows").toArray();
        QCOMPARE(rows.size(), 7);
        QJsonArray history;
        for (const auto& value : rows) {
            history.append(value);
            QTest::newRow(value.toObject().value("phase").toString().toUtf8().constData()) << history;
        }
    }
    void stockGeometry() {
        QFETCH(QJsonArray, history);
        LiveSkin live;
        const auto source = fixture();
        QVERIFY(!source.isEmpty());
        QCOMPARE(live.load(source.toStdString()), std::string());
        for (const auto& value : history) {
            const auto row = value.toObject();
            const auto extent = row.value("client").toString().split('x');
            QCOMPARE(extent.size(), 2);
            live.setClient(extent[0].toInt(), extent[1].toInt());
            phase(live, row.value("phase").toString());
        }
        const auto row = history.last().toObject();
        const auto controls = row.value("controls").toObject();
        for (auto it = controls.begin(); it != controls.end(); ++it) {
            std::string size;
            QVERIFY(live.winget(it.key().toStdString(), "size", size));
            QCOMPARE(QString::fromStdString(size), it.value().toString());
        }
        const auto fraction = row.value("fraction").toObject();
        for (auto it = fraction.begin(); it != fraction.end(); ++it) {
            std::string value;
            QVERIFY(live.winget("main.fraction", it.key().toStdString(), value));
            QCOMPARE(QString::fromStdString(value), it.value().toString());
        }
        // Empty slots must not leave an invisible or draggable divider.
        const auto snapshot = live.snapshot();
        QCOMPARE(snapshot.tree.splitters.size(), row.value("phase") == "both" ? size_t(2) : size_t(0));
    }
    void inferredPreviewVersusLiveEmptySide() {
        auto source = fixture();
        source.replace("\"leftonly\"", "\"primary\""); // no suffix overlap with wrapper.only
        // Offline preview may infer a pane from its own on-show; LiveSkin must not.
        source.replace("window \"extra_right\"\n    elem \"extra_right\"\n        type = MAIN",
                       "window \"extra_right\"\n    elem \"extra_right\"\n        type = MAIN\n        on-show = \".winset primary.right=extra_right\"");
        const auto parsed = parseDmf(source.toStdString());
        QVERIFY(parsed.ok);
        auto preview = layoutSkin(parsed.file, {0, 0, 640, 480});
        QVERIFY(preview.byName("extra_right.content"));
        QCOMPARE(preview.byName("first.content")->rect.dx(), 74);
        auto live = layoutSkin(parsed.file, {0, 0, 640, 480}, {}, nullptr, false);
        QVERIFY(!live.byName("extra_right.content"));
        QCOMPARE(live.byName("first.content")->rect.dx(), 300);
        // The offline preview also fills a genuinely unclaimed singleton.
        QCOMPARE(preview.byName("chat.browser")->rect, (Rect{0, 160, 300, 310}));
    }
};
FUGUE_TEST_GUILESS_MAIN(ChildSizingTest)
#include "child_sizing_test.moc"
