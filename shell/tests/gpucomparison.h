#pragma once
#include "shell/spriterenderer.h"
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <QOffscreenSurface>
#include <QDebug>
#include <memory>

// Q_INIT_RESOURCE must resolve the generated symbol in the global namespace.
inline void initGpuResources() { Q_INIT_RESOURCE(mapshaders); }

namespace fugue::shell::test {
// Native GPU texture readback, not a desktop screenshot or presentation test.
// Two independent renderer pools share a device; all channels compare exactly.
struct GpuComparison {
    std::unique_ptr<QOffscreenSurface> fallback;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiRenderPassDescriptor> descriptor;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    SpriteRenderer reference{false}, bounded{true};
    uint64_t uploaded=0;
    bool initialize(bool gl, QSize size, bool software = false) {
        ::initGpuResources();
        if (gl) {
            QRhiGles2InitParams params;
            fallback.reset(QRhiGles2InitParams::newFallbackSurface());params.fallbackSurface=fallback.get();
            rhi.reset(QRhi::create(QRhi::OpenGLES2,&params));
        } else {
#ifdef Q_OS_WIN
            QRhiD3D11InitParams params;
            rhi.reset(QRhi::create(QRhi::D3D11,&params,software ? QRhi::PreferSoftwareRenderer : QRhi::Flags{}));
#endif
        }
        if (!rhi || !rhi->isTextureFormatSupported(QRhiTexture::RGBA16F,QRhiTexture::RenderTarget)) return false;
        qInfo()<<rhi->backendName()<<rhi->driverInfo().deviceName;
        texture.reset(rhi->newTexture(QRhiTexture::RGBA8,size,1,QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!texture->create()) return false;
        target.reset(rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(QRhiColorAttachment(texture.get()))));
        descriptor.reset(target->newCompatibleRenderPassDescriptor());target->setRenderPassDescriptor(descriptor.get());
        return target->create() && reference.initialize(rhi.get(),descriptor.get(),1) && bounded.initialize(rhi.get(),descriptor.get(),1);
    }
    QImage render(SpriteRenderer& renderer, const MapSprites& scene, QRect source, QColor clear = Qt::transparent) {
        QRhiCommandBuffer* cb=nullptr;
        if (rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess) return {};
        auto* updates=rhi->nextResourceUpdateBatch();uploaded=renderer.prepare(updates,scene,source);
        renderer.renderOffscreen(cb,updates);
        cb->beginPass(target.get(),clear,{1,0});
        cb->setViewport(QRhiViewport(0,0,float(texture->pixelSize().width()),float(texture->pixelSize().height())));
        renderer.draw(cb);cb->endPass();
        QRhiReadbackResult readback;bool completed=false;readback.completed=[&]{completed=true;};
        updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(texture.get()),&readback);
        cb->resourceUpdate(updates);
        if (rhi->endOffscreenFrame()!=QRhi::FrameOpSuccess || !renderer.ready() || !completed) return {};
        return QImage(reinterpret_cast<const uchar*>(readback.data.constData()),readback.pixelSize.width(),
            readback.pixelSize.height(),QImage::Format_RGBA8888).copy();
    }
};
}
