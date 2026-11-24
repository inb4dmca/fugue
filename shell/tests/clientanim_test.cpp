// animate(client) timing against stock DreamDaemon 516.1684 (tests/data/clientanim-world). Every frame is ingested at
// the tick of the "@N" chat marker before it. The expected offsets are what stock DreamSeeker's chain evaluator gives:
// parallel sequences overlap, a head step starts at its own StepDelay, a direct client.pixel_* set shifts a tween in
// flight, and vg's directional_recoil fired every two ticks holds the view kicked until the burst ends.
#include <QtTest>
#include "tests/testmain.h"
#include "model/world.h"
#include "shell/tests/replayfixture.h"
#include "shell/tests/animationfixture.h"

using namespace fugue;

namespace {
constexpr int64_t kT0 = 1'000'000'000'000'000'000;
constexpr int64_t kTick = 100'000'000;
constexpr uint32_t kRes = 0x163d55fd;
constexpr uint32_t kPlayer = 0xc82828, kMarker = 0x28c828, kHud = 0x2828dc;
int64_t at(double tick) { return kT0 + int64_t(tick * double(kTick)); }
}  // namespace

class ClientAnim : public QObject {
    Q_OBJECT
    struct Frame {
        uint16_t typ;
        Bytes body;
        int tick;
    };
    std::vector<Frame> frames_;
    std::unique_ptr<shell::test::ReplayFixture> replay_;
    size_t nextFrame_ = 0;
    double sampledTick_ = -1;

    void reset() {
        replay_ = std::make_unique<shell::test::ReplayFixture>();
        nextFrame_ = 0;
        sampledTick_ = -1;
    }
    QImage image(double tick) {
        if (!replay_ || tick < sampledTick_) reset();
        while (nextFrame_ < frames_.size() && frames_[nextFrame_].tick <= int(tick)) {
            const auto& frame = frames_[nextFrame_++];
            replay_->ingest(frame.typ, frame.body, at(frame.tick));
        }
        // The original fixture supplies this one static resource after wire ingest;
        // it does not record resource arrival times.
        if (!replay_->loadResources("clientanim-world")) {
            QTest::qFail(qPrintable(replay_->error), __FILE__, __LINE__);
            return {};
        }
        replay_->refresh(at(tick));
        sampledTick_ = tick;
        return AnimationFixture::raster(replay_->frame, replay_->sprites);
    }
    static uint32_t rgb(const QImage& img, int x, int y) { return img.pixel(x, y) & 0xffffff; }

    // The view offset at `tick`, read off where the player's tile is drawn: it sits at (160,160) unmoved, and the map
    // moves by -x across and +y down.
    QPoint offset(double tick) {
        QImage img = image(tick);
        for (int y = 0; y < img.height(); y++)
            for (int x = 0; x < img.width(); x++)
                if (rgb(img, x, y) == kPlayer) return {160 - x, y - 160};
        return {9999, 9999};
    }

private slots:
    void initTestCase() {
        shell::test::ReplayFixture recording;
        int tick = 0;
        bool ordered = true;
        QVERIFY2(recording.readFrames("frames.txt", "clientanim-world", [&](uint16_t type, const Bytes& body) {
            if (type == S2C::Text && body.size() > 1 && body[0] == '@') {
                bool valid = false;
                const int next = QByteArray(reinterpret_cast<const char*>(body.data() + 1), qsizetype(body.size() - 1))
                    .split('\0').front().toInt(&valid);
                ordered = ordered && valid && next >= tick;
                tick = next;
            }
            frames_.push_back({type, body, tick});
        }), qPrintable(recording.error));
        QVERIFY2(ordered, "Recorded tick markers must be valid and ordered");
        QCOMPARE(int(frames_.size()), 609);
        QVERIFY2(recording.loadResources("clientanim-world"), qPrintable(recording.error));
        QCOMPARE(recording.resources.size(), size_t(1));
        QVERIFY(recording.resources.count(kRes) && !recording.resources.at(kRes).isEmpty());
        // Known-good unanimated control validates the adapter before chains.
        QCOMPARE(offset(30), QPoint(0, 0));
    }

    void init() { reset(); }

