#include "shell/spriterenderer.h"
#include "shell/compositionplan.h"
#include "shell/blurkernel.h"
#include <QFile>
#include <rhi/qrhi.h>
#include <algorithm>
#include <map>
#include <vector>
#include <cstring>
#include <chrono>
#include <unordered_map>

namespace fugue::shell {
namespace {
QShader loadShader(const char* path) {
    QFile file(QString::fromUtf8(path));
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}
struct Vertex { float x, y, u, v, r, g, b, a; };
}
struct SpriteRenderer::Impl {
    struct Texture {
        uint64_t used = 0;
        QImage image;
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiShaderResourceBindings> bindings, smoothBindings;
    };
    struct Surface {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiShaderResourceBindings> bindings, smoothBindings;
    };
    struct Draw { QRhiShaderResourceBindings* bindings; quint32 vertex, uniform; int blend; quint32 count = 6; };
    QRhi* rhi = nullptr;
    QRhiRenderPassDescriptor* finalPass = nullptr;
    std::unique_ptr<QRhiRenderPassDescriptor> offscreenPass, byteCapturePass;
    // Appearance block: 240 bytes, then 16 vec4 of wave/ripple parameters.
    static constexpr int uniformBytes = 496;
    int samples = 1, stride = 512;
    bool floatSurfaces = false;
    std::unique_ptr<QRhiSampler> sampler, smoothSampler;
    std::unique_ptr<QRhiTexture> dummy;
    std::unique_ptr<QRhiShaderResourceBindings> layout, solid;
    std::unique_ptr<QRhiGraphicsPipeline> pipelines[3][8];
    std::unique_ptr<QRhiBuffer> vertices, uniforms;
    std::unordered_map<qint64, Texture> textures;
    uint64_t textureEpoch = 0;
    std::map<int, Surface> surfaces;
    std::shared_ptr<const CompositionPlan> plan;
    std::vector<std::vector<Draw>> passDraws;
    std::vector<Draw> finalDraws;
    bool prepared = false, dirty = false;
    Stats stats;

