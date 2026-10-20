#include "rendergpu/executor.h"
#include "rendergpu/timing.h"
#include "render/goexp.h"
#include <QFile>
#include <QHashFunctions>
#include <QRect>
#include <rhi/qrhi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace fugue::rendergpu {
namespace {

// Operation numbers match composite.frag. Filters operate on textures, never on CPU pixel buffers.
enum Operation { Blend, Black, Colour, HudBlit, Warp, Blur, OutlineSeed, OutlineStep, OutlineFinish,
                 ShadowSilhouette, ShadowFinish, SpriteCopy };
constexpr int kKernelRadius = 64;

struct RecordingProfiler {
    using Clock = std::chrono::steady_clock;
    Stats& stats;
    bool enabled;
    Clock::time_point wall{};
    double cpu = -1;
    RecordingProfiler(Stats& s, bool e) : stats(s), enabled(e) {
        if (enabled) { wall = Clock::now(); cpu = threadCpuMs(); }
    }
    void mark(size_t phase) {
        if (!enabled) return;
        const auto now = Clock::now();
        const double used = threadCpuMs();
        stats.recordWallMs[phase] = std::chrono::duration<double, std::milli>(now - wall).count();
        stats.recordCpuMs[phase] = cpuDelta(cpu, used);
        wall = now; cpu = used;
    }
    ~RecordingProfiler() { mark(4); } // declared first: include destruction of retired bindings/textures
};

// Matches the std140 block in composite.frag. The common prefix is used by every draw; only Blur and
// ShadowSilhouette read kernel. Active fields get disjoint aligned slices, even when QRhi defers uploads.
struct Params {
    std::array<int32_t, 4> placement{};
    std::array<float, 4> tint{255, 255, 255, 255};
    std::array<int32_t, 4> options{};
    std::array<float, 20> matrix{};
    std::array<float, 4> effect{};
    std::array<float, 4> extra{};
    std::array<int32_t, 4> controls{};
    std::array<float, 132> kernel{}; // 33 vec4s: Gaussian weights or integer shadow-dilation row extents
};
static_assert(sizeof(Params) == 704);
constexpr size_t kCommonParamsSize = offsetof(Params, kernel);
static_assert(kCommonParamsSize == 176);

size_t paramBytes(const Params& params, bool pack) {
    const int op = params.options[3];
    return !pack || op == Blur || op == ShadowSilhouette ? sizeof(Params) : kCommonParamsSize;
}

struct Surface {
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiRenderPassDescriptor> pass;
    std::unique_ptr<QRhiTextureRenderTarget> target;
};

// Keys retain only immutable plane inputs, never an entire historical DrawList/hit scene. Source
// pixels are compared by value (samePlanePass), not hashes. Both input and output storage are capped.
struct CachedPlane {
    render::PlanePass input;
    std::vector<uint64_t> dependencies; // resolved slate generations, in relay-layer order; 0 = missing
    std::unique_ptr<Surface> surface;
    uint64_t generation = 0, inputBytes = 0, passes = 0;
    bool ready = false;
};
constexpr uint64_t kPlaneCacheBytes = 32 * 1024 * 1024;

struct SpriteTexture;

// Four per-instance attributes; matrices deliberately stay on the existing per-draw shader.
struct SpriteInstance {
    std::array<float, 4> rect;   // clipped logical destination x0,y0,x1,y1
    std::array<float, 4> source; // logical placement x,y, atlas origin x,y
    std::array<float, 4> tint;
    std::array<float, 4> view;   // 2/width, 2/height, native framebuffer/NDC Y sign, unused
};
static_assert(sizeof(SpriteInstance) == 64);

struct AtlasCell {
    quint64 source = 0; // immutable sprite texture resource ID, never a recyclable pointer
    QRect rect;
};
struct SpriteAtlas {
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiShaderResourceBindings> bindings; // destroyed before texture
    std::vector<AtlasCell> cells;
};
constexpr size_t kMinInstances = 4, kMaxInstances = 1024;
constexpr int kAtlasEdge = 1024;

struct Draw {
    // Native handles let a plane's command range be retargeted to a retained attachment before encoding,
    // without altering earlier planes' commands or adding a copy/quantization pass.
    QRhiTextureRenderTarget* output = nullptr;
    QRhiTexture* attachment = nullptr;
    void setOutput(Surface* surface) { output = surface->target.get(); attachment = surface->texture.get(); }
    QRhiTexture* source = nullptr;
    QRhiTexture* destination = nullptr;
    Params params;
    QRhiShaderResourceBindings* bindings = nullptr;
    quint32 uniformOffset = 0;
    SpriteTexture* sprite = nullptr; // only direct cutouts; borrowed while planning this frame
    quint32 instanceOffset = 0, instanceCount = 0; // batch head only
    bool instanced = false; // head + following draws consumed by this batch; no individual uniforms
    bool clear = false;
    bool continuePass = false;
    bool scissored = false;
};

struct LayerBatch {
    Surface* output = nullptr;
    QRhiTexture* destination = nullptr; // immutable snapshot before the first layer in this pass
    // Exact pixel occupancy, horizontally packed into words. A rectangle list required O(n^2)
    // overlap tests and flushed after 256 entries, copying the entire viewport repeatedly when
    // zoomed out. This costs O(covered rows * covered words), independent of previous layer count.
    // Allocate lazily (opaque-only planes need none), and clear only words touched by this batch.
    int stride = 0;
    std::vector<uint64_t> dirty;
    std::vector<size_t> touched;

    void reset(QSize size) {
        if (!stride) {
            stride = (size.width() + 63) / 64;
            dirty.resize(size_t(stride) * size.height());
        }
        for (size_t index : touched) dirty[index] = 0;
        touched.clear();
    }
    template<class F> bool visit(const QRect& rect, F&& word) const {
        if (rect.isEmpty()) return false;
        const int first = rect.left() / 64, last = rect.right() / 64;
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            for (int x = first; x <= last; ++x) {
                uint64_t mask = ~uint64_t(0);
                if (x == first) mask &= ~uint64_t(0) << (rect.left() % 64);
                if (x == last) mask &= ~uint64_t(0) >> (63 - rect.right() % 64);
                if (word(size_t(y) * stride + x, mask)) return true;
            }
        }
        return false;
    }
    bool intersects(const QRect& rect) const {
        return visit(rect, [&](size_t index, uint64_t mask) { return (dirty[index] & mask) != 0; });
    }
    void add(const QRect& rect) {
        visit(rect, [&](size_t index, uint64_t mask) {
            if (!dirty[index]) touched.push_back(index);
            dirty[index] |= mask;
            return false;
        });
    }
};

struct SpriteTexture {
    std::unique_ptr<QRhiTexture> texture;
    int w = 0, h = 0;
    std::vector<uint8_t> pixels; // snapshot: no pointer identity or mutable aliases survive as cache keys
    bool binaryAlpha = true;
};

bool canCopy(bool binaryAlpha, const render::DrawLayer& layer) {
    if (layer.blend != render::kBlendDefault && layer.blend != render::kBlendOver) return false;
    if (layer.cmat) {
        const auto& m = *layer.cmat;
        // Only prove simple alpha columns. RGB can use the full matrix, including its constant row.
        // Other matrices retain the compatibility shader, even if this particular image happens to be binary.
        if (m[3] != 0 || m[7] != 0 || m[11] != 0) return false;
        if (m[15] == 0 && (m[19] <= 0 || m[19] >= 1)) return true; // constant transparent/opaque
        if (m[15] != 1 || m[19] != 0) return false;
    }
    return layer.alpha <= 0 || (binaryAlpha && layer.alpha >= 255);
}

bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

QShader shader(const char* path) {
    QFile file(QString::fromLatin1(path));
    require(file.open(QIODevice::ReadOnly), "cannot open GPU shader resource");
    QShader result = QShader::fromSerialized(file.readAll());
    require(result.isValid(), "invalid GPU shader package");
    return result;
}

// Reject offscreen layers before subtracting their coordinates in the shader; int32 extremes must not overflow.
bool intersects(int64_t x, int64_t y, int sw, int sh, int w, int h) {
    return x < w && y < h && x + sw > 0 && y + sh > 0;
}

bool validMatrix(const render::ColorMatrixPtr& matrix) {
    if (!matrix) return true;
    // Bound float32 intermediate arithmetic, not just coefficient conversion.
    return std::all_of(matrix->begin(), matrix->end(), [](double v) { return std::isfinite(v) && std::abs(v) <= 1e6; });
}

