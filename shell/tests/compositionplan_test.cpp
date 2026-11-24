#include <QtTest>
#include "shell/compositionplan.h"
#include "shell/mapinput.h"
#include "shell/tests/planesourcefixture.h"
#include "shell/tests/turflocalfixture.h"
#include "shell/spriterenderer.h"
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <QOffscreenSurface>
#include <QDir>
#include <QFile>
#include "shell/entitymap.h"
using namespace fugue::shell;
static void initGpuResources() { Q_INIT_RESOURCE(mapshaders); }
namespace {
MapSprite ink(int plane = 0) {
    MapSprite s; s.plane = plane; s.atom = 42; s.haveKey = true; s.key = 7; s.targetIdentity = 9;
    s.image = QImage(32,32,QImage::Format_RGBA8888); s.image.fill(Qt::red);
    s.iconBottom = {0,32}; s.cell = {1,1}; return s;
}
MapSprite master(int plane, QString target = {}) {
    MapSprite s; s.kind = MapSprite::Master; s.plane = plane; s.renderTarget = target; return s;
}
MapSprite relay(int plane, QString source) {
    MapSprite s; s.kind = MapSprite::Screen; s.plane = plane; s.renderSource = source;
    s.screenInteractive = true; s.atom = 43; return s;
}
// Deterministic small contributors over a large captured plane. Hidden RGB,
// tiny alpha, generated alpha and non-byte material coefficients are deliberate.
MapSprites contributorScene(QRect source, int count, int variant = 0) {
    auto raster = QImage(17,13,QImage::Format_RGBA8888);
    const int alpha[] = {0,1,3,7,17,63,128,193,255};
    for (int y=0;y<raster.height();++y) for (int x=0;x<raster.width();++x)
        raster.setPixelColor(x,y,QColor((x*73+y*29)%256,(x*31+y*53)%256,
            (x*17+y*97)%256,alpha[(x+3*y)%9]));
    MapSprites scene;
    for (int i=0;i<count;++i) {
        auto s=ink(1);s.image=raster;
        s.position=source.topLeft()+QPoint((i*19)%(source.width()+20)-10,(i*11)%(source.height()+16)-8);
        s.tint={.73f,.91f,.57f,.67f};s.blend=i%5;
        if (i%3==0) {s.color[19]=.21f;s.color[3]=.17f;s.color[16]=.13f;}
        if (variant==1) {s.size={23+i%7,19+i%3};s.smooth=true;}
        if (variant==7 || variant==8)
            s.position=source.topLeft()+QPoint(31+(i*3)%37,19+(i*7)%31);
        if (variant==2 && i%4==0) {
            StaticAffine a;a.forward=QTransform(.91,.27,-.19,1.03,.375,-.625);
            a.inverse=a.forward.inverted();a.bounds=a.forward.mapRect(QRectF(raster.rect())).toAlignedRect();s.affine=a;
        }
        scene.push_back(s);
    }
    if (variant==3 || variant==4 || variant==7 || variant==8) {
        auto outer=std::make_shared<AppearanceGroup>();outer->tint[3]=.81f;
        if (variant==7) outer->blend=count%5; // generated contributors exercise every supported blend
        if (variant==4 || variant==8) outer->transform=QTransform(.93,.21,-.17,1.07,.375,-.625);
        auto inner=std::make_shared<AppearanceGroup>();inner->parent=outer;inner->color[19]=.11f;
        for (int i=0;i<count;++i) {
            scene[i].appearanceGroup=i%3==0 ? inner : outer;
            scene[i].groupOccurrence=1;scene[i].groupOrigin=source.center();scene[i].groupLayer=float(i);
        }
    }
    auto m=master(1,"capture");
    if (variant==5 || variant==6) {
        auto effects=std::make_shared<PlaneEffects>();
        PlaneFilter outline;outline.kind=PlaneFilter::Outline;outline.stock=true;outline.size=1.5;outline.color={.7f,.2f,.9f,.53f};
        PlaneFilter blur;blur.stock=true;blur.size=1;
        if (variant==6) {outline.kind=PlaneFilter::Shadow;outline.x=.5;outline.y=-.75;}
        effects->filters={outline,blur};m.planeEffects=effects;
    }
    scene.push_back(m);return scene;
}
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
        initGpuResources();
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
const QRect viewport(0,0,32,32);
std::optional<MapPointer> pick(const std::shared_ptr<const CompositionPlan>& plan, QPoint point = {8,8}) {
    return mapPointerAtPresentation(point,viewport,{1,1},viewport,
        {{0,0,1,1},{1,1},32,0,1},"map",true,{},plan);
}
}
class CompositionPlanTest : public QObject {
    Q_OBJECT
private slots:
    void contributorMaterialBounds() {
        const QRect source(13,-9,832,704);
        auto scene=contributorScene(source,1024);
        const auto old=CompositionPlan::build(scene,source,{true,false});
        const auto bounded=CompositionPlan::build(scene,source,{true,true});
        QVERIFY(old->supported && bounded->supported);
        QCOMPARE(old->schedule,bounded->schedule);QCOMPARE(old->livePasses,uint64_t(3072));
        int tightened=0;
        for (size_t i=0;i<old->passes.size();++i) {
            const auto& a=old->passes[i];const auto& b=bounded->passes[i];
            QCOMPARE(a.byteCapture,b.byteCapture);QCOMPARE(a.straightOutput,b.straightOutput);
            QCOMPARE(a.items.size(),b.items.size());
            if (a.straightOutput) {
                const auto clipped=b.items[0].sprite->rect().intersected(source);
                QCOMPARE(b.bounds,clipped.isEmpty() ? source : clipped);
                tightened+=a.bounds!=b.bounds;
            } else QCOMPARE(a.bounds,b.bounds); // every accumulated/rounded surface unchanged
            for (size_t j=0;j<a.items.size();++j) {
                QCOMPARE(a.items[j].input,b.items[j].input);QCOMPARE(a.items[j].effect,b.items[j].effect);
                QCOMPARE(a.items[j].sprite->blend,b.items[j].sprite->blend);
            }
        }
        QCOMPARE(tightened,1003);
        QVERIFY(bounded->storage.workPixels < old->storage.workPixels*7/10);
        qInfo()<<"logical weighted work pixels reference/bounded"<<old->storage.workPixels<<bounded->storage.workPixels;
        // No writer aliases a still-live reader, including final outputs.
        for (const auto& plan:{old,bounded}) {
            std::map<int,int> owners;
            for (int index:plan->schedule) {
                const auto& pass=plan->passes[index];
                for (const auto& item:pass.items) if (item.input>=0) {
                    const int slot=plan->passes[item.input].surface;
                    QCOMPARE(owners.at(slot),item.input);QVERIFY(slot!=pass.surface);
                }
                owners[pass.surface]=index;
            }
            for (const auto& item:plan->final) if (item.input>=0)
                QCOMPARE(owners.at(plan->passes[item.input].surface),item.input);
        }
        const auto grouped=contributorScene(source,73,7);
        const auto oldGroup=CompositionPlan::build(grouped,source,{true,false});
        const auto newGroup=CompositionPlan::build(grouped,source,{true,true});
        QVERIFY(oldGroup->supported && newGroup->supported);int generatedTightened=0;
        for (size_t i=0;i<newGroup->passes.size();++i) {
            const auto& p=newGroup->passes[i];
            if (p.straightOutput && p.items[0].input>=0 && p.bounds!=oldGroup->passes[i].bounds)
                ++generatedTightened;
        }
        QVERIFY(generatedTightened>0);
        const auto byteOnly=CompositionPlan::build(scene,source,{false,true});
        QVERIFY(byteOnly->supported);
        for (const auto& p:byteOnly->passes) if(p.straightOutput) QCOMPARE(p.bounds,source);
        scene.clear();QCOMPARE(bounded->sprites.size(),qsizetype(1025));
        // Named sources resolve later; affines need their half-texel support.
        auto s=ink(1);s.tint[3]=.5f;s.renderSource="*later";
        auto plan=CompositionPlan::build({s,master(1,"visible")},source,{true,true});
        for (const auto& p:plan->passes) if(p.straightOutput) QCOMPARE(p.bounds,source);
        s.renderSource.clear();s.affine=StaticAffine{};
        plan=CompositionPlan::build({s,master(1,"visible")},source,{true,true});
        for (const auto& p:plan->passes) if(p.straightOutput) QCOMPARE(p.bounds,source);
    }
    void contributorMaterialNative_data() {
        QTest::addColumn<bool>("gl");
#ifdef Q_OS_WIN
        QTest::newRow("d3d11")<<false;
#endif
        QTest::newRow("opengl")<<true;
    }
    void contributorMaterialNative() {
        if (QGuiApplication::platformName()=="offscreen") QSKIP("Requires native GPU texture readback (no desktop capture)");
        QFETCH(bool,gl);GpuComparison gpu;const QRect source(11,-7,193,137);
        QVERIFY(gpu.initialize(gl,source.size()));
        int comparisons=0;
        for (int variant=0;variant<9;++variant) {
            auto scene=contributorScene(source,73,variant);
            QImage original;
            for (int phase=0;phase<4;++phase) {
                // Replay, shrink/move to force pool reuse, then restore old content.
                auto current=scene;
                if (phase==2) {current.remove(9,47);for (auto& s:current) if(s.kind!=MapSprite::Master) s.position+=QPoint(5,-3);}
                const auto a=gpu.render(gpu.reference,current,source);QVERIFY(!a.isNull());
                if (phase>0) QCOMPARE(gpu.uploaded,uint64_t(0));
                const auto b=gpu.render(gpu.bounded,current,source);QVERIFY(!b.isNull());
                if (phase>0) QCOMPARE(gpu.uploaded,uint64_t(0));
                QVERIFY(!gpu.reference.preparedPlan()->backend.boundedMaterials);
                QVERIFY(gpu.bounded.preparedPlan()->backend.boundedMaterials);
                QVERIFY(gpu.bounded.preparedPlan()->storage.workPixels < gpu.reference.preparedPlan()->storage.workPixels);
                if (phase==0) original=a;else if(phase!=2) QCOMPARE(a,original);
                QVERIFY2(a==b,qPrintable(QString("exact RGBA comparison variant=%1 phase=%2").arg(variant).arg(phase)));
                ++comparisons;
            }
        }
        for (int blend=0;blend<5;++blend) {
            const auto grouped=contributorScene(source,70+blend,7);
            const auto a=gpu.render(gpu.reference,grouped,source);
            const auto b=gpu.render(gpu.bounded,grouped,source);
            QVERIFY(!a.isNull() && !b.isNull());
            QVERIFY(gpu.bounded.preparedPlan()->storage.workPixels < gpu.reference.preparedPlan()->storage.workPixels);
            QCOMPARE(a,b);++comparisons;
        }
        GpuComparison large;const QRect largeSource(-23,17,832,704);
        QVERIFY(large.initialize(gl,largeSource.size()));
        const auto dense=contributorScene(largeSource,1024);
        const auto a=large.render(large.reference,dense,largeSource);
        const auto b=large.render(large.bounded,dense,largeSource);
        QVERIFY(!a.isNull() && !b.isNull());
        QVERIFY(large.bounded.preparedPlan()->backend.boundedMaterials);
        QVERIFY(large.bounded.preparedPlan()->storage.workPixels < large.reference.preparedPlan()->storage.workPixels);
        QCOMPARE(a,b);++comparisons;
        // Device resource recreation must not reset the candidate selection.
        QVERIFY(large.bounded.initialize(large.rhi.get(),large.descriptor.get(),1));
        QCOMPARE(large.render(large.bounded,dense,largeSource),a);++comparisons;
        QVERIFY(large.bounded.preparedPlan()->backend.boundedMaterials);
        QVERIFY(large.bounded.preparedPlan()->storage.workPixels < large.reference.preparedPlan()->storage.workPixels);
        qInfo()<<comparisons<<"exact full RGBA native comparisons; not presentation/timing evidence";
    }
    void anonymousGroupsAreOccurrenceScopedAndBounded() {
        auto group=std::make_shared<AppearanceGroup>();group->tint[3]=.5f;
        auto a=ink();a.appearanceGroup=group;a.groupOccurrence=1;
        auto b=a;b.position={16,0};b.groupOccurrence=2;
        auto plan=CompositionPlan::build({a,b},{0,0,64,32},{true});QVERIFY(plan->supported);
        // A group of one icon has no surface: the icon is drawn with the group's material (stock 516.1689,
        // stock-parity-world ta-kt-alone-turn).
        QCOMPARE(plan->final.size(),size_t(2));QCOMPARE(plan->final[0].input,-1);QCOMPARE(plan->final[1].input,-1);
        QCOMPARE(plan->final[0].sprite->tint[3],.5f);QCOMPARE(plan->final[0].original,qsizetype(0));
        auto a2=a;a2.position={4,0};auto b2=b;b2.position={20,0};
        plan=CompositionPlan::build({a,a2,b,b2},{0,0,64,32},{true});QVERIFY(plan->supported);
        QCOMPARE(plan->final.size(),size_t(2));QVERIFY(plan->final[0].input>=0 && plan->final[0].input!=plan->final[1].input);
        // Untransformed group parts keep their own ink (stock 516.1688, tests/data/click-world kt-root).
        QVERIFY(!plan->bindings[0].refused && !plan->bindings[1].refused);
        b.groupOccurrence=1;plan=CompositionPlan::build({a,b},{0,0,64,32},{true});
        QVERIFY(plan->supported);QCOMPARE(plan->final.size(),size_t(1));
        // A turned group's surface is the box of its parts' ink and ends there (surface-bounds): two 8 px boxes 16 px apart.
        auto turned=std::make_shared<AppearanceGroup>();turned->transform=QTransform().rotate(45);
        auto c=ink();c.image.fill(Qt::transparent);
        for (int y=4;y<12;++y) for (int x=4;x<12;++x) c.image.setPixelColor(x,y,Qt::red);
        c.appearanceGroup=turned;c.groupOccurrence=3;auto d=c;d.position={16,0};
        const auto hard=CompositionPlan::build({c,d},{0,0,64,32},{true});QVERIFY(hard->supported);
        QCOMPARE(hard->final.size(),size_t(1));QVERIFY(hard->final[0].hardEdge);
        QCOMPARE(QRect(hard->final[0].sprite->position,hard->final[0].sprite->size),QRect(4,4,24,8));
        // The frame owns its descriptor even when the caller drops its references.
        a.appearanceGroup.reset();b.appearanceGroup.reset();group.reset();
        QCOMPARE(plan->sprites[0].appearanceGroup->tint[3],.5f);
        std::shared_ptr<const AppearanceGroup> nested;
        for (int i=0;i<33;++i) {auto node=std::make_shared<AppearanceGroup>();node->parent=nested;nested=node;}
        a.appearanceGroup=nested;QVERIFY(!CompositionPlan::build({a},viewport,{true})->supported);
        auto invalid=std::make_shared<AppearanceGroup>();invalid->color[0]=std::numeric_limits<float>::infinity();
        a.appearanceGroup=invalid;QVERIFY(!CompositionPlan::build({a},viewport,{true})->supported);
    }
    void planeMaskDependenciesAndRefusals() {
        auto mask=ink(3);mask.image.fill(QColor(0,0,0,128));
        auto output=master(1);auto effects=std::make_shared<PlaneEffects>();
        PlaneFilter filter;filter.kind=PlaneFilter::AlphaMask;filter.stock=true;filter.source="*mask";
        effects->filters={filter};output.planeEffects=effects;
        MapSprites sprites{ink(1),output,mask,master(3,"*mask")};
        auto build=[&]{return CompositionPlan::build(sprites,viewport,{true});};
        auto plan=build();QVERIFY(plan->supported);
        int maskPass=-1,dependency=-1;
        for(size_t p=0;p<plan->passes.size();++p) for(const auto& item:plan->passes[p].items)
            if(item.maskSource) {maskPass=int(p);dependency=item.input;}
        QVERIFY(maskPass>=0 && dependency>=0);QCOMPARE(dependency,plan->targets.at("*mask"));
        QVERIFY(std::find(plan->schedule.begin(),plan->schedule.end(),dependency) <
            std::find(plan->schedule.begin(),plan->schedule.end(),maskPass));
        auto padded=std::make_shared<PlaneEffects>();PlaneFilter blur;blur.stock=true;blur.size=1;
        padded->filters={blur};sprites[3].planeEffects=padded;
        plan=build();QVERIFY(plan->supported);
        const auto bounds=plan->passes[plan->targets.at("*mask")].bounds;QVERIFY(bounds!=viewport);
        for(const auto& pass:plan->passes) for(const auto& item:pass.items) if(item.maskSource)
            QCOMPARE(item.sprite->rect(),bounds);
        sprites[3].planeEffects.reset();
        // Missing/ambiguous masks use a fresh transparent source, never the
        // previous frame's texture. Inversion is material, not source selection.
        for(bool duplicate:{false,true}) {
            auto changed=sprites;
            if(duplicate) changed.push_back(master(4,"*mask"));else changed.removeLast();
            plan=CompositionPlan::build(changed,viewport,{true});QVERIFY(plan->supported);
            for(const auto& pass:plan->passes) for(const auto& item:pass.items) if(item.maskSource) {
                QVERIFY(item.input>=0);QVERIFY(plan->passes[item.input].items.empty());
            }
        }
        // A self-dependent capture cuts its mask edge deterministically.
        sprites[1].renderTarget="*self";effects->filters[0].source="*self";
        sprites.push_back(relay(5,"*self"));plan=build();QVERIFY(plan->supported);
        bool cut=false;
        for(const auto& pass:plan->passes) for(const auto& item:pass.items) if(item.maskSource && item.cyclicInput>=0) {
            cut=true;QVERIFY(item.input>=0);QVERIFY(plan->passes[item.input].items.empty());
            QVERIFY(std::find(plan->schedule.begin(),plan->schedule.end(),item.input)!=plan->schedule.end());
        }
        QVERIFY(cut);
        QCOMPARE(CompositionPlan::build(sprites,viewport,{false})->masterDisposition.at(1),
            CompositionPlan::MasterDisposition::Refused);
        // The master's own colour applies after the mask (stock-parity-world mask-color).
        sprites[1].tint[0]=.5f;
        QCOMPARE(build()->masterDisposition.at(1),CompositionPlan::MasterDisposition::Captured);
    }
    void sharedBindingsAndOwnership() {
        test::PlaneSourceFixture f;
        f.relay({{"filters",test::PlaneSourceFixture::blur("0000803f")}});
        auto second = f.sprites.back(); second.atom = 202; second.plane = 71;
        f.sprites.push_back(second);
        const auto plan = CompositionPlan::build(f.sprites,f.frame.source,{true});
        QVERIFY(plan->supported);
        std::vector<qsizetype> consumers;
        for (qsizetype i = 0; i < plan->sprites.size(); ++i)
            if (!plan->sprites[i].renderSource.isEmpty()) consumers.push_back(i);
        QCOMPARE(consumers.size(),size_t(2));
        const auto& a = plan->bindings[size_t(consumers[0])];
        const auto& b = plan->bindings[size_t(consumers[1])];
        QVERIFY(a.planeSource >= 0); QVERIFY(!a.refused && !b.refused);
        QCOMPARE(a.planeSource,b.planeSource); QCOMPARE(a.sourceOperation,b.sourceOperation);
        QVERIFY(a.sourceOperation >= 0);
        QCOMPARE(plan->final[plan->final.size()-2].input,a.sourceOperation);
        QCOMPARE(plan->final.back().input,b.sourceOperation);
        const auto selected = plan->planeSources.surfaces[a.planeSource].master;
        QCOMPARE(selected,plan->masters.at(60));
        // Pending worker values must not be substituted for the supplied frame.
        f.sprites.clear();
        const auto hit = mapPointerAtPresentation({69,37},f.frame.source,{5,3},f.frame.source,
            f.frame.geometry,"map",true,f.sprites,plan);
        QVERIFY(hit && hit->relayHit); QCOMPARE(hit->composition,plan);
        QCOMPARE(hit->click.over.ref,202u);
        QVERIFY(!hit->relayHit->producer.composition && !hit->relayHit->ink->composition);
        QCOMPARE(hit->relayHit->producer.click.over.ref,plan->sprites[selected].atom);
        const auto hole = mapPointerAtPresentation({79,47},f.frame.source,{5,3},f.frame.source,
            f.frame.geometry,"map",true,{},plan);
        QVERIFY(hole && !hole->relayHit); // generated blur ink is not coverage
    }
    void selectedMastersAndDuplicateNames() {
        auto refused = master(1,"*source");
        auto effects = std::make_shared<PlaneEffects>(); effects->supported = false; refused.planeEffects = effects;
        const auto valid = master(1,"*source");
        MapSprites scene{ink(1),refused,valid,relay(2,"*source")};
        auto plan = CompositionPlan::build(scene,viewport,{true});
        QVERIFY(plan->supported); QCOMPARE(plan->masters.at(1),qsizetype(2));
        QCOMPARE(plan->bindings[3].sourceOperation,plan->targets.at("*source"));
        QVERIFY(!plan->bindings[3].refused);
        scene[2] = refused;
        const auto replaced = CompositionPlan::build(scene,viewport,{true});
        QVERIFY(replaced->supported); QVERIFY(replaced->bindings[3].refused);
        QCOMPARE(replaced->masterDisposition.at(1),CompositionPlan::MasterDisposition::Refused);
        QCOMPARE(replaced->final.back().input,-2);
        QVERIFY(!plan->bindings[3].refused); // earlier plan owns its immutable scene
        scene[2] = valid; scene.push_back(master(3,"*source"));
        const auto duplicate = CompositionPlan::build(scene,viewport,{true});
        QVERIFY(duplicate->supported); QVERIFY(duplicate->bindings[3].refused);
        QCOMPARE(duplicate->bindings[3].planeSource,-1);
        QCOMPARE(duplicate->planeTargetCounts.at("*source"),2);
        QCOMPARE(duplicate->final.back().input,-2);
    }
    void localBindingsDuplicatesAndCycles() {
        auto part = ink(); part.localGroup = 1; part.renderTarget = "*local";
        auto consumer = relay(1,"*local"); consumer.position = {4,0};
        MapSprites scene{part,consumer};
        const auto plan = CompositionPlan::build(scene,viewport,{true});
        QVERIFY(plan->supported); QCOMPARE(plan->bindings[1].localSource,0);
        QCOMPARE(plan->bindings[1].sourceOperation,plan->targets.at("*local"));
        QCOMPARE(plan->passes[plan->targets.at("*local")].bounds,viewport);
        QCOMPARE(plan->final.back().sprite->rect(),viewport.translated(4,0));
        const auto hit = pick(plan); QVERIFY(hit && hit->relayHit);
        QCOMPARE(hit->composition,plan); QCOMPARE(hit->click.over.ref,43u);
        QCOMPARE(mapPointerStack(*hit).size(),1);
        auto other = part; other.localGroup = 2; scene.push_back(other);
        const auto duplicate = CompositionPlan::build(scene,viewport,{true});
        // Two producers of one name: stock relays one of them (stock-parity-world shared-render-target), here the first.
        QVERIFY(duplicate->supported); QVERIFY(!duplicate->bindings[1].refused);
        QCOMPARE(duplicate->bindings[1].localSource,0);
        part.renderSource = "*other"; other.renderTarget = "*other"; other.renderSource = "*local";
        const auto cycle = CompositionPlan::build({part,other,consumer},viewport,{true});
        QVERIFY(cycle->supported); QVERIFY(cycle->bindings[2].refused);
        QVERIFY(!cycle->local.groups[0].valid && !cycle->local.groups[1].valid);
        QVERIFY(cycle->local.groups[0].cyclic);
        QVERIFY(cycle->schedule.empty());
    }
    void planeCycleEdgesAndSchedule() {
        const auto plan = CompositionPlan::build({relay(1,"*b"),master(1,"*a"),
            relay(2,"*a"),master(2,"*b"),relay(3,"*a")},viewport,{true});
        QVERIFY(plan->supported);
        int cycles = 0;
        std::set<int> executed;
        for (int index : plan->schedule) {
            const auto& pass = plan->passes[index];
            QCOMPARE(pass.bounds,viewport); QVERIFY(pass.surface >= 0);
            for (const auto& item : pass.items) {
                if (item.input >= 0) QVERIFY(executed.count(item.input));
                if (item.cyclicInput >= 0) {
                    ++cycles; QCOMPARE(item.input,-2); QVERIFY(item.original >= 0);
                    QVERIFY(plan->bindings[size_t(item.original)].refused);
                    QCOMPARE(plan->bindings[size_t(item.original)].sourceOperation,-2);
                }
            }
            executed.insert(index);
        }
        QCOMPARE(cycles,1); QCOMPARE(plan->schedule.size(),size_t(plan->livePasses));
        QVERIFY(!pick(plan)->relayHit); // chained original-ink ownership remains unsupported
    }
    void backendAdmissionBoundsAndOmissions() {
        auto m = master(1); auto effects = std::make_shared<PlaneEffects>();
        PlaneFilter outline; outline.kind = PlaneFilter::Outline; outline.size = 2; outline.stock = true;
        effects->filters = {outline}; effects->treeSourceAlphaInput = true; m.planeEffects = effects;
        const auto full = CompositionPlan::build({ink(1),m},viewport,{true});
        QVERIFY(full->supported); QVERIFY(!full->hiddenTree.count(1));
        QCOMPARE(pick(full)->click.over.ref,42u);
        QVERIFY(std::any_of(full->passes.begin(),full->passes.end(),[](const auto& pass) {
            return pass.bounds.width() > viewport.width();
        }));
        const auto limited = CompositionPlan::build({ink(1),m},viewport,{false});
        QVERIFY(limited->supported); QVERIFY(limited->hiddenTree.count(1));
        QCOMPARE(limited->masterDisposition.at(1),CompositionPlan::MasterDisposition::Refused);
        QVERIFY(limited->final.empty()); QVERIFY(mapPointerStack(*pick(limited)).empty());
        QVERIFY(pick(limited)->click.over.ref != 42u);
        // Omitted filters remain omission metadata, not a new refusal or effect.
        effects = std::make_shared<PlaneEffects>(); effects->ignoredFilters = 3; m.planeEffects = effects;
        const auto omitted = CompositionPlan::build({ink(1),m},viewport,{false});
        QVERIFY(omitted->supported); QCOMPARE(omitted->ignoredFilters,uint64_t(3));
        QVERIFY(omitted->passes.empty()); QCOMPARE(pick(omitted)->click.over.ref,42u);
        test::PlaneSourceFixture f;
        f.relay({{"filters",test::PlaneSourceFixture::blur("0000803f")}});
        const auto relayPlan = CompositionPlan::build(f.sprites,f.frame.source,{false});
        QVERIFY(relayPlan->supported);
        for (qsizetype i = 0; i < f.sprites.size(); ++i)
            if (!f.sprites[i].planeSourceDependency.isEmpty()) QVERIFY(relayPlan->bindings[size_t(i)].refused);
        const auto hit = mapPointerAtPresentation({69,37},f.frame.source,{5,3},f.frame.source,
            f.frame.geometry,"map",true,{},relayPlan);
        QVERIFY(hit && !hit->relayHit);
    }
    void surfaceRejectionDiagnostics() {
        QCOMPARE(planSurfaces({}, {}, 2).rejection.reason,PlanRejection::EmptySource);
        QCOMPARE(planSurfaces({1,1}, {}, 3).rejection.reason,PlanRejection::InvalidPixelWeight);
        for (const SurfaceLifetime bad : {SurfaceLifetime{{},0}, {{1,1},-1}, {{1,1},2}}) {
            const auto plan=planSurfaces({1,1},std::array{bad},2);
            QVERIFY(!plan.supported); QCOMPARE(plan.rejection.reason,PlanRejection::InvalidPass);
            QCOMPARE(plan.rejection.pass,0);
        }
        // Numeric overflow remains invalid; these are planner-only sizes, never allocated.
        const int huge=std::numeric_limits<int>::max();
        const auto overflow=planSurfaces({1,1},std::array<SurfaceLifetime,3>{{
            {{huge,huge},1},{{huge,huge},2},{{huge,huge},3}}},2);
        QVERIFY(!overflow.supported); QCOMPARE(overflow.rejection.reason,PlanRejection::ArithmeticOverflow);
        const auto recovered=planSurfaces({32,32},{},2);
        QVERIFY(recovered.supported); QCOMPARE(recovered.rejection.reason,PlanRejection::None);
    }
    void warningOnlyPlanning() {
        MapSprites scene;
        for(int i=0;i<2200;++i) scene.push_back(ink(1));
        scene.push_back(master(1,"*source"));
        const auto hidden=CompositionPlan::build(scene,viewport,{true});
        QVERIFY(hidden->supported); QCOMPARE(hidden->metrics.generated,uint64_t(4400));
        QCOMPARE(hidden->metrics.passes,uint64_t(0)); QCOMPARE(hidden->metrics.surfaces,uint64_t(1));
        scene.push_back(relay(2,"*source"));
        const auto live=CompositionPlan::build(scene,viewport,{true});
        QVERIFY(live->supported); QVERIFY(pick(live));
        QCOMPARE(live->metrics.generated,uint64_t(4400)); QCOMPARE(live->metrics.passes,uint64_t(4400));
        QCOMPARE(live->schedule.size(),size_t(4400)); // deep iterative DFS, no old cap/truncation
        QVERIFY(live->metrics.surfaces < 10);
        const auto large=CompositionPlan::build({}, {0,0,65536,32768}, {true});
        QVERIFY(large->supported);
        QCOMPARE(large->metrics.workPixels,uint64_t(2147483648ull));
        QCOMPARE(large->metrics.storagePixels,large->metrics.workPixels);
        MapSprites pinned;
        for(int i=0;i<64;++i) {pinned.push_back(ink(i));pinned.push_back(master(i,QString::number(i)));}
        const auto storage=CompositionPlan::build(pinned,viewport,{true});
        QVERIFY(storage->supported); QVERIFY(storage->metrics.surfaces > CompositionMetrics::surfaceCap);
        const auto normal=CompositionPlan::build({ink()},viewport,{true});
        QVERIFY(normal->supported); QCOMPARE(normal->metrics.generated,uint64_t(0));
        QCOMPARE(live->metrics.generated,uint64_t(4400)); // immutable prior metrics
        const auto empty=CompositionPlan::build({}, {}, {true});
        QVERIFY(!empty->supported); QVERIFY(!empty->metrics.available);
    }
    void warningPresentation() {
        CompositionMetrics m;
        QVERIFY(m.html().contains("-- / 4096")); QVERIFY(!m.html().contains("#ffbf69"));
        const auto members=std::array{&CompositionMetrics::generated,&CompositionMetrics::passes,
            &CompositionMetrics::surfaces,&CompositionMetrics::workPixels,&CompositionMetrics::storagePixels};
        for(size_t i=0;i<members.size();++i) {
            const auto cap=m.rows()[i].cap;
            for(int delta : {-1,0,1}) {
                m={};m.available=true;m.*members[i]=cap+delta;
                QCOMPARE(m.html().count("#ffbf69"),delta>0 ? 1 : 0);
                QVERIFY(m.html().contains(QString::number(cap+delta)+" / "+QString::number(cap)));
            }
        }
        m={};m.available=true; QVERIFY(!m.html().contains("#ffbf69"));
    }
    // Sets of tests/data/stock-parity-world, replayed without a server: replay/<set>.txt is what DreamDaemon 516.1689
    // sent this client for the set (the app's recorder, FUGUE_RECORD_DIR), replay/res its resources, and
    // replay/<set>.png the map stock DreamSeeker 516.1689 drew for the same world, grabbed six seconds after login
    // (tools/stockprobe/stockprobe.py record writes all three). No channel of any pixel may be more than 8 levels from
    // stock's.
    // Drawn by Windows' software Direct3D, elsewhere by OpenGL (Mesa's llvmpipe passes: LIBGL_ALWAYS_SOFTWARE=1 under
    // xvfb-run with QT_QPA_PLATFORM=xcb; the offscreen platform has no OpenGL). Without a device the test is skipped,
    // or fails when FUGUE_REQUIRE_GPU is set, as CI sets it.
private:
    std::unique_ptr<GpuComparison> replayGpu_;   // one device for every set
private slots:
    void cleanupTestCase() { replayGpu_.reset(); }
    void stockParityReplay_data() {
        QTest::addColumn<QString>("set");
        for (const auto& name : QDir(QString(FUGUE_TEST_DATA_DIR) + "/stock-parity-world/replay").entryList({"*.png"}, QDir::Files))
            QTest::newRow(qPrintable(name.chopped(4))) << name.chopped(4);
    }
    void stockParityReplay() {
        QFETCH(QString, set);
        const QString dir = QString(FUGUE_TEST_DATA_DIR) + "/stock-parity-world/replay/";
        auto bytes = [](const QString& file) { QFile f(file); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
        const int64_t login = 1'000'000'000'000'000'000, grabbed = login + 6'000'000'000;
        fugue::model::Session session; TurfMap terrain; EntityMap entities;
        session.setIngestClockNs(login);
        for (auto line : bytes(dir + set + ".txt").split('\n')) {
            if (!line.startsWith("0x")) continue;
            const int at = int(line.indexOf(' ', line.indexOf("len=")));
            const auto body = at < 0 ? QByteArray() : QByteArray::fromHex(line.mid(at + 1));
            session.ingest(uint16_t(line.mid(2, 4).toUInt(nullptr, 16)), fugue::Bytes(body.begin(), body.end()));
        }
        std::map<uint32_t, QByteArray> resources;
        for (const auto& name : QDir(dir + "res").entryList(QDir::Files)) {
            const auto data = bytes(dir + "res/" + name); resources[name.toUInt(nullptr, 16)] = data;
            session.noteResource(name.toUInt(nullptr, 16), fugue::Bytes(data.begin(), data.end()), "");
        }
        auto read = [&](uint32_t id, QByteArray& out) {
            const auto it = resources.find(id); if (it == resources.end()) return false; out = it->second; return true;
        };
        const auto frame = terrain.prepare(session.world(), read, grabbed, false);
        const auto sprites = entities.prepare(session.world(), frame, read, grabbed, nullptr, true);
        QImage stock(dir + set + ".png"); stock = stock.convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(frame.source.size(), stock.size());
        auto& gpu = replayGpu_;
        if (!gpu) {
            gpu = std::make_unique<GpuComparison>();
#ifdef Q_OS_WIN
            const bool gl = false;
#else
            const bool gl = true;
#endif
            if (!gpu->initialize(gl, stock.size(), true)) {
                gpu.reset();
                if (qEnvironmentVariableIsSet("FUGUE_REQUIRE_GPU")) QFAIL("No GPU device to draw the map with");
                QSKIP("No GPU device to draw the map with");
            }
        }
        const QImage drawn = gpu->render(gpu->bounded, sprites, frame.source, Qt::black);
        QCOMPARE(drawn.size(), stock.size());
        int off = 0; QPoint first;
        for (int y = 0; y < stock.height(); ++y) for (int x = 0; x < stock.width(); ++x) {
            const QRgb a = drawn.pixel(x, y), b = stock.pixel(x, y);
            if (std::abs(qRed(a) - qRed(b)) > 8 || std::abs(qGreen(a) - qGreen(b)) > 8 || std::abs(qBlue(a) - qBlue(b)) > 8)
                if (!off++) first = {x, y};
        }
        if (!off) return;
        const QString saved = QDir::temp().filePath("fugue-parity-" + set + ".png");   // to look at beside stock's
        drawn.save(saved);
        QFAIL(qPrintable(QString("%1 pixels differ from stock, the first at %2,%3; drawn: %4")
                             .arg(off).arg(first.x()).arg(first.y()).arg(saved)));
    }
};
QTEST_MAIN(CompositionPlanTest)
#include "compositionplan_test.moc"