    std::unique_ptr<QRhiShaderResourceBindings> bindings(QRhiTexture* texture, bool smooth = false) {
        std::unique_ptr<QRhiShaderResourceBindings> b(rhi->newShaderResourceBindings());
        b->setBindings({QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
            0, QRhiShaderResourceBinding::FragmentStage, uniforms.get(), uniformBytes),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture, smooth ? smoothSampler.get() : sampler.get())});
        if (!b->create()) return {};
        return b;
    }
    bool pipeline(int offscreen, int mode, QRhiRenderPassDescriptor* pass) {
        auto& p = pipelines[offscreen][mode];
        if (p) return true;
        p.reset(rhi->newGraphicsPipeline());
        p->setShaderStages({{QRhiShaderStage::Vertex, loadShader(":/map/shaders/sprite.vert.qsb")},
                            {QRhiShaderStage::Fragment, loadShader(":/map/shaders/sprite.frag.qsb")}});
        QRhiVertexInputLayout input;
        input.setBindings({{sizeof(Vertex)}});
        input.setAttributes({{0,0,QRhiVertexInputAttribute::Float2,0},
                             {0,1,QRhiVertexInputAttribute::Float2,2*sizeof(float)},
                             {0,2,QRhiVertexInputAttribute::Float4,4*sizeof(float)}});
        p->setVertexInputLayout(input);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;
        blend.srcColor = blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstColor = blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        if (mode == 2 || mode == 3) blend.dstColor = QRhiGraphicsPipeline::One;
        if (mode == 3) blend.opColor = QRhiGraphicsPipeline::ReverseSubtract;
        if (mode == 4) {
            blend.srcColor = QRhiGraphicsPipeline::DstColor;
            blend.srcAlpha = QRhiGraphicsPipeline::Zero;
            blend.dstAlpha = QRhiGraphicsPipeline::One;
        }
        if (mode == 5) {
            blend.srcColor = blend.srcAlpha = QRhiGraphicsPipeline::Zero;
            blend.dstColor = blend.dstAlpha = QRhiGraphicsPipeline::SrcAlpha;
        }
        if (mode == 6) {
            // Stock bloom's subtract (byondwin sub_10121240 mode 3): RGB reverse-subtract, alpha left as it was.
            blend.opColor = QRhiGraphicsPipeline::ReverseSubtract;
            blend.dstColor = QRhiGraphicsPipeline::One;
            blend.srcAlpha = QRhiGraphicsPipeline::Zero;
            blend.dstAlpha = QRhiGraphicsPipeline::One;
        }
        if (mode == 7) {
            // BLEND_INSET_OVERLAY: over what is already drawn, masked by its alpha, which stays as it was.
            blend.srcColor = QRhiGraphicsPipeline::DstAlpha;
            blend.srcAlpha = QRhiGraphicsPipeline::Zero;
            blend.dstAlpha = QRhiGraphicsPipeline::One;
        }
        p->setTargetBlends({blend}); p->setShaderResourceBindings(layout.get());
        p->setSampleCount(offscreen ? 1 : samples); p->setRenderPassDescriptor(pass);
        if (!p->create()) { p.reset(); return false; }
        return true;
    }
    void draw(QRhiCommandBuffer* cb, const std::vector<Draw>& draws, int offscreen) {
        if (draws.empty()) return;
        const QRhiCommandBuffer::VertexInput input(vertices.get(), 0);
        int previous = -1;
        for (const auto& draw : draws) {
            if (draw.blend != previous) {
                cb->setGraphicsPipeline(pipelines[offscreen][draw.blend].get());
                cb->setVertexInput(0, 1, &input); previous = draw.blend;
            }
            const QRhiCommandBuffer::DynamicOffset offset(0, draw.uniform);
            cb->setShaderResources(draw.bindings, 1, &offset);
            cb->draw(draw.count, 1, draw.vertex);
        }
    }
};
SpriteRenderer::SpriteRenderer(bool boundedMaterials)
    : d_(std::make_unique<Impl>()), boundedMaterials_(boundedMaterials) {}
