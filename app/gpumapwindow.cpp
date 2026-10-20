#ifdef FUGUE_GPU
#include "app/gpumapwindow.h"
#include "rendergpu/executor.h"
#include "shell/skinwindow.h"
#include "shell/displaysettings.h"
#include <QCoreApplication>
#include <QFile>
#include <QOffscreenSurface>
#include <QPlatformSurfaceEvent>
#include <QSurfaceFormat>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
QShader shader(const char* path) {
    QFile file(QString::fromLatin1(path));
    require(file.open(QIODevice::ReadOnly), "cannot open presentation shader");
    auto result = QShader::fromSerialized(file.readAll());
    require(result.isValid(), "invalid presentation shader");
    return result;
}
QString backendName() {
    auto name = qEnvironmentVariable("FUGUE_GPU_BACKEND");
    if (!name.isEmpty()) return name;
#ifdef Q_OS_MACOS
    return "metal";
#elif defined(Q_OS_WIN)
    return "d3d11";
#else
    return "opengl";
#endif
}
}

struct GpuMapWindow::Impl {
    QPointer<fugue::shell::MapWidget> map;
    QString backend = backendName(), failure, id, traceSuffix;
    GpuDrawList pending, current, rejected;
    bool overlayDirty = true, surfaceDirty = true, activeFrame = false, detailedMetrics = false;
    bool profile = qEnvironmentVariableIntValue("FUGUE_GPU_PROFILE") != 0;
    bool verify = qEnvironmentVariableIntValue("FUGUE_GPU_VERIFY") != 0;
    Clock::time_point epoch = Clock::now(), lastLog{}, lastVerify{};
    uint64_t frames = 0;
    QFile trace;
    // Callback state outlives QRhi even if submission/finish fails during device loss.
    QRhiReadbackResult readback;
    bool completed = false;
    // Resource destruction order: bindings/pipeline/targets before textures/buffers, then executor and QRhi.
    std::unique_ptr<QOffscreenSurface> fallback;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<fugue::rendergpu::Executor> executor;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
    std::unique_ptr<QRhiSwapChain> swapchain;
    std::unique_ptr<QRhiBuffer> uniforms;
    std::unique_ptr<QRhiSampler> nearest, linear;
    std::unique_ptr<QRhiTexture> overlay;
    std::unique_ptr<QRhiShaderResourceBindings> bindings, layoutBindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    QRhiTexture* texture = nullptr; // borrowed from executor, valid until its next record()
    quint64 boundSource = 0, boundOverlay = 0;
    bool boundSmooth = false;
    QRhiReadbackResult* capture = nullptr; // only set by explicit snapshotView()
    Impl(fugue::shell::MapWidget* m, const QString& name) : map(m), id(name) {
        static uint64_t nextView = 0; // all presenters are created on the GUI thread
        if (!id.isEmpty()) traceSuffix = QString(".view-%1").arg(++nextView);
    }
};

GpuMapWindow::GpuMapWindow(fugue::shell::MapWidget* map, const QString& id) : p_(std::make_unique<Impl>(map, id)) {
    qRegisterMetaType<fugue::app::RenderPhases>();
    if (p_->backend == "metal") setSurfaceType(MetalSurface);
    else if (p_->backend == "d3d11") setSurfaceType(Direct3DSurface);
    else if (p_->backend == "opengl") {
        setSurfaceType(OpenGLSurface);
        QSurfaceFormat format;
        format.setVersion(3, 2); format.setProfile(QSurfaceFormat::CoreProfile);
        setFormat(format);
    }
    connect(map, &fugue::shell::MapWidget::presentationChanged, this, &GpuMapWindow::updateView);
}
GpuMapWindow::~GpuMapWindow() = default;
GpuDrawList GpuMapWindow::displayedList() const { return p_->current; }

