#include <QTemporaryDir>
#include <QtTest>
#include "tests/recordedinput.h"
#include "tests/testmain.h"

using namespace fugue;
using namespace fugue::testinput;

class RecordedInputTest : public QObject {
    Q_OBJECT
private slots:
    void retainsOrderedFrames_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("body-length") << QByteArray("0x0027 len=2 aBcd\n0x0018 len=0\n0x0034 len=1 09\n");
        QTest::newRow("body-plus-one") << QByteArray("0x0027 len=3 aBcd\n0x0018 len=1 \n0x0034 len=2 09\n");
        QTest::newRow("headers-crlf-whitespace")
            << QByteArray("=== retained recording ===\r\n\t# independent input\r\n \r\n"
                          " 0x0027\tlen=3\tABcd \r\n0x0018 len=1\t\r\n 0x0034 len=2 09 ");
    }
    void retainsOrderedFrames() {
        QFETCH(QByteArray, contents);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QString::fromUtf8("retained-\xc3\xa8.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();

        std::vector<RecordedFrame> frames{{0xffff, {0xff}}};
        QString error = "previous error";
        QVERIFY2(readRecordedFrames(path, frames, error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(frames.size(), size_t(3));
        QCOMPARE(frames[0].type, uint16_t(0x0027));
        QVERIFY(frames[0].body == Bytes({0xab, 0xcd}));
        QCOMPARE(frames[1].type, uint16_t(0x0018));
        QVERIFY(frames[1].body.empty());
        QCOMPARE(frames[2].type, uint16_t(0x0034));
        QVERIFY(frames[2].body == Bytes({0x09}));
    }
    void rejectsIncompleteRecordings_data() {
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("reason");
        const QByteArray first = "0x0027 len=3 abcd\n";
        QTest::newRow("odd-hex") << first + "0x0008 len=2 a\n" << 2 << QString("hexadecimal digits");
        QTest::newRow("invalid-hex") << first + "0x0008 len=2 gg\n" << 2 << QString("hexadecimal digits");
        QTest::newRow("truncated-body") << first + "0x0008 len=4 aa\n" << 2 << QString("does not match");
        QTest::newRow("missing-body") << first + "0x0008 len=3\n" << 2 << QString("does not match");
        QTest::newRow("mixed-lengths") << first + "0x0008 len=1 aa\n" << 2 << QString("Mixed len conventions");
        QTest::newRow("invalid-type") << first + "0x00gg len=1\n" << 2 << QString("Expected 0xTTTT");
        QTest::newRow("wide-type") << first + "0x10000 len=1\n" << 2 << QString("Expected 0xTTTT");
        QTest::newRow("negative-length") << first + "0x0008 len=-1\n" << 2 << QString("Expected 0xTTTT");
        QTest::newRow("overflow-length")
            << first + "0x0008 len=18446744073709551616\n" << 2 << QString("out of range");
        QTest::newRow("unexpected-text") << first + "record omitted\n" << 2 << QString("Expected 0xTTTT");
        QTest::newRow("inline-comment") << first + "0x0008 len=2 aa # note\n" << 2 << QString("Expected 0xTTTT");
        QTest::newRow("empty") << QByteArray() << 1 << QString("no frames");
        QTest::newRow("header-only") << QByteArray("=== recording ===\n# no input\n") << 2 << QString("no frames");
    }
    void rejectsIncompleteRecordings() {
        QFETCH(QByteArray, contents);
        QFETCH(int, line);
        QFETCH(QString, reason);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("frames.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();

        std::vector<RecordedFrame> frames{{0xffff, {0xff}}};
        QString error;
        QVERIFY(!readRecordedFrames(path, frames, error));
        QVERIFY(frames.empty()); // neither old output nor a valid prefix is a usable recording
        QVERIFY2(error.startsWith(path + ':' + QString::number(line) + ':'), qPrintable(error));
        QVERIFY2(error.contains(reason), qPrintable(error));
    }
    void missingFileIsNotAnEmptyRecording() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("missing.txt");
        std::vector<RecordedFrame> frames{{0xffff, {0xff}}};
        QString error;
        QVERIFY(!readRecordedFrames(path, frames, error));
        QVERIFY(frames.empty());
        QVERIFY2(error.startsWith(path + ":0: Cannot read recording:"), qPrintable(error));
    }
    void resourcesRetainIdsAndBytes() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile first(dir.filePath("0aBcDe01"));
        QVERIFY(first.open(QIODevice::WriteOnly));
        QCOMPARE(first.write(QByteArray::fromHex("00ff1020")), qint64(4));
        first.close();
        QFile second(dir.filePath("ffffffff"));
        QVERIFY(second.open(QIODevice::WriteOnly));
        QCOMPARE(second.write("second"), qint64(6));
        second.close();

        std::map<uint32_t, QByteArray> resources{{1, "old"}};
        QString error = "previous error";
        QVERIFY2(readRecordedResources(dir.path(), resources, error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(resources.size(), size_t(2));
        QCOMPARE(resources.at(0x0abcde01), QByteArray::fromHex("00ff1020"));
        QCOMPARE(resources.at(0xffffffff), QByteArray("second"));
    }
    void invalidResourcesDiscardTheWholeInput_data() {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QByteArray>("contents");
        QTest::addColumn<bool>("directory");
        QTest::addColumn<QString>("reason");
        QTest::newRow("non-hex-name") << QString("not-hex!") << QByteArray("data") << false << QString("resource ID");
        QTest::newRow("short-id") << QString("abcdef") << QByteArray("data") << false << QString("resource ID");
        QTest::newRow("wide-id") << QString("1abcdef01") << QByteArray("data") << false << QString("resource ID");
        QTest::newRow("empty-file") << QString("abcdef01") << QByteArray() << false << QString("empty");
        QTest::newRow("directory-entry") << QString("abcdef01") << QByteArray() << true << QString("Expected a resource file");
    }
    void invalidResourcesDiscardTheWholeInput() {
        QFETCH(QString, name);
        QFETCH(QByteArray, contents);
        QFETCH(bool, directory);
        QFETCH(QString, reason);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile valid(dir.filePath("00000001"));
        QVERIFY(valid.open(QIODevice::WriteOnly));
        QCOMPARE(valid.write("valid"), qint64(5));
        valid.close();
        if (directory) {
            QVERIFY(QDir(dir.path()).mkdir(name));
        } else {
            QFile invalid(dir.filePath(name));
            QVERIFY(invalid.open(QIODevice::WriteOnly));
            QCOMPARE(invalid.write(contents), qint64(contents.size()));
        }

        std::map<uint32_t, QByteArray> resources{{2, "old"}};
        QString error;
        QVERIFY(!readRecordedResources(dir.path(), resources, error));
        QVERIFY(resources.empty());
        QVERIFY2(error.startsWith(dir.filePath(name) + ':'), qPrintable(error));
        QVERIFY2(error.contains(reason), qPrintable(error));
    }
    void missingAndEmptyResourceDirectoriesFail() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        std::map<uint32_t, QByteArray> resources{{1, "old"}};
        QString error;
        QVERIFY(!readRecordedResources(dir.path(), resources, error));
        QVERIFY(resources.empty());
        QVERIFY2(error.contains("contains no files"), qPrintable(error));
        resources.emplace(1, "old");
        QVERIFY(!readRecordedResources(dir.filePath("missing"), resources, error));
        QVERIFY(resources.empty());
        QVERIFY2(error.contains("Missing resource directory"), qPrintable(error));
    }
};

FUGUE_TEST_GUILESS_MAIN(RecordedInputTest)
#include "recordedinput_test.moc"