SpriteRenderer::~SpriteRenderer() = default;
bool SpriteRenderer::ready() const { return d_->prepared; }
std::shared_ptr<const CompositionPlan> SpriteRenderer::preparedPlan() const {
    return d_->prepared ? d_->plan : nullptr;
}
SpriteRenderer::Stats SpriteRenderer::takeStats() {
    const auto result = d_->stats;
    d_->stats.preparations = d_->stats.replays = d_->stats.planRefusals = 0; d_->stats.prepareMs = 0;
    return result;
}
bool SpriteRenderer::initialize(QRhi* rhi, QRhiRenderPassDescriptor* pass, int samples) {
    d_ = std::make_unique<Impl>(); auto& d = *d_;
    d.rhi = rhi; d.finalPass = pass; d.samples = samples; d.stride = rhi->ubufAligned(Impl::uniformBytes);
    d.floatSurfaces = rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::RenderTarget);
    d.uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, d.stride));
    d.sampler.reset(rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                   QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    d.smoothSampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    d.dummy.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1,1)));
    if (!d.uniforms->create() || !d.sampler->create() || !d.smoothSampler->create() || !d.dummy->create()) return false;
    d.layout = d.bindings(d.dummy.get());
    return d.layout && d.pipeline(0, 0, pass);
}
uint64_t SpriteRenderer::prepare(QRhiResourceUpdateBatch* updates, const MapSprites& sprites,
                                 QRect source) {
    auto& d = *d_;
    struct Measure {
        Stats& stats;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        ~Measure() { stats.prepareMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); }
    } measure{d.stats};
    ++d.stats.preparations;
    if (d.prepared && d.plan->sprites.constData() == sprites.constData() && d.plan->sprites.size() == sprites.size() &&
        d.plan->source == source) { d.dirty = false; ++d.stats.replays; return 0; }
    d.stats.draws = d.stats.surfaces = d.stats.passes = d.stats.surfaceBytes = d.stats.workPixels = d.stats.ignoredFilters = 0;
    d.stats.rejection = {}; d.stats.composition = {};
    d.stats.requestedSurfaces = d.stats.candidatePasses = d.stats.reachablePasses = 0;
    d.stats.sourceSize = source.size();
    d.prepared = false; d.plan.reset(); d.passDraws.clear(); d.finalDraws.clear();
    if (!d.layout || source.isEmpty()) return 0;
    // Graph planning is model-free and retained independently of GPU resources.
    d.plan = CompositionPlan::build(sprites,source,{d.floatSurfaces,boundedMaterials_});
    const auto& plan = *d.plan;
    d.stats.ignoredFilters = plan.ignoredFilters;
    d.stats.requestedSurfaces = plan.requestedSurfaces;
    d.stats.rejection = plan.rejection;
    d.stats.candidatePasses = plan.passes.size();
    d.stats.reachablePasses = plan.livePasses;
    d.stats.composition = plan.metrics;
    if (!plan.supported) { ++d.stats.planRefusals; return 0; }
    const auto& storage = plan.storage;
    const auto& final = plan.final;
    // API/index representability, not a workload budget. Check before allocation.
    const size_t maxDraws = std::min(size_t(std::numeric_limits<int>::max()) / d.stride,
        size_t(std::numeric_limits<int>::max()) / (6 * sizeof(Vertex)));
    size_t count = final.size();
    for (int i : plan.schedule) {
        if (count > maxDraws || plan.passes[i].items.size() > maxDraws-count) {
            d.stats.rejection = {PlanRejection::IndexRange,maxDraws+1,maxDraws};
            ++d.stats.planRefusals; return 0;
        }
        count += plan.passes[i].items.size();
    }
    if (count > maxDraws || storage.storagePixels > std::numeric_limits<uint64_t>::max()/4) {
        d.stats.rejection.reason = PlanRejection::ArithmeticOverflow;
        ++d.stats.planRefusals; return 0;
    }
    d.passDraws.resize(plan.passes.size());
    std::erase_if(d.surfaces,[&](const auto& p) { return p.first >= int(storage.sizes.size()); });
    for (int i = 0; i < int(storage.sizes.size()); ++i) {
        auto& s = d.surfaces[i];
        const bool byteCapture = storage.storageClasses[i] == 1;
        const auto format = d.floatSurfaces && !byteCapture ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8;
        if (!s.texture || s.texture->pixelSize() != storage.sizes[i] || s.texture->format() != format) {
            s = {};
            s.texture.reset(d.rhi->newTexture(format,storage.sizes[i],1,QRhiTexture::RenderTarget));
            if (!s.texture->create()) return 0;
            s.target.reset(d.rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(QRhiColorAttachment(s.texture.get()))));
            auto& descriptor = byteCapture ? d.byteCapturePass : d.offscreenPass;
            if (!descriptor) descriptor.reset(s.target->newCompatibleRenderPassDescriptor());
            s.target->setRenderPassDescriptor(descriptor.get());
            if (!s.target->create()) return 0;
        }
    }
    const int uniformBytes = int(std::max<size_t>(1, count) * d.stride);
    if (d.uniforms->size() < quint32(uniformBytes)) {
        d.uniforms->setSize(uniformBytes);
        if (!d.uniforms->create()) return 0;
        // Recreate bindings after the backing resource changes (including retained textures).
        for (auto& [key,t] : d.textures) { t.bindings.reset(); t.smoothBindings.reset(); }
        for (auto& [key,s] : d.surfaces) { s.bindings.reset(); s.smoothBindings.reset(); }
        d.solid.reset();
    }
    ++d.textureEpoch;
    std::vector<Vertex> vertices; vertices.reserve(count * 6);
    std::vector<char> uniforms;
    std::map<std::tuple<ColorMatrix, std::array<float,8>, std::array<float,12>, std::array<float,64>>, quint32> colors;
    const std::array<float,64> noParams{};
    const std::array<float,64>* lastParams = &noParams;
    std::array<float,12> lastEffect{};
    ColorMatrix lastColor = identityColor;
    std::array<float,8> lastOptions{};
    quint32 lastUniform = UINT32_MAX;
    uint64_t uploaded = 0;
    int drawCount = 0;
    auto record = [&](const CompositionPlan::Item& item, std::vector<Impl::Draw>& draws, int offscreen, QRect capture, bool straightOutput = false) -> bool {
        if (item.input == -2) return true;
        const auto& sprite = *item.sprite;
        QRect rect = sprite.rect();
        QRhiShaderResourceBindings* bindings = nullptr;
        const bool premultiplied = item.input >= 0;
        auto effect = item.effect;
        // A KEEP_TOGETHER surface ends at its own box (item.hardEdge): the sampler repeats its edge texel there.
        if (premultiplied && sprite.affine && sprite.smooth && !item.hardEdge) {  // a PIXEL_SCALE relay samples its surface unfiltered
            effect = {8,0,0,0,0,0,0,0,1.0f/sprite.size.width(),1.0f/sprite.size.height(),1,0};
            // A bilinear border sample has half a texel of support outside the
            // captured rectangle. Do not clamp it or cut it off at the quad edge.
            rect = sprite.affine->forward.mapRect(QRectF(-.5,-.5,sprite.size.width()+1,sprite.size.height()+1))
                .toAlignedRect().translated(sprite.position);
        }
        const int input = premultiplied ? 1 : sprite.kind == MapSprite::Blackness ? 2 : 0;
        // BLEND_MULTIPLY of an icon multiplies by its straight colour, then keeps the destination by 1 - alpha (stock
        // 516.1689, stock-parity-world blend-reset-layers overlay-multiply). A group's or plane's surface is premultiplied in
        // stock too (attachment-tree-world group-half-multiply).
        const bool multiply = sprite.blend == 4 && input == 0 && !item.alphaMask && !item.stockSubtract && !item.straightInput;
        std::array<float,8> options{float(input),1,1,straightOutput || multiply ? 1.0f : item.straightInput ? -1.0f : 0.0f,0,0,0,0};
        if (input == 2) {
            if (!d.solid) d.solid=d.bindings(d.dummy.get());
            bindings=d.solid.get();
        } else if (premultiplied) {
            const auto& from = plan.passes[item.input];
            auto& surface = d.surfaces[from.surface];
            const auto capacity = surface.texture->pixelSize();
            options[1] = float(from.bounds.width())/capacity.width();
            options[2] = float(from.bounds.height())/capacity.height();
            options[4] = .5f/capacity.width();options[5] = .5f/capacity.height();
            auto& selected = sprite.smooth ? surface.smoothBindings : surface.bindings;
            if (!selected) selected = d.bindings(surface.texture.get(), sprite.smooth);
            bindings = selected.get();
        } else {
            if (sprite.image.isNull()) return true;
            const qint64 key = sprite.image.cacheKey();
            auto [it, inserted] = d.textures.try_emplace(key); auto& t = it->second;
            t.used = d.textureEpoch;
            if (inserted) {
                t.image = sprite.image; t.texture.reset(d.rhi->newTexture(QRhiTexture::RGBA8, sprite.image.size()));
                if (!t.texture->create()) { d.textures.erase(it); return false; }
                updates->uploadTexture(t.texture.get(), sprite.image); uploaded += uint64_t(sprite.image.sizeInBytes());
            }
            auto& selected = sprite.smooth ? t.smoothBindings : t.bindings;
            if (!selected) selected = d.bindings(t.texture.get(), sprite.smooth);
            bindings = selected.get();
        }
        if (!bindings) return false;
        const QRect clipped = rect.intersected(capture);
        if (clipped.isEmpty()) return true;
        const QSize output = capture.size();
        const float x0 = 2.0f*(clipped.x()-capture.x())/output.width()-1;
        const float y0 = 1-2.0f*(clipped.y()-capture.y())/output.height();
        const float x1 = x0+2.0f*clipped.width()/output.width(), y1 = y0-2.0f*clipped.height()/output.height();
        const float u0 = float(clipped.x()-rect.x())/rect.width(), v0 = float(clipped.y()-rect.y())/rect.height();
        const float u1 = u0+float(clipped.width())/rect.width(), v1 = v0+float(clipped.height())/rect.height();
        const quint32 first = quint32(vertices.size());
        auto vertex = [&](float x,float y,float u,float v) {
            // QRhi's render-to-texture orientation differs on GL, not on D3D/Vulkan.
            if (d.rhi->isYUpInFramebuffer()) y = -y;
            vertices.push_back({x,y,u,v,sprite.tint[0],sprite.tint[1],sprite.tint[2],sprite.tint[3]});
        };
        if (sprite.affine) {
            // Keep UVs attached to the actual quad; the GPU clips triangles at the
            // viewport. Clipping its AABB and stretching UVs would destroy shape.
            auto corner = [&](float u, float v) {
                const QSize dimensions = premultiplied ? sprite.size : sprite.image.size();
                const QPointF p = sprite.affine->forward.map(QPointF(u*dimensions.width(), v*dimensions.height())) + sprite.position;
                vertex(float(2*(p.x()-capture.x())/output.width()-1),
                       float(1-2*(p.y()-capture.y())/output.height()), u, v);
            };
            const float dx = premultiplied && !item.hardEdge ? .5f/sprite.size.width() : 0;
            const float dy = premultiplied && !item.hardEdge ? .5f/sprite.size.height() : 0;
            corner(-dx,-dy); corner(-dx,1+dy); corner(1+dx,-dy);
            corner(1+dx,-dy); corner(-dx,1+dy); corner(1+dx,1+dy);
        } else {
            vertex(x0,y0,u0,v0); vertex(x0,y1,u0,v1); vertex(x1,y0,u1,v0);
            vertex(x1,y0,u1,v0); vertex(x0,y1,u0,v1); vertex(x1,y1,u1,v1);
        }
        // Runs of ordinary sprites usually share identity color even when their
        // textures differ. Avoid a 20-float tree search for every such item.
        const std::array<float,64>* params = item.params ? item.params.get() : &noParams;
        if (lastUniform == UINT32_MAX || lastColor != sprite.color || lastOptions != options || lastEffect != effect ||
            *lastParams != *params) {
            auto [color, inserted] = colors.try_emplace({sprite.color, options, effect, *params}, quint32(drawCount * d.stride));
            lastUniform = color->second; lastColor = sprite.color; lastOptions = options; lastEffect = effect;
            lastParams = params;
            if (inserted) {
                uniforms.resize(++drawCount * d.stride, 0);
                std::memcpy(uniforms.data()+lastUniform, sprite.color.data(), 80);
                std::memcpy(uniforms.data()+lastUniform+80,options.data(),16);
                std::memcpy(uniforms.data()+lastUniform+224,options.data()+4,16);
                std::memcpy(uniforms.data()+lastUniform+96, effect.data(), 48);
                if (item.params) std::memcpy(uniforms.data()+lastUniform+240, item.params->data(), 256);
                if (effect[0] == 9 || effect[0] == 10 ||
                    ((effect[0] == 3 || effect[0] == 4) && effect[11] > 0.5f)) {
                    const auto kernel = stockBlurKernel(effect[1]);
                    std::memcpy(uniforms.data()+lastUniform+144,kernel.taps.data(),80);
                }
            }
        }
        const quint32 offset = lastUniform;
        const int blend = item.alphaMask ? 5 : item.stockSubtract ? 6 : sprite.blend == 5 ? 7 :
                          sprite.blend >= 0 && sprite.blend <= 4 ? sprite.blend : 0;
        auto* descriptor = offscreen == 2 ? d.byteCapturePass.get() : offscreen ? d.offscreenPass.get() : d.finalPass;
        if (!d.pipeline(offscreen, blend, descriptor)) return false;
        if (!draws.empty() && draws.back().bindings == bindings && draws.back().uniform == offset &&
            draws.back().blend == blend && draws.back().vertex + draws.back().count == first)
            draws.back().count += 6;
        else draws.push_back({bindings, first, offset, blend});
        return true;
    };
    for (int i : plan.schedule) {
        const auto& pass = plan.passes[i];
        for (const auto& item : pass.items)
            if (!record(item, d.passDraws[i], pass.byteCapture ? 2 : 1, pass.bounds, pass.straightOutput)) return uploaded;
    }
    for (const auto& item : final) if (!record(item, d.finalDraws, false, source)) return uploaded;
    // Animation cells disappear from a single plan but return on the next cycle.
    // Keep a bounded idle texture working set instead of uploading every cycle.
    uint64_t idleBytes = 0;
    std::vector<decltype(d.textures)::iterator> idle;
    for (auto it = d.textures.begin(); it != d.textures.end(); ++it) if (it->second.used != d.textureEpoch) {
        idleBytes += it->second.image.sizeInBytes(); idle.push_back(it);
    }
    if (idleBytes > 64 * 1024 * 1024 || idle.size() > 4096) {
        std::sort(idle.begin(), idle.end(), [](auto a, auto b) { return a->second.used < b->second.used; });
        size_t idleCount = idle.size();
        for (auto it : idle) {
            if (idleBytes <= 64 * 1024 * 1024 && idleCount <= 4096) break;
            idleBytes -= it->second.image.sizeInBytes(); --idleCount; d.textures.erase(it);
        }
    }
    if (!vertices.empty()) {
        const quint32 bytes = quint32(vertices.size()*sizeof(Vertex));
        if (!d.vertices || d.vertices->size() < bytes) {
            d.vertices.reset(d.rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, bytes));
            if (!d.vertices->create()) return uploaded;
        }
        updates->updateDynamicBuffer(d.vertices.get(),0,bytes,vertices.data());
        updates->updateDynamicBuffer(d.uniforms.get(),0,drawCount*d.stride,uniforms.data());
    }
    d.stats.surfaces = storage.sizes.size() + 1; d.stats.passes = plan.livePasses;
    d.stats.surfaceBytes = storage.storagePixels*4; d.stats.workPixels = storage.workPixels;
    d.stats.draws = d.finalDraws.size();
    for (const auto& draws : d.passDraws) d.stats.draws += draws.size();
    d.prepared = d.dirty = true; return uploaded;
}
void SpriteRenderer::renderOffscreen(QRhiCommandBuffer* cb, QRhiResourceUpdateBatch* updates) {
    auto& d = *d_;
    cb->resourceUpdate(updates);
    if (!d.prepared || !d.dirty) return;
    for (int index : d.plan->schedule) {
        const auto& pass = d.plan->passes[index]; const auto& surface = d.surfaces[pass.surface];
        cb->beginPass(surface.target.get(), Qt::transparent, {1.0f,0});
        const auto size = pass.bounds.size();
        // QRhi viewports have a bottom-left origin even on top-down render
        // targets. Keep the logical image at texture UV origin on both APIs.
        const int y = d.rhi->isYUpInFramebuffer() ? 0 : surface.texture->pixelSize().height()-size.height();
        cb->setViewport(QRhiViewport(0,float(y),float(size.width()),float(size.height())));
        d.draw(cb,d.passDraws[index],pass.byteCapture ? 2 : 1); cb->endPass();
    }
}
void SpriteRenderer::draw(QRhiCommandBuffer* cb) {
    if (d_->prepared) d_->draw(cb,d_->finalDraws,false);
}
}