void GpuMapWindow::reject(const QString& reason, bool terminal) {
    auto& p = *p_;
    if (terminal) p.failure = reason;
    if (p.pending && p.rejected != p.pending) {
        p.rejected = p.pending;
        size_t worldLayers = 0, hudLayers = 0;
        for (const auto& plane : p.pending->planes) worldLayers += plane.layers.size();
        for (const auto& hud : p.pending->hud) hudLayers += hud.layers.size();
        std::fprintf(stderr, "[gpu-present] CPU fallback: %s (view=%s size=%dx%d planes=%zu world-layers=%zu hud-layers=%zu)\n",
                     qPrintable(reason), p.id.isEmpty() ? "main" : qPrintable(p.id), p.pending->outW, p.pending->outH,
                     p.pending->planes.size(), worldLayers, hudLayers);
        emit rejected(p.pending, reason);
    }
}

bool GpuMapWindow::setList(GpuDrawList list) {
    auto& p = *p_;
    p.pending = std::move(list);
    if (!p.failure.isEmpty()) { reject(p.failure, true); return false; }
    std::string error;
    if (p.executor && !p.executor->supports(p.pending, &error)) {
        reject(QString::fromStdString(error), false);
        return false;
    }
    requestUpdate();
    return true;
}

void GpuMapWindow::setDetailedMetrics(bool enabled) {
    p_->detailedMetrics = enabled;
    if (p_->executor) p_->executor->setProfileRecording(enabled || p_->profile || p_->trace.isOpen());
}

void GpuMapWindow::updateView() {
    p_->overlayDirty = true;
    requestUpdate();
}

