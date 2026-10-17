// RenderThread: executes a DrawList (CPU executor, straight-alpha RGBA8) off the GUI thread and hands the GUI a
// finished opaque QImage. The mailbox is latest-wins: if rendering falls behind, older lists are dropped, not queued.
// The GUI thread only blits (MapWidget::paintEvent).
#pragma once
#include <QImage>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <map>
#include <set>
#include <optional>
#include <memory>
#include <string>
#include "render/hittest.h"
#include "shell/mapwidget.h"   // declares the HitPtr metatype the frame signal needs
#include "render/ir.h"

class RenderThread : public QThread {
    Q_OBJECT
public:
    void submit(fugue::render::DrawList dl);
    // A secondary map control's list (render::assembleSecondaryMap), by control id, with its own latest-wins slot and
    // unchanged-list skip. Executed after the main map's.
    void submitAux(const std::string& id, fugue::render::DrawList dl);
    // A widget for that map was (re)built with no picture yet: the next list for `id` is executed even when it equals the
    // last one, so the new widget gets a frame. Any thread.
    void forgetAux(const std::string& id);
    void requestStop();
    uint64_t executed() const { return executed_; }
    uint64_t dropped() const { return dropped_; }
    uint64_t unchanged() const { return unchanged_; }   // lists identical to the last executed one (not re-executed)
signals:
    // Emitted from this thread; GUI receivers get it queued. `hit` is the scene this image was drawn from (null if the
    // list carried none), so hit-tests match the picture on screen.
    void frame(const QImage& img, std::shared_ptr<const fugue::render::HitScene> hit);
    // The list was identical to the last executed one: the image stands, only the hit scene is newer.
    void sceneOnly(std::shared_ptr<const fugue::render::HitScene> hit);
    // A secondary map's finished picture (submitAux).
    void auxFrame(const QString& id, const QImage& img);
protected:
    void run() override;
private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::optional<fugue::render::DrawList> slot_;
    bool stop_ = false;
    std::atomic<uint64_t> executed_{0}, dropped_{0}, unchanged_{0};
    std::optional<fugue::render::DrawList> last_;   // the last EXECUTED list (this thread only)
    std::map<std::string, fugue::render::DrawList> auxSlots_;   // guarded by mu_
    std::set<std::string> auxForget_;                           // guarded by mu_ (forgetAux)
    std::map<std::string, fugue::render::DrawList> auxLast_;    // this thread only
};
