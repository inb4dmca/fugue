// Stock DreamSeeker 516.1688 answers from tests/data/winget-controls-world.
// Regression: tgstation's fit_viewport queries "mainwindow.split;mapwindow", "size".
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "skin/skinfeed.h"

using namespace fugue::skin;

namespace {
QString fixture(const char* name) {
    return QStringLiteral(FUGUE_TEST_DATA_DIR "/winget-controls-world/") + QString::fromLatin1(name);
}
std::string skinText() {
    QFile f(fixture("skin.dmf"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll().toStdString();
}
}

class WingetControls : public QObject {
    Q_OBJECT
private slots:
    void stockAnswers_data() {
        QTest::addColumn<QString>("control");
        QTest::addColumn<QString>("params");
        QTest::addColumn<QString>("answer");
        QFile f(fixture("dreamseeker-answers.jsonl"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        int count = 0;
        while (!f.atEnd()) {
            const QByteArray line = f.readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            QVERIFY(doc.isObject());
            const QJsonObject row = doc.object();
            const QString control = row.value("control").toString();
            const QString params = row.value("params").toString();
            const QByteArray name = (control + " / " + params).toUtf8();
            QTest::newRow(name.constData()) << control << params << row.value("answer").toString();
            ++count;
        }
        QCOMPARE(count, 120);
    }
    void stockAnswers() {
        QFETCH(QString, control);
        QFETCH(QString, params);
        QFETCH(QString, answer);
        auto live = std::make_shared<LiveSkin>();
        QVERIFY(!skinText().empty());
        QCOMPARE(live->load(skinText()), std::string());
        live->setClient(640, 480);
        SkinFeed feed(live);
        WingetQuery q{control.toStdString(), params.toStdString(), 42};
        int replies = 0;
        feed.submitWinget(q, 0, [&](uint32_t id, const std::string& value) {
            ++replies;
            QCOMPARE(id, uint32_t(42));
            QCOMPARE(QString::fromStdString(value), answer);
        });
        QCOMPARE(replies, 1);
        QCOMPARE(feed.held(), size_t(0));
        // A present zero flag is the same named-property arm.
        q.hasFlag = true;
        q.flag = 0;
        QCOMPARE(QString::fromStdString(feed.answer(q)), answer);
    }
    void deferredUntilLayout() {
        auto live = std::make_shared<LiveSkin>();
        SkinFeed feed(live);
        const WingetQuery q{"alpha;beta", "size", 7};
        int replies = 0;
        auto reply = [&](uint32_t id, const std::string& value) {
            ++replies;
            QCOMPARE(id, uint32_t(7));
            QCOMPARE(value, std::string("alpha.size=100x40;beta.size=80x30"));
        };
        feed.submitWinget(q, 0, reply);
        QCOMPARE(feed.held(), size_t(1));
        QCOMPARE(replies, 0);
        QCOMPARE(live->load(skinText()), std::string());
        feed.pump(1, reply);
        QCOMPARE(replies, 0);
        live->setClient(640, 480);
        feed.pump(2, reply);
        QCOMPARE(replies, 1);
        QCOMPARE(feed.held(), size_t(0));
        feed.pump(3, reply);
        QCOMPARE(replies, 1);
    }
};
QTEST_GUILESS_MAIN(WingetControls)
#include "wingetcontrols_test.moc"
