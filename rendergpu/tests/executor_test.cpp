#include <QtTest>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_map>
#include "rendergpu/executor.h"

using namespace fugue::render;
using fugue::rendergpu::Executor;

namespace {
// Every optimization off: the plain recording each optimized path must match byte for byte.
class ReferenceExecutor : public Executor {
public:
    explicit ReferenceExecutor(QRhi& rhi, fugue::rendergpu::Limits limits = {}, fugue::rendergpu::Options options = {})
        : Executor(rhi, limits, options, fugue::rendergpu::Features{false, false, false, false}) {}
};

SpritePtr image(int w, int h, std::initializer_list<Rgba8> pixels) {
    auto s = std::make_shared<Sprite>();
    s->w = w; s->h = h;
    for (Rgba8 p : pixels) s->px.insert(s->px.end(), {p.r, p.g, p.b, p.a});
    return s;
}
SpritePtr solid(int w, int h, Rgba8 p) {
    auto s = std::make_shared<Sprite>();
    s->w = w; s->h = h;
    for (int i = 0; i < w * h; i++) s->px.insert(s->px.end(), {p.r, p.g, p.b, p.a});
    return s;
}
ColorMatrixPtr identityMatrix() {
    return std::make_shared<ColorMatrix>(ColorMatrix{
        1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1,  0, 0, 0, 0});
}
DrawLayer layer(SpritePtr s, int x = 0, int y = 0) {
    DrawLayer l;
    l.sprite = std::move(s); l.x = x; l.y = y;
    return l;
}
DrawList list(int w, int h, std::initializer_list<DrawLayer> layers) {
    DrawList dl;
    dl.outW = w; dl.outH = h;
    dl.planes.push_back(PlanePass{0, layers, {}});
    return dl;
}
// Freeze mutable test builders before handing them to the explicitly immutable recording API.
std::shared_ptr<const DrawList> freeze(const DrawList& source) {
    auto dl = std::make_shared<DrawList>(source);
    std::unordered_map<const Sprite*, SpritePtr> sprites;
    auto matrix = [](const ColorMatrixPtr& m) -> ColorMatrixPtr { return m ? std::make_shared<ColorMatrix>(*m) : nullptr; };
    auto layers = [&](auto& ls) {
        for (auto& l : ls) {
            if (l.sprite) {
                auto& copy = sprites[l.sprite.get()];
                if (!copy) copy = std::make_shared<Sprite>(*l.sprite);
                l.sprite = copy;
            }
            l.cmat = matrix(l.cmat);
        }
    };
    for (auto& p : dl->planes) {
        layers(p.layers);
        if (p.master) {
            auto m = std::make_shared<PlaneMasterSpec>(*p.master);
            m->cmat = matrix(m->cmat); p.master = m;
        }
    }
    for (auto& h : dl->hud) layers(h.layers);
    return dl;
}
}  // namespace

class GpuExecutorTest : public QObject {
    Q_OBJECT
private:
    // Destruction order matters: the executor before QRhi, QRhi before the GL fallback surface.
    std::unique_ptr<QOffscreenSurface> surface_;
    std::unique_ptr<QRhi> rhi_;
    std::unique_ptr<Executor> executor_;
    RgbaImage last_;

    void compare(const DrawList& dl, int tolerance = 0, const DrawList* reference = nullptr, Executor* executor = nullptr,
                 std::shared_ptr<const DrawList> immutable = {}) {
        compareUsing(executor ? *executor : *executor_, dl, tolerance, reference, std::move(immutable));
    }