void setMatrix(Params& params, const render::ColorMatrixPtr& matrix) {
    params.options[2] = matrix ? 1 : 0;
    if (matrix) std::transform(matrix->begin(), matrix->end(), params.matrix.begin(), [](double v) { return float(v); });
}

void setMasterColour(Params& params, const render::PlaneMasterSpec& master) {
    if (master.cmat) setMatrix(params, master.cmat); // matrix replaces the legacy scalar sentinel
    else params.tint = {float(master.r), float(master.g), float(master.b), float(std::clamp(master.alpha, 0, 255))};
}

bool hasWarp(const render::PlaneMasterSpec& master) {
    return master.hasXform && master.xf != std::array<float, 6>{1, 0, 0, 0, 1, 0};
}

// The CPU filter uses this ladder without byte quantization between steps. Only its scalar parameters are
// calculated here; the alpha fields themselves stay on floating-point GPU surfaces.
std::vector<double> outlineSteps(double size) {
    if (size <= 1) return {size};
    std::vector<double> steps;
    double step = 1;
    for (;;) {
        if (2 * step >= size && step > 2) step = size * 0.5;
        steps.push_back(step);
        size -= step;
        step *= 1.75;
        if (size <= step) break;
    }
    steps.push_back(size);
    return steps;
}

Params blurParams(double sigma, bool horizontal) {
    Params p;
    p.options[3] = Blur;
    int radius = int(std::ceil(sigma * 3));
    p.controls = {radius, horizontal ? 1 : 0, 0, 0};
    std::array<double, 2 * kKernelRadius + 1> weights{};
    double sum = 0;
    double twoSig2 = 2 * sigma * sigma;
    for (int i = -radius; i <= radius; i++) {
        double v = gomath::exp(-double(i * i) / twoSig2);
        weights[size_t(i + radius)] = v;
        sum += v;
    }
    for (int i = 0; i <= 2 * radius; i++) p.kernel[size_t(i)] = float(weights[size_t(i)] / sum);
    return p;
}

// Invert in float64 like warpAboutCentre, then upload float32 coefficients. Degenerate matrices draw transparent.
bool warpParams(const render::PlaneMasterSpec& m, int w, int h, Params& p) {
    for (float v : m.xf) if (!std::isfinite(v) || std::abs(v) > 1e6f) return false;
    p.options[3] = Warp;
    double a = m.xf[0], b = m.xf[1], d = m.xf[3], e = m.xf[4];
    double det = a * e - b * d;
    if (det == 0) { p.controls[0] = 1; return true; }
    std::array<double, 4> inv{e / det, -b / det, -d / det, a / det};
    for (size_t i = 0; i < inv.size(); i++) {
        if (!std::isfinite(inv[i]) || std::abs(inv[i]) > 1e6) return false;
        p.effect[i] = float(inv[i]);
    }
    p.extra = {m.xf[2], -m.xf[5], float(w) / 2, float(h) / 2};
    return true;
}

}  // namespace

struct Executor::Impl {
    QRhi& rhi;
    Limits limits;
    Options options;
    Features features;
    Stats stats;
    // Borrowed handles only. Clear before each plan; retain capacity to avoid repeatedly allocating
    // and copying multi-megabyte Params arrays as a zoomed-out scene grows the vector every frame.
    std::vector<Draw> drawPlan;
    QSize size;
    // Scene ping-pong, then plane ping-pong. Named slates additionally retain their completed pixels.
    std::array<Surface, 4> surfaces;
    std::array<std::unique_ptr<Surface>, 2> shadowSurfaces;
    std::array<std::unique_ptr<Surface>, 2> outlineSurfaces;
    std::vector<std::unique_ptr<Surface>> transientSurfaces; // uncached slates/HUD canvases used by the last list
    std::vector<std::unique_ptr<CachedPlane>> planeCache; // indexed by position in the previous immutable list
    uint64_t nextPlaneGeneration = 0;
    bool recordComplete = false; // never reuse partially planned uploads/pixels after an exception
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiBuffer> layoutBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> layoutBindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    std::unique_ptr<QRhiGraphicsPipeline> outlinePipeline; // R32F alpha accumulation, without RGBA8 quantization
    std::unique_ptr<QRhiBuffer> instanceBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> instanceLayout;
    std::unique_ptr<QRhiGraphicsPipeline> instancePipeline;
    std::vector<std::unique_ptr<SpriteAtlas>> atlases; // only atlases used by the previous executed list
    std::unordered_multimap<size_t, std::unique_ptr<SpriteTexture>> sprites;
    std::vector<std::unique_ptr<QRhiBuffer>> uniforms;
    std::vector<std::unique_ptr<QRhiShaderResourceBindings>> bindings;
    // All draw bindings can share dynamic offsets, including compatibility/effect draws.
    // Resource IDs, not recyclable addresses, identify the page, source, destination and sampler.
    using BindingKey = std::array<quint64, 4>;
    std::map<BindingKey, std::unique_ptr<QRhiShaderResourceBindings>> cachedBindings;
    std::weak_ptr<const render::DrawList> validatedList; // only the explicitly immutable overloads use this
    uint64_t validatedTextureBytes = 0;

    Impl(QRhi& r, Limits l, Options o, Features f) : rhi(r), limits(l), options(o), features(f) {}

