// Differential coverage for separable HUD sampling against the original per-pixel path.
#include <QtTest>
#include <array>
#include <bit>
#include <cstdio>
#include "net/resource.h"
#include "render/pngwrite.h"
#include "render/scene.h"

using namespace fugue;
namespace {
struct Fixture {
    model::Session session;
    render::ResStore resources;
    render::SceneCache cache;

    bool init(int cols = 15, int rows = 13) {
        auto& w = session.worldMut();
        w.bounds.have = true;
        w.bounds.maxX = w.bounds.viewW = cols;
        w.bounds.maxY = w.bounds.viewH = rows;
        w.bounds.winW = w.bounds.winH = 1;
        w.grid.have = true; w.grid.cells = {0};
        w.hudOrder = {1}; w.hud[1] = "{\"appid\":1}";
        // Non-square source, distinct rows/columns and transparent (nonzero RGB),
        // translucent and opaque pixels exercise both row reuse and alpha handling.
        render::RgbaImage image;
        image.w = 32; image.h = 24; image.px.resize(32 * 24 * 4);
        for (int y = 0; y < image.h; ++y) for (int x = 0; x < image.w; ++x) {
            auto* p = image.px.data() + (y * image.w + x) * 4;
            p[0] = uint8_t(x * 7); p[1] = uint8_t(y * 9); p[2] = uint8_t(x + y);
            p[3] = (x + y) % 3 == 0 ? 0 : (x + y) % 3 == 1 ? 128 : 255;
        }
        const auto bytes = render::encodePng(image);
        const auto id = net::resId(bytes);
        w.resources.emplace_back(id, 0);
        return resources.add(bytes, &id, nullptr);
    }
    void transform(const std::array<float, 6>& matrix, const std::string& loc = "CENTER,CENTER") {
        std::string json = "{\"iconResID\":0,\"hasTransform\":true,\"screenLoc\":\"" + loc + "\",\"transform\":[";
        for (size_t i = 0; i < matrix.size(); ++i) {
            if (i) json += ',';
            char f[40];
            std::snprintf(f, sizeof f, "{\"f32\":\"0x%08x\"}", std::bit_cast<uint32_t>(matrix[i]));
            json += f;
        }
        session.worldMut().appearances[1].fieldsJson = json + "]}";
    }
    bool assemble(render::DrawList& dl, render::SceneStats& stats, bool reference) {
        struct Reset { ~Reset() { render::setSceneMutation(0); } } reset;
        render::setSceneMutation(reference ? 42 : 0);
        stats.profilePreparation = true;
        return render::assembleMapControl(session, resources, 0, dl, stats, nullptr, true, {}, &cache);
    }
};
}

class HudWarp : public QObject {
    Q_OBJECT
private slots:
    void exactPixels() {
        Fixture f; QVERIFY(f.init());
        const std::array<float, 6> matrices[] = {
            {2, 0, 0, 0, 2, 0}, {92.4f, 0, 0, 0, 92.4f, 0},
            {-3, 0, 0.5f, 0, 4, -0.5f}, {3, 0, -0.5f, 0, -4, 0.5f},
            {-0.5f, 0, 7.25f, 0, -0.25f, -9.75f},
            {1, 0, 0.5f, 0, 1, 0.5f}, {1, -0.0f, -0.5f, 0, 1, -0.5f},
            {0.125f, 0, 0, 0, 8, 0}, {8, 0, 0, 0, 0.125f, 0},
            {0, 0, 0, 0, 2, 0}, // singular
            {1, 0, 10000, 0, 1, -10000}, // off-canvas
            {2, 0.25f, 1, -0.5f, 2, 3}, {0, 1, 0, -1, 0, 0} // general path
        };
        for (const auto& matrix : matrices) for (const char* loc : {"CENTER,CENTER", "1:3,1:-5", "15,13", "2,3 to 3,4"}) {
            f.transform(matrix, loc);
            render::DrawList reference, actual; render::SceneStats before, after;
            QVERIFY(f.assemble(reference, before, true));
            QVERIFY(f.assemble(actual, after, false));
            QVERIFY(render::sameDrawList(reference, actual));
            QCOMPARE(render::execute(reference).px, render::execute(actual).px);
            QVERIFY(actual.hit && reference.hit);
            QCOMPARE(actual.hit->hud.size(), reference.hit->hud.size());
            // Mouse masks are still the unwarped source, not the optimized output.
            if (!actual.hit->hud.empty()) {
                const auto& a = actual.hit->hud.front(); const auto& b = reference.hit->hud.front();
                QCOMPARE(a.tileW, b.tileW); QCOMPARE(a.tileH, b.tileH);
                QCOMPARE(a.layers.size(), b.layers.size());
                for (size_t i = 0; i < a.layers.size(); ++i)
                    QCOMPARE(a.layers[i].sprite->px, b.layers[i].sprite->px);
            }
        }
    }
    void maximumZoom() {
        Fixture f; QVERIFY(f.init(71, 71));
        f.transform({92.4f, 0, 0, 0, 92.4f, 0});
        render::DrawList reference, actual; render::SceneStats before, after;
        QVERIFY(f.assemble(reference, before, true)); QVERIFY(f.assemble(actual, after, false));
        QCOMPARE(actual.outW, 2272); QCOMPARE(actual.outH, 2272);
        QVERIFY(render::sameDrawList(reference, actual));
        QVERIFY(!actual.planes.empty());
        QCOMPARE(actual.planes.front().layers.front().sprite->w, 2272);
        QVERIFY(after.hudMs > 0); QVERIFY(after.gridMs > 0);
        render::DrawList unprofiled; render::SceneStats stats;
        QVERIFY(render::assembleMapControl(f.session, f.resources, 0, unprofiled, stats, nullptr, true));
        QVERIFY(render::sameDrawList(actual, unprofiled));
        QCOMPARE(stats.gridMs, 0.0); QCOMPARE(stats.placementMs, 0.0);
        QCOMPARE(stats.hudMs, 0.0); QCOMPARE(stats.planesMs, 0.0);
    }
    void benchmark() {
        if (!qEnvironmentVariableIntValue("FUGUE_SCENE_BENCHMARK")) QSKIP("Set FUGUE_SCENE_BENCHMARK=1 for HUD sampling timings");
        Fixture f; QVERIFY(f.init(71, 71)); f.transform({92.4f, 0, 0, 0, 92.4f, 0});
        render::DrawList expected; render::SceneStats stats; QVERIFY(f.assemble(expected, stats, true));
        for (bool reference : {true, false, false, true}) {
            double elapsed = 0;
            constexpr int warmups = 8, count = 60;
            for (int i = 0; i < warmups + count; ++i) {
                render::DrawList dl; render::SceneStats sample;
                QVERIFY(f.assemble(dl, sample, reference));
                if (i >= warmups) elapsed += sample.hudMs;
                QVERIFY(render::sameDrawList(expected, dl));
            }
            qInfo().nospace() << (reference ? "scalar" : "separable") << " HUD=" << elapsed / count << "ms";
        }
    }
};
QTEST_GUILESS_MAIN(HudWarp)
#include "hudwarp_test.moc"