    template<class Gpu>
    void compareUsing(Gpu& gpu, const DrawList& dl, int tolerance = 0, const DrawList* reference = nullptr,
                      std::shared_ptr<const DrawList> immutable = {}) {
        std::string error;
        QVERIFY2(immutable ? gpu.supports(immutable, &error) : gpu.supports(dl, &error), error.c_str());
        QRhiCommandBuffer* cb = nullptr;
        QCOMPARE(rhi_->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
        QRhiTexture* texture = immutable ? gpu.record(immutable, *cb, &error) : gpu.record(dl, *cb, &error);
        if (!texture) {
            rhi_->endOffscreenFrame();
            QFAIL(error.c_str());
        }
        QRhiReadbackResult result;
        bool complete = false;
        result.completed = [&] { complete = true; };
        auto updates = rhi_->nextResourceUpdateBatch();
        updates->readBackTexture(QRhiReadbackDescription(texture), &result);
        cb->resourceUpdate(updates);
        QCOMPARE(rhi_->endOffscreenFrame(), QRhi::FrameOpSuccess);
        // Offscreen frames without Async flags finish readbacks before returning.
        QVERIFY(complete);
        QCOMPARE(result.format, QRhiTexture::RGBA8);
        QCOMPARE(result.pixelSize, QSize(dl.outW, dl.outH));
        RgbaImage cpu = execute(reference ? *reference : dl);
        QCOMPARE(size_t(result.data.size()), cpu.px.size());
        last_.w = dl.outW; last_.h = dl.outH;
        const auto* pixels = reinterpret_cast<const uint8_t*>(result.data.constData());
        last_.px.assign(pixels, pixels + result.data.size());
        int worst = 0;
        size_t at = 0, different = 0;
        for (size_t i = 0; i < cpu.px.size(); i++) {
            int delta = std::abs(int(cpu.px[i]) - int(last_.px[i]));
            if (delta) ++different;
            if (delta > worst) { worst = delta; at = i; }
        }
        QString detail = QString("max error %1 at (%2,%3) channel %4: CPU %5 GPU %6; %7 differing channels")
                             .arg(worst).arg((at / 4) % size_t(dl.outW)).arg((at / 4) / size_t(dl.outW))
                             .arg(at % 4).arg(cpu.px[at]).arg(last_.px[at]).arg(different);
        QVERIFY2(worst <= tolerance, qPrintable(detail));
    }

    void compareCached(const DrawList& dl, Executor& cached, Executor& uncached, int tolerance = 2) {
        compare(dl, tolerance, nullptr, &uncached); // identical production optimizations, without output retention
        const auto pixels = last_.px;
        auto immutable = freeze(dl);
        std::weak_ptr<const DrawList> weak = immutable;
        compare(*immutable, tolerance, nullptr, &cached, immutable);
        QCOMPARE(last_.px, pixels); // NO extra pixel tolerance for caching
        immutable.reset();
        QVERIFY(weak.expired()); // retaining a plane must not retain the whole list or its hit sidecar
        QCOMPARE(cached.stats().renderPasses + cached.stats().avoidedPlanePasses, uncached.stats().renderPasses);
    }

    void compareFusion(const DrawList& dl, Executor& fused, ReferenceExecutor& unfused, uint64_t removedPasses,
                       int tolerance = 1) {
        compareUsing(unfused, dl, tolerance);
        const auto original = last_.px;
        const auto before = unfused.stats();
        compare(dl, tolerance, nullptr, &fused);
        QCOMPARE(last_.px, original); // fusion must match the old GPU path EXACTLY, not just CPU tolerance
        QCOMPARE(fused.stats().renderPasses + removedPasses, before.renderPasses);
        QCOMPARE(fused.stats().fullSurfaceDraws + removedPasses, before.fullSurfaceDraws);
    }

private slots:
    void initTestCase() {
        QString backend = qEnvironmentVariable("FUGUE_GPU_TEST_BACKEND");
        QRhi::Flags flags;
        if (qEnvironmentVariableIntValue("FUGUE_GPU_TEST_SOFTWARE")) flags |= QRhi::PreferSoftwareRenderer;
#ifdef Q_OS_WIN
        if (backend.isEmpty()) backend = "d3d11";
        if (backend == "d3d11") {
            QRhiD3D11InitParams params;
            rhi_.reset(QRhi::create(QRhi::D3D11, &params, flags));
        }
#endif
#if QT_CONFIG(metal)
        if (backend.isEmpty()) backend = "metal";
        if (backend == "metal") {
            QRhiMetalInitParams params;
            rhi_.reset(QRhi::create(QRhi::Metal, &params, flags));
        }
#endif
#if QT_CONFIG(opengl)
        if (backend.isEmpty()) backend = "opengl";
        if (backend == "opengl") {
            QRhiGles2InitParams params;
            params.format.setVersion(3, 2);
            params.format.setProfile(QSurfaceFormat::CoreProfile);
            surface_.reset(QRhiGles2InitParams::newFallbackSurface(params.format));
            params.fallbackSurface = surface_.get();
            rhi_.reset(QRhi::create(QRhi::OpenGLES2, &params, flags));
        }
#endif
        // Opting into this target means it must actually execute graphics work, never pass via the null backend.
        QVERIFY2(rhi_, qPrintable("Cannot create requested GPU backend: " + backend));
        qInfo() << "GPU backend:" << backend << "device:" << rhi_->driverInfo().deviceName;
        executor_ = std::make_unique<Executor>(*rhi_);
    }

    void emptyAndResize() {
        DrawList dl;
        dl.outW = 7; dl.outH = 3;
        compare(dl);
        dl.groundBlack = false;
        compare(dl);
        dl.outW = 1; dl.outH = 9;
        compare(dl);
        dl.outW = 11; dl.outH = 2; dl.groundBlack = true;
        compare(dl);
    }

    void coordinatesClippingAndOrdering() {
        auto s = image(2, 3, {{255, 0, 0, 255}, {0, 255, 0, 255},
                              {0, 0, 255, 255}, {255, 255, 0, 255},
                              {255, 0, 255, 255}, {0, 255, 255, 255}});
        DrawList dl = list(7, 5, {layer(s, 1, 1), layer(s, -1, -1), layer(s, 6, 4),
                                  layer(s, -10000, 0), layer(s, 0, 10000)});
        compare(dl);
        std::reverse(dl.planes[0].layers.begin(), dl.planes[0].layers.end());
        compare(dl);
        dl.groundBlack = false;
        compare(dl);
    }

    void batchedCutoutsAndCompatibilityTransitions() {
        auto s = image(3, 2, {{99, 87, 65, 0}, {12, 34, 56, 255}, {255, 200, 90, 255},
                              {123, 77, 91, 255}, {210, 123, 88, 0}, {234, 100, 70, 255}});
        auto dl = list(37, 29, {});
        for (int i = 0; i < 300; ++i) {
            auto l = layer(s, i % 39 - 2, (i * 7) % 30 - 1);
            l.hasTint = true; l.tintR = uint8_t(i); l.tintG = 137; l.tintB = 213;
            l.alpha = 300; // clamped to 255, still safe
            l.blend = uint8_t(i % 2);
            dl.planes[0].layers.push_back(l);
        }
        compare(dl);
        QCOMPARE(executor_->stats().renderPasses, uint64_t(3)); // clear plane, clear scene, plane over + flatten
        QCOMPARE(executor_->stats().spriteDraws, uint64_t(300));
        QCOMPARE(executor_->stats().fullSurfaceDraws, uint64_t(1));
        auto batched = last_.px;
        Executor compatibility(*rhi_, {}, {false, false});
        compare(dl, 0, nullptr, &compatibility);
        QCOMPARE(last_.px, batched);
        QCOMPARE(compatibility.stats().spriteDraws, uint64_t(0));
        QCOMPARE(compatibility.stats().renderPasses, uint64_t(303));

        // A full-surface compatibility draw may be followed by cutouts in the SAME pass. Check order,
        // clipping, and transparent RGB cleanup after every blend mode, including zero-alpha matrices.
        auto matrix = std::make_shared<ColorMatrix>();
        matrix->fill(0); (*matrix)[16] = 0.7; (*matrix)[17] = 0.3;
        for (int mode = kBlendDefault; mode <= kBlendInset; ++mode) {
            auto first = layer(solid(7, 5, {171, 65, 33, 127}));
            first.blend = uint8_t(mode);
            auto colouredClear = first; colouredClear.cmat = matrix;
            auto fractional = layer(s, 1, 1); fractional.alpha = 254;
            auto mixed = list(7, 5, {first, layer(s, -1, 1), colouredClear, layer(s, 5, -1),
                                      fractional, layer(s, 1, 1)});
            mixed.groundBlack = false;
            compare(mixed, 1);
            QCOMPARE(executor_->stats().spriteDraws, uint64_t(mode <= kBlendOver ? 4 : 3));
            auto optimized = last_.px;
            compare(mixed, 1, nullptr, &compatibility);
            QCOMPARE(last_.px, optimized);
        }
    }

    void disjointCompatibilityBatches() {
        auto s = image(4, 2, {{170, 90, 40, 0}, {70, 230, 67, 1}, {10, 70, 90, 127}, {170, 120, 50, 255},
                              {90, 210, 76, 254}, {130, 60, 70, 128}, {12, 34, 56, 3}, {20, 90, 210, 255}});
        auto matrix = std::make_shared<ColorMatrix>(ColorMatrix{
            0, 1, 0, 0,  0, 0, 1, 0,  1, 0, 0, 0,  0, 0, 0, 0.75,  0.1, 0, 0, 0.1});
        Executor compatibility(*rhi_, {}, {false, false});
        for (int mode = kBlendDefault; mode <= kBlendInset; ++mode) {
            auto dl = list(19, 11, {layer(solid(19, 11, {93, 70, 150, 101}))});
            for (int i = 0; i < 9; ++i) {
                auto l = layer(s, (i % 3) * 5 - 1, (i / 3) * 4 - 1);
                l.blend = uint8_t(mode); l.cmat = matrix;
                l.hasTint = true; l.tintG = 177; l.alpha = 198;
                dl.planes[0].layers.push_back(l);
            }
            dl.groundBlack = false;
            compare(dl, 1);
            QCOMPARE(executor_->stats().regionDraws, uint64_t(8));
            QCOMPARE(executor_->stats().renderPasses, uint64_t(5));
            auto optimized = last_.px;
            compare(dl, 1, nullptr, &compatibility);
            QCOMPARE(last_.px, optimized);

            // Overlap with an earlier region must split the batch even if it misses the most recent region.
            dl.planes[0].layers.push_back(dl.planes[0].layers[1]);
            compare(dl, 1);
            QCOMPARE(executor_->stats().renderPasses, uint64_t(6));
            optimized = last_.px;
            compare(dl, 1, nullptr, &compatibility);
            QCOMPARE(last_.px, optimized);
        }
        auto translucent = solid(2, 2, {100, 30, 200, 127});
        auto opaque = solid(2, 2, {20, 220, 80, 255});
        auto mixed = list(11, 7, {layer(translucent), layer(opaque, 6, 0),
                                   layer(translucent, 6, 0), layer(translucent, 0, 3)});
        compare(mixed, 1); // the direct overwrite must be included in the snapshot's dirty regions
        QCOMPARE(executor_->stats().regionDraws, uint64_t(1));
        QCOMPARE(executor_->stats().renderPasses, uint64_t(5));
        auto optimized = last_.px;
        compare(mixed, 1, nullptr, &compatibility);
        QCOMPARE(last_.px, optimized);

        // HUD canvases use their own dimensions and the same overlap rules, then the integer HUD blit.
        HudPass hud; hud.tileW = mixed.outW; hud.tileH = mixed.outH;
        hud.layers = mixed.planes[0].layers; hud.dests = {{-1, 1}, {5, -1}};
        auto hudList = list(17, 11, {}); hudList.hud = {hud};
        compare(hudList, 1);
        QVERIFY(executor_->stats().regionDraws > 0);
        optimized = last_.px;
        compare(hudList, 1, nullptr, &compatibility);
        QCOMPARE(last_.px, optimized);
    }

    void boundedDisjointBatches() {
        auto translucent = solid(1, 1, {20, 70, 230, 127});
        auto dl = list(520, 1, {});
        for (int i = 0; i < 260; ++i) dl.planes[0].layers.push_back(layer(translucent, i * 2));
        compare(dl);
        QCOMPARE(executor_->stats().renderPasses, uint64_t(5)); // two bounded batches, not 260 passes
        QCOMPARE(executor_->stats().regionDraws, uint64_t(258));
        auto optimized = last_.px;
        Executor compatibility(*rhi_, {}, {false, false});
        compare(dl, 0, nullptr, &compatibility);
        QCOMPARE(last_.px, optimized);

        dl.planes[0].layers = {layer(translucent)};
        auto opaque = solid(1, 1, {210, 50, 90, 255});
        for (int i = 1; i <= 260; ++i) dl.planes[0].layers.push_back(layer(opaque, i));
        dl.planes[0].layers.push_back(layer(translucent, 260));
        compare(dl, 1); // exceeding the tracking budget must invalidate, not forget older dirty rectangles
        QCOMPARE(executor_->stats().regionDraws, uint64_t(0));
        optimized = last_.px;
        compare(dl, 1, nullptr, &compatibility);
        QCOMPARE(last_.px, optimized);
    }

    void matrixCutouts() {
        auto binary = image(2, 2, {{190, 40, 78, 0}, {50, 140, 210, 255},
                                   {230, 70, 10, 255}, {60, 210, 100, 0}});
        auto fractional = solid(2, 2, {70, 130, 190, 127});
        auto matrix = std::make_shared<ColorMatrix>(ColorMatrix{
            0, 1, 0, 0,  0, 0, 1, 0,  1, 0, 0, 0,  0, 0, 0, 1,  0.1, 0.2, 0.3, 0});
        Executor compatibility(*rhi_, {}, {false, false});
        for (int kind = 0; kind < 6; ++kind) {
            (*matrix)[3] = 0;
            (*matrix)[15] = 1;
            (*matrix)[19] = 0;
            auto l = layer(binary, -1, 1); l.cmat = matrix;
            l.hasTint = true; l.tintR = 191;
            if (kind == 1) { (*matrix)[15] = 0; (*matrix)[19] = 1; l.sprite = fractional; l.alpha = 0; }
            if (kind == 2) { (*matrix)[15] = 0; (*matrix)[19] = -0.1; l.sprite = fractional; }
            if (kind == 3) { (*matrix)[3] = 0.25; }
            if (kind == 4) { (*matrix)[15] = 0.5; }
            if (kind == 5) { (*matrix)[15] = 0; (*matrix)[19] = 0.5; }
            auto dl = list(5, 4, {l}); dl.groundBlack = false;
            compare(dl, 1);
            QCOMPARE(executor_->stats().spriteDraws, uint64_t(kind < 3 ? 1 : 0));
            auto optimized = last_.px;
            compare(dl, 1, nullptr, &compatibility);
            QCOMPARE(last_.px, optimized);
        }
    }

    void spriteContentCache() {
        Executor cached(*rhi_);
        auto dl = list(7, 5, {layer(solid(2, 3, {70, 190, 43, 255})),
                              layer(solid(2, 3, {70, 190, 43, 255}), 3, 1)});
        compare(dl, 0, nullptr, &cached);
        QCOMPARE(cached.stats().uploadedSprites, uint64_t(1)); // distinct identities, identical content
        QCOMPARE(cached.stats().uploadedBytes, uint64_t(24));
        QCOMPARE(cached.stats().reusedSprites, uint64_t(0));
        dl.planes[0].layers[0].sprite = solid(2, 3, {70, 190, 43, 255}); // assembler creates fresh objects
        compare(dl, 0, nullptr, &cached);
        QCOMPARE(cached.stats().uploadedSprites, uint64_t(0));
        QCOMPARE(cached.stats().reusedSprites, uint64_t(1));

        auto changed = std::make_shared<Sprite>(*dl.planes[0].layers[0].sprite);
        dl.planes[0].layers[0].sprite = changed;
        changed->w = 3; changed->h = 2; // same byte payload but different dimensions is NOT a cache hit
        compare(dl, 0, nullptr, &cached);
        QCOMPARE(cached.stats().uploadedSprites, uint64_t(1));
        QCOMPARE(cached.stats().reusedSprites, uint64_t(1));
        changed->px[3] = 127; // even a caller retaining a mutable alias must not poison cache content/classification
        compare(dl, 1, nullptr, &cached);
        QCOMPARE(cached.stats().uploadedSprites, uint64_t(1));
        QCOMPARE(cached.stats().spriteDraws, uint64_t(1));
        changed->px[3] = 255;
        compare(dl, 0, nullptr, &cached);
        QCOMPARE(cached.stats().uploadedSprites, uint64_t(1)); // absent textures expire after one list
        QCOMPARE(cached.stats().spriteDraws, uint64_t(2));
        compare(list(1, 1, {}), 0, nullptr, &cached);
        compare(dl, 0, nullptr, &cached);
        QCOMPARE(cached.stats().uploadedSprites, uint64_t(2));
        QCOMPARE(cached.stats().reusedSprites, uint64_t(0));

        Executor uncached(*rhi_, {}, {true, false});
        for (int i = 0; i < 2; ++i) {
            compare(dl, 0, nullptr, &uncached);
            QCOMPARE(uncached.stats().uploadedSprites, uint64_t(2));
            QCOMPARE(uncached.stats().reusedSprites, uint64_t(0));
        }
    }

    void reusableUniformsAndBindings() {
        Executor pooled(*rhi_);
        Executor compatibility(*rhi_, {}, {false, false, false});
        auto s = image(2, 2, {{200, 70, 90, 0}, {50, 170, 230, 255},
                              {70, 90, 210, 255}, {230, 120, 50, 0}});
        auto dl = list(37, 19, {});
        const auto identity = identityMatrix(); // keep this a per-draw uniform/binding reuse test
        // Enough draws to overflow one uniform page on this adapter (176 active bytes per ordinary draw).
        const int perPage = std::min(65536, rhi_->resourceLimit(QRhi::MaxUniformBufferRange)) / rhi_->ubufAligned(176);
        for (int i = 0; i < perPage + 64; ++i) {
            auto l = layer(s, i % 37, (i * 7) % 19);
            l.hasTint = true; l.tintR = uint8_t(i); l.cmat = identity;
            dl.planes[0].layers.push_back(l);
        }
        compare(dl, 0, nullptr, &pooled);
        QVERIFY(pooled.stats().createdUniformPages > 1);
        QVERIFY(pooled.stats().createdBindings < 20); // repeated texture bindings share dynamic uniform offsets
        auto original = last_.px;
        compare(dl, 0, nullptr, &pooled);
        QCOMPARE(last_.px, original);
        QCOMPARE(pooled.stats().createdUniformPages, uint64_t(0));
        QCOMPARE(pooled.stats().createdBindings, uint64_t(0)); // composition/flatten bindings are now reused too
        for (size_t i = 0; i < dl.planes[0].layers.size(); ++i) {
            auto& l = dl.planes[0].layers[i];
            l.alpha = i % 3 == 0 ? 127 : 255;
            l.tintR = uint8_t(i * 3); l.y = int(i % 19);
        }
        // Switching shader paths changes which slices/bindings are used; no previous frame's data may leak.
        compare(dl, 2, nullptr, &pooled);
        auto optimized = last_.px;
        compare(dl, 2, nullptr, &compatibility);
        QCOMPARE(last_.px, optimized);
        for (auto& l : dl.planes[0].layers) l.alpha = 255;
        dl.outW = 41; dl.outH = 23; // recreates surfaces and sampler, invalidating cached bindings
        compare(dl, 0, nullptr, &pooled);
        QVERIFY(pooled.stats().createdBindings > 2);
        QCOMPARE(pooled.stats().createdUniformPages, uint64_t(0));
        compare(list(3, 2, {}), 0, nullptr, &pooled); // drop all source textures/bindings, retain page capacity
        compare(dl, 0, nullptr, &pooled);
        QCOMPARE(pooled.stats().uploadedSprites, uint64_t(1));
        for (int i = 0; i < 2; ++i) {
            compare(dl, 0, nullptr, &compatibility);
            QVERIFY(compatibility.stats().createdUniformPages > 1);
        }
    }

    void reusableCompatibilitySurfacesAndBindings() {
        Executor pooled(*rhi_);
        Executor unpooled(*rhi_, {}, {true, true, false}); // ONLY allocation reuse differs
        auto s = image(2, 2, {{200, 70, 90, 0}, {50, 170, 230, 127},
                              {70, 90, 210, 255}, {230, 120, 50, 1}});
        auto dl = list(13, 9, {layer(solid(13, 9, {50, 90, 170, 180}))});
        auto master = std::make_shared<PlaneMasterSpec>();
        master->renderTarget = "*cached";
        FilterSpec blur; blur.kind = FilterSpec::Blur; blur.size = 0.75;
        FilterSpec outline; outline.kind = FilterSpec::Outline; outline.size = 1.25; outline.alpha = 93;
        FilterSpec shadow; shadow.kind = FilterSpec::DropShadow; shadow.offset = 1; shadow.x = 1; shadow.alpha = 127;
        master->filters = {blur, outline, shadow};
        PlanePass target{1, {}, master};
        for (int i = 0; i < 90; ++i) {
            auto l = layer(s, i % 12, (i * 3) % 8);
            l.alpha = 170; l.blend = uint8_t(i % 6);
            target.layers.push_back(l);
        }
        dl.planes.push_back(target);
        DrawLayer relay; relay.relay = "*cached"; relay.relayBottomY = dl.outH;
        dl.planes.push_back(PlanePass{2, {relay, layer(s, 3, 2)}, {}});
        HudPass h; h.tileW = 4; h.tileH = 3;
        h.layers = {layer(s), layer(s, 1, 1)};
        h.dests = {{0, 0}, {5, 4}, {6, 4}};
        dl.hud.push_back(h);
        h.tileW = 3; h.tileH = 2;
        dl.hud.push_back(h);
        auto check = [&] {
            compare(dl, 2, nullptr, &pooled);
            auto pixels = last_.px;
            compare(dl, 2, nullptr, &unpooled);
            QCOMPARE(last_.px, pixels); // allocation changes must not introduce ANY pixel differences
        };
        for (int frame = 0; frame < 3; ++frame) {
            // Fresh identities and changed uniforms must not prevent allocation reuse or retain stale pixels.
            dl.planes[1].layers[0].sprite = std::make_shared<Sprite>(*s);
            dl.planes[1].layers[0].alpha = 100 + frame;
            check();
            if (frame == 0) {
                QVERIFY(pooled.stats().createdSurfaces > 4);
                QVERIFY(pooled.stats().createdBindings > 0);
            } else {
                QCOMPARE(pooled.stats().createdSurfaces, uint64_t(0));
                QCOMPARE(pooled.stats().createdBindings, uint64_t(0));
            }
            QVERIFY(unpooled.stats().createdSurfaces > 0);
            QVERIFY(unpooled.stats().createdBindings > 0);
        }
        std::swap(dl.hud[0], dl.hud[1]); // matching by dimensions, not ordinal or name
        check();
        QCOMPARE(pooled.stats().createdSurfaces, uint64_t(0));
        master->filters.clear(); // retire scratch bindings before attachments
        dl.hud.clear();
        check();
        dl.hud.push_back(h); // absent surfaces expire, rather than accumulating a cross-frame pool
        master->filters = {outline, shadow};
        check();
        QVERIFY(pooled.stats().createdSurfaces > 0);
        dl.outW = 15; dl.outH = 11;
        check();
        QVERIFY(pooled.stats().createdSurfaces >= 4);
        check();
        QCOMPARE(pooled.stats().createdSurfaces, uint64_t(0));
        QCOMPARE(pooled.stats().createdBindings, uint64_t(0));
        dl.planes.clear(); dl.hud.clear();
        check(); // no stale pixels after releasing all transient attachments
    }

    void immutableValidation() {
        Executor gpu(*rhi_);
        auto dl = std::make_shared<const DrawList>(list(7, 5, {layer(solid(2, 2, {70, 90, 210, 255}))}));
        std::string error;
        QVERIFY(gpu.supports(dl, &error));
        error = "old diagnostic";
        QVERIFY(gpu.supports(dl, &error));
        QVERIFY(error.empty());
        compare(*dl, 0, nullptr, &gpu, dl);
        compare(*dl, 0, nullptr, &gpu, dl);
        QCOMPARE(gpu.stats().createdBindings, uint64_t(0));
        std::weak_ptr<const DrawList> weak = dl;
        dl.reset();
        QVERIFY(weak.expired()); // caching validation must not retain a scene

        auto invalid = list(7, 5, {}); invalid.outW = 0;
        auto bad = std::make_shared<const DrawList>(invalid);
        QVERIFY(!gpu.supports(bad, &error));
        QVERIFY(!error.empty());
        QVERIFY(!gpu.supports(std::shared_ptr<const DrawList>{}, &error));
        auto mutableList = list(7, 5, {});
        QVERIFY(gpu.supports(mutableList, &error));
        mutableList.outH = 0;
        QVERIFY(!gpu.supports(mutableList, &error)); // reference API always revalidates

        QRhiCommandBuffer* cb = nullptr;
        QCOMPARE(rhi_->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
        auto* rejected = gpu.record(bad, *cb, &error);
        QCOMPARE(rhi_->endOffscreenFrame(), QRhi::FrameOpSuccess);
        QVERIFY(!rejected);
        QVERIFY(!error.empty());
        dl = std::make_shared<const DrawList>(list(3, 2, {}));
        compare(*dl, 0, nullptr, &gpu, dl); // validation failure must not poison recording
    }

    void instancedAtlasSprites() {
        ReferenceExecutor reference(*rhi_);
        Executor gpu(*rhi_);
        auto dl = list(73, 51, {}); dl.groundBlack = false;
        for (int i = 0; i < 1024; ++i) {
            auto s = std::make_shared<Sprite>(); s->w = s->h = 8;
            for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
                s->px.insert(s->px.end(), {uint8_t(i), uint8_t((i >> 8) * 50 + y), uint8_t(x * 31),
                                          uint8_t((x + y + i) % 3 ? 255 : 0)});
            auto l = layer(s, (i * 7) % 79 - 6, (i * 13) % 57 - 6);
            l.hasTint = true; l.tintR = uint8_t(i * 17); l.tintG = 137;
            l.blend = uint8_t(i % 2); l.alpha = i % 13 ? 300 : 0;
            dl.planes[0].layers.push_back(l);
        }
        auto check = [&] { compareFusion(dl, gpu, reference, 0, 0); };
        check();
        QCOMPARE(gpu.stats().spriteDraws, uint64_t(1024));
        QCOMPARE(gpu.stats().instancedSprites, uint64_t(1024));
        QCOMPARE(gpu.stats().instancedDraws, uint64_t(1));
        QCOMPARE(gpu.stats().atlasUploads, uint64_t(1024));
        QCOMPARE(gpu.stats().uploadedInstanceBytes, uint64_t(1024 * 64));
        for (auto& l : dl.planes[0].layers) l.sprite = std::make_shared<Sprite>(*l.sprite);
        check(); // new assembler identities, same owned pixel content
        QCOMPARE(gpu.stats().atlasUploads, uint64_t(0));
        QCOMPARE(gpu.stats().createdBindings, uint64_t(0));
        auto changed = std::make_shared<Sprite>(*dl.planes[0].layers[1].sprite);
        changed->px[0] = 254;
        dl.planes[0].layers[1].sprite = changed;
        check();
        QCOMPARE(gpu.stats().atlasUploads, uint64_t(1));
        QCOMPARE(gpu.stats().uploadedAtlasBytes, uint64_t(8 * 8 * 4));
        changed->w = 16; changed->h = 4; // same byte payload; changed atlas shape must invalidate affected cells
        check();
        std::swap(dl.planes[0].layers[0], dl.planes[0].layers[511]);
        check(); // order matters for overlapping cutouts
        dl.outW = 81; dl.outH = 57;
        check();
        QVERIFY(gpu.stats().atlasUploads > 0);
        auto empty = list(81, 57, {}); empty.groundBlack = false;
        compareFusion(empty, gpu, reference, 0, 0);
        QCOMPARE(gpu.stats().atlasBytes, uint64_t(0));
        check();
        QVERIFY(gpu.stats().atlasUploads > 0); // absent atlases expire instead of accumulating across lists
    }

    void instancedTintRounding() {
        ReferenceExecutor reference(*rhi_);
        Executor gpu(*rhi_);
        auto ramp = std::make_shared<Sprite>(); ramp->w = 256; ramp->h = 1;
        for (int c = 0; c < 256; ++c) ramp->px.insert(ramp->px.end(), {uint8_t(c), uint8_t(255 - c), uint8_t(c), 255});
        auto dl = list(256, 256, {}); dl.groundBlack = false;
        for (int tint = 0; tint < 256; ++tint) {
            auto l = layer(ramp, 0, tint); l.hasTint = true;
            l.tintR = uint8_t(tint); l.tintG = uint8_t(255 - tint); l.tintB = uint8_t(tint * 37);
            dl.planes[0].layers.push_back(l);
        }
        compareFusion(dl, gpu, reference, 0, 0); // exhaustive colour/tint byte pairs, including rounding boundaries
        QCOMPARE(gpu.stats().instancedSprites, uint64_t(256));
        QCOMPARE(gpu.stats().instancedDraws, uint64_t(1));
        for (int c = 0; c < 256; ++c) ramp->px[size_t(c) * 4 + 3] = c % 3 ? 255 : 0;
        for (size_t i = 0; i < dl.planes[0].layers.size(); ++i) dl.planes[0].layers[i].alpha = i % 3 ? 300 : -1;
        compareFusion(dl, gpu, reference, 0, 0); // source and layer transparency still discard after tint
        QCOMPARE(gpu.stats().atlasUploads, uint64_t(1));
    }

    void instancedOrderBarriersAndHud() {
        ReferenceExecutor reference(*rhi_);
        Executor gpu(*rhi_);
        auto s = image(3, 2, {{190, 90, 30, 0}, {200, 70, 150, 255}, {20, 160, 230, 255},
                              {80, 50, 190, 255}, {40, 200, 70, 0}, {180, 170, 50, 255}});
        auto dl = list(17, 13, {}); dl.groundBlack = false;
        auto run = [&] {
            for (int i = 0; i < 8; ++i) {
                auto l = layer(s, i % 4 - 1, i % 3 - 1);
                l.hasTint = true; l.tintR = uint8_t(i * 31);
                dl.planes[0].layers.push_back(l);
            }
        };
        run();
        auto fractional = layer(solid(7, 5, {210, 30, 90, 127}));
        dl.planes[0].layers.push_back(fractional); // a destination snapshot splits the run
        run();
        auto matrix = std::make_shared<ColorMatrix>(); matrix->fill(0);
        (*matrix)[16] = 1; (*matrix)[19] = 1;
        auto opaqueMatrix = layer(solid(3, 3, {10, 80, 150, 0}), 1, 1); opaqueMatrix.cmat = matrix;
        dl.planes[0].layers.push_back(opaqueMatrix); // matrix can make transparent pixels opaque; not instanced
        run();
        auto master = std::make_shared<PlaneMasterSpec>(); master->renderTarget = "*slate";
        dl.planes.push_back(PlanePass{1, dl.planes[0].layers, master});
        DrawLayer relay; relay.relay = "*slate"; relay.relayBottomY = dl.outH;
        dl.planes[0].layers.push_back(relay);
        run(); // a resolved relay also splits instancing without changing the compatibility batch's dirty regions
        HudPass h; h.tileW = 9; h.tileH = 7; h.layers = dl.planes[0].layers;
        h.dests = {{-1, -1}, {4, 5}, {4, 5}, {10000, 0}};
        dl.hud = {h};
        compareFusion(dl, gpu, reference, 0, 1);
        QVERIFY(gpu.stats().instancedDraws >= 9);
        QVERIFY(gpu.stats().instancedSprites < gpu.stats().spriteDraws); // matrix draws stayed on the old path
        dl.groundBlack = true;
        compareFusion(dl, gpu, reference, 1, 1);
    }

    void instancedBudgetsAndSplits() {
        auto small = list(8, 8, {}); small.groundBlack = false;
        for (int i = 0; i < 4; ++i) small.planes[0].layers.push_back(layer(solid(2, 2, {uint8_t(i * 70), 90, 210, 255}), i, i));
        ReferenceExecutor reference(*rhi_);
        for (uint64_t extra : {uint64_t(0), uint64_t(63), uint64_t(64)}) {
            fugue::rendergpu::Limits limits;
            limits.maxDimension = 8;
            limits.maxTextureBytes = 8 * 8 * 16 + 4 * 2 * 2 * 4 + extra;
            Executor gpu(*rhi_, limits);
            auto immutable = std::make_shared<const DrawList>(small);
            QVERIFY(gpu.supports(immutable));
            QVERIFY(gpu.supports(list(1, 1, {}))); // unrelated reference validation must not corrupt the cached budget
            compare(*immutable, 0, nullptr, &gpu, immutable);
            QCOMPARE(gpu.stats().instancedDraws, uint64_t(extra == 64));
            QCOMPARE(gpu.stats().atlasBytes, extra == 64 ? uint64_t(64) : uint64_t(0));
            if (extra == 64) {
                auto crowded = small;
                crowded.planes[0].layers.push_back(layer(solid(2, 2, {1, 2, 3, 255}), 10000, 0));
                compareFusion(crowded, gpu, reference, 0, 0);
                QCOMPARE(gpu.stats().atlasBytes, uint64_t(0)); // even an existing atlas cannot exceed new headroom
                compareFusion(small, gpu, reference, 0, 0);
                QCOMPARE(gpu.stats().atlasUploads, uint64_t(4));
            }
        }
        Executor gpu(*rhi_);
        auto dl = list(17, 13, {}); dl.groundBlack = false;
        for (int i = 0; i < 20; ++i)
            dl.planes[0].layers.push_back(layer(solid(300, 300, {uint8_t(i * 11), 91, 230, 255}), -i, -i));
        compareFusion(dl, gpu, reference, 0, 0);
        QCOMPARE(gpu.stats().instancedDraws, uint64_t(2)); // two 3x3-cell atlases, then a tiny non-instanced tail
        QCOMPARE(gpu.stats().instancedSprites, uint64_t(18));
        QCOMPARE(gpu.stats().atlasBytes, uint64_t(2 * 900 * 900 * 4));
        std::reverse(dl.planes[0].layers.begin(), dl.planes[0].layers.end());
        compareFusion(dl, gpu, reference, 0, 0);
        QCOMPARE(gpu.stats().atlasUploads, uint64_t(18));
        auto shared = solid(2, 2, {190, 70, 130, 255});
        dl.planes[0].layers.clear();
        for (int i = 0; i < 2050; ++i) dl.planes[0].layers.push_back(layer(shared, i % 17, i % 13));
        compareFusion(dl, gpu, reference, 0, 0);
        QCOMPARE(gpu.stats().instancedDraws, uint64_t(2));
        QCOMPARE(gpu.stats().instancedSprites, uint64_t(2048)); // instance-count cap, not texture capacity
        dl.planes[0].layers = {layer(solid(1025, 1, {255, 10, 70, 255}))};
        for (int i = 0; i < 4; ++i) dl.planes[0].layers.push_back(layer(shared, i, i));
        compareFusion(dl, gpu, reference, 0, 0);
        QCOMPARE(gpu.stats().instancedSprites, uint64_t(4)); // oversized source stays direct; following run still batches
    }

    void packedUniformPages() {
        ReferenceExecutor reference(*rhi_);
        Executor packed(*rhi_);
        auto dl = list(37, 19, {});
        dl.groundBlack = false; // identical draw plan: isolate packing, not pass fusion
        auto sprite = solid(2, 2, {200, 170, 230, 255});
        const auto identity = identityMatrix(); // matrix draws stay on the ordinary packed-uniform path
        for (int i = 0; i < 1024; ++i) {
            auto l = layer(sprite, i % 37, (i * 7) % 19);
            l.hasTint = true; l.tintR = uint8_t(i); l.tintG = uint8_t(i * 3); l.cmat = identity;
            dl.planes[0].layers.push_back(l);
        }
        const uint64_t stride = uint64_t(rhi_->ubufAligned(176));
        const uint64_t pageSize = uint64_t(std::min(65536, rhi_->resourceLimit(QRhi::MaxUniformBufferRange)));
        const uint64_t capacity = (pageSize - 704) / stride + 1;
        for (int frame = 0; frame < 3; ++frame) {
            for (size_t i = 0; i < dl.planes[0].layers.size(); ++i)
                dl.planes[0].layers[i].tintR = uint8_t(i + frame * 31);
            compareFusion(dl, packed, reference, 0, 0);
            const auto stats = packed.stats();
            const auto count = stats.spriteDraws + stats.regionDraws + stats.fullSurfaceDraws;
            const auto pages = (count + capacity - 1) / capacity;
            const auto last = count - (pages - 1) * capacity;
            const auto expected = (pages - 1) * ((capacity - 1) * stride + 704) + (last - 1) * stride + 704;
            QCOMPARE(stats.uploadedUniformBytes, expected); // includes the final full-block binding on EACH page
            QCOMPARE(reference.stats().uploadedUniformBytes, count * uint64_t(rhi_->ubufAligned(704)));
            QVERIFY(stats.uploadedUniformBytes < reference.stats().uploadedUniformBytes);
            if (frame == 0) {
                QCOMPARE(stats.createdUniformPages, pages);
                QVERIFY(stats.createdUniformPages < reference.stats().createdUniformPages);
            } else {
                QCOMPARE(stats.createdUniformPages, uint64_t(0));
                QCOMPARE(stats.createdBindings, uint64_t(0));
            }
        }
        dl = list(1, 1, {}); dl.groundBlack = false;
        compareFusion(dl, packed, reference, 0, 0); // clear-only frame must not upload or address an empty page
        QCOMPARE(packed.stats().uploadedUniformBytes, uint64_t(0));
    }

    void packedKernelBoundaries() {
        ReferenceExecutor reference(*rhi_);
        Executor packed(*rhi_);
        const int pageSize = std::min(65536, rhi_->resourceLimit(QRhi::MaxUniformBufferRange));
        const int capacity = (pageSize - 704) / rhi_->ubufAligned(176) + 1;
        auto sprite = solid(2, 2, {170, 210, 73, 255});
        auto matrix = std::make_shared<ColorMatrix>(ColorMatrix{
            0, 1, 0, 0,  0, 0, 1, 0,  1, 0, 0, 0,  0, 0, 0, 1,  0.1, 0, 0, 0});
        for (int prefix : {0, 1, std::max(0, capacity - 2), capacity - 1, capacity, capacity + 1, 2 * capacity - 1}) {
            auto dl = list(17, 13, {}); dl.groundBlack = false;
            for (int i = 0; i < prefix; ++i) {
                auto l = layer(sprite, i % 16, (i * 7) % 12);
                l.hasTint = true; l.tintR = uint8_t(i); l.tintB = uint8_t(i * 3);
                l.cmat = matrix; // no instancing: the prefix must really reach uniform-page boundaries
                dl.planes[0].layers.push_back(l);
            }
            // Keep even the zero-prefix plane active, without adding a draw before the filters.
            dl.planes[0].layers.push_back(DrawLayer{});
            auto master = std::make_shared<PlaneMasterSpec>();
            master->hasXform = true; master->xf = {1, 0.2f, -0.5f, 0, 1, 0.75f};
            FilterSpec blur; blur.kind = FilterSpec::Blur; blur.size = 1.25;
            FilterSpec shadow; shadow.kind = FilterSpec::DropShadow; shadow.offset = 3.25; shadow.size = 0.5;
            shadow.alpha = 93; shadow.r = 50; shadow.g = 170; shadow.b = 210; shadow.x = -1; shadow.y = 2;
            FilterSpec outline; outline.kind = FilterSpec::Outline; outline.size = 1.25; outline.alpha = 97;
            master->filters = {blur, shadow, outline};
            dl.planes[0].master = master;
            // Kernel-bearing and compact operations straddle page ends in both directions. Reuse those
            // buffers with different kernels to catch stale tails, overlap, and late dynamic uploads.
            for (double sigma : {0.25, 1.5, 0.75}) {
                master->filters[0].size = sigma;
                compareFusion(dl, packed, reference, 0, 2);
                QVERIFY(packed.stats().uploadedUniformBytes <= reference.stats().uploadedUniformBytes);
            }
        }
    }

    void fusedMasterColour_data() {
        QTest::addColumn<int>("mode");
        for (int mode = kBlendDefault; mode <= kBlendInset; ++mode)
            QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }

    void fusedMasterColour() {
        QFETCH(int, mode);
        ReferenceExecutor unfused(*rhi_);
        Executor fused(*rhi_);
        auto s = image(3, 2, {{240, 50, 170, 0}, {100, 160, 210, 1}, {80, 25, 120, 127},
                              {230, 190, 70, 128}, {40, 255, 130, 254}, {80, 90, 200, 255}});
        const auto matrix = std::make_shared<ColorMatrix>(ColorMatrix{
            0, 0.7, 0, 0,  0, 0, 0.5, 0,  0.8, 0, 0, 0,  0, 0, 0, 0.5,  0.1, 0, 0, 0.2});
        for (bool useMatrix : {false, true}) for (int slate : {0, 1, 2}) for (int warp : {0, 1, 2, 3})
            for (bool filtered : {false, true}) for (bool black : {false, true}) {
                auto master = std::make_shared<PlaneMasterSpec>();
                master->hasColor = true;
                master->r = 91; master->g = 170; master->b = 230; master->alpha = 151;
                master->blend = uint8_t(mode);
                if (useMatrix) { master->cmat = matrix; master->alpha = 0; } // matrix REPLACES scalar
                master->hasXform = warp != 0; // warp=1: explicit identity is also fusible
                if (warp == 2) master->xf = {1, 0.2f, -0.75f, -0.3f, 1, 0.5f};
                if (warp == 3) master->xf = {0, 0, 0, 0, 0, 0}; // singular: still cannot move colour past warp
                if (filtered) {
                    FilterSpec blur; blur.kind = FilterSpec::Blur; blur.size = 0.75;
                    master->filters = {blur};
                }
                auto dl = list(9, 7, {layer(solid(9, 7, {190, 70, 130, 93}))});
                dl.planes.push_back(PlanePass{1, {layer(s, -1, 0), layer(s, 3, 2)}, master});
                if (slate) {
                    master->renderTarget = slate == 1 ? "*fused" : "visible";
                    DrawLayer relay; relay.relay = master->renderTarget; relay.relayBottomY = dl.outH;
                    dl.planes.push_back(PlanePass{2, {relay}, {}});
                }
                dl.groundBlack = black;
                compareFusion(dl, fused, unfused, uint64_t(warp < 2) + uint64_t(black), 2);
            }

        // A matrix can reveal RGB under zero alpha after filters, or make the ENTIRE empty plane opaque.
        auto constant = std::make_shared<ColorMatrix>(ColorMatrix{
            1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 0,  0.2, 0.1, 0.3, 1});
        auto master = std::make_shared<PlaneMasterSpec>();
        master->cmat = constant; master->blend = uint8_t(mode);
        FilterSpec blur; blur.kind = FilterSpec::Blur; blur.size = 1;
        FilterSpec outline; outline.kind = FilterSpec::Outline; outline.size = 1.25; outline.alpha = 93;
        FilterSpec shadow; shadow.kind = FilterSpec::DropShadow; shadow.offset = 1; shadow.alpha = 127;
        master->filters = {blur, outline, shadow};
        auto dl = list(9, 7, {layer(solid(1, 1, {200, 70, 30, 5}), 3, 2)});
        dl.planes[0].master = master;
        compareFusion(dl, fused, unfused, 2, 2);
        dl.planes[0].layers = {DrawLayer{}}; // missing sprite, but not an empty/unresolved-only plane
        compareFusion(dl, fused, unfused, 2, 2);
        (*constant)[19] = 0;
        compareFusion(dl, fused, unfused, 2, 2);
        dl.groundBlack = false;
        compareFusion(dl, fused, unfused, 1, 2);
        (*constant)[19] = 1;
        DrawLayer missing; missing.relay = "*unresolved";
        dl.planes[0].layers = {missing};
        compareFusion(dl, fused, unfused, 0, 0); // unresolved-only planes must still skip opacity-creating matrices

        master->cmat.reset(); master->hasColor = true; master->filters.clear();
        master->r = 73; master->g = 191; master->b = 254;
        dl.planes[0].layers = {layer(s)};
        for (int alpha : {-1, 0, 1, 127, 128, 254, 255, 300}) for (bool black : {false, true}) {
            master->alpha = alpha; dl.groundBlack = black;
            compareFusion(dl, fused, unfused, 1 + uint64_t(black), 1);
        }
    }

    void fusedBlackIntegerRounding() {
        ReferenceExecutor unfused(*rhi_);
        Executor fused(*rhi_);
        auto s = std::make_shared<Sprite>(); s->w = 256; s->h = 256;
        for (int a = 0; a < 256; ++a) for (int c = 0; c < 256; ++c)
            s->px.insert(s->px.end(), {uint8_t(c), uint8_t(255 - c), uint8_t(c), uint8_t(a)});
        auto dl = list(256, 256, {layer(s)});
        compareFusion(dl, fused, unfused, 1, 0); // every byte colour/alpha pair, exact CPU and GPU
        dl = list(258, 257, {layer(solid(258, 257, {123, 97, 21, 127}))});
        HudPass h; h.tileW = 256; h.tileH = 256; h.layers = {layer(s)};
        h.dests = {{-1, -1}, {1, 0}, {10000, 0}};
        dl.hud = {h};
        compareFusion(dl, fused, unfused, 1, 0); // integer blit THEN integer flatten; includes outside-dest pixels
        h.dests.clear(); dl.hud.push_back(h);
        compareFusion(dl, fused, unfused, 1, 0); // a dead trailing canvas must not hide the last scene write
        dl.groundBlack = false;
        compareFusion(dl, fused, unfused, 0, 0);
        dl.groundBlack = true; dl.planes.clear(); dl.hud = {h};
        compareFusion(dl, fused, unfused, 0, 0); // no scene draw: keep standalone black flatten
        dl.hud.clear();
        compareFusion(dl, fused, unfused, 0, 0);
        dl.groundBlack = false;
        compareFusion(dl, fused, unfused, 0, 0);
    }

    void fusedDuplicateSlates() {
        ReferenceExecutor unfused(*rhi_);
        Executor fused(*rhi_);
        auto first = std::make_shared<PlaneMasterSpec>();
        first->renderTarget = "visible"; first->hasColor = true; first->alpha = 127;
        auto second = std::make_shared<PlaneMasterSpec>(*first); second->r = 71; second->alpha = 190;
        DrawLayer relay; relay.relay = "visible"; relay.relayBottomY = 5;
        auto dl = list(7, 5, {relay}); // ordinary planes see the final table
        dl.planes.push_back(PlanePass{1, {layer(solid(2, 2, {230, 70, 140, 200}))}, first});
        dl.planes.push_back(PlanePass{2, {relay, layer(solid(2, 2, {30, 210, 110, 93}), 1, 1)}, second});
        compareFusion(dl, fused, unfused, 3, 1); // earlier target dependency, then final visible copy; no double colour
        dl.groundBlack = false;
        compareFusion(dl, fused, unfused, 2, 1);
    }

    void cachedPlaneInputs() {
        Executor cached(*rhi_), uncached(*rhi_);
        auto sprite = std::make_shared<Sprite>(*solid(4, 2, {190, 70, 130, 127}));
        auto matrix = std::make_shared<ColorMatrix>(*identityMatrix());
        auto master = std::make_shared<PlaneMasterSpec>();
        master->hasColor = true; master->r = 199; master->alpha = 187; master->cmat = matrix;
        FilterSpec blur; blur.size = 0.75;
        FilterSpec shadow; shadow.kind = FilterSpec::DropShadow; shadow.offset = 1; shadow.alpha = 117;
        master->filters = {blur, shadow};
        auto dl = list(17, 13, {layer(solid(17, 13, {30, 70, 90, 113}))});
        auto l = layer(sprite, 3, 2); l.cmat = matrix; l.hasTint = true; l.tintG = 180;
        dl.planes.push_back({1, {l, layer(sprite, 6, 5)}, master});
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(0));
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(2));
        QCOMPARE(cached.stats().filteredPlaneCacheHits, uint64_t(1));
        QCOMPARE(cached.stats().renderPasses, uint64_t(3)); // scene clear + two blends; flatten remains fused
        QCOMPARE(cached.stats().uploadedSprites, uint64_t(0));
        QCOMPARE(cached.stats().createdSurfaces, uint64_t(0));
        QCOMPARE(cached.stats().planeCacheBytes, uint64_t(17 * 13 * 4 * 2));
        const auto baseline = freeze(dl);
        for (int field = 0; field < 37; ++field) {
            // Reset and warm; only the changed plane may miss, even when it shares source pixels/matrices.
            dl = *baseline;
            compareCached(dl, cached, uncached);
            master = std::make_shared<PlaneMasterSpec>(*dl.planes[1].master);
            matrix = std::make_shared<ColorMatrix>(*master->cmat);
            master->cmat = matrix; dl.planes[1].master = master;
            auto& changed = dl.planes[1].layers[0];
            sprite = std::make_shared<Sprite>(*changed.sprite); changed.sprite = sprite;
            switch (field) {
            case 0: sprite->px[0] ^= 255; break;
            case 1: sprite->w = 2; sprite->h = 4; break;
            case 2: --changed.x; break;
            case 3: ++changed.y; break;
            case 4: changed.hasTint = false; break;
            case 5: changed.tintR = 7; break;
            case 6: changed.tintG = 17; break;
            case 7: changed.tintB = 77; break;
            case 8: changed.alpha = 31; break;
            case 9: changed.blend = kBlendMultiply; break;
            case 10: { auto m = std::make_shared<ColorMatrix>(*changed.cmat); (*m)[19] = 0.3; changed.cmat = m; break; }
            case 11: master->hasColor = false; break;
            case 12: master->r = 73; break;
            case 13: master->g = 79; break;
            case 14: master->b = 83; break;
            case 15: master->alpha = 93; break;
            case 16: master->blend = kBlendAdd; break;
            case 17: (*matrix)[16] = 0.2; (*matrix)[15] = 0; break; // preserve RGB under zero alpha
            case 18: master->hasXform = true; break; // explicit identity
            case 19: master->hasXform = true; master->xf = {1, 0.1f, 0.25f, -0.1f, 1, -0.5f}; break;
            case 20: master->filters[0].size = 1; break;
            case 21: master->filters[1].offset = 1.5; break;
            case 22: master->filters[1].r = 211; break;
            case 23: master->filters[1].g = 149; break;
            case 24: master->filters[1].b = 139; break;
            case 25: master->filters[1].alpha = 101; break;
            case 26: master->filters[1].flags = 3; break;
            case 27: master->filters[1].x = -1; break;
            case 28: master->filters[1].y = 1; break;
            case 29: master->filters[0].kind = FilterSpec::Outline; break;
            case 30: std::reverse(master->filters.begin(), master->filters.end()); break;
            case 31: ++master->unappliedFilters; break;
            case 32: std::reverse(dl.planes[1].layers.begin(), dl.planes[1].layers.end()); break;
            case 33: dl.planes[1].layers.pop_back(); break;
            case 34: dl.planes[1].plane = 3; break;
            case 35: changed.relay = "missing"; break;
            case 36: master->hasXform = true; master->xf = {0, 0, 0, 0, 0, 0}; break; // singular
            }
            compareCached(dl, cached, uncached);
            QCOMPARE(cached.stats().planeCacheHits, uint64_t(1));
            compareCached(dl, cached, uncached);
            QCOMPARE(cached.stats().planeCacheHits, uint64_t(2));
        }
        dl.groundBlack = !dl.groundBlack;
        HudPass hud; hud.tileW = 3; hud.tileH = 2;
        hud.layers = {layer(solid(3, 2, {11, 91, 43, 111}))}; hud.dests = {{-1, 2}, {8, 6}};
        dl.hud = {hud};
        compareCached(dl, cached, uncached); // HUD/black changes do not invalidate plane pixels
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(2));
        dl.outW += 2; dl.outH += 1;
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(0));
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(2));
        compare(dl, 2, nullptr, &cached); // borrowed recording must expire immutable output caches
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(0));
        auto empty = dl; empty.planes.clear();
        compareCached(empty, cached, uncached);
        QCOMPARE(cached.stats().planeCacheBytes, uint64_t(0));
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(0));
    }

    void cachedRelayDependencies() {
        Executor cached(*rhi_), uncached(*rhi_);
        DrawLayer relay; relay.relay = "visible"; relay.relayBottomY = 7;
        auto first = std::make_shared<PlaneMasterSpec>(); first->renderTarget = "visible";
        first->hasColor = true; first->alpha = 131;
        auto second = std::make_shared<PlaneMasterSpec>(*first); second->r = 171;
        auto dl = list(9, 7, {relay}); // sees final duplicate, not the first target
        dl.planes.push_back({1, {layer(solid(2, 2, {230, 70, 140, 200}))}, first});
        dl.planes.push_back({2, {relay, layer(solid(2, 2, {30, 210, 110, 93}), 1, 1)}, second});
        dl.planes.push_back({3, {layer(solid(1, 1, {70, 120, 30, 99}), 7, 4)}, {}});
        compareCached(dl, cached, uncached);
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(4));
        dl.planes[1].layers[0].x = 2; // invalidates second target, then ordinary relay plane
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(1));
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(4));
        first->renderTarget = "*other"; // second target's self-relay becomes unresolved, but ordinary sees it
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(1));
        second->renderTarget = "*other"; // ordinary unresolved-only plane must skip even master effects
        auto reveal = std::make_shared<PlaneMasterSpec>();
        auto cm = std::make_shared<ColorMatrix>(*identityMatrix()); (*cm)[19] = 1; reveal->cmat = cm;
        dl.planes[0].master = reveal;
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planes, uint64_t(3));
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(2));
        first->renderTarget = "visible"; // missing -> resolved dependency must invalidate target two
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(1));
        dl.planes[0].layers[0].relayBottomY = 5;
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(3));
        std::swap(dl.planes[1], dl.planes[2]); // target evaluation order, not just a set of sources
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(1));
        compareCached(dl, cached, uncached);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(4));
    }

    void cachedPlaneBudgets() {
        auto dl = list(8, 8, {}); dl.planes.clear();
        auto sprite = solid(2, 2, {90, 70, 31, 255});
        for (int i = 0; i < 3; ++i) dl.planes.push_back({double(i), {layer(sprite, i, i)}, {}});
        constexpr uint64_t base = 8 * 8 * 16 + 2 * 2 * 4;
        for (uint64_t extra : {0, 255, 256, 512, 768}) {
            fugue::rendergpu::Limits limits; limits.maxTextureBytes = base + extra;
            Executor cached(*rhi_, limits), uncached(*rhi_, limits);
            compareCached(dl, cached, uncached, 0);
            compareCached(dl, cached, uncached, 0);
            QCOMPARE(cached.stats().planeCacheHits, extra / 256);
            QCOMPARE(cached.stats().planeCacheBytes, (extra / 256) * 256);
            QVERIFY(cached.stats().planeCacheBytes + cached.stats().atlasBytes <= extra);
        }
        fugue::rendergpu::Options options; options.reuseResources = false;
        Executor noReuse(*rhi_, {}, options), uncached(*rhi_);
        compareCached(dl, noReuse, uncached, 0);
        compareCached(dl, noReuse, uncached, 0);
        QCOMPARE(noReuse.stats().cachedPlanes, uint64_t(0));
        // Named slates already have storage in preflight; caching them requires no extra GPU payload.
        auto master = std::make_shared<PlaneMasterSpec>(); master->renderTarget = "visible";
        dl.planes[0].master = master;
        fugue::rendergpu::Limits limits; limits.maxTextureBytes = base + 256;
        Executor slates(*rhi_, limits);
        compareCached(dl, slates, uncached, 0);
        compareCached(dl, slates, uncached, 0);
        QCOMPARE(slates.stats().planeCacheHits, uint64_t(1));
        QCOMPARE(slates.stats().cachedPlanes, uint64_t(1));
    }

    void cachedPlaneCapsAndAtlasHeadroom() {
        auto tiny = list(8, 8, {});
        auto sprite = solid(2, 2, {110, 31, 79, 255});
        for (int i = 0; i < 4; ++i) tiny.planes[0].layers.push_back(layer(sprite, i, i));
        for (uint64_t extra : {256, 272}) {
            fugue::rendergpu::Limits limits; limits.maxTextureBytes = 8 * 8 * 16 + 16 + extra;
            Executor cached(*rhi_, limits), uncached(*rhi_, limits);
            compareCached(tiny, cached, uncached, 0);
            QCOMPARE(cached.stats().planeCacheBytes, uint64_t(256));
            QCOMPARE(cached.stats().atlasBytes, extra - 256);
            compareCached(tiny, cached, uncached, 0);
            QCOMPARE(cached.stats().planeCacheHits, uint64_t(1));
            QCOMPARE(cached.stats().atlasBytes, uint64_t(0)); // hit skips sprite/atlas uploads entirely
        }
        Executor cached(*rhi_), uncached(*rhi_);
        auto many = list(1024, 1024, {}); many.planes.clear();
        for (int i = 0; i < 9; ++i) many.planes.push_back({double(i), {layer(solid(1, 1, {31, 79, 110, 255}), i, i)}, {}});
        compareCached(many, cached, uncached, 0);
        compareCached(many, cached, uncached, 0);
        QCOMPARE(cached.stats().planeCacheHits, uint64_t(8));
        QCOMPARE(cached.stats().planeCacheBytes, uint64_t(32 * 1024 * 1024));
        auto large = std::make_shared<Sprite>(); large->w = large->h = 2048;
        large->px.assign(size_t(2048 * 2048 * 4), 255);
        auto large2 = std::make_shared<Sprite>(*large);
        auto wideInputs = list(8, 8, {layer(large), layer(large2), layer(sprite)});
        compareCached(wideInputs, cached, uncached, 0);
        compareCached(wideInputs, cached, uncached, 0);
        QCOMPARE(cached.stats().planeCacheInputBytes, uint64_t(0)); // >32 MiB retained inputs: no admission
        QCOMPARE(cached.stats().cachedPlanes, uint64_t(0));
        wideInputs = list(8, 8, {layer(large), layer(large), layer(large)});
        compareCached(wideInputs, cached, uncached, 0);
        compareCached(wideInputs, cached, uncached, 0);
        QCOMPARE(cached.stats().cachedPlanes, uint64_t(0)); // repeated sources must not bypass the comparison-work cap
        auto reserved = std::make_shared<Sprite>(*solid(1, 1, {90, 70, 31, 255}));
        reserved->px.reserve(33 * 1024 * 1024); // tiny visible payload, large retained allocation
        auto immutable = std::make_shared<const DrawList>(list(8, 8, {layer(reserved)}));
        for (int i = 0; i < 2; ++i) compare(*immutable, 0, nullptr, &cached, immutable);
        QCOMPARE(cached.stats().cachedPlanes, uint64_t(0));
        fugue::rendergpu::Options options; options.cacheSprites = false;
        Executor noSpriteCache(*rhi_, {}, options);
        compareCached(tiny, noSpriteCache, uncached, 0);
        compareCached(tiny, noSpriteCache, uncached, 0);
        QCOMPARE(noSpriteCache.stats().planeCacheHits, uint64_t(1));
        QCOMPARE(noSpriteCache.stats().uploadedSprites, uint64_t(0));
    }

    void pipelineBenchmark_data() {
        QTest::addColumn<int>("workload");
        QTest::newRow("plane-fusion") << 0;
        QTest::newRow("uniform-packing") << 1;
        QTest::newRow("sprite-instancing") << 2;
    }
    void pipelineBenchmark() {
        if (!qEnvironmentVariableIntValue("FUGUE_GPU_BENCHMARK")) QSKIP("Set FUGUE_GPU_BENCHMARK=1 for the A/B benchmark");
        QFETCH(int, workload);
        const bool planes = workload == 0;
        DrawList dl; dl.outW = dl.outH = 512;
        if (planes) {
            auto sprite = solid(512, 512, {190, 170, 230, 255});
            for (int i = 0; i < 12; ++i) {
                auto master = std::make_shared<PlaneMasterSpec>();
                master->hasColor = true; master->r = uint8_t(70 + i * 13);
                master->g = uint8_t(210 - i * 11); master->alpha = 130 + i * 7;
                dl.planes.push_back(PlanePass{double(i), {layer(sprite)}, master});
            }
        } else {
            dl.groundBlack = false; // same draw plan on both executors: isolates uniform packing
            dl.planes.push_back(PlanePass{});
            auto sprite = solid(16, 16, {190, 170, 230, 255});
            const auto identity = identityMatrix(); // isolate packing, not instancing
            for (int i = 0; i < 1024; ++i) {
                auto l = layer(sprite, (i % 32) * 16, (i / 32) * 16);
                l.hasTint = true; l.tintR = uint8_t(i); l.tintG = uint8_t(i * 3);
                if (workload == 1) l.cmat = identity;
                else l.sprite = solid(16, 16, {uint8_t(i), uint8_t(i / 4), 77, 255});
                dl.planes[0].layers.push_back(l);
            }
        }
        std::vector<uint8_t> original;
        auto benchmark = [&](auto& gpu, bool optimized) {
            compareUsing(gpu, dl, planes ? 2 : 0);
            if (!optimized) original = last_.px;
            else QCOMPARE(last_.px, original);
            double recordMs = 0, submitMs = 0;
            constexpr int warmups = 8, frames = 60;
            for (int i = 0; i < warmups + frames; ++i) {
                QElapsedTimer timer; timer.start();
                QRhiCommandBuffer* cb = nullptr;
                QCOMPARE(rhi_->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
                std::string error;
                auto* texture = gpu.record(dl, *cb, &error);
                if (!texture) { rhi_->endOffscreenFrame(); QFAIL(error.c_str()); }
                const double recorded = timer.nsecsElapsed() / 1e6;
                QRhiReadbackResult result;
                bool complete = false;
                result.completed = [&] { complete = true; };
                auto* updates = rhi_->nextResourceUpdateBatch();
                updates->readBackTexture(QRhiReadbackDescription(texture), &result);
                cb->resourceUpdate(updates);
                QCOMPARE(rhi_->endOffscreenFrame(), QRhi::FrameOpSuccess);
                const double finished = timer.nsecsElapsed() / 1e6;
                QVERIFY(complete);
                QCOMPARE(result.data.size(), qsizetype(original.size()));
                QVERIFY(std::equal(original.begin(), original.end(),
                                   reinterpret_cast<const uint8_t*>(result.data.constData())));
                if (i >= warmups) { recordMs += recorded; submitMs += finished - recorded; }
            }
            qInfo().nospace() << (optimized ? "optimized" : "test reference") << ": record=" << recordMs / frames
                              << "ms submit/wait/readback=" << submitMs / frames << "ms total="
                              << (recordMs + submitMs) / frames << "ms passes=" << gpu.stats().renderPasses
                              << " full-draws=" << gpu.stats().fullSurfaceDraws
                              << " uniform-bytes=" << gpu.stats().uploadedUniformBytes
                              << " instanced-draws=" << gpu.stats().instancedDraws
                              << " instanced-sprites=" << gpu.stats().instancedSprites
                              << " instance-bytes=" << gpu.stats().uploadedInstanceBytes
                              << " atlas-upload-bytes=" << gpu.stats().uploadedAtlasBytes;
        };
        ReferenceExecutor reference(*rhi_);
        Executor optimized(*rhi_);
        benchmark(reference, false);
        benchmark(optimized, true);
        benchmark(optimized, true); // reverse order as well; scheduling/clock ramp-up can bias short runs
        benchmark(reference, false);
    }

    void cachedPlaneMissesKeepBindingsStable() {
        Executor cached(*rhi_), uncached(*rhi_);
        auto dl = list(32, 24, {layer(solid(32, 24, {90, 31, 70, 127}))});
        auto master = std::make_shared<PlaneMasterSpec>();
        FilterSpec blur; blur.size = 1; master->filters = {blur};
        dl.planes.push_back({1, {layer(solid(3, 2, {170, 110, 200, 127}), 2, 5)}, master});
        for (int frame = 0; frame < 12; ++frame) {
            ++dl.planes.back().layers[0].x;
            compareCached(dl, cached, uncached);
            QCOMPARE(cached.stats().planeCacheHits, uint64_t(frame > 0));
            if (frame > 1) {
                QCOMPARE(cached.stats().createdSurfaces, uint64_t(0));
                QCOMPARE(cached.stats().createdBindings, uint64_t(0));
                QCOMPARE(cached.stats().uploadedSprites, uint64_t(0));
            }
        }
    }

    void planeCacheBenchmark() {
        if (!qEnvironmentVariableIntValue("FUGUE_GPU_BENCHMARK")) QSKIP("Set FUGUE_GPU_BENCHMARK=1 for the A/B benchmark");
        DrawList dl; dl.outW = dl.outH = 256;
        auto master = std::make_shared<PlaneMasterSpec>(); master->hasColor = true; master->alpha = 190;
        FilterSpec blur; blur.size = 1;
        FilterSpec shadow; shadow.kind = FilterSpec::DropShadow; shadow.offset = 1; shadow.size = 1; shadow.alpha = 117;
        master->filters = {blur, shadow};
        for (int i = 0; i < 8; ++i)
            dl.planes.push_back({double(i), {layer(solid(8, 8, {uint8_t(30 * i), 190, 71, 127}), 20 * i, 15 * i)}, master});
        Executor cached(*rhi_), uncached(*rhi_);
        std::vector<std::shared_ptr<const DrawList>> frames;
        std::vector<std::vector<uint8_t>> expected;
        for (int i = 0; i < 8; ++i) {
            dl.planes.back().layers[0].x = 140 + i;
            frames.push_back(freeze(dl));
            compareCached(dl, cached, uncached);
            expected.push_back(last_.px);
        }
        auto benchmark = [&](Executor& gpu, bool retain) {
            constexpr int warmups = 8, count = 60;
            double recordMs = 0, finishMs = 0;
            for (int i = 0; i < warmups + count; ++i) {
                const size_t phase = size_t(i) % frames.size();
                QRhiCommandBuffer* cb = nullptr;
                QCOMPARE(rhi_->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
                QElapsedTimer timer; timer.start();
                std::string error;
                auto* texture = retain ? gpu.record(frames[phase], *cb, &error) : gpu.record(*frames[phase], *cb, &error);
                if (!texture) { rhi_->endOffscreenFrame(); QFAIL(error.c_str()); }
                const double recorded = timer.nsecsElapsed() / 1e6;
                QRhiReadbackResult result; bool complete = false;
                result.completed = [&] { complete = true; };
                auto* updates = rhi_->nextResourceUpdateBatch();
                updates->readBackTexture(QRhiReadbackDescription(texture), &result); cb->resourceUpdate(updates);
                QCOMPARE(rhi_->endOffscreenFrame(), QRhi::FrameOpSuccess);
                const double finished = timer.nsecsElapsed() / 1e6;
                QVERIFY(complete);
                QCOMPARE(result.data.size(), qsizetype(expected[phase].size()));
                QVERIFY(std::equal(expected[phase].begin(), expected[phase].end(),
                                   reinterpret_cast<const uint8_t*>(result.data.constData())));
                if (i >= warmups) { recordMs += recorded; finishMs += finished - recorded; }
            }
            const auto s = gpu.stats();
            qInfo().nospace() << (retain ? "cached" : "uncached") << ": record=" << recordMs / count
                              << "ms submit/wait/readback=" << finishMs / count << "ms passes=" << s.renderPasses
                              << " hits=" << s.planeCacheHits << '/' << s.planes << " avoided=" << s.avoidedPlanePasses
                              << " cached-bytes=" << s.planeCacheBytes;
        };
        benchmark(uncached, false); benchmark(cached, true);
        benchmark(cached, true); benchmark(uncached, false);
    }

    void spriteBatchBenchmark() {
        if (!qEnvironmentVariableIntValue("FUGUE_GPU_BENCHMARK")) QSKIP("Set FUGUE_GPU_BENCHMARK=1 for the A/B benchmark");
        auto dl = list(512, 512, {});
        for (int i = 0; i < 1024; ++i)
            dl.planes[0].layers.push_back(layer(solid(16, 16, {uint8_t(i), uint8_t(i / 4), 77, 255}),
                                               (i % 32) * 16, (i / 32) * 16));
        const auto cpu = execute(dl);
        for (bool optimized : {false, true}) {
            Executor gpu(*rhi_, {}, {optimized, optimized, optimized});
            double recordMs = 0, submitMs = 0;
            constexpr int warmups = 2, frames = 10;
            for (int i = 0; i < warmups + frames; ++i) {
                // Simulate the assembler rebuilding immutable sprites each frame.
                for (auto& l : dl.planes[0].layers) l.sprite = std::make_shared<Sprite>(*l.sprite);
                QElapsedTimer timer;
                timer.start();
                QRhiCommandBuffer* cb = nullptr;
                QCOMPARE(rhi_->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
                std::string error;
                auto* texture = gpu.record(dl, *cb, &error);
                if (!texture) { rhi_->endOffscreenFrame(); QFAIL(error.c_str()); }
                const double recorded = timer.nsecsElapsed() / 1e6;
                QRhiReadbackResult result;
                bool complete = false;
                result.completed = [&] { complete = true; };
                auto* updates = rhi_->nextResourceUpdateBatch();
                updates->readBackTexture(QRhiReadbackDescription(texture), &result);
                cb->resourceUpdate(updates);
                QCOMPARE(rhi_->endOffscreenFrame(), QRhi::FrameOpSuccess);
                const double finished = timer.nsecsElapsed() / 1e6;
                QVERIFY(complete);
                QCOMPARE(result.data.size(), qsizetype(cpu.px.size()));
                QVERIFY(std::equal(cpu.px.begin(), cpu.px.end(),
                                   reinterpret_cast<const uint8_t*>(result.data.constData())));
                if (i >= warmups) { recordMs += recorded; submitMs += finished - recorded; }
            }
            const auto s = gpu.stats();
            qInfo().nospace() << (optimized ? "optimized" : "compatibility") << ": record=" << recordMs / frames
                              << "ms submit/wait/readback=" << submitMs / frames << "ms total="
                              << (recordMs + submitMs) / frames << "ms passes=" << s.renderPasses
                              << " sprite-layers=" << s.spriteDraws << " instanced-draws=" << s.instancedDraws
                              << " instanced-sprites=" << s.instancedSprites << " uploads=" << s.uploadedSprites
                              << " reused=" << s.reusedSprites;
        }
    }

    void extremeOffscreenCoordinates() {
        auto s = solid(2, 3, {255, 0, 0, 255});
        auto dl = list(7, 5, {layer(s, std::numeric_limits<int>::min(), 0),
                              layer(s, 0, std::numeric_limits<int>::max())});
        // The CPU clip arithmetic is not defined for int32 extremes; compare against the equivalent empty scene.
        DrawList empty;
        empty.outW = dl.outW; empty.outH = dl.outH;
        compare(dl, 0, &empty);
    }

    void alphaTintAndMatrices() {
        auto s = image(3, 2, {{33, 100, 210, 0}, {80, 99, 34, 1}, {2, 134, 255, 127},
                              {50, 91, 200, 128}, {255, 200, 117, 254}, {40, 220, 98, 255}});
        DrawLayer l = layer(s);
        l.hasTint = true; l.tintR = 90; l.tintG = 221; l.tintB = 51;
        for (int alpha : {-1, 0, 1, 127, 128, 254, 255, 300}) {
            l.alpha = alpha;
            DrawList dl = list(3, 2, {layer(solid(3, 2, {40, 100, 230, 75})), l});
            dl.groundBlack = false;
            compare(dl, 1);
            dl.groundBlack = true;
            compare(dl, 1);
        }
        auto matrix = std::make_shared<ColorMatrix>(ColorMatrix{
            0, 1, 0, 0,  0, 0, 1, 0,  1, 0, 0, 0,  0, 0, 0, 0.5,
            0.1, -0.1, 0.2, 0.25});
        l.cmat = matrix;
        l.alpha = 0; // matrix constants must still produce visible pixels
        DrawList dl = list(5, 4, {l});
        dl.groundBlack = false;
        compare(dl, 1);
        matrix->fill(0);
        (*matrix)[16] = 1; (*matrix)[19] = 1;
        compare(dl); // source becomes opaque red, including the fully transparent texel
    }

    void blendModes_data() {
        QTest::addColumn<int>("mode");
        for (int mode = kBlendDefault; mode <= kBlendInset; mode++)
            QTest::newRow(qPrintable(QString::number(mode))) << mode;
    }
    void blendModes() {
        QFETCH(int, mode);
        auto s = image(3, 2, {{140, 80, 210, 0}, {255, 99, 34, 1}, {20, 134, 255, 127},
                              {150, 91, 200, 128}, {255, 200, 117, 254}, {40, 220, 98, 255}});
        DrawLayer l = layer(s); l.blend = uint8_t(mode);
        for (int alpha : {0, 1, 127, 128, 254, 255}) {
            DrawList dl = list(3, 2, {layer(solid(3, 2, {77, 170, 235, uint8_t(alpha)})), l});
            dl.groundBlack = false;
            compare(dl, 1);
            dl.groundBlack = true;
            compare(dl, 1);
        }
    }

    void planesAndRepeatedFrames() {
        DrawList dl = list(5, 3, {layer(solid(5, 3, {1, 1, 1, 128}))});
        dl.planes.push_back(PlanePass{10, {layer(solid(3, 3, {255, 20, 80, 77}), 1, 0),
                                          layer(solid(3, 3, {20, 255, 40, 180}), 2, 0)}, {}});
        dl.planes.push_back(PlanePass{11, {DrawLayer{}}, {}});
        dl.planes.push_back(PlanePass{12, {}, {}});
        dl.groundBlack = false;
        compare(dl, 1);
        auto previous = last_.px;
        compare(dl, 1);
        QCOMPARE(last_.px, previous);
        dl.groundBlack = true;
        compare(dl, 1);
        dl.planes.erase(dl.planes.begin() + 1);
        compare(dl); // pooled surfaces must not leak the previous plane's pixels
    }

    void planeMasters() {
        auto base = layer(solid(3, 2, {150, 200, 110, 180}));
        auto upper = layer(solid(2, 2, {200, 70, 180, 127}), 1, 0);
        for (int mode = kBlendDefault; mode <= kBlendInset; mode++) {
            auto master = std::make_shared<PlaneMasterSpec>();
            master->hasColor = true;
            master->r = 87; master->g = 210; master->b = 120; master->alpha = 151;
            master->blend = uint8_t(mode);
            auto dl = list(3, 2, {base});
            dl.planes.push_back(PlanePass{1, {upper}, master});
            dl.groundBlack = false;
            compare(dl, 1);
            dl.groundBlack = true;
            compare(dl, 1);
            // A matrix replaces scalar colour+alpha; applying the alpha=0 sentinel would erase lighting.
            master->alpha = 0;
            master->cmat = std::make_shared<ColorMatrix>(ColorMatrix{
                1, 0, 0, 0,  0, 0.5, 0, 0,  0, 0, 1, 0,  0, 0, 0, 0.5,  0, 0, 0, 0.1});
            compare(dl, 1);
        }
    }

    void renderTargetsAndRelays() {
        auto target = std::make_shared<PlaneMasterSpec>();
        target->renderTarget = "*later";
        target->hasColor = true; target->alpha = 137;
        DrawLayer relay;
        relay.relay = "*later"; relay.x = 1; relay.relayBottomY = 4;
        auto dl = list(5, 4, {relay});
        dl.planes.push_back(PlanePass{10, {layer(solid(3, 2, {25, 200, 110, 255}), 1, 1)}, target});
        compare(dl, 1); // the lower plane must see the later target, but the * target must not draw itself
        auto second = std::make_shared<PlaneMasterSpec>();
        second->renderTarget = "visible";
        second->cmat = std::make_shared<ColorMatrix>(ColorMatrix{
            0, 1, 0, 0,  0, 0, 1, 0,  1, 0, 0, 0,  0, 0, 0, 1,  0, 0, 0, 0});
        dl.planes.push_back(PlanePass{20, {relay}, second}); // a slate can reference an earlier slate
        compare(dl, 1);
        DrawLayer forward; forward.relay = "visible"; forward.relayBottomY = 3;
        dl.planes[1].layers.push_back(forward); // unresolved in pass 1; do NOT resolve it later by recomposing
        compare(dl, 1);
        // Duplicate names use the final table for visible slate planes, without overwriting earlier dependencies.
        auto duplicate = std::make_shared<PlaneMasterSpec>(*second);
        dl.planes.push_back(PlanePass{30, {layer(solid(2, 1, {255, 0, 0, 255}))}, duplicate});
        compare(dl, 1);
        dl.groundBlack = false;
        compare(dl, 1);
        relay.relay = "*missing";
        auto missing = list(5, 4, {relay});
        auto colour = std::make_shared<PlaneMasterSpec>();
        auto matrix = std::make_shared<ColorMatrix>(); matrix->fill(0); (*matrix)[19] = 1; (*matrix)[16] = 1;
        colour->cmat = matrix;
        missing.planes[0].master = colour;
        compare(missing); // a plane with only unresolved relays skips even an opacity-creating master matrix
    }

    void blurFilters_data() {
        QTest::addColumn<double>("size");
        for (double size : {-1.0, 0.0, 0.25, 0.75, 1.5, 4.2, 10.0, 64.0 / 3})
            QTest::newRow(qPrintable(QString::number(size))) << size;
    }
    void blurFilters() {
        QFETCH(double, size);
        auto master = std::make_shared<PlaneMasterSpec>();
        FilterSpec f; f.kind = FilterSpec::Blur; f.size = size;
        master->filters = {f};
        auto s = image(3, 2, {{240, 10, 20, 255}, {10, 220, 30, 128}, {80, 30, 255, 1},
                              {30, 70, 180, 0}, {20, 180, 240, 254}, {170, 100, 30, 77}});
        auto dl = list(9, 7, {layer(s), layer(s, 5, 4)});
        dl.planes[0].master = master;
        dl.groundBlack = false;
        compare(dl, 1);
        dl.groundBlack = true;
        compare(dl, 1);
        // Edge clamping, including one-pixel axes, must preserve a spatially constant image.
        dl = list(1, 5, {layer(solid(1, 5, {91, 173, 227, 127}))});
        dl.planes[0].master = master;
        compare(dl);
    }

    void outlineFilters_data() {
        QTest::addColumn<double>("size");
        QTest::addColumn<int>("flags");
        QTest::addColumn<int>("alpha");
        for (double size : {0.0, 0.5, 1.0, 1.25, 2.5, 5.0, 12.0})
            for (int flags : {0, 1, 2, 3})
                for (int alpha : {0, 93, 255})
                    QTest::newRow(qPrintable(QString("%1-flags%2-a%3").arg(size).arg(flags).arg(alpha))) << size << flags << alpha;
    }
    void outlineFilters() {
        QFETCH(double, size); QFETCH(int, flags); QFETCH(int, alpha);
        auto master = std::make_shared<PlaneMasterSpec>();
        FilterSpec f; f.kind = FilterSpec::Outline; f.size = size; f.flags = uint16_t(flags);
        f.r = 220; f.g = 70; f.b = 31; f.alpha = uint8_t(alpha);
        master->filters = {f};
        auto dl = list(13, 9, {layer(solid(2, 3, {20, 170, 230, 255}), 4, 3),
                               layer(solid(2, 1, {50, 210, 30, 127}), -1, 0)});
        dl.planes[0].master = master;
        dl.groundBlack = false;
        compare(dl, 1);
        dl.groundBlack = true;
        compare(dl, 1);
    }

    void shadowFilters_data() {
        QTest::addColumn<double>("size");
        QTest::addColumn<double>("offset");
        QTest::addColumn<int>("alpha");
        for (double size : {0.0, 0.8, 2.0})
            for (double offset : {-1.0, 0.0, 0.5, 1.5, 3.25, double(float(std::sqrt(13.0)))})
                for (int alpha : {0, 91, 255})
                    QTest::newRow(qPrintable(QString("%1-grow%2-a%3").arg(size).arg(offset).arg(alpha))) << size << offset << alpha;
    }
    void shadowFilters() {
        QFETCH(double, size); QFETCH(double, offset); QFETCH(int, alpha);
        auto master = std::make_shared<PlaneMasterSpec>();
        FilterSpec f; f.kind = FilterSpec::DropShadow; f.size = size; f.offset = offset;
        f.r = 80; f.g = 30; f.b = 210; f.alpha = uint8_t(alpha); f.x = -2; f.y = 3;
        master->filters = {f};
        auto dl = list(11, 8, {layer(solid(3, 2, {230, 180, 20, 200}), 4, 1),
                               layer(solid(1, 2, {40, 200, 120, 1}), 0, 0)});
        dl.planes[0].master = master;
        dl.groundBlack = false;
        compare(dl, 1);
        dl.groundBlack = true;
        compare(dl, 1);
        master->filters[0].x = 10000; // entirely clipped shadow still draws the original
        compare(dl);
    }

    void planeTransforms() {
        const std::vector<std::array<float, 6>> matrices{
            {1, 0, 0, 0, 1, 0}, {1, 0, 2, 0, 1, -1}, {1, 0, -1.25f, 0, 1, 0.75f},
            {0, -1, 0, 1, 0, 0}, {-1, 0, 0, 0, -1, 0}, {-1, 0, 0, 0, 1, 0},
            {1.5f, 0, 0, 0, 0.5f, 0}, {1, 0.4f, 0, -0.2f, 1, 0},
            {0.8660254f, -0.5f, 0.5f, 0.5f, 0.8660254f, -0.25f},
            {0, 0, 0, 0, 0, 0}, {1, 2, 3, 2, 4, 5}, {1, 0, 10000, 0, 1, -10000}
        };
        auto s = image(3, 2, {{220, 20, 70, 255}, {10, 170, 230, 128}, {90, 10, 200, 0},
                              {60, 210, 30, 1}, {40, 120, 190, 77}, {240, 190, 10, 255}});
        for (int width : {8, 9}) for (const auto& matrix : matrices) {
            auto master = std::make_shared<PlaneMasterSpec>(); master->hasXform = true; master->xf = matrix;
            auto dl = list(width, width - 2, {layer(s), layer(s, 4, 3)});
            dl.planes[0].master = master;
            dl.groundBlack = false;
            compare(dl, 1);
            dl.groundBlack = true;
            compare(dl, 1);
        }
    }

    void filterColourTransformOrderAndSlates() {
        auto master = std::make_shared<PlaneMasterSpec>();
        FilterSpec outline; outline.kind = FilterSpec::Outline; outline.size = 2.5;
        outline.r = 230; outline.g = 30; outline.b = 80; outline.alpha = 151;
        FilterSpec blur; blur.kind = FilterSpec::Blur; blur.size = 0.8;
        FilterSpec shadow; shadow.kind = FilterSpec::DropShadow; shadow.size = 0.5; shadow.offset = 1;
        shadow.x = -1; shadow.y = 2; shadow.r = 20; shadow.g = 230; shadow.b = 50; shadow.alpha = 170;
        master->filters = {outline, blur, shadow};
        master->cmat = std::make_shared<ColorMatrix>(ColorMatrix{
            0, 0.7, 0, 0,  0, 0, 0.5, 0,  0.8, 0, 0, 0,  0, 0, 0, 0.5,  0.1, 0, 0, 0});
        master->hasColor = true; master->alpha = 0; // matrix must replace, not multiply by, this sentinel
        master->hasXform = true; master->xf = {1.25f, 0.2f, 1, 0, 0.75f, -0.5f};
        master->blend = kBlendMultiply;
        auto dl = list(11, 9, {layer(solid(11, 9, {190, 120, 210, 255}))});
        dl.planes.push_back(PlanePass{1, {layer(solid(3, 2, {40, 170, 220, 200}), 3, 2)}, master});
        compare(dl, 2);
        auto correct = execute(dl).px;
        setPipelineMutation(11); // applying colour before filters must visibly change this fixture
        auto wrong = execute(dl).px;
        setPipelineMutation(0);
        QVERIFY(correct != wrong);
        master->renderTarget = "*filtered";
        DrawLayer relay; relay.relay = "*filtered"; relay.x = -1; relay.relayBottomY = 8;
        dl.planes.insert(dl.planes.begin() + 1, PlanePass{0.5, {relay}, {}});
        compare(dl, 2);
        // Reuse all scratch surfaces for a second filter list and a different viewport size.
        master->filters = {shadow, outline, blur};
        dl.outW = 8; dl.outH = 12;
        compare(dl, 2);
        master->filters.clear();
        compare(dl, 1);
    }

    void legacyHudCanvases() {
        auto dl = list(9, 7, {layer(solid(9, 7, {70, 180, 110, 127}))});
        HudPass h;
        h.tileW = 4; h.tileH = 3;
        h.layers = {layer(solid(3, 2, {220, 30, 110, 127}), -1, -1),
                    layer(solid(2, 2, {40, 170, 210, 255}), 2, 1)};
        h.layers[1].relay = "*ignored-in-hud"; // HUD composition uses the sprite, not its relay
        h.dests = {{-1, -1}, {2, 1}, {2, 1}, {7, 5}, {-10000, 0}, {0, 10000}};
        dl.hud = {h};
        dl.groundBlack = false;
        compare(dl);
        dl.groundBlack = true;
        compare(dl);
        // A larger-than-viewport canvas and different-sized subsequent tile exercise per-pass viewport/scissor state.
        h.tileW = 13; h.tileH = 11; h.dests = {{-3, -2}};
        dl.hud.push_back(h);
        HudPass small; small.tileW = 1; small.tileH = 1;
        small.layers = {layer(solid(1, 1, {123, 231, 78, 50}))}; small.dests = {{8, 0}, {0, 6}};
        dl.hud.push_back(small);
        compare(dl);
        dl.hud.push_back(HudPass{}); // a nonpositive canvas is a no-op
        compare(dl);
        for (int mode = kBlendDefault; mode <= kBlendInset; mode++) {
            dl.hud[0].layers[1].blend = uint8_t(mode);
            dl.hud[0].layers[1].hasTint = true; dl.hud[0].layers[1].tintG = 137;
            dl.hud[0].layers[1].alpha = 91;
            compare(dl, 1);
        }
    }

    void transparentEffectRgbAndHudMatrices() {
        // Blur can leave RGB under byte alpha=0. A subsequent matrix can reveal it; do not discard those pixels.
        auto matrix = std::make_shared<ColorMatrix>(ColorMatrix{
            1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 0,  0, 0, 0, 1});
        auto master = std::make_shared<PlaneMasterSpec>(); master->cmat = matrix;
        FilterSpec blur; blur.kind = FilterSpec::Blur; blur.size = 1;
        master->filters = {blur};
        auto dl = list(7, 5, {layer(solid(2, 1, {200, 70, 30, 5}), 2, 2)});
        dl.planes[0].master = master;
        compare(dl, 1);
        dl.planes.clear(); // a HUD-only scene with an opacity-creating per-layer matrix
        HudPass h; h.tileW = 3; h.tileH = 2;
        h.layers = {layer(solid(2, 2, {20, 170, 220, 0}), -1, 0)};
        h.layers[0].hasTint = true; h.layers[0].tintG = 123; h.layers[0].cmat = matrix;
        h.dests = {{1, 1}, {5, -1}};
        dl.hud = {h}; dl.groundBlack = false;
        compare(dl, 1);
    }

    void hudBlitIntegerRounding() {
        auto s = std::make_shared<Sprite>(); s->w = 256; s->h = 256;
        for (int a = 0; a < 256; a++) for (int c = 0; c < 256; c++)
            s->px.insert(s->px.end(), {uint8_t(c), uint8_t(255 - c), uint8_t(c), uint8_t(a)});
        auto dl = list(256, 256, {layer(solid(256, 256, {123, 97, 21, 127}))});
        HudPass h; h.tileW = 256; h.tileH = 256; h.layers = {layer(s)}; h.dests = {{0, 0}, {0, 0}};
        dl.hud = {h}; dl.groundBlack = false;
        compare(dl); // all colour/alpha byte pairs, including transparent-source destination truncation
        dl.groundBlack = true;
        compare(dl);
    }

    void planeBoundaryRounding() {
        auto a = layer(solid(1, 1, {35, 53, 2, 198}));
        auto b = layer(solid(1, 1, {226, 51, 195, 135}));
        auto c = layer(solid(1, 1, {137, 142, 62, 205}));
        auto grouped = list(1, 1, {a});
        grouped.planes.push_back(PlanePass{1, {b, c}, {}});
        grouped.groundBlack = false;
        auto flat = list(1, 1, {a, b, c});
        flat.groundBlack = false;
        // Alpha-over is associative only before byte quantization; removing the plane boundary changes blue.
        QCOMPARE(execute(grouped).px, (std::vector<uint8_t>{139, 126, 71, 250}));
        QCOMPARE(execute(flat).px, (std::vector<uint8_t>{139, 126, 72, 250}));
        compare(grouped);
        compare(flat);
    }

    void blackFlattenIntegerRounding() {
        auto s = std::make_shared<Sprite>();
        s->w = 256; s->h = 256;
        for (int a = 0; a < 256; a++) {
            for (int c = 0; c < 256; c++)
                s->px.insert(s->px.end(), {uint8_t(c), uint8_t(255 - c), uint8_t(c), uint8_t(a)});
        }
        compare(list(256, 256, {layer(s)}));
    }

    void uniformPagesAndRandomScenes() {
        std::mt19937 rng(0xF090E);
        DrawList dl;
        dl.outW = 11; dl.outH = 7;
        auto s = std::make_shared<Sprite>();
        s->w = 3; s->h = 4; s->px.resize(3 * 4 * 4);
        for (auto& b : s->px) b = uint8_t(rng());
        // More than one 64 KiB uniform page on all supported backends.
        for (int p = 0; p < 3; p++) {
            PlanePass plane;
            plane.plane = p;
            for (int i = 0; i < 180; i++) {
                DrawLayer l = layer(s, int(rng() % 12) - 2, int(rng() % 8) - 2);
                l.blend = uint8_t(rng() % 6);
                l.alpha = int(rng() % 256);
                l.hasTint = true;
                l.tintR = uint8_t(rng()); l.tintG = uint8_t(rng()); l.tintB = uint8_t(rng());
                plane.layers.push_back(l);
            }
            dl.planes.push_back(std::move(plane));
        }
        compare(dl, 2);
        dl.groundBlack = false;
        compare(dl, 2);
    }

    void rejectsMalformedAndOverBudget() {
        DrawList good = list(2, 3, {layer(solid(2, 3, {255, 0, 0, 255}))});
        std::string error = "stale";
        QVERIFY(executor_->supports(good, &error));
        QVERIFY(error.empty());
        auto rejects = [&](const DrawList& dl, const char* text) {
            QVERIFY(!executor_->supports(dl, &error));
            QVERIFY2(error.find(text) != std::string::npos, error.c_str());
            QRhiCommandBuffer* cb = nullptr;
            QCOMPARE(rhi_->beginOffscreenFrame(&cb), QRhi::FrameOpSuccess);
            auto* texture = executor_->record(dl, *cb, &error);
            auto status = rhi_->endOffscreenFrame();
            QVERIFY(!texture);
            QCOMPARE(status, QRhi::FrameOpSuccess);
            QVERIFY(!error.empty());
        };
        auto dl = good; dl.outW = -1; rejects(dl, "dimensions");
        dl = good; dl.outH = std::numeric_limits<int>::max(); rejects(dl, "dimensions");
        dl = good; dl.hud.push_back(HudPass{}); compare(dl);
        dl.hud[0].tileW = 10000; dl.hud[0].tileH = 1; rejects(dl, "HUD dimensions");
        auto master = std::make_shared<PlaneMasterSpec>();
        master->filters.push_back(FilterSpec{});
        dl = good; dl.planes[0].master = master;
        master->filters[0].size = std::numeric_limits<double>::quiet_NaN(); rejects(dl, "numeric");
        master->filters[0].size = 22; rejects(dl, "radius");
        master->filters[0].kind = FilterSpec::Outline; master->filters[0].size = 65; rejects(dl, "radius");
        master->filters[0].kind = FilterSpec::DropShadow; master->filters[0].size = 0;
        master->filters[0].offset = 65; rejects(dl, "radius");
        master->filters[0].offset = std::numeric_limits<double>::infinity(); rejects(dl, "numeric");
        master->filters[0].kind = FilterSpec::Kind(255); rejects(dl, "filter kind");
        master->filters.assign(65, FilterSpec{}); rejects(dl, "filter count");
        master->filters.clear(); master->hasXform = true; master->xf[0] = std::numeric_limits<float>::infinity();
        rejects(dl, "transform");
        master->xf[0] = 1e-8f; rejects(dl, "transform");
        master->xf[0] = 1; compare(dl);
        master->blend = 255; rejects(dl, "blend");
        dl = good; dl.planes[0].layers[0].blend = 255; rejects(dl, "blend");
        auto bad = std::make_shared<Sprite>(); bad->w = 2; bad->h = 3; bad->px.resize(3);
        dl = good; dl.planes[0].layers[0].sprite = bad; rejects(dl, "byte count");
        bad->w = 0; rejects(dl, "dimensions");
        auto matrix = std::make_shared<ColorMatrix>();
        matrix->fill(0); (*matrix)[0] = std::numeric_limits<double>::infinity();
        dl = good; dl.planes[0].layers[0].cmat = matrix; rejects(dl, "numeric");
        (*matrix)[0] = std::numeric_limits<double>::quiet_NaN(); rejects(dl, "numeric");
        (*matrix)[0] = 1e7; rejects(dl, "numeric");
        compare(good); // errors must not poison the next frame
    }

    void resourceBudgets() {
        auto s = solid(2, 3, {255, 0, 0, 255});
        DrawList dl = list(2, 3, {layer(s), layer(s)});
        fugue::rendergpu::Limits limits;
        limits.maxTextureBytes = 2 * 3 * 4 * 5; // four surfaces plus ONE shared sprite
        Executor limited(*rhi_, limits);
        std::string error;
        QVERIFY(limited.supports(dl, &error));
        dl.planes[0].layers[1].sprite = solid(2, 3, {0, 255, 0, 255});
        QVERIFY(!limited.supports(dl, &error));
        QVERIFY(error.find("budget") != std::string::npos);
        dl.planes[0].layers.pop_back();
        auto master = std::make_shared<PlaneMasterSpec>(); master->renderTarget = "*extra";
        dl.planes[0].master = master;
        QVERIFY(!limited.supports(dl, &error));
        QVERIFY(error.find("budget") != std::string::npos);
        dl.planes[0].master.reset();
        dl.planes[0].layers.push_back(layer(s));
        limits.maxLayers = 1;
        Executor layerLimited(*rhi_, limits);
        QVERIFY(!layerLimited.supports(dl, &error));
        QVERIFY(error.find("layer count") != std::string::npos);
        limits.maxPlanes = 0;
        Executor planeLimited(*rhi_, limits);
        QVERIFY(!planeLimited.supports(dl, &error));
        QVERIFY(error.find("plane count") != std::string::npos);

        limits = {};
        limits.maxFilterSamples = 1;
        Executor workLimited(*rhi_, limits);
        master->renderTarget.clear(); master->filters = {FilterSpec{}}; master->filters[0].size = 1;
        dl.planes[0].master = master;
        QVERIFY(!workLimited.supports(dl, &error));
        QVERIFY(error.find("sample work") != std::string::npos);
        master->filters.clear();
        limits = {}; limits.maxRenderPasses = 1;
        Executor passLimited(*rhi_, limits);
        QVERIFY(!passLimited.supports(dl, &error));
        QVERIFY(error.find("render pass") != std::string::npos);
        limits = {}; limits.maxHudDestinations = 1;
        Executor hudLimited(*rhi_, limits);
        HudPass h; h.tileW = 1; h.tileH = 1; h.dests = {{0, 0}, {1, 1}};
        dl.hud = {h};
        QVERIFY(!hudLimited.supports(dl, &error));
        QVERIFY(error.find("HUD destination") != std::string::npos);
        limits = {}; limits.maxTextureBytes = 120;
        Executor scratchLimited(*rhi_, limits);
        dl.hud.clear(); master->filters = {FilterSpec{}};
        master->filters[0].kind = FilterSpec::Outline; master->filters[0].size = 1;
        QVERIFY(!scratchLimited.supports(dl, &error));
        QVERIFY(error.find("outline scratch memory") != std::string::npos);
        master->filters[0].kind = FilterSpec::DropShadow;
        QVERIFY(!scratchLimited.supports(dl, &error));
        QVERIFY(error.find("shadow scratch memory") != std::string::npos);
    }
};

QTEST_MAIN(GpuExecutorTest)
#include "executor_test.moc"