    bool supports(const render::DrawList& dl, std::string* error, uint64_t* textureBytes = nullptr) const {
        if (error) error->clear();
        int dim = std::min(limits.maxDimension, rhi.resourceLimit(QRhi::TextureSizeMax));
        if (dl.outW <= 0 || dl.outH <= 0 || dl.outW > dim || dl.outH > dim)
            return fail(error, "output dimensions exceed GPU limits or are not positive");
        if (limits.maxPlanes < 0 || dl.planes.size() > size_t(limits.maxPlanes))
            return fail(error, "plane count exceeds GPU limit");
        if (limits.maxHudPasses < 0 || dl.hud.size() > size_t(limits.maxHudPasses))
            return fail(error, "HUD pass count exceeds GPU limit");
        const uint64_t pixels = uint64_t(dl.outW) * uint64_t(dl.outH);
        uint64_t bytes = 0, samples = 0, layerPixels = 0;
        uint64_t passes = 1 + uint64_t(dl.groundBlack);
        size_t layers = 0, filters = 0, destinations = 0;
        bool outline = false, shadow = false;
        const int radiusLimit = std::clamp(limits.maxFilterRadius, 0, kKernelRadius);
        auto memory = [&](uint64_t n, const char* message) {
            if (n > limits.maxTextureBytes - bytes)
                return fail(error, std::string(message) + ": requested " + std::to_string(bytes + n) +
                                   " bytes, limit " + std::to_string(limits.maxTextureBytes));
            bytes += n;
            return true;
        };
        auto work = [&](uint64_t taps) {
            uint64_t n = pixels * taps;
            if (n > limits.maxFilterSamples - samples) return fail(error, "filter sample work exceeds GPU budget");
            samples += n;
            return true;
        };
        auto layerWork = [&](int64_t x, int64_t y, int w, int h, int width, int height) {
            const int64_t clippedW = std::max<int64_t>(0, std::min<int64_t>(width, x + w) - std::max<int64_t>(0, x));
            const int64_t clippedH = std::max<int64_t>(0, std::min<int64_t>(height, y + h) - std::max<int64_t>(0, y));
            const uint64_t n = uint64_t(clippedW) * uint64_t(clippedH);
            if (n > limits.maxLayerPixels - layerPixels)
                return fail(error, "layer pixel work exceeds GPU budget: requested " + std::to_string(layerPixels + n) +
                                   ", limit " + std::to_string(limits.maxLayerPixels));
            layerPixels += n;
            return true;
        };
        auto blur = [&](double sigma) {
            if (sigma <= 0) return true;
            if (sigma < 1e-6) return fail(error, "blur size exceeds GPU numeric limits");
            if (sigma > double(radiusLimit) / 3) return fail(error, "blur radius exceeds GPU limit");
            passes += 2;
            return work(uint64_t(2 * (2 * int(std::ceil(sigma * 3)) + 1)));
        };
        if (!memory(pixels * 16, "surface memory exceeds GPU budget")) return false;
        std::unordered_map<const render::Sprite*, bool> seen; // validated image and binary-alpha classification
        auto checkLayers = [&](const std::vector<render::DrawLayer>& ls, int width, int height, bool relays) {
            LayerBatch batch;
            bool snapshot = false;
            if (limits.maxLayers < 0 || ls.size() > size_t(limits.maxLayers) - layers)
                return fail(error, "layer count exceeds GPU limit: requested at least " +
                                   std::to_string(layers + ls.size()) + ", limit " + std::to_string(limits.maxLayers));
            layers += ls.size();
            for (const auto& l : ls) {
                if (l.blend > render::kBlendInset) return fail(error, "unknown GPU blend mode");
                if (!validMatrix(l.cmat)) return fail(error, "colour matrix exceeds GPU numeric limits");
                bool binary = false;
                if (l.sprite) {
                    auto [it, inserted] = seen.try_emplace(l.sprite.get(), true);
                    if (inserted) {
                        const auto& s = *l.sprite;
                        if (s.w <= 0 || s.h <= 0 || s.w > dim || s.h > dim)
                            return fail(error, "sprite dimensions exceed GPU limits or are not positive");
                        uint64_t n = uint64_t(s.w) * uint64_t(s.h) * 4;
                        if (n != s.px.size()) return fail(error, "sprite byte count does not match dimensions");
                        if (n > std::numeric_limits<quint32>::max()) return fail(error, "sprite upload exceeds GPU limit");
                        if (!memory(n, "sprite memory exceeds GPU budget")) return false;
                        for (size_t i = 3; i < s.px.size(); i += 4)
                            if (s.px[i] != 0 && s.px[i] != 255) { it->second = false; break; }
                    }
                    binary = it->second;
                }
                // Count physical passes, not logical sprite draws. This mirrors layer composition before
                // plane caching/fusion, so it remains an upper bound. Unknown slate footprints conservatively
                // start a pass and invalidate the snapshot; HUD layers ignore relay names, like recording.
                if (relays && !l.relay.empty()) {
                    if (!layerWork(l.x, int64_t(l.relayBottomY) - height, width, height, width, height)) return false;
                    ++passes; snapshot = false; continue;
                }
                if (!l.sprite || !intersects(l.x, l.y, l.sprite->w, l.sprite->h, width, height)) continue;
                if (!layerWork(l.x, l.y, l.sprite->w, l.sprite->h, width, height)) return false;
                if (!options.batchSprites) { ++passes; continue; }
                const int x = std::max(0, l.x), y = std::max(0, l.y);
                const QRect rect(x, y, int(std::min<int64_t>(width, int64_t(l.x) + l.sprite->w)) - x,
                                      int(std::min<int64_t>(height, int64_t(l.y) + l.sprite->h)) - y);
                if (canCopy(binary, l)) {
                    if (snapshot) batch.add(rect);
                } else {
                    if (!snapshot || batch.intersects(rect)) {
                        ++passes;
                        batch.reset(QSize(width, height));
                        snapshot = true;
                    }
                    batch.add(rect);
                }
            }
            return true;
        };
        for (const auto& plane : dl.planes) {
            passes += 2; // clear + scene composite (conservative even for empty/offscreen planes)
            if (!checkLayers(plane.layers, dl.outW, dl.outH, true)) return false;
            if (!plane.master) continue;
            const auto& m = *plane.master;
            if (m.blend > render::kBlendInset) return fail(error, "unknown GPU master blend mode");
            if (!validMatrix(m.cmat)) return fail(error, "master colour matrix exceeds GPU numeric limits");
            if (m.hasColor || m.cmat) ++passes;
            if (!m.renderTarget.empty()) {
                ++passes;
                if (!memory(pixels * 4, "slate memory exceeds GPU budget")) return false;
            }
            if (m.hasXform) {
                Params p;
                if (!warpParams(m, dl.outW, dl.outH, p)) return fail(error, "master transform exceeds GPU numeric limits");
                ++passes;
            }
            if (limits.maxFilters < 0 || m.filters.size() > size_t(limits.maxFilters) - filters)
                return fail(error, "filter count exceeds GPU limit");
            filters += m.filters.size();
            for (const auto& f : m.filters) {
                if (!std::isfinite(f.size)) return fail(error, "filter size exceeds GPU numeric limits");
                switch (f.kind) {
                case render::FilterSpec::Blur:
                    if (!blur(f.size)) return false;
                    break;
                case render::FilterSpec::Outline:
                    if (f.size <= 0 || f.alpha == 0) break;
                    if (f.size > radiusLimit) return fail(error, "outline radius exceeds GPU limit");
                    outline = true;
                    passes += outlineSteps(f.size).size() + 2;
                    if (!work(outlineSteps(f.size).size() * 36 + 3)) return false;
                    break;
                case render::FilterSpec::DropShadow: {
                    if (!std::isfinite(f.offset)) return fail(error, "shadow offset exceeds GPU numeric limits");
                    if (f.offset > radiusLimit) return fail(error, "shadow radius exceeds GPU limit");
                    shadow = true;
                    int grow = f.offset > 0 ? int(std::ceil(f.offset)) : 0;
                    passes += 2;
                    if (!work(uint64_t((2 * grow + 1) * (2 * grow + 1)) + 2) || !blur(f.size)) return false;
                    break;
                }
                default: return fail(error, "unknown GPU filter kind");
                }
            }
        }
        if (shadow && !memory(pixels * 8, "shadow scratch memory exceeds GPU budget")) return false;
        if (outline) {
            if (!rhi.isTextureFormatSupported(QRhiTexture::R32F, QRhiTexture::RenderTarget))
                return fail(error, "GPU device lacks R32F outline render targets");
            if (!memory(pixels * 8, "outline scratch memory exceeds GPU budget")) return false;
        }
        for (const auto& h : dl.hud) {
            if (h.tileW <= 0 || h.tileH <= 0) continue; // CPU ignores empty canvases, including their layers/destinations
            if (h.tileW > dim || h.tileH > dim) return fail(error, "HUD dimensions exceed GPU limit");
            if (limits.maxHudDestinations < 0 || h.dests.size() > size_t(limits.maxHudDestinations) - destinations)
                return fail(error, "HUD destination count exceeds GPU limit");
            destinations += h.dests.size();
            passes += 1 + h.dests.size();
            if (!checkLayers(h.layers, h.tileW, h.tileH, false)) return false;
            if (!memory(uint64_t(h.tileW) * uint64_t(h.tileH) * 8, "HUD canvas memory exceeds GPU budget")) return false;
        }
        if (limits.maxRenderPasses < 0 || passes > uint64_t(limits.maxRenderPasses))
            return fail(error, "render pass count exceeds GPU limit");
        if (textureBytes) *textureBytes = bytes;
        return true;
    }

