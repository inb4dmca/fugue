// Final MAP resampling only. Scene pixels remain at native icon resolution.
#pragma once
#include <QRectF>
#include <QSize>
#include <algorithm>
#include <cmath>
#include <limits>

namespace fugue::shell {
struct MapPresentationSettings {
    enum Mode { Distort, Normal, Blur } mode = Normal;
    double zoom = 0, iconSize = 0;
    bool letterbox = true;
    bool operator==(const MapPresentationSettings&) const = default;
};
struct MapPresentation {
    QRectF destination; // logical widget coordinates; retained with the rendered scene
    double scale = 0; // logical pixels per native scene pixel
    bool linear = false;
    bool sharpen = false; // Normal's bounded pixel-edge transition, in physical pixels
};
inline MapPresentation mapPresentation(QSizeF widget, QSize scene, int tile,
                                       MapPresentationSettings settings = {}) {
    MapPresentation out;
    if (widget.isEmpty() || scene.isEmpty() || tile <= 0) return out;
    const auto positive = [](double v) { return std::isfinite(v) && v > 0; };
    if (positive(settings.zoom)) out.scale = settings.zoom;
    else if (positive(settings.iconSize)) out.scale = settings.iconSize / tile;
    else {
        const double x = widget.width() / scene.width(), y = widget.height() / scene.height();
        out.scale = settings.letterbox ? std::min(x, y) : std::max(x, y);
    }
    // Normal is not ordinary bilinear magnification. The final shader narrows
    // its transition using the actual framebuffer scale (not logical zoom).
    // Minification remains linear; Distort's point sampler is unchanged.
    out.linear = settings.mode != MapPresentationSettings::Distort;
    out.sharpen = settings.mode == MapPresentationSettings::Normal;
    const QSizeF extent(scene.width() * out.scale, scene.height() * out.scale);
    if (!std::isfinite(extent.width()) || !std::isfinite(extent.height()) ||
        extent.width() > std::numeric_limits<int>::max() || extent.height() > std::numeric_limits<int>::max()) return {};
    out.destination = QRectF(QPointF((widget.width()-extent.width())/2, (widget.height()-extent.height())/2), extent);
    return out;
}
}