bool GpuMapWindow::initialize() {
    auto& p = *p_;
    if (p.rhi) return true;
    QRhi::Flags flags = QRhi::EnableTimestamps;
    if (qEnvironmentVariableIntValue("FUGUE_GPU_SOFTWARE")) flags |= QRhi::PreferSoftwareRenderer;
#if QT_CONFIG(metal)
    if (p.backend == "metal") {
        QRhiMetalInitParams params;
        p.rhi.reset(QRhi::create(QRhi::Metal, &params, flags));
    }
#endif
#ifdef Q_OS_WIN
    if (p.backend == "d3d11") {
        QRhiD3D11InitParams params;
        p.rhi.reset(QRhi::create(QRhi::D3D11, &params, flags));
    }
#endif
#if QT_CONFIG(opengl)
    if (p.backend == "opengl") {
        p.fallback.reset(QRhiGles2InitParams::newFallbackSurface(format()));
        QRhiGles2InitParams params;
        params.format = format(); params.fallbackSurface = p.fallback.get(); params.window = this;
        p.rhi.reset(QRhi::create(QRhi::OpenGLES2, &params, flags));
    }
#endif
    if (!p.rhi) { reject("cannot initialize direct backend " + p.backend, true); return false; }
    fugue::rendergpu::Options options;
    options.batchSprites = qEnvironmentVariable("FUGUE_GPU_BATCH_SPRITES") != "0";
    options.cacheSprites = qEnvironmentVariable("FUGUE_GPU_CACHE_SPRITES") != "0";
    options.reuseResources = qEnvironmentVariable("FUGUE_GPU_REUSE_RESOURCES") != "0";
    options.profileRecording = p.profile || !qEnvironmentVariable("FUGUE_GPU_TRACE").isEmpty();
    p.executor = std::make_unique<fugue::rendergpu::Executor>(*p.rhi, fugue::rendergpu::Limits{}, options);
    p.swapchain.reset(p.rhi->newSwapChain());
    p.swapchain->setWindow(this);
    p.pass.reset(p.swapchain->newCompatibleRenderPassDescriptor());
    p.swapchain->setRenderPassDescriptor(p.pass.get());
    p.uniforms.reset(p.rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
    require(p.uniforms->create(), "cannot create presentation uniform buffer");
    p.nearest.reset(p.rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                     QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    p.linear.reset(p.rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                    QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    require(p.nearest->create() && p.linear->create(), "cannot create presentation samplers");
    if (auto path = qEnvironmentVariable("FUGUE_GPU_TRACE"); !path.isEmpty()) {
        // Each native view owns a separate trace, including remounted auxiliary views.
        p.trace.setFileName(path + p.traceSuffix + ".present.csv");
        if (p.trace.open(QIODevice::WriteOnly | QIODevice::Text))
            p.trace.write("frame,start_ms,begin_ms,record_ms,present_ms,submit_ms,total_cpu_ms,last_available_gpu_ms,passes,full_draws,new_bindings,"
                          "setup_ms,plan_ms,bindings_ms,encode_ms,retire_ms,"
                          "setup_cpu_ms,plan_cpu_ms,bindings_cpu_ms,encode_cpu_ms,retire_cpu_ms,width,height,new_surfaces,uniform_bytes,"
                          "sprite_layers,region_draws,instanced_draws,instanced_sprites,instance_bytes,atlas_bytes,atlas_upload_bytes,atlas_uploads,"
                          "planes,cached_planes,plane_cache_hits,filtered_plane_cache_hits,avoided_plane_passes,plane_cache_bytes,plane_cache_input_bytes\n");
        else std::fprintf(stderr, "[gpu-present] cannot open trace: %s\n", qPrintable(p.trace.errorString()));
    }
    std::fprintf(stderr, "[gpu-present] %s / %s: native swapchain for %s; no per-frame readback (trace suffix: %s)\n",
                 qPrintable(p.backend), p.rhi->driverInfo().deviceName.constData(),
                 p.id.isEmpty() ? "main map" : qPrintable(p.id), qPrintable(p.traceSuffix));
    return true;
}

void GpuMapWindow::exposeEvent(QExposeEvent*) {
    p_->surfaceDirty = true;
    if (isExposed()) requestUpdate();
}

bool GpuMapWindow::event(QEvent* event) {
    if (event->type() == QEvent::Resize || event->type() == QEvent::DevicePixelRatioChange ||
        event->type() == QEvent::ScreenChangeInternal) {
        p_->surfaceDirty = p_->overlayDirty = true;
        requestUpdate();
    }
    if (event->type() == QEvent::UpdateRequest) { render(); return true; }
    if (event->type() == QEvent::PlatformSurface &&
        static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
        if (p_->swapchain) p_->swapchain->destroy();
        p_->surfaceDirty = true;
    }
    // The native child covers the map but must behave like the existing MapWidget for focus, macros and hit tests.
    if (p_->map) switch (event->type()) {
    case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove: case QEvent::Wheel: case QEvent::KeyPress: case QEvent::KeyRelease:
    case QEvent::ShortcutOverride: case QEvent::Leave:
        QCoreApplication::sendEvent(p_->map, event);
        return true;
    case QEvent::FocusIn:
        p_->map->setFocus(Qt::OtherFocusReason);
        break;
    default: break;
    }
    return QWindow::event(event);
}

void GpuMapWindow::render() {
    auto& p = *p_;
    if (!isExposed() || !p.map || !p.pending || p.pending == p.rejected || !p.failure.isEmpty()) return;
    const auto start = Clock::now();
    try {
        if (!initialize()) return;
        std::string error;
        if (!p.executor->supports(p.pending, &error)) { reject(QString::fromStdString(error), false); return; }
        const QSize surface = p.swapchain->surfacePixelSize();
        if (surface.isEmpty()) return;
        if (p.surfaceDirty || p.swapchain->currentPixelSize() != surface) {
            require(p.swapchain->createOrResize(), "cannot create/resize map swapchain");
            p.surfaceDirty = false; p.overlayDirty = true;
        }
        auto status = p.rhi->beginFrame(p.swapchain.get());
        if (status == QRhi::FrameOpSwapChainOutOfDate) {
            require(p.swapchain->createOrResize(), "cannot recreate map swapchain");
            status = p.rhi->beginFrame(p.swapchain.get());
        }
        require(status == QRhi::FrameOpSuccess, "cannot begin direct GPU frame (possibly device loss)");
        p.activeFrame = true;
        auto* cb = p.swapchain->currentFrameCommandBuffer();
        const double gpuTime = p.rhi->isFeatureSupported(QRhi::Timestamps) ? cb->lastCompletedGpuTime() * 1000 : -1;
        const double previousGpuMs = gpuTime > 0 ? gpuTime : -1;
        const auto begun = Clock::now();
        const bool changed = p.current != p.pending;
        const bool profileRecording = p.detailedMetrics || p.profile || p.trace.isOpen();
        if (changed) {
            p.executor->setProfileRecording(profileRecording);
            // The next list may resize/recreate surfaces; retire SRBs before their sampled resource wrappers.
            p.bindings.reset();
            p.texture = p.executor->record(p.pending, *cb, &error);
            require(p.texture != nullptr, error.empty() ? "direct GPU recording failed" : error.c_str());
        }
        const auto recorded = Clock::now();
        const QSize target = p.swapchain->currentPixelSize();
        auto release = [](QRhiResourceUpdateBatch* u) { u->release(); };
        std::unique_ptr<QRhiResourceUpdateBatch, decltype(release)> updates(p.rhi->nextResourceUpdateBatch(), release);
        if (p.overlayDirty || !p.overlay) {
            auto image = p.map->overlayImage();
            if (image.isNull()) { image = QImage(1, 1, QImage::Format_RGBA8888_Premultiplied); image.fill(Qt::transparent); }
            if (!p.overlay || p.overlay->pixelSize() != image.size()) {
                p.bindings.reset();
                p.overlay.reset(p.rhi->newTexture(QRhiTexture::RGBA8, image.size()));
                require(p.overlay->create(), "cannot create performance overlay texture");
            }
            QRhiTextureSubresourceUploadDescription pixels(image.constBits(), quint32(image.sizeInBytes()));
            pixels.setSourceSize(image.size());
            updates->uploadTexture(p.overlay.get(), QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, pixels)}));
            p.overlayDirty = false;
        }
        const QSize source = p.texture->pixelSize();
        const QRect image = p.map->imageRect(source);
        const float sx = float(target.width()) / std::max(1, p.map->width());
        const float sy = float(target.height()) / std::max(1, p.map->height());
        const bool smooth = p.map->smoothScaling(source);
        const std::array<float, 8> params{image.x() * sx, image.y() * sy, image.width() * sx, image.height() * sy,
                                         float(target.width()), float(target.height()), p.rhi->isYUpInFramebuffer() ? 1.f : 0.f,
                                         smooth ? 1.f : 0.f};
        updates->updateDynamicBuffer(p.uniforms.get(), 0, sizeof(params), params.data());
        if (!p.bindings || p.boundSource != p.texture->globalResourceId() ||
            p.boundOverlay != p.overlay->globalResourceId() || p.boundSmooth != smooth) {
            p.bindings.reset(p.rhi->newShaderResourceBindings());
            p.bindings->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, p.uniforms.get()),
                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, p.texture,
                                                         smooth ? p.linear.get() : p.nearest.get()),
                QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, p.overlay.get(), p.nearest.get())
            });
            require(p.bindings->create(), "cannot create presentation bindings");
            p.boundSource = p.texture->globalResourceId(); p.boundOverlay = p.overlay->globalResourceId(); p.boundSmooth = smooth;
        }
        if (!p.pipeline) {
            // Stable layout-only SRB: real scene/overlay bindings may be retired on every list or resize.
            p.layoutBindings.reset(p.rhi->newShaderResourceBindings());
            p.layoutBindings->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::FragmentStage, nullptr),
                QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, nullptr, nullptr),
                QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, nullptr, nullptr)
            });
            require(p.layoutBindings->create(), "cannot create presentation binding layout");
            p.pipeline.reset(p.rhi->newGraphicsPipeline());
            p.pipeline->setShaderStages({{QRhiShaderStage::Vertex, shader(":/fugue/gpu/composite.vert.qsb")},
                                         {QRhiShaderStage::Fragment, shader(":/fugue/gpu/present.frag.qsb")}});
            p.pipeline->setVertexInputLayout(QRhiVertexInputLayout{});
            p.pipeline->setShaderResourceBindings(p.layoutBindings.get());
            p.pipeline->setRenderPassDescriptor(p.pass.get());
            require(p.pipeline->create(), "cannot create presentation pipeline");
        }
        cb->resourceUpdate(updates.release());
        cb->beginPass(p.swapchain->currentFrameRenderTarget(), Qt::black, {1.f, 0});
        cb->setGraphicsPipeline(p.pipeline.get());
        cb->setViewport(QRhiViewport(0, 0, float(target.width()), float(target.height())));
        cb->setShaderResources(p.bindings.get());
        cb->draw(3);
        cb->endPass();
        if (p.capture) {
            auto* readback = p.rhi->nextResourceUpdateBatch();
            readback->readBackTexture(QRhiReadbackDescription(), p.capture);
            cb->resourceUpdate(readback);
        }
        const auto presented = Clock::now();
        status = p.rhi->endFrame(p.swapchain.get()); // asynchronous; do not finish(), wait, or read back here
        p.activeFrame = false;
        require(status == QRhi::FrameOpSuccess, "direct GPU submission failed (possibly device loss)");
        const auto finished = Clock::now();
        if (changed) {
            p.current = p.pending;
            ++p.frames;
            const auto stats = p.executor->stats();
            if (p.trace.isOpen()) {
                auto row = QByteArray::number(qulonglong(p.frames));
                for (double v : {ms(p.epoch, start), ms(start, begun), ms(begun, recorded), ms(recorded, presented),
                                 ms(presented, finished), ms(start, finished), previousGpuMs})
                    row += ',' + QByteArray::number(v, 'f', 6);
                row += ',' + QByteArray::number(qulonglong(stats.renderPasses)) + ',' + QByteArray::number(qulonglong(stats.fullSurfaceDraws))
                     + ',' + QByteArray::number(qulonglong(stats.createdBindings));
                for (double v : stats.recordWallMs) row += ',' + QByteArray::number(v, 'f', 6);
                for (double v : stats.recordCpuMs) row += ',' + QByteArray::number(v, 'f', 6);
                row += ',' + QByteArray::number(p.current->outW) + ',' + QByteArray::number(p.current->outH)
                     + ',' + QByteArray::number(qulonglong(stats.createdSurfaces))
                     + ',' + QByteArray::number(qulonglong(stats.uploadedUniformBytes));
                for (uint64_t count : {stats.spriteDraws, stats.regionDraws, stats.instancedDraws, stats.instancedSprites,
                                       stats.uploadedInstanceBytes, stats.atlasBytes, stats.uploadedAtlasBytes, stats.atlasUploads,
                                       stats.planes, stats.cachedPlanes, stats.planeCacheHits, stats.filteredPlaneCacheHits,
                                       stats.avoidedPlanePasses, stats.planeCacheBytes, stats.planeCacheInputBytes})
                    row += ',' + QByteArray::number(qulonglong(count));
                row += '\n';
                if (p.trace.write(row) != row.size()) p.trace.close();
            }
            if (finished - p.lastLog >= std::chrono::seconds(1)) {
                if (p.profile) std::fprintf(stderr, "[gpu-present] CPU submit=%.2fms (begin=%.2f record=%.2f present=%.2f end=%.2f) "
                                            "GPU-last-available=%.2fms passes=%llu instances=%llu/%llu draws planes-reused=%llu/%llu skipped-passes=%llu; "
                                            "asynchronous, NOT completion latency\n",
                                            ms(start, finished), ms(start, begun), ms(begun, recorded), ms(recorded, presented),
                                            ms(presented, finished), previousGpuMs, (unsigned long long)stats.renderPasses,
                                            (unsigned long long)stats.instancedSprites, (unsigned long long)stats.instancedDraws,
                                            (unsigned long long)stats.planeCacheHits, (unsigned long long)stats.planes,
                                            (unsigned long long)stats.avoidedPlanePasses);
                if (p.trace.isOpen()) p.trace.flush();
                p.lastLog = finished;
            }
            fugue::app::RenderPhases phases;
            phases.wallMs = {ms(start, begun), ms(begun, recorded), ms(recorded, presented), ms(presented, finished)};
            if (profileRecording) phases.recordMs = stats.recordWallMs;
            emit submitted(p.current, ms(start, finished), previousGpuMs,
                           p.backend + " / " + QString::fromUtf8(p.rhi->driverInfo().deviceName), phases);
            // Explicit diagnostics only: this readback + CPU comparison stalls the GUI and is NOT a performance run.
            if (p.verify && finished - p.lastVerify >= std::chrono::seconds(1)) {
                const auto actual = snapshot();
                const auto reference = fugue::render::execute(*p.current);
                if (!actual.isNull() && actual.size() == QSize(reference.w, reference.h)) {
                    int worst = 0; size_t different = 0, overTolerance = 0;
                    for (int y = 0; y < reference.h; ++y) for (int i = 0; i < reference.w * 4; ++i) {
                        int delta = std::abs(int(actual.constScanLine(y)[i]) - int(reference.px[size_t(y) * reference.w * 4 + i]));
                        worst = std::max(worst, delta); different += delta != 0; overTolerance += delta > 2;
                    }
                    std::fprintf(stderr, "[gpu-present-verify] %dx%d max-byte-error=%d differing-channels=%zu over-2=%zu; "
                                         "synchronous diagnostic readback, excluded from submit timing\n",
                                 reference.w, reference.h, worst, different, overTolerance);
                } else std::fprintf(stderr, "[gpu-present-verify] failed to capture scene\n");
                p.lastVerify = Clock::now();
            }
        }
    } catch (const std::exception& e) {
        if (p.activeFrame) { p.rhi->endFrame(p.swapchain.get(), QRhi::SkipPresent); p.activeFrame = false; }
        reject(QString::fromUtf8(e.what()), true);
    }
}

