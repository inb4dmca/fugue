// Runs worker-side terrain and entity preparation, then rasterizes flat fixture
// sprites with a small CPU oracle. Does not exercise GPU composition/presentation.
#pragma once
#include "shell/entitymap.h"
#include <QPainter>

struct AnimationFixture {
    fugue::shell::TurfMap terrain;
    fugue::shell::EntityMap entities;
    QImage image(const fugue::model::World& world, uint32_t resource, const std::string& bytes, int64_t now) {
        auto read = [&](uint32_t id, QByteArray& out) {
            if (id != resource) return false;
            out = QByteArray(bytes.data(), qsizetype(bytes.size())); return true;
        };
        const auto frame = terrain.prepare(world, read, now, false);
        if (frame.image.isNull()) return {};
        const auto sprites = entities.prepare(world, frame, read, now, nullptr, true);
        QImage out(frame.source.size(), QImage::Format_ARGB32_Premultiplied); out.fill(Qt::black);
        QPainter painter(&out);
        for (const auto& sprite : sprites) {
            if (sprite.image.isNull()) continue;
            QImage tinted = sprite.image.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < tinted.height(); ++y) for (int x = 0; x < tinted.width(); ++x) {
                const auto c = tinted.pixelColor(x,y);
                tinted.setPixelColor(x,y,QColor(int(std::lround(c.red()*sprite.tint[0])), int(std::lround(c.green()*sprite.tint[1])),
                    int(std::lround(c.blue()*sprite.tint[2])), int(std::lround(c.alpha()*sprite.tint[3]))));
            }
            painter.drawImage(QRect(sprite.position - frame.source.topLeft(), sprite.size), tinted);
        }
        return out;
    }
};
