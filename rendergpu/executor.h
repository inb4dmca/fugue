// Experimental texture-producing executor. The CPU executor remains the compatibility reference.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <QtCore/qglobal.h>
#include "render/ir.h"

QT_BEGIN_NAMESPACE
class QRhi;
class QRhiCommandBuffer;
class QRhiTexture;
QT_END_NAMESPACE

namespace fugue::rendergpu {

struct Limits {
    int maxDimension = 4096;
    int maxPlanes = 256;
    int maxLayers = 65536; // logical draws, not physical passes; zoomed-out SS13 maps exceed 16K layers
    int maxFilters = 64;
    int maxFilterRadius = 64;   // shader kernel capacity; values above 64 are capped
    int maxHudPasses = 256;
    int maxHudDestinations = 4096;
    int maxRenderPasses = 16384;
    uint64_t maxFilterSamples = 256 * 1024 * 1024; // conservative texture-tap budget per list
    uint64_t maxLayerPixels = 256 * 1024 * 1024; // clipped sprite/relay coverage, bounds large opaque overdraw too
    uint64_t maxTextureBytes = 128 * 1024 * 1024;
};

struct Options {
    bool batchSprites = true; // binary-alpha instancing/overwrites and disjoint snapshot-based layer batches
    bool cacheSprites = true; // content-keyed reuse from the previous list, not pointer-keyed across frames
    bool reuseResources = true; // uniform/binding/attachment reuse; also plane outputs for immutable recording
    bool profileRecording = false; // wall/thread-CPU phases; opt-in diagnostic overhead
};

// Optimizations over the plain one-pass-per-step recording; production keeps them all on. Turning them off gives the
// unoptimized reference the tests compare against.
struct Features {
    bool fusePasses = true;      // plane colour and the black-ground flatten folded into neighbouring passes
    bool packUniforms = true;    // short uniform blocks for operations without a kernel
    bool instanceSprites = true; // instanced binary-alpha sprites (also needs Options::batchSprites)
    bool cachePlanes = true;     // plane outputs kept across immutable lists (also needs Options::reuseResources)
};

struct Stats {
    uint64_t renderPasses = 0;
    uint64_t fullSurfaceDraws = 0;
    uint64_t spriteDraws = 0; // logical binary-alpha layers, including those combined into instanced draws
    uint64_t instancedDraws = 0, instancedSprites = 0; // native draw calls and logical sprites in those calls
    uint64_t atlasUploads = 0, uploadedAtlasBytes = 0, atlasBytes = 0; // extra uploads and current atlas storage
    uint64_t uploadedInstanceBytes = 0;
    uint64_t regionDraws = 0; // scissored compatibility draws sharing a destination snapshot
    uint64_t uploadedSprites = 0;
    uint64_t uploadedBytes = 0;
    uint64_t reusedSprites = 0;
    std::array<uint64_t, 6> compatibilityLayers{}; // default, over, add, subtract, multiply, inset
    uint64_t matrixLayers = 0;
    uint64_t createdUniformPages = 0;
    uint64_t createdBindings = 0;
    uint64_t createdSurfaces = 0; // scene/plane, effect scratch, slate and HUD render targets
    uint64_t uploadedUniformBytes = 0; // actual page uploads, including alignment and the final binding's padding
    uint64_t planes = 0, cachedPlanes = 0, planeCacheHits = 0, filteredPlaneCacheHits = 0;
    uint64_t avoidedPlanePasses = 0; // actual composition/filter passes skipped by hits (not final scene blends)
    uint64_t planeCacheBytes = 0; // retained outputs, including named slates already in preflight's budget
    uint64_t planeCacheInputBytes = 0; // conservative input/metadata budget; source capacities counted per layer
    // setup, plan/uploads, uniforms/bindings, native command encoding, retirement of previous resources.
    std::array<double, 5> recordWallMs{}, recordCpuMs{};
};

class Executor {
public:
    // rhi must outlive the executor. Construction, calls and destruction belong to rhi's owning thread.
    explicit Executor(QRhi& rhi, Limits limits = {}, Options options = {}, Features features = {});
    ~Executor();
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    // Rejects unsupported IR, malformed images and excessive resource requests before submitting any commands.
    // This is NOT a bit-exactness guarantee: shader arithmetic is float32, unlike the CPU's float64.
    bool supports(const render::DrawList& list, std::string* reason = nullptr) const;

    // Opt-in validation reuse for transitively IMMUTABLE lists, including sprites, matrices and masters.
    // Neither the list nor its dependencies may change through mutable aliases while it is shared.
    // Validation uses a weak single-entry cache; the reference overload always revalidates.
    bool supports(const std::shared_ptr<const render::DrawList>& list, std::string* reason = nullptr) const;

    // Call once in an active QRhi frame, outside a render pass. Does not begin/end frames or read pixels back.
    // On failure returns nullptr with an error, without recording commands; the caller may use the CPU executor.
    // On success the owned straight-alpha RGBA8 texture is valid until the next record() call or destruction.
    // Texel (0,0) is the image's top-left on ALL backends; readbacks need no framebuffer-Y flip.
    // It may also be sampled in later frames on this QRhi before the next record().
    // All input pixels/uniforms are copied; list need not outlive this call.
    QRhiTexture* record(const render::DrawList& list, QRhiCommandBuffer& commands, std::string* error = nullptr);
    // Same recording contract, with the immutable-input requirement and validation cache described above.
    // Also retains bounded plane inputs/outputs for cross-frame reuse. Their sprites/matrices/masters must stay
    // immutable even after the original list is released. The reference overload does not cache plane outputs.
    QRhiTexture* record(const std::shared_ptr<const render::DrawList>& list, QRhiCommandBuffer& commands,
                       std::string* error = nullptr);

    // Counters for the last record() attempt; only complete when record() succeeds. Owning thread only.
    Stats stats() const;
    // Toggle diagnostic phase clocks without recreating GPU resources. Owning thread only.
    void setProfileRecording(bool enabled);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fugue::rendergpu
