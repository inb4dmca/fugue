#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include "audio/sink.h"

class AudioSinkTest : public QObject {
    Q_OBJECT
private slots:
    void silentAndWav() {
        using namespace fugue::audio;
        std::string err;
        const float samples[] = {0.0f, 0.5f, -0.5f, 0.0f};
        auto silent = openSink("none", 44100, &err);
        QVERIFY(dynamic_cast<NullSink*>(silent.get()));
        QVERIFY(silent->write(samples, 2, &err));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto wav = openSink("wav:" + (dir.path() + "/mix.wav").toStdString(), 44100, &err);
        QVERIFY(wav->write(samples, 2, &err));
        wav->close();
        wav->close();
        QFile file(dir.path() + "/mix.wav");
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto bytes = file.readAll();
        QVERIFY(bytes.startsWith("RIFF"));
        QCOMPARE(bytes.mid(8, 4), QByteArray("WAVE"));
        QCOMPARE(bytes.size(), qsizetype(44 + 8));
    }
    void commandPipe() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        std::string err;
        auto sink = fugue::audio::openExecSink({"/bin/dd", "of=" + (dir.path() + "/pcm").toStdString()}, &err);
        QVERIFY2(sink, err.c_str());
        const float silence[8]{};
        QVERIFY2(sink->write(silence, 4, &err), err.c_str());
        sink->close();
        sink->close();
        QFile file(dir.path() + "/pcm");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray(16, '\0'));
        QVERIFY(!fugue::audio::openExecSink({"/nonexistent/fugue-player"}, &err));
        QVERIFY(!err.empty());
    }
#ifdef __APPLE__
    void invalidCoreAudioRate() {
        std::string err;
        QVERIFY(!fugue::audio::openCoreAudioSink(0, &err));
        QVERIFY(!err.empty());
    }
    void coreAudioDevice() {
        if (!qEnvironmentVariableIsSet("FUGUE_TEST_COREAUDIO")) QSKIP("Set FUGUE_TEST_COREAUDIO=1 to test a real output device");
        std::string err;
        auto sink = fugue::audio::openSink("auto", 44100, &err);
        QCOMPARE(sink->name(), std::string("coreaudio"));
        const float silence[2048]{};
        for (int i = 0; i < 8; ++i) QVERIFY2(sink->write(silence, 1024, &err), err.c_str());
        sink->close();
        sink->close();
    }
#endif
};
QTEST_GUILESS_MAIN(AudioSinkTest)
#include "audio_sink_test.moc"
