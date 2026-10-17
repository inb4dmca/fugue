#include "shell/mapwidget.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace fugue::shell {

MapWidget::MapWidget(QWidget* parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setObjectName("map");
    setMouseTracking(true);   // button-less motion too, for the status bar's hover name (updateHover)
}

void MapWidget::setFrame(const QImage& img, HitPtr hit) {
    img_ = img;
    hit_ = std::move(hit);
    shown_++;
    // A new frame can move a different atom under a still pointer, so the hover is re-resolved on every frame.
    if (hovering_) updateHover();
    update();
}

void MapWidget::setMapScale(double zoom, int iconSize, bool letterbox, const std::string& mode) {
    zoom_ = zoom > 0 ? zoom : 0;
    iconSize_ = zoom_ > 0 ? 0 : std::max(0, iconSize);
    letterbox_ = letterbox;
    mode_ = mode.empty() ? "normal" : mode;
    update();
}

// Scaling follows stock DreamSeeker 516.
double MapWidget::scaleF() const {
    if (img_.isNull() || img_.width() <= 0 || img_.height() <= 0) return 1;
    if (zoom_ > 0) return zoom_;
    if (iconSize_ > 0) return double(iconSize_) / tile_;
    double sx = double(width()) / img_.width(), sy = double(height()) / img_.height();
    return letterbox_ ? std::min(sx, sy) : std::max(sx, sy);
}

// Centred on both axes, whether smaller (letterboxed) or larger (centre-cropped) than the control.
QRect MapWidget::imageRect() const {
    if (img_.isNull()) return {};
    double s = scaleF();
    int w = std::max(1, int(std::lround(img_.width() * s))), h = std::max(1, int(std::lround(img_.height() * s)));
    return QRect((width() - w) / 2, (height() - h) / 2, w, h);
}

void MapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (img_.isNull()) return;
    // zoom-mode selects the resampler only: normal is linear at a fractional factor and point at an integer one;
    // distort is always point; blur is always linear.
    double s = scaleF();
    bool integral = std::fabs(s - std::round(s)) < 1e-9;
    bool smooth = mode_ == "blur" || (mode_ != "distort" && !integral);
    p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
    p.drawImage(imageRect(), img_);
}

bool MapWidget::toImage(const QPoint& pos, int& ix, int& iy, bool clampToImage) const {
    if (img_.isNull()) return false;
    QRect r = imageRect();
    if (r.width() <= 0 || r.height() <= 0) return false;
    if (!clampToImage && !r.contains(pos)) return false;
    int x = pos.x() - r.x(), y = pos.y() - r.y();
    ix = int(std::floor(double(x) * img_.width() / r.width()));
    iy = int(std::floor(double(y) * img_.height() / r.height()));
    ix = std::clamp(ix, 0, img_.width() - 1);
    iy = std::clamp(iy, 0, img_.height() - 1);
    return true;
}

static int x11Button(Qt::MouseButton b) {
    return b == Qt::LeftButton ? 1 : b == Qt::MiddleButton ? 2 : b == Qt::RightButton ? 3 : 0;
}

void MapWidget::emitPointer(MapPointer::Kind k, QMouseEvent* e, int button) {
    MapPointer mp;
    mp.kind = k;
    mp.scene = hit_;
    if (!toImage(e->position().toPoint(), mp.ev.imgX, mp.ev.imgY, /*clamp=*/k != MapPointer::Press)) return;
    mp.ev.winX = e->position().toPoint().x();
    mp.ev.winY = e->position().toPoint().y();
    mp.ev.button = button;
    mp.ev.mods.ctrl = e->modifiers() & Qt::ControlModifier;
    mp.ev.mods.shift = e->modifiers() & Qt::ShiftModifier;
    mp.ev.mods.alt = e->modifiers() & Qt::AltModifier;
    mp.ev.tMs = qint64(e->timestamp());   // Qt's event clock (ms); only relative timing matters
    emit pointer(mp);
}

void MapWidget::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    int b = x11Button(e->button());
    if (b == 0 || heldButton_) return;   // wheel and extra buttons; a second button while one is held is ignored
    if (b == 3 && !rightClick_) {
        // Stock DreamSeeker answers the right button locally unless the map declares right-click = true.
        int ix, iy;
        QString name;
        if (hit_ && toImage(e->position().toPoint(), ix, iy, false)) {
            fugue::render::Naming n;
            if (fugue::render::nameMouse(*hit_, ix, iy, n)) name = QString::fromStdString(n.hit.name);
        }
        emit localMenuRequested(e->globalPosition().toPoint(), name);
        MapPointer mp;
        mp.kind = MapPointer::Press;
        mp.scene = hit_;
        if (toImage(e->position().toPoint(), mp.ev.imgX, mp.ev.imgY, false)) {
            mp.ev.winX = e->position().toPoint().x(); mp.ev.winY = e->position().toPoint().y();
            mp.ev.button = 3;
            mp.ev.mods.ctrl = e->modifiers() & Qt::ControlModifier;
            mp.ev.mods.shift = e->modifiers() & Qt::ShiftModifier;
            mp.ev.mods.alt = e->modifiers() & Qt::AltModifier;
            emit localMenuPointer(e->globalPosition().toPoint(), mp);
        }
        return;
    }
    int ix, iy;
    if (!toImage(e->position().toPoint(), ix, iy, false)) return;   // on the letterbox
    heldButton_ = b;
    emitPointer(MapPointer::Press, e, b);
}

// The status bar names the topmost atom under the pointer, using the same render::hitAt as a click, so the bar and a
// click always agree. Stock DreamSeeker skips mouse_opacity 0 atoms and shows nothing over a nameless atom, the
// letterbox, or once the pointer leaves the map. This differs from stock, which keeps a blank bar when the pointer
// re-enters onto the atom it last named; this names it at once.
void MapWidget::updateHover() {
    QString name;
    int ix, iy;
    if (hovering_ && hit_ && toImage(hoverPos_, ix, iy, /*clamp=*/false)) {
        fugue::render::Hit h;
        // HitAtom::name is already plain: the scene strips the text-macro marker when it fills it (render/scene.cpp).
        if (fugue::render::hitAt(*hit_, ix, iy, h)) name = QString::fromStdString(h.name);
    }
    if (name == hoverName_) return;
    hoverName_ = name;
    emit hoverChanged(name);
}

void MapWidget::mouseMoveEvent(QMouseEvent* e) {
    hovering_ = true;
    hoverPos_ = e->position().toPoint();
    updateHover();
    if (heldButton_) emitPointer(MapPointer::Move, e, heldButton_);
}

void MapWidget::leaveEvent(QEvent* e) {
    hovering_ = false;
    updateHover();
    QWidget::leaveEvent(e);
}

void MapWidget::mouseReleaseEvent(QMouseEvent* e) {
    int b = x11Button(e->button());
    if (!heldButton_ || b != heldButton_) return;
    heldButton_ = 0;
    emitPointer(MapPointer::Release, e, b);
}

}  // namespace fugue::shell