    // animate(pixel_x = 32, time = 20, PARALLEL) at 31, then animate(pixel_y = 32, time = 20, PARALLEL) at 36: the
    // frame at 36 re-sends the first step and adds the second with flags 0x4 and StepDelay 0, so y runs 36 .. 56
    // while x runs 31 .. 51.
    void parallelOverlap() {
        QCOMPARE(offset(30), QPoint(0, 0));
        QCOMPARE(offset(36), QPoint(8, 0));
        QCOMPARE(offset(41), QPoint(16, 8));
        QCOMPARE(offset(51), QPoint(32, 24));
        QCOMPARE(offset(56), QPoint(32, 32));
        QImage img = image(56);  // the marker past the east edge fills the revealed column; the HUD stays
        QCOMPARE(rgb(img, 340, 200), kMarker);
        QCOMPARE(rgb(img, 0, 320), kHud);
        QCOMPARE(offset(65), QPoint(32, 32));  // the Count = 0 frame at 60 clears the chain; 0x0007 still says 32, 32
        QCOMPARE(offset(66), QPoint(0, 0));
    }

    // vg's directional_recoil shape: a 4-tick SINE | EASE_OUT kick to (20, 8) at 76, then at 81 the return, sent as a
    // head step (flags 0x40000080, StepDelay 0) behind the re-sent kick. The return starts at 81, not where the kick
    // ended (80): the view holds (20, 8) until then.
    void kickThenReturn() {
        QCOMPARE(offset(78), QPoint(14, 5));
        QCOMPARE(offset(80), QPoint(20, 8));
        QCOMPARE(offset(81), QPoint(20, 8));
        QCOMPARE(offset(87), QPoint(15, 6));  // half way through 12 ticks of SINE | EASE_IN
        QCOMPARE(offset(93), QPoint(0, 0));
    }

    // A 2-tick kick at 227 and the return at 232: the return is the head step of the frame at 232 (StepDelay 0), so it
    // starts at 232, three ticks after the kick ended. Stock DreamSeeker measured 0.49 s from kick to return.
    void headStartsAtStepDelay() {
        QCOMPARE(offset(229), QPoint(20, 0));
        QCOMPARE(offset(232), QPoint(20, 0));
        QCOMPARE(offset(238), QPoint(15, 0));
        QCOMPARE(offset(244), QPoint(0, 0));
    }

    // animate(pixel_x = 32, time = 20) at 101; pixel_x = 6 at 106 (a 0x0007, no 0x00f0): the tween keeps its timing and
    // ends on 6, shifted by the -26 the set made.
    void directSetShiftsTween() {
        QCOMPARE(offset(105), QPoint(6, 0));
        // The retained WorldState changes pixel_x from 32 to 6 at tick 106.
        // Fractional sampling must not ingest that event early or change its arrival clock.
        QVERIFY(!image(105.5).isNull());
        QCOMPARE(replay_->session.world().viewPixelX, 32);
        QCOMPARE(replay_->session.ingestClockNs(), at(105));
        QCOMPARE(offset(106), QPoint(-18, 0));
        QCOMPARE(replay_->session.world().viewPixelX, 6);
        QVERIFY(!image(106.5).isNull());
        QCOMPARE(replay_->session.world().viewPixelX, 6);
        QCOMPARE(replay_->session.ingestClockNs(), at(106));
        QCOMPARE(offset(111), QPoint(-10, 0));
        QCOMPARE(offset(121), QPoint(6, 0));
        QCOMPARE(offset(130), QPoint(6, 0));
    }

    // The same tween with pixel_y = 6 set at 146: y moves at once, x carries on.
    void directSetOtherAxis() {
        QCOMPARE(offset(146), QPoint(8, 6));
        QCOMPARE(offset(151), QPoint(16, 6));
        QCOMPARE(offset(161), QPoint(32, 6));
    }

    // directional_recoil eight times, two ticks apart, from 181: each return (a head) starts from the kick before it
    // and each new kick (a parallel) adds 16 on top, so the view stays kicked between 16 and 30 until the last return
    // ends at 200.
    void recoilBurst() {
        QCOMPARE(offset(182), QPoint(16, 0));
        QCOMPARE(offset(185), QPoint(16, 0));
        QCOMPARE(offset(185.5), QPoint(27, 0));
        QCOMPARE(offset(186), QPoint(30, 0));
        QCOMPARE(offset(192), QPoint(30, 0));
        QCOMPARE(offset(198), QPoint(14, 0));
        QCOMPARE(offset(200), QPoint(0, 0));
    }
};

FUGUE_TEST_MAIN(ClientAnim)
#include "clientanim_test.moc"