QImage GpuMapWindow::snapshot() {
    auto& p = *p_;
    if (!p.rhi || !p.executor || !p.current || p.activeFrame || !p.failure.isEmpty()) return {};
    QRhiCommandBuffer* cb = nullptr;
    if (p.rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        reject("cannot begin GPU screenshot frame", true); return {};
    }
    p.bindings.reset();
    std::string error;
    auto* texture = p.executor->record(p.current, *cb, &error);
    auto& result = p.readback;
    result = {}; p.completed = false;
    if (texture) {
        p.texture = texture;
        result.completed = [state = p_.get()] { state->completed = true; };
        auto* updates = p.rhi->nextResourceUpdateBatch();
        updates->readBackTexture(texture, &result);
        cb->resourceUpdate(updates);
    }
    auto status = p.rhi->endOffscreenFrame();
    if (status != QRhi::FrameOpSuccess) { reject("GPU screenshot submission failed", true); return {}; }
    if (!texture || !p.completed || result.format != QRhiTexture::RGBA8 ||
        result.pixelSize != QSize(p.current->outW, p.current->outH) ||
        result.data.size() != qint64(result.pixelSize.width()) * result.pixelSize.height() * 4) {
        reject("GPU screenshot recording/readback failed", true); return {};
    }
    // Executor texture rows are logical top-down even on OpenGL. No presentation flip or scaling here.
    return QImage(reinterpret_cast<const uchar*>(result.data.constData()), result.pixelSize.width(), result.pixelSize.height(),
                  QImage::Format_RGBA8888).copy();
}