    std::unique_ptr<QRhiShaderResourceBindings> makeBindings(QRhiBuffer* buffer, QRhiTexture* src, QRhiTexture* dst) {
        auto b = std::unique_ptr<QRhiShaderResourceBindings>(rhi.newShaderResourceBindings());
        b->setBindings({
            QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(0, QRhiShaderResourceBinding::FragmentStage,
                                                                      buffer, sizeof(Params)),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, src, sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, dst, sampler.get())
        });
        require(b->create(), "cannot create GPU shader bindings");
        ++stats.createdBindings;
        return b;
    }

    void createSurface(Surface& s, QSize wanted, QRhiTexture::Format format = QRhiTexture::RGBA8) {
        // Destroy attachments before their textures and pass descriptors.
        s.target.reset(); s.pass.reset(); s.texture.reset();
        s.texture.reset(rhi.newTexture(format, wanted, 1,
                                      QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        require(s.texture->create(), "cannot create GPU surface texture");
        s.target.reset(rhi.newTextureRenderTarget(QRhiTextureRenderTargetDescription(s.texture.get())));
        s.pass.reset(s.target->newCompatibleRenderPassDescriptor());
        s.target->setRenderPassDescriptor(s.pass.get());
        require(s.target->create(), "cannot create GPU render target");
        ++stats.createdSurfaces;
    }

    std::unique_ptr<QRhiGraphicsPipeline> makePipeline(QRhiRenderPassDescriptor* pass) {
        auto p = std::unique_ptr<QRhiGraphicsPipeline>(rhi.newGraphicsPipeline());
        p->setShaderStages({{QRhiShaderStage::Vertex, shader(":/fugue/gpu/composite.vert.qsb")},
                            {QRhiShaderStage::Fragment, shader(":/fugue/gpu/composite.frag.qsb")}});
        p->setVertexInputLayout(QRhiVertexInputLayout{});
        p->setShaderResourceBindings(layoutBindings.get());
        p->setRenderPassDescriptor(pass);
        // Hardware blending stays disabled: compatibility draws read the previous surface. Cutout draws
        // replace opaque pixels and discard transparent ones, preserving straight-alpha byte rounding.
        p->setFlags(QRhiGraphicsPipeline::UsesScissor);
        require(p->create(), "cannot create GPU composition pipeline");
        return p;
    }

    void ensureSurfaces(QSize wanted) {
        if (pipeline && size == wanted) return;
        atlases.clear();
        instancePipeline.reset(); instanceLayout.reset();
        outlinePipeline.reset();
        for (auto& s : outlineSurfaces) s.reset();
        for (auto& s : shadowSurfaces) s.reset();
        pipeline.reset();
        layoutBindings.reset();
        layoutBuffer.reset();
        sampler.reset();
        for (auto& s : surfaces) createSurface(s, wanted);
        sampler.reset(rhi.newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                     QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        require(sampler->create(), "cannot create GPU sampler");
        layoutBuffer.reset(rhi.newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Params)));
        require(layoutBuffer->create(), "cannot create GPU layout buffer");
        // This binding set defines only the layout. Every draw uses its own bindings and uniform slice.
        layoutBindings = makeBindings(layoutBuffer.get(), surfaces[0].texture.get(), surfaces[1].texture.get());
        pipeline = makePipeline(surfaces[0].pass.get());
        size = wanted;
    }

    void ensureInstancePipeline() {
        if (instancePipeline) return;
        auto layout = std::unique_ptr<QRhiShaderResourceBindings>(rhi.newShaderResourceBindings());
        layout->setBindings({QRhiShaderResourceBinding::sampledTexture(
            0, QRhiShaderResourceBinding::FragmentStage, nullptr, nullptr)});
        require(layout->create(), "cannot create GPU instance binding layout");
        ++stats.createdBindings;
        auto p = std::unique_ptr<QRhiGraphicsPipeline>(rhi.newGraphicsPipeline());
        p->setShaderStages({{QRhiShaderStage::Vertex, shader(":/fugue/gpu/sprites.vert.qsb")},
                                          {QRhiShaderStage::Fragment, shader(":/fugue/gpu/sprites.frag.qsb")}});
        QRhiVertexInputLayout input;
        input.setBindings({QRhiVertexInputBinding(sizeof(SpriteInstance), QRhiVertexInputBinding::PerInstance)});
        input.setAttributes({
            {0, 0, QRhiVertexInputAttribute::Float4, offsetof(SpriteInstance, rect)},
            {0, 1, QRhiVertexInputAttribute::Float4, offsetof(SpriteInstance, source)},
            {0, 2, QRhiVertexInputAttribute::Float4, offsetof(SpriteInstance, tint)},
            {0, 3, QRhiVertexInputAttribute::Float4, offsetof(SpriteInstance, view)}
        });
        p->setVertexInputLayout(input);
        p->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        p->setCullMode(QRhiGraphicsPipeline::None);
        p->setFlags(QRhiGraphicsPipeline::UsesScissor);
        p->setShaderResourceBindings(layout.get());
        p->setRenderPassDescriptor(surfaces[0].pass.get());
        require(p->create(), "cannot create GPU sprite instance pipeline");
        instanceLayout = std::move(layout);
        instancePipeline = std::move(p);
    }

    void batchInstances(std::vector<Draw>& draws, QRhiResourceUpdateBatch& updates, uint64_t budget) {
        auto previous = std::move(atlases);
        atlases.clear();
        if (!features.instanceSprites || !options.batchSprites || !rhi.isFeatureSupported(QRhi::Instancing) ||
            rhi.resourceLimit(QRhi::MaxVertexInputs) < 4 || rhi.resourceLimit(QRhi::MaxVertexOutputs) < 2) return;
        // Duplicate atlas storage is optional, bounded by BOTH spare preflight budget and a fixed cap.
        budget = std::min<uint64_t>(budget, 32 * 1024 * 1024);
        const int edge = std::min({kAtlasEdge, limits.maxDimension, rhi.resourceLimit(QRhi::TextureSizeMax)});
        const float ySign = rhi.isYUpInFramebuffer() == rhi.isYUpInNDC() ? 1.f : -1.f;
        std::vector<SpriteInstance> instances;
        auto eligible = [](const Draw& d) {
            return d.sprite && d.params.options[3] == SpriteCopy && !d.params.options[2] && d.continuePass;
        };
        for (size_t first = 0; first < draws.size();) {
            if (!eligible(draws[first])) { ++first; continue; }
            auto* output = draws[first].output;
            std::vector<std::pair<SpriteTexture*, QRect>> cells;
            std::vector<size_t> cellIndices;
            std::unordered_map<quint64, size_t> slotBySource;
            int x = 0, y = 0, rowHeight = 0, width = 0, height = 0;
            size_t end = first;
            for (; end < draws.size() && end - first < kMaxInstances; ++end) {
                const auto& d = draws[end];
                if (!eligible(d) || d.output != output) break; // NEVER cross a compatibility draw or pass boundary
                auto* sprite = d.sprite;
                const auto id = sprite->texture->globalResourceId();
                auto found = slotBySource.find(id);
                if (found != slotBySource.end()) {
                    cellIndices.push_back(found->second);
                    continue;
                }
                if (sprite->w > edge || sprite->h > edge) break;
                int nx = x, ny = y, nr = rowHeight;
                if (nx + sprite->w > edge) { nx = 0; ny += nr; nr = 0; }
                if (ny + sprite->h > edge) break;
                const int nw = std::max(width, nx + sprite->w), nh = std::max(height, ny + sprite->h);
                if (uint64_t(nw) * uint64_t(nh) * 4 > budget) break;
                cellIndices.push_back(cells.size());
                slotBySource.emplace(id, cells.size());
                cells.emplace_back(sprite, QRect(nx, ny, sprite->w, sprite->h));
                x = nx + sprite->w; y = ny; rowHeight = std::max(nr, sprite->h);
                width = nw; height = nh;
            }
            if (end - first < kMinInstances) {
                first = std::max(first + 1, end); // tiny/oversized/budget-limited runs keep the old shader
                continue;
            }
            ensureInstancePipeline();
            const QSize extent(width, height);
            auto found = std::find_if(previous.begin(), previous.end(), [&](const auto& a) {
                return a && a->texture->pixelSize() == extent;
            });
            std::unique_ptr<SpriteAtlas> atlas;
            if (found != previous.end()) atlas = std::move(*found);
            else {
                atlas = std::make_unique<SpriteAtlas>();
                atlas->texture.reset(rhi.newTexture(QRhiTexture::RGBA8, extent));
                require(atlas->texture->create(), "cannot create GPU sprite atlas");
                atlas->bindings.reset(rhi.newShaderResourceBindings());
                atlas->bindings->setBindings({QRhiShaderResourceBinding::sampledTexture(
                    0, QRhiShaderResourceBinding::FragmentStage, atlas->texture.get(), sampler.get())});
                require(atlas->bindings->create(), "cannot create GPU sprite atlas bindings");
                ++stats.createdBindings;
            }
            std::vector<AtlasCell> nextCells;
            for (size_t i = 0; i < cells.size(); ++i) {
                const auto& [sprite, rect] = cells[i];
                const auto id = sprite->texture->globalResourceId();
                if (i >= atlas->cells.size() || atlas->cells[i].source != id || atlas->cells[i].rect != rect) {
                    QRhiTextureSubresourceUploadDescription data(sprite->pixels.data(), quint32(sprite->pixels.size()));
                    data.setSourceSize(QSize(sprite->w, sprite->h));
                    data.setDestinationTopLeft(rect.topLeft());
                    updates.uploadTexture(atlas->texture.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, data)));
                    ++stats.atlasUploads;
                    stats.uploadedAtlasBytes += sprite->pixels.size();
                }
                nextCells.push_back({id, rect});
            }
            atlas->cells = std::move(nextCells);
            require(instances.size() + end - first <= size_t(std::numeric_limits<int>::max()) / sizeof(SpriteInstance),
                    "GPU instance buffer exceeds size limits");
            auto& head = draws[first];
            head.instanceOffset = quint32(instances.size() * sizeof(SpriteInstance));
            head.instanceCount = quint32(end - first);
            head.bindings = atlas->bindings.get();
            const QSize target = output->pixelSize();
            for (size_t i = first; i < end; ++i) {
                auto& d = draws[i];
                d.instanced = true;
                const auto& p = d.params.placement;
                const QRect& cell = cells[cellIndices[i - first]].second;
                SpriteInstance instance;
                instance.rect = {float(std::max(0, p[0])), float(std::max(0, p[1])),
                                 float(std::min(target.width(), p[0] + p[2])),
                                 float(std::min(target.height(), p[1] + p[3]))};
                instance.source = {float(p[0]), float(p[1]), float(cell.x()), float(cell.y())};
                instance.tint = d.params.tint;
                instance.view = {2.f / target.width(), 2.f / target.height(), ySign, 0};
                instances.push_back(instance);
            }
            const uint64_t bytes = uint64_t(width) * uint64_t(height) * 4;
            budget -= bytes;
            stats.atlasBytes += bytes;
            atlases.push_back(std::move(atlas));
            first = end;
        }
        if (!instances.empty()) {
            const quint32 bytes = quint32(instances.size() * sizeof(SpriteInstance)); // checked before adding instances
            if (!instanceBuffer || instanceBuffer->size() < bytes) {
                instanceBuffer.reset(rhi.newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, bytes));
                require(instanceBuffer->create(), "cannot create GPU sprite instance buffer");
            }
            updates.updateDynamicBuffer(instanceBuffer.get(), 0, quint32(bytes), instances.data());
            stats.uploadedInstanceBytes = uint64_t(bytes);
        }
    }

    QRhiTexture* record(const render::DrawList& dl, QRhiCommandBuffer& cb, uint64_t textureBytes, bool immutable = false) {
        RecordingProfiler profiler(stats, options.profileRecording);
        drawPlan.clear();
        if (!options.reuseResources) std::vector<Draw>().swap(drawPlan);
        // QRhi defers native resource destruction when prior frames still reference them.
        bindings.clear();
        if (!recordComplete) {
            cachedBindings.clear();
            planeCache.clear(); atlases.clear(); sprites.clear();
        }
        recordComplete = false;
        const QSize wanted(dl.outW, dl.outH);
        const bool cachePlanes = features.cachePlanes && immutable && options.reuseResources;
        const bool reusePlanes = cachePlanes && size == wanted;
        // Destroy cached bindings before changing any sampler, buffer or texture they reference.
        if (!options.reuseResources || !options.cacheSprites || !pipeline || size != wanted) cachedBindings.clear();
        if (!options.reuseResources) { uniforms.clear(); transientSurfaces.clear(); instanceBuffer.reset(); }
        if (!options.reuseResources || !options.cacheSprites) atlases.clear();
        if (!options.cacheSprites) sprites.clear();
        // Keep only textures/bindings used by the new list. Declaration order ensures unused old bindings
        // are destroyed before unused old textures, including on exception. Both texture generations are
        // bounded by supports()'s per-list budget; uniform pages retain a bounded high-water capacity.
        auto previousSprites = std::move(sprites);
        auto previousSurfaces = std::move(transientSurfaces);
        auto previousPlanes = std::move(planeCache); // must outlive previousBindings, including on exceptions
        auto previousBindings = std::move(cachedBindings);
        sprites.clear(); transientSurfaces.clear(); cachedBindings.clear(); planeCache.clear();
        if (cachePlanes) planeCache.resize(dl.planes.size());
        ensureSurfaces(wanted);
        // Match surfaces by size/format, not slate name or HUD order. Every reused attachment is
        // cleared or fully overwritten by the draw plan; this caches allocations, never scene pixels.
        auto acquireOwnedSurface = [&](QSize extent) {
            auto found = std::find_if(previousSurfaces.begin(), previousSurfaces.end(), [&](const auto& s) {
                return s && s->texture->pixelSize() == extent && s->texture->format() == QRhiTexture::RGBA8;
            });
            std::unique_ptr<Surface> s;
            if (options.reuseResources && found != previousSurfaces.end()) {
                s = std::move(*found);
            } else {
                s = std::make_unique<Surface>();
                createSurface(*s, extent);
            }
            return s;
        };
        auto acquireSurface = [&](QSize extent) {
            auto s = acquireOwnedSurface(extent);
            auto* result = s.get();
            transientSurfaces.push_back(std::move(s));
            return result;
        };
        bool outline = false, shadow = false;
        for (const auto& plane : dl.planes) if (plane.master) {
            for (const auto& f : plane.master->filters) {
                outline |= f.kind == render::FilterSpec::Outline && f.size > 0 && f.alpha != 0;
                shadow |= f.kind == render::FilterSpec::DropShadow;
            }
        }
        if (!outline) outlinePipeline.reset();
        auto scratch = [&](auto& pair, bool needed, QRhiTexture::Format format) {
            for (auto& s : pair) {
                // Old bindings may still refer to this attachment. Retire them before its wrapper.
                if (!needed) {
                    if (s) previousSurfaces.push_back(std::move(s));
                } else if (!s) {
                    auto created = std::make_unique<Surface>();
                    createSurface(*created, size, format);
                    s = std::move(created);
                }
            }
        };
        scratch(shadowSurfaces, shadow, QRhiTexture::RGBA8);
        scratch(outlineSurfaces, outline, QRhiTexture::R32F);
        if (outline && !outlinePipeline) outlinePipeline = makePipeline(outlineSurfaces[0]->pass.get());
        profiler.mark(0);
        auto release = [](QRhiResourceUpdateBatch* b) { b->release(); };
        std::unique_ptr<QRhiResourceUpdateBatch, decltype(release)> updates(rhi.nextResourceUpdateBatch(), release);
        std::unordered_map<const render::Sprite*, SpriteTexture*> uploaded;
        auto upload = [&](const render::Sprite& s) -> SpriteTexture* {
            auto it = uploaded.find(&s);
            if (it != uploaded.end()) return it->second;
            const size_t hash = qHashBits(s.px.data(), s.px.size());
            auto findContent = [&](auto& cache) {
                auto range = cache.equal_range(hash);
                auto match = std::find_if(range.first, range.second, [&](const auto& entry) {
                    const auto& t = *entry.second;
                    return t.w == s.w && t.h == s.h && t.pixels == s.px;
                });
                return match == range.second ? cache.end() : match;
            };
            auto current = findContent(sprites);
            if (current != sprites.end()) {
                uploaded.emplace(&s, current->second.get());
                return current->second.get();
            }
            std::unique_ptr<SpriteTexture> t;
            auto previous = findContent(previousSprites);
            if (previous != previousSprites.end()) {
                t = std::move(previous->second);
                previousSprites.erase(previous);
                ++stats.reusedSprites;
            } else {
                t = std::make_unique<SpriteTexture>();
                t->w = s.w; t->h = s.h; t->pixels = s.px;
                for (size_t i = 3; i < s.px.size(); i += 4) {
                    if (s.px[i] != 0 && s.px[i] != 255) { t->binaryAlpha = false; break; }
                }
                t->texture.reset(rhi.newTexture(QRhiTexture::RGBA8, QSize(s.w, s.h)));
                require(t->texture->create(), "cannot create GPU sprite texture");
                QRhiTextureSubresourceUploadDescription data(s.px.data(), quint32(s.px.size()));
                data.setSourceSize(QSize(s.w, s.h));
                updates->uploadTexture(t->texture.get(), QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, data)}));
                ++stats.uploadedSprites;
                stats.uploadedBytes += s.px.size();
            }
            auto* result = t.get();
            uploaded.emplace(&s, result);
            sprites.emplace(hash, std::move(t));
            return result;
        };
        auto& draws = drawPlan;
        auto clear = [&](Surface* surface) {
            Draw d;
            d.setOutput(surface);
            d.clear = true;
            draws.push_back(d);
        };
        auto composite = [&](Surface* output, QRhiTexture* src, QRhiTexture* dest, Params params) {
            Draw d;
            d.setOutput(output);
            d.source = src;
            d.destination = dest;
            d.params = params;
            draws.push_back(d);
        };
        auto spriteCopy = [&](Surface* output, SpriteTexture* sprite, const render::DrawLayer& layer, Params params) {
            if (!options.batchSprites || !sprite || !canCopy(sprite->binaryAlpha, layer)) return false;
            // Layer composition starts clear and preserves RGB=0 wherever destination alpha=0. Thus discarding
            // a zero-alpha texel is equivalent to the CPU's transparent-RGB cleanup. Do NOT use this after
            // filters/master colour/warps (those can leave RGB under alpha=0), or for finished-plane/HUD blits.
            params.options[3] = SpriteCopy;
            Draw d;
            d.setOutput(output);
            d.source = sprite->texture.get();
            d.sprite = sprite;
            d.destination = d.source; // unused binding must still not alias the output attachment
            d.params = params;
            d.continuePass = !draws.empty() && draws.back().output == output->target.get();
            d.scissored = true;
            require(d.continuePass, "GPU sprite batch has no initialized surface");
            draws.push_back(d);
            return true;
        };
        auto composeLayer = [&](Surface* current, Surface* next, QRhiTexture* src, SpriteTexture* sprite,
                                const render::DrawLayer& layer, Params params, LayerBatch& batch) {
            const auto& p = params.placement;
            const QSize extent = current->texture->pixelSize();
            int x = std::max(0, p[0]), y = std::max(0, p[1]);
            QRect rect(x, y, int(std::min<int64_t>(extent.width(), int64_t(p[0]) + p[2])) - x,
                            int(std::min<int64_t>(extent.height(), int64_t(p[1]) + p[3])) - y);
            if (spriteCopy(current, sprite, layer, params)) {
                // Direct overwrites can overlap, but subsequent snapshot-based draws must avoid them too.
                if (batch.output == current) batch.add(rect);
                return false;
            }
            if (options.batchSprites && batch.output == current && !batch.intersects(rect)) {
                // The first layer in this pass copied the entire previous surface. In a region untouched
                // since then, that immutable snapshot is still the correct destination for ANY blend mode.
                // Keep order and byte quantization; never sample the attachment being written.
                Draw d;
                d.setOutput(current); d.source = src; d.destination = batch.destination; d.params = params;
                d.continuePass = true; d.scissored = true;
                draws.push_back(d);
                batch.add(rect);
                return false;
            }
            composite(next, src, current->texture.get(), params);
            batch.output = next;
            batch.destination = current->texture.get();
            if (options.batchSprites) {
                batch.reset(extent);
                batch.add(rect);
            }
            return true;
        };
        struct Slate { Surface* surface; uint64_t generation; };
        std::unordered_map<std::string, Slate> slates;
        uint64_t extraPlaneBytes = 0;
        const uint64_t surfaceBytes = uint64_t(dl.outW) * uint64_t(dl.outH) * 4;
        auto preparePlane = [&](size_t index, const render::PlanePass& p, bool slate) -> CachedPlane* {
            ++stats.planes;
            if (!cachePlanes || surfaceBytes > kPlaneCacheBytes - stats.planeCacheBytes ||
                (!slate && surfaceBytes > limits.maxTextureBytes - textureBytes - extraPlaneBytes)) return nullptr;
            std::vector<uint64_t> dependencies;
            for (const auto& l : p.layers) if (!l.relay.empty()) {
                auto source = slates.find(l.relay);
                dependencies.push_back(source == slates.end() ? 0 : source->second.generation);
            }
            CachedPlane* old = reusePlanes && index < previousPlanes.size() ? previousPlanes[index].get() : nullptr;
            const bool hit = old && old->ready && old->dependencies == dependencies && render::samePlanePass(old->input, p);
            uint64_t inputBytes = hit ? old->inputBytes : 0;
            if (!hit) {
                // Account for retained allocations, not just visible pixels. A tiny sprite can carry a
                // much larger reserved vector; masters likewise retain their filter/string capacities.
                inputBytes = sizeof(CachedPlane) + p.layers.size() * sizeof(render::DrawLayer);
                if (p.master) {
                    inputBytes += sizeof(render::PlaneMasterSpec) + p.master->renderTarget.capacity() + 1 +
                                  p.master->filters.capacity() * sizeof(render::FilterSpec);
                    if (p.master->cmat) inputBytes += sizeof(render::ColorMatrix);
                }
                for (const auto& l : p.layers) {
                    inputBytes += l.relay.size() + 1; // layer/string values are copied, not the original capacities
                    if (l.cmat) inputBytes += sizeof(render::ColorMatrix);
                    // Deliberately count repeated sources per layer. This ALSO bounds samePlanePass's
                    // worst-case byte comparisons across all old entries to the same 32 MiB budget.
                    if (l.sprite) inputBytes += sizeof(render::Sprite) + l.sprite->px.capacity();
                }
                inputBytes += dependencies.capacity() * sizeof(uint64_t);
            }
            if (inputBytes > kPlaneCacheBytes - stats.planeCacheInputBytes) return nullptr;
            auto& entry = planeCache[index];
            if (hit) {
                entry = std::move(previousPlanes[index]);
                ++stats.planeCacheHits;
                stats.filteredPlaneCacheHits += p.master && !p.master->filters.empty();
                stats.avoidedPlanePasses += entry->passes;
            } else {
                entry = std::make_unique<CachedPlane>();
                entry->input = p; // only this overload promises transitively immutable dependencies
                entry->dependencies = std::move(dependencies);
                entry->inputBytes = inputBytes;
                entry->generation = ++nextPlaneGeneration;
                if (old) entry->surface = std::move(old->surface);
            }
            ++stats.cachedPlanes;
            stats.planeCacheBytes += surfaceBytes; // includes named slates already reserved in preflight
            stats.planeCacheInputBytes += inputBytes;
            if (!slate) extraPlaneBytes += surfaceBytes;
            return entry.get();
        };
        auto finishPlane = [&](CachedPlane* entry, size_t firstDraw) {
            if (!entry) return;
            for (size_t i = firstDraw; i < draws.size(); ++i) entry->passes += !draws[i].continuePass;
            entry->ready = true;
        };
        auto fuseMasterColour = [&](const render::PlanePass& p) {
            // Colour must remain AFTER filters and BEFORE resampling. Without a warp, its quantized
            // bytes can feed the final plane blend (or slate copy) directly instead of an RGBA8 roundtrip.
            return features.fusePasses && p.master && (p.master->hasColor || p.master->cmat) && !hasWarp(*p.master);
        };
        auto composePlane = [&](const render::PlanePass& p) {
            int plane = 2;
            clear(&surfaces[size_t(plane)]);
            LayerBatch batch;
            for (const auto& l : p.layers) {
                QRhiTexture* src = nullptr;
                SpriteTexture* sprite = nullptr;
                int sw, sh;
                int64_t x = l.x, y = l.y;
                if (!l.relay.empty()) {
                    auto it = slates.find(l.relay);
                    if (it == slates.end()) continue;
                    src = it->second.surface->texture.get();
                    sw = dl.outW; sh = dl.outH;
                    y = int64_t(l.relayBottomY) - sh;
                } else {
                    if (!l.sprite) continue;
                    sw = l.sprite->w; sh = l.sprite->h;
                }
                if (!intersects(x, y, sw, sh, dl.outW, dl.outH)) continue;
                if (!src) {
                    sprite = upload(*l.sprite);
                    src = sprite->texture.get();
                }
                Params params;
                params.placement = {int32_t(x), int32_t(y), sw, sh};
                if (l.hasTint) params.tint = {float(l.tintR), float(l.tintG), float(l.tintB), 255};
                params.tint[3] = float(std::clamp(l.alpha, 0, 255));
                params.options[0] = l.blend;
                setMatrix(params, l.cmat);
                if (composeLayer(&surfaces[size_t(plane)], &surfaces[size_t(5 - plane)], src, sprite, l, params, batch))
                    plane = 5 - plane;
            }
            if (p.master) for (const auto& f : p.master->filters) {
                auto* original = surfaces[size_t(plane)].texture.get();
                if (f.kind == render::FilterSpec::Blur) {
                    if (f.size <= 0) continue;
                    for (bool horizontal : {true, false}) {
                        auto* src = surfaces[size_t(plane)].texture.get();
                        composite(&surfaces[size_t(5 - plane)], src, src, blurParams(f.size, horizontal));
                        plane = 5 - plane;
                    }
                } else if (f.kind == render::FilterSpec::Outline) {
                    if (f.size <= 0 || f.alpha == 0) continue;
                    Params params;
                    params.options[3] = OutlineSeed;
                    composite(outlineSurfaces[0].get(), original, original, params);
                    int alpha = 0;
                    for (double t : outlineSteps(f.size)) {
                        params = Params{};
                        params.options[3] = OutlineStep;
                        params.effect = {float(t), float((f.flags & 2) ? t : t * 0.70710677), float(double(f.alpha) / 255), 0};
                        params.controls[0] = (f.flags & 1) ? 1 : 0;
                        composite(outlineSurfaces[size_t(1 - alpha)].get(), outlineSurfaces[size_t(alpha)]->texture.get(),
                                  original, params);
                        alpha = 1 - alpha;
                    }
                    params = Params{};
                    params.options[3] = OutlineFinish;
                    params.tint = {float(f.r), float(f.g), float(f.b), 255};
                    composite(&surfaces[size_t(5 - plane)], original, outlineSurfaces[size_t(alpha)]->texture.get(), params);
                    plane = 5 - plane;
                } else if (f.kind == render::FilterSpec::DropShadow) {
                    Params params;
                    params.options[3] = ShadowSilhouette;
                    params.tint = {float(f.r), float(f.g), float(f.b), float(f.alpha)};
                    int radius = f.offset > 0 ? int(std::ceil(f.offset)) : 0;
                    params.controls[0] = radius;
                    // Derive disk membership in float64 like maxAlphaWithin. Squaring a float32 radius in the
                    // shader can cross an integer distance boundary and incorrectly grow a whole extra pixel.
                    double distanceSquared = f.offset > 0 ? f.offset * f.offset : 0;
                    for (int y = -radius; y <= radius; y++) {
                        int extent = -1;
                        for (int x = 0; x <= radius && double(x * x + y * y) <= distanceSquared; x++) extent = x;
                        params.kernel[size_t(y + radius)] = float(extent);
                    }
                    composite(shadowSurfaces[0].get(), original, original, params);
                    int shadow = 0;
                    if (f.size > 0) for (bool horizontal : {true, false}) {
                        auto* src = shadowSurfaces[size_t(shadow)]->texture.get();
                        composite(shadowSurfaces[size_t(1 - shadow)].get(), src, src, blurParams(f.size, horizontal));
                        shadow = 1 - shadow;
                    }
                    params = Params{};
                    params.options[3] = ShadowFinish;
                    params.placement = {f.x, f.y, dl.outW, dl.outH};
                    // Entirely offscreen offsets must not reach int32 subtraction in the shader.
                    params.controls[0] = intersects(f.x, f.y, dl.outW, dl.outH, dl.outW, dl.outH) ? 0 : 1;
                    composite(&surfaces[size_t(5 - plane)], original, shadowSurfaces[size_t(shadow)]->texture.get(), params);
                    plane = 5 - plane;
                }
            }
            if (p.master && (p.master->hasColor || p.master->cmat) && !fuseMasterColour(p)) {
                const auto& m = *p.master;
                Params params;
                params.placement = {0, 0, dl.outW, dl.outH};
                params.options[3] = Colour; // NOT alpha-over onto a fresh surface
                setMasterColour(params, m);
                auto* src = surfaces[size_t(plane)].texture.get();
                composite(&surfaces[size_t(5 - plane)], src, src, params);
                plane = 5 - plane;
            }
            if (p.master && hasWarp(*p.master)) {
                Params params;
                require(warpParams(*p.master, dl.outW, dl.outH, params), "master transform exceeds GPU numeric limits");
                auto* src = surfaces[size_t(plane)].texture.get();
                composite(&surfaces[size_t(5 - plane)], src, src, params);
                plane = 5 - plane;
            }
            return plane;
        };
        auto isSlate = [](const render::PlanePass& p) { return p.master && !p.master->renderTarget.empty(); };
        // As on the CPU, target planes run once in ascending order. A target can reference an earlier target.
        for (size_t i = 0; i < dl.planes.size(); ++i) {
            const auto& p = dl.planes[i];
            if (!isSlate(p)) continue;
            auto* entry = preparePlane(i, p, true);
            if (entry && entry->ready) {
                slates[p.master->renderTarget] = {entry->surface.get(), entry->generation};
                continue;
            }
            const size_t firstDraw = draws.size();
            int plane = composePlane(p);
            if (entry && !entry->surface) entry->surface = acquireOwnedSurface(size);
            auto* saved = entry ? entry->surface.get() : acquireSurface(size);
            Params params;
            params.placement = {0, 0, dl.outW, dl.outH};
            params.options[3] = Colour;
            if (fuseMasterColour(p)) setMasterColour(params, *p.master);
            auto* src = surfaces[size_t(plane)].texture.get();
            composite(saved, src, src, params);
            finishPlane(entry, firstDraw);
            slates[p.master->renderTarget] = {saved, entry ? entry->generation : ++nextPlaneGeneration};
        }
        int scene = 0;
        std::optional<size_t> lastSceneDraw;
        clear(&surfaces[size_t(scene)]);
        // Ordinary planes now see the COMPLETE slate table, including masters above their own plane.
        for (size_t i = 0; i < dl.planes.size(); ++i) {
            const auto& p = dl.planes[i];
            QRhiTexture* src = nullptr;
            Params params;
            if (isSlate(p)) {
                if (p.master->renderTarget[0] == '*') continue;
                src = slates.at(p.master->renderTarget).surface->texture.get();
            } else {
                bool any = std::any_of(p.layers.begin(), p.layers.end(), [&](const render::DrawLayer& l) {
                    return l.relay.empty() || slates.count(l.relay) != 0;
                });
                if (!any) continue;
                auto* entry = preparePlane(i, p, false);
                if (entry && entry->ready) {
                    src = entry->surface->texture.get();
                } else {
                    const size_t firstDraw = draws.size();
                    int plane = composePlane(p);
                    src = surfaces[size_t(plane)].texture.get();
                    if (entry) {
                        if (!entry->surface) entry->surface = acquireOwnedSurface(size);
                        // Render directly into the retained output: replace every use of the final scratch
                        // attachment within THIS plane's command range. The other scratch texture stays
                        // shared. Keeping physical attachments stable also preserves SRB reuse on misses.
                        auto* retained = entry->surface->texture.get();
                        for (size_t d = firstDraw; d < draws.size(); ++d) {
                            auto& draw = draws[d];
                            if (draw.attachment == src) draw.setOutput(entry->surface.get());
                            if (draw.source == src) draw.source = retained;
                            if (draw.destination == src) draw.destination = retained;
                        }
                        src = retained;
                        finishPlane(entry, firstDraw);
                    }
                }
                if (fuseMasterColour(p)) setMasterColour(params, *p.master);
            }
            params.placement = {0, 0, dl.outW, dl.outH};
            params.options[0] = p.master ? p.master->blend : uint8_t(render::kBlendDefault);
            params.options[1] = 1;
            composite(&surfaces[size_t(1 - scene)], src, surfaces[size_t(scene)].texture.get(), params);
            lastSceneDraw = draws.size() - 1;
            scene = 1 - scene;
        }
        for (const auto& h : dl.hud) {
            if (h.tileW <= 0 || h.tileH <= 0) continue;
            Surface* tiles[2];
            for (auto& tile : tiles) tile = acquireSurface(QSize(h.tileW, h.tileH));
            int tile = 0;
            clear(tiles[tile]);
            LayerBatch batch;
            for (const auto& l : h.layers) {
                // CPU HudPass composition draws sprites only, even if a layer also carries a relay name.
                if (!l.sprite || !intersects(l.x, l.y, l.sprite->w, l.sprite->h, h.tileW, h.tileH)) continue;
                Params params;
                params.placement = {l.x, l.y, l.sprite->w, l.sprite->h};
                if (l.hasTint) params.tint = {float(l.tintR), float(l.tintG), float(l.tintB), 255};
                params.tint[3] = float(std::clamp(l.alpha, 0, 255));
                params.options[0] = l.blend;
                setMatrix(params, l.cmat);
                auto* sprite = upload(*l.sprite);
                if (composeLayer(tiles[tile], tiles[1 - tile], sprite->texture.get(), sprite, l, params, batch))
                    tile = 1 - tile;
            }
            for (const auto& dest : h.dests) {
                if (!intersects(dest.x, dest.y, h.tileW, h.tileH, dl.outW, dl.outH)) continue;
                Params params;
                params.placement = {dest.x, dest.y, h.tileW, h.tileH};
                params.options[3] = HudBlit;
                composite(&surfaces[size_t(1 - scene)], tiles[tile]->texture.get(), surfaces[size_t(scene)].texture.get(), params);
                lastSceneDraw = draws.size() - 1;
                scene = 1 - scene;
            }
        }
        if (dl.groundBlack) {
            if (features.fusePasses && lastSceneDraw) {
                // Only the LAST scene write may flatten. Later dead HUD canvases, if any, never read
                // the scene. Blend/HudBlit both produce byte-valued results before integer flattening;
                // the shader must also flatten the unchanged pixels outside a HUD destination.
                draws[*lastSceneDraw].params.controls[3] = 1;
            } else {
                Params params;
                params.options[3] = Black;
                // Source is unused by the flatten operation, but must not alias its output attachment.
                composite(&surfaces[size_t(1 - scene)], surfaces[2].texture.get(), surfaces[size_t(scene)].texture.get(), params);
                scene = 1 - scene;
            }
        }

        require(std::count_if(draws.begin(), draws.end(), [](const Draw& d) { return !d.continuePass; }) <=
                limits.maxRenderPasses, "render pass count exceeds GPU limit");
        // Atlases are optional duplicate storage. Never exceed preflight's existing texture budget,
        // reject an otherwise supported list, or retain an unbounded historical atlas set.
        batchInstances(draws, *updates, limits.maxTextureBytes - textureBytes - extraPlaneBytes);
        profiler.mark(1);
        // Small uniform pages also work on backends with a 64 KiB uniform-buffer limit.
        const int fullStride = rhi.ubufAligned(sizeof(Params));
        const int commonStride = rhi.ubufAligned(kCommonParamsSize);
        const int pageSize = std::min(65536, rhi.resourceLimit(QRhi::MaxUniformBufferRange));
        require(commonStride > 0 && fullStride >= commonStride && pageSize >= fullStride,
                "GPU uniform-buffer range is too small");
        // Every SRB still binds the complete 704-byte shader block. An ordinary draw's unused kernel
        // tail may overlap FOLLOWING parameters; its 176 active bytes never do. Kernel-using operations
        // reserve the full block. Leave enough initialized tail padding for the final binding in each
        // page, so even the full declared range is in bounds on every backend. No shader/layout changes
        // or additional bindings are needed. All page data is uploaded once, before any draws execute.
        const int bindingTail = features.packUniforms ? int(sizeof(Params)) : fullStride;
        size_t page = 0;
        for (size_t first = 0; first < draws.size();) {
            while (first < draws.size() && (draws[first].clear || draws[first].instanced)) ++first;
            if (first == draws.size()) break;
            size_t end = first;
            int offset = 0, dataSize = 0;
            for (; end < draws.size(); ++end) {
                auto& d = draws[end];
                if (d.clear || d.instanced) continue;
                if (offset + bindingTail > pageSize) break;
                d.uniformOffset = quint32(offset);
                dataSize = offset + bindingTail;
                offset += paramBytes(d.params, features.packUniforms) == sizeof(Params) ? fullStride : commonStride;
            }
            QByteArray data(dataSize, '\0');
            if (page == uniforms.size()) {
                auto buffer = std::unique_ptr<QRhiBuffer>(rhi.newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                                                     quint32(pageSize)));
                require(buffer->create(), "cannot create GPU uniform page");
                uniforms.push_back(std::move(buffer));
                ++stats.createdUniformPages;
            }
            auto* buffer = uniforms[page].get();
            for (size_t i = first; i < end; ++i) {
                Draw& d = draws[i];
                if (d.clear || d.instanced) continue;
                std::memcpy(data.data() + d.uniformOffset, &d.params, paramBytes(d.params, features.packUniforms));
                if (options.reuseResources) {
                    BindingKey key{buffer->globalResourceId(), d.source->globalResourceId(),
                                   d.destination->globalResourceId(), sampler->globalResourceId()};
                    auto current = cachedBindings.find(key);
                    if (current == cachedBindings.end()) {
                        std::unique_ptr<QRhiShaderResourceBindings> b;
                        auto previous = previousBindings.find(key);
                        if (previous != previousBindings.end()) {
                            b = std::move(previous->second);
                            previousBindings.erase(previous);
                        } else {
                            b = makeBindings(buffer, d.source, d.destination);
                        }
                        current = cachedBindings.emplace(key, std::move(b)).first;
                    }
                    d.bindings = current->second.get();
                } else {
                    auto b = makeBindings(buffer, d.source, d.destination);
                    d.bindings = b.get();
                    bindings.push_back(std::move(b));
                }
            }
            updates->updateDynamicBuffer(buffer, 0, quint32(data.size()), data.constData());
            stats.uploadedUniformBytes += uint64_t(data.size());
            first = end;
            ++page;
        }
        profiler.mark(2);
        // No commands are recorded until all resources have been created successfully.
        cb.resourceUpdate(updates.release());
        for (size_t i = 0; i < draws.size(); ++i) {
            const Draw& d = draws[i];
            if (!d.continuePass) {
                cb.beginPass(d.output, QColor(0, 0, 0, 0), {1.0f, 0});
                ++stats.renderPasses;
            }
            if (d.instanceCount) {
                const QSize extent = d.attachment->pixelSize();
                cb.setGraphicsPipeline(instancePipeline.get());
                cb.setShaderResources(d.bindings);
                cb.setViewport(QRhiViewport(0, 0, float(extent.width()), float(extent.height())));
                cb.setScissor(QRhiScissor(0, 0, extent.width(), extent.height()));
                const QRhiCommandBuffer::VertexInput input{instanceBuffer.get(), d.instanceOffset};
                cb.setVertexInput(0, 1, &input);
                cb.draw(4, d.instanceCount);
                stats.spriteDraws += d.instanceCount;
                stats.instancedSprites += d.instanceCount;
                ++stats.instancedDraws;
                i += d.instanceCount - 1; // following logical draws belong to this same ordered batch/pass
            } else if (!d.clear) {
                cb.setGraphicsPipeline(d.attachment->format() == QRhiTexture::R32F ? outlinePipeline.get() : pipeline.get());
                QRhiCommandBuffer::DynamicOffset offset{0, d.uniformOffset};
                cb.setShaderResources(d.bindings, 1, &offset);
                const QSize extent = d.attachment->pixelSize();
                cb.setViewport(QRhiViewport(0, 0, float(extent.width()), float(extent.height())));
                int x = 0, y = 0, w = extent.width(), h = extent.height();
                if (d.scissored) {
                    const auto& p = d.params.placement;
                    x = std::max(0, p[0]); y = std::max(0, p[1]);
                    w = int(std::min<int64_t>(extent.width(), int64_t(p[0]) + p[2])) - x;
                    h = int(std::min<int64_t>(extent.height(), int64_t(p[1]) + p[3])) - y;
                    // QRhi scissors use bottom-left coordinates; our logical rows follow native fragment Y.
                    if (!rhi.isYUpInFramebuffer()) y = extent.height() - y - h;
                    if (d.params.options[3] == SpriteCopy) ++stats.spriteDraws;
                    else ++stats.regionDraws;
                } else {
                    ++stats.fullSurfaceDraws;
                }
                if (d.params.options[3] == Blend && d.params.options[1] == 0) {
                    ++stats.compatibilityLayers[size_t(d.params.options[0])];
                    if (d.params.options[2]) ++stats.matrixLayers;
                }
                cb.setScissor(QRhiScissor(x, y, w, h));
                cb.draw(3);
            }
            if (i + 1 == draws.size() || !draws[i + 1].continuePass) cb.endPass();
        }
        profiler.mark(3);
        recordComplete = true; // cache contents become valid only after all updates/draws have been recorded
        return surfaces[size_t(scene)].texture.get();
    }
};

