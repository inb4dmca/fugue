#include "app/renderthread.h"

void RenderThread::submit(fugue::render::DrawList dl) {
    {
        std::lock_guard<std::mutex> g(mu_);
        if (slot_) dropped_++;
        slot_ = std::move(dl);
    }
    cv_.notify_one();
}

void RenderThread::submitAux(const std::string& id, fugue::render::DrawList dl) {
    {
        std::lock_guard<std::mutex> g(mu_);
        auxSlots_[id] = std::move(dl);
    }
    cv_.notify_one();
}

void RenderThread::forgetAux(const std::string& id) {
    std::lock_guard<std::mutex> g(mu_);
    auxForget_.insert(id);
}

void RenderThread::requestStop() {
    { std::lock_guard<std::mutex> g(mu_); stop_ = true; }
    cv_.notify_all();
}

void RenderThread::run() {
    for (;;) {
        fugue::render::DrawList dl;
        std::map<std::string, fugue::render::DrawList> aux;
        bool haveMain = false;
        {
            std::unique_lock<std::mutex> g(mu_);
            cv_.wait(g, [&] { return stop_ || slot_ || !auxSlots_.empty(); });
            if (stop_) return;
            if (slot_) { dl = std::move(*slot_); slot_.reset(); haveMain = true; }
            aux.swap(auxSlots_);
            for (const std::string& id : auxForget_) auxLast_.erase(id);
            auxForget_.clear();
        }
        for (auto& [id, al] : aux) {
            auto last = auxLast_.find(id);
            if (last != auxLast_.end() && fugue::render::sameDrawList(al, last->second)) continue;
            fugue::render::RgbaImage img = fugue::render::execute(al);
            auxLast_[id] = std::move(al);
            if (img.w <= 0 || img.h <= 0) continue;
            QImage q(img.px.data(), img.w, img.h, img.w * 4, QImage::Format_RGBX8888);
            emit auxFrame(QString::fromStdString(id), q.copy());
        }
        if (!haveMain) continue;
        auto hit = dl.hit;
        // An unchanged picture is not executed again: the executor is a pure function of the list (render/ir.h), so the
        // image on screen is still this list's output. Only the hit scene is refreshed, since atom refs and names can
        // change without a pixel changing. Most hand-offs repeat the previous list, and executing one is a real cost.
        if (last_ && fugue::render::sameDrawList(dl, *last_)) {
            unchanged_++;
            emit sceneOnly(std::move(hit));
            continue;
        }
        fugue::render::RgbaImage img = fugue::render::execute(dl);
        executed_++;
        last_ = std::move(dl);
        last_->hit.reset();   // only the pixels are compared; do not keep an old hit scene alive
        if (img.w <= 0 || img.h <= 0) continue;
        // The map is opaque after the black-ground flatten (alpha == 255 everywhere), so RGBX8888 is safe.
        // copy() detaches from the vector's storage before it dies.
        QImage q(img.px.data(), img.w, img.h, img.w * 4, QImage::Format_RGBX8888);
        emit frame(q.copy(), std::move(hit));
    }
}