QImage GpuMapWindow::snapshotView() {
    auto& p = *p_;
    if (!p.current || p.activeFrame || !isExposed() || !p.failure.isEmpty()) return {};
    auto& result = p.readback;
    result = {}; p.completed = false;
    result.completed = [state = p_.get()] { state->completed = true; };
    auto pending = p.pending;
    const auto flags = p.swapchain->flags();
    p.swapchain->setFlags(flags | QRhiSwapChain::UsedAsTransferSource);
    p.surfaceDirty = true;
    p.pending = p.current;
    p.capture = &result;
    render();
    const auto status = p.rhi->finish(); // only explicit captures wait; ordinary frames never do
    p.capture = nullptr;
    p.pending = std::move(pending);
    p.swapchain->setFlags(flags);
    p.surfaceDirty = true;
    if (status != QRhi::FrameOpSuccess) { reject("GPU view screenshot completion failed", true); return {}; }
    if (!p.failure.isEmpty()) { reject(p.failure, true); return {}; }
    if (!p.completed || result.data.size() != qint64(result.pixelSize.width()) * result.pixelSize.height() * 4) {
        reject("GPU view screenshot readback failed", true); return {};
    }
    QImage::Format format;
    if (result.format == QRhiTexture::RGBA8) format = QImage::Format_RGBA8888;
    else if (result.format == QRhiTexture::BGRA8) format = QImage::Format_ARGB32;
    else return {};
    QImage image(reinterpret_cast<const uchar*>(result.data.constData()), result.pixelSize.width(), result.pixelSize.height(), format);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    return p.rhi->isYUpInFramebuffer() ? image.flipped(Qt::Vertical) : image.copy();
#else
    return p.rhi->isYUpInFramebuffer() ? image.mirrored(false, true) : image.copy();
#endif
}

