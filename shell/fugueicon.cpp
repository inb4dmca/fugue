#include "shell/fugueicon.h"
#include <QFile>
#include <vector>
#include "render/dmi.h"

// The resource lives in the fugue_shell static library, so nothing references its initializer and the linker would drop it.
static void initFugueResources() { Q_INIT_RESOURCE(fugue); }

namespace fugue::shell {

static void initOnce() {
    static const bool once = (initFugueResources(), true);
    (void)once;
}

QIcon fugueIcon() {
    initOnce();
    // Every size is its own file: QIcon only scales between files, so a 16, 24, 32 or 48 request gets hand-fitted
    // pixels rather than a downscaled 256. No SVG, since a PNG-built QIcon would rasterise it once at its natural size.
    QIcon i;
    for (int s : kFugueIconSizes) i.addFile(QStringLiteral(":/fugue/%1.png").arg(s), QSize(s, s));
    return i;
}

std::vector<std::pair<int, QByteArray>> fugueIconPngs() {
    initOnce();
    std::vector<std::pair<int, QByteArray>> v;
    for (int s : kFugueIconSizes) {
        QFile f(QStringLiteral(":/fugue/%1.png").arg(s));
        if (f.open(QIODevice::ReadOnly)) v.push_back({s, f.readAll()});
    }
    return v;
}

QImage skinIconImage(const QByteArray& bytes) {
    if (bytes.startsWith("\x89PNG")) {
        auto r = render::parseIcon(reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()));
        if (r.ok() && !r.icon->plainImage && !r.icon->states.empty()) {
            std::vector<uint8_t> px;
            if (!r.icon->frame(r.icon->states[0], 0, 0, px)) return {};
            return QImage(px.data(), r.icon->width, r.icon->height, r.icon->width * 4, QImage::Format_RGBA8888).copy();
        }
    }
    return QImage::fromData(bytes);
}

}  // namespace fugue::shell