Executor::Executor(QRhi& rhi, Limits limits, Options options, Features features)
    : impl_(std::make_unique<Impl>(rhi, limits, options, features)) {}
Executor::~Executor() = default;

bool Executor::supports(const render::DrawList& list, std::string* reason) const {
    return impl_->supports(list, reason);
}

bool Executor::supports(const std::shared_ptr<const render::DrawList>& list, std::string* reason) const {
    if (!list) return fail(reason, "missing immutable GPU draw list");
    if (impl_->validatedList.lock() == list) {
        if (reason) reason->clear();
        return true;
    }
    uint64_t textureBytes = 0;
    if (!impl_->supports(*list, reason, &textureBytes)) return false;
    impl_->validatedList = list;
    impl_->validatedTextureBytes = textureBytes;
    return true;
}

Stats Executor::stats() const { return impl_->stats; }
void Executor::setProfileRecording(bool enabled) { impl_->options.profileRecording = enabled; }

QRhiTexture* Executor::record(const render::DrawList& list, QRhiCommandBuffer& commands, std::string* error) {
    impl_->stats = {};
    try {
        uint64_t textureBytes = 0;
        if (!impl_->supports(list, error, &textureBytes)) return nullptr;
        return impl_->record(list, commands, textureBytes);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return nullptr;
    }
}

QRhiTexture* Executor::record(const std::shared_ptr<const render::DrawList>& list, QRhiCommandBuffer& commands,
                             std::string* error) {
    impl_->stats = {};
    try {
        if (!supports(list, error)) return nullptr;
        return impl_->record(*list, commands, impl_->validatedTextureBytes, true);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return nullptr;
    }
}

}  // namespace fugue::rendergpu
