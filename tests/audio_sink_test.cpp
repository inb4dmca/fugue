#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include "audio/sink.h"

class AudioSinkTest : public QObject {
    Q_OBJECT
private slots:
    void disabledAndWav() {
        std::string err;
        auto silent = fugue::audio::openSink("none", 44100, &err);
        QVERIFY(silent);
        QVERIFY(err.empty());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.path() + "/pcm.wav";
        auto wav = fugue::audio::openWavSink(path.toStdString(), 44100, &err);
        QVERIFY2(wav != nullptr, err.c_str());
        const float samples[] = {0.0f, 0.0f, 1.0f, -1.0f};
        QVERIFY(wav->write(samples, 2, &err));
        wav->close();
        wav->close(); // idempotent
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        auto bytes = f.readAll();
        QVERIFY(bytes.startsWith("RIFF"));
        QCOMPARE(bytes.mid(8, 4), QByteArray("WAVE"));
        QVERIFY(bytes.size() >= 44 + 8);
    }
    void nativeWindowsDevice() {
#ifndef _WIN32
        QSKIP("Windows audio backend");
#else
        if (qgetenv("FUGUE_TEST_AUDIO") != "1") QSKIP("Set FUGUE_TEST_AUDIO=1 to exercise a real Windows output device");
        std::string err;
        auto sink = fugue::audio::openSink("auto", 44100, &err);
        QVERIFY2(err.empty(), err.c_str());
        QCOMPARE(sink->name(), std::string("waveout"));
        float silence[2048]{};
        for (int i = 0; i < 12; ++i) QVERIFY2(sink->write(silence, 1024, &err), err.c_str());
        sink->close();
        sink->close();
        QVERIFY(!sink->write(silence, 1024, &err));
#endif
    }
};
QTEST_APPLESS_MAIN(AudioSinkTest)
#include "audio_sink_test.moc"