GpuMapController::GpuMapController(RenderThread& renderer, fugue::shell::SkinWindow& window)
    : QObject(&window), renderer_(renderer), window_(window) {
    qRegisterMetaType<GpuDrawList>();
    connect(window.displaySettings(), &fugue::shell::DisplaySettings::performanceOverlayChanged, this, [this] { updateProfiling(); });
    connect(window.displaySettings(), &fugue::shell::DisplaySettings::performanceDetailsChanged, this, [this] { updateProfiling(); });
    connect(&renderer, &RenderThread::gpuListReady, this, [this] {
        auto frames = renderer_.takeDirectFrames();
        if (!window_.displaySettings()->useGpu()) return;
        for (auto& [id, frame] : frames) {
            auto& view = views_[id];
            view.hit = std::move(frame.hit);
            if (view.latest == frame.list) {
                if (view.latest == view.presented) setHit(id, view);
            } else {
                view.latest = std::move(frame.list);
                mount(id);
            }
        }
    });
    connect(&window, &fugue::shell::SkinWindow::mapReady, this, [this] { mount({}); });
    connect(&window, &fugue::shell::SkinWindow::auxMapBuilt, this, [this](const QString& id) {
        mount(id.toLower().toStdString());
    });
    connect(&window, &fugue::shell::SkinWindow::auxMapRemoved, this, [this](const QString& id) {
        auto it = views_.find(id.toLower().toStdString());
        if (it == views_.end()) return;
        if (it->second.container) delete it->second.container.data();
        views_.erase(it);
    });
    // Ignore queued CPU images after switching back to GPU; direct fallback is identity-checked separately.
    connect(&renderer, &RenderThread::frame, this, [this](const QImage& image, fugue::shell::HitPtr hit) {
        if (!window_.displaySettings()->useGpu()) window_.setMapFrame(image, std::move(hit));
    });
    connect(&renderer, &RenderThread::auxFrame, this, [this](const QString& id, const QImage& image) {
        if (!window_.displaySettings()->useGpu()) window_.setAuxMapFrame(id, image);
    });
    connect(&renderer, &RenderThread::sceneOnly, this, [this](fugue::shell::HitPtr hit) {
        if (!window_.displaySettings()->useGpu()) window_.setMapHit(std::move(hit));
    });
    connect(&renderer, &RenderThread::fallbackFrame, this, [this](const QString& name, GpuDrawList list, const QImage& image) {
        const auto id = name.toStdString();
        auto it = views_.find(id);
        if (it == views_.end() || list != it->second.latest || !window_.displaySettings()->useGpu()) return;
        auto& view = it->second;
        view.presented = list;
        if (id.empty()) window_.setMapFrame(image, view.hit);
        else window_.setAuxMapFrame(name, image);
    });
    connect(window.displaySettings(), &fugue::shell::DisplaySettings::rendererChanged, this, [this](bool gpu) {
        if (!gpu) for (auto& [id, view] : views_) {
            view.latest.reset(); view.presented.reset();
            if (view.container) view.container->hide();
        }
    });
}
GpuMapController::~GpuMapController() {
    for (auto& [id, view] : views_) if (view.container) delete view.container.data();
}

