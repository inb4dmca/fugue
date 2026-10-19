// Map swapchain presenter. Ordinary frames never use endOffscreenFrame/readback.
#pragma once
#ifdef FUGUE_GPU
#include "app/renderthread.h"
#include <QWindow>
#include <QPointer>
#include <memory>

namespace fugue::shell { class SkinWindow; }

class GpuMapWindow : public QWindow {
    Q_OBJECT
public:
    explicit GpuMapWindow(fugue::shell::MapWidget* map, const QString& id = {});
    ~GpuMapWindow() override;
    bool setList(GpuDrawList list);
    void updateView();
    void setDetailedMetrics(bool enabled);
    QImage snapshot(); // synchronous native-size readback ONLY on explicit screenshot requests
    QImage snapshotView(); // explicit diagnostic capture of the swapchain, including scaling/overlay
    GpuDrawList displayedList() const;
signals:
    void submitted(GpuDrawList list, double cpuMs, double previousGpuMs, const QString& backend,
                   const fugue::app::RenderPhases& phases);
    void rejected(GpuDrawList list, const QString& reason);
protected:
    bool event(QEvent* event) override;
    void exposeEvent(QExposeEvent*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    bool initialize();
    void render();
    void reject(const QString& reason, bool terminal);
};

class GpuMapController : public QObject {
public:
    GpuMapController(RenderThread& renderer, fugue::shell::SkinWindow& window);
    ~GpuMapController() override;
private:
    RenderThread& renderer_;
    fugue::shell::SkinWindow& window_;
    struct View {
        GpuDrawList latest, presented;
        fugue::shell::HitPtr hit;
        QPointer<fugue::shell::MapWidget> map;
        QPointer<QWidget> container;
        QPointer<GpuMapWindow> gpu;
    };
    std::map<std::string, View> views_; // empty key is the main map
    void mount(const std::string& id);
    void setHit(const std::string& id, View& view);
    void updateProfiling();
};
#endif