void GpuMapController::updateProfiling() {
    const auto* settings = window_.displaySettings();
    for (auto& [id, view] : views_) if (view.gpu)
        view.gpu->setDetailedMetrics(settings->performanceOverlay() && settings->performanceDetails());
}

void GpuMapController::setHit(const std::string& id, View& view) {
    if (id.empty()) window_.setMapHit(view.hit);
    else if (view.map) view.map->setHit(view.hit);
}

void GpuMapController::mount(const std::string& id) {
    auto it = views_.find(id);
    if (it == views_.end() || !it->second.latest || !window_.displaySettings()->useGpu()) return;
    auto& view = it->second;
    auto* map = id.empty() ? window_.map() : window_.auxMap(QString::fromStdString(id));
    if (!map) return; // retain the latest list until the popup/control is built
    if (view.map != map || !view.gpu) {
        if (view.container) delete view.container.data();
        view.map = map;
        view.presented.reset();
        view.gpu = new GpuMapWindow(map, QString::fromStdString(id));
        view.gpu->setDetailedMetrics(window_.displaySettings()->performanceOverlay() && window_.displaySettings()->performanceDetails());
        view.container = QWidget::createWindowContainer(view.gpu, map);
        view.container->setFocusPolicy(Qt::StrongFocus);
        map->setExternalView(view.container, view.gpu);
        QPointer<GpuMapWindow> gpu = view.gpu;
        connect(gpu, &GpuMapWindow::rejected, this, [this, id, gpu](GpuDrawList list, const QString& reason) {
            auto it = views_.find(id);
            if (it != views_.end() && it->second.gpu == gpu && list == it->second.latest && window_.displaySettings()->useGpu())
                renderer_.submitFallback(id, list, reason.toStdString());
        });
        connect(gpu, &GpuMapWindow::submitted, this, [this, id, gpu](GpuDrawList list, double cpuMs, double gpuMs, const QString& backend,
                                                                 const fugue::app::RenderPhases& phases) {
            auto it = views_.find(id);
            if (it == views_.end() || it->second.gpu != gpu || list != it->second.latest || !window_.displaySettings()->useGpu()) return;
            auto& view = it->second;
            view.presented = list;
            auto snapshot = [gpu] { return gpu ? gpu->snapshot() : QImage(); };
            if (id.empty()) window_.setMapExternalFrame(QSize(list->outW, list->outH), view.hit, snapshot);
            else if (view.map) view.map->setExternalFrame(QSize(list->outW, list->outH), view.hit, snapshot);
            renderer_.reportSubmission(id, cpuMs, gpuMs, backend.toStdString(), phases);
        });
    }
    if (view.gpu->setList(view.latest)) view.container->show();
}
#endif
