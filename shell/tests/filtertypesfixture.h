// Wave, ripple and bloom cases of tests/data/filter-types-world (stock 516.1688): the fixture's icons, its stock.json
// rows and one filtered sprite per case. Requires FUGUE_TEST_DATA_DIR.
#pragma once
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <memory>
#include "model/json.h"
#include "shell/mapsprite.h"
#include "shell/planeeffects.h"

namespace fugue::test::filtertypes {
// prepare.py's icons, pixel for pixel.
inline QImage icon(bool shape) {
    QImage im(32, 32, QImage::Format_RGBA8888); im.fill(Qt::transparent);
    if (shape) {
        for (int y = 8; y <= 23; ++y) for (int x = 8; x <= 23; ++x) im.setPixelColor(x, y, QColor(240, 220, 40));
        for (int y = 12; y <= 19; ++y) for (int x = 24; x <= 25; ++x) im.setPixelColor(x, y, QColor(240, 220, 40, 128));
    } else {
        for (int y = 6; y < 26; ++y) for (int x = 6; x < 26; ++x)
            im.setPixelColor(x, y, (x/2 + y/2) % 2 ? QColor(240, 220, 40) : QColor(40, 80, 230));
    }
    for (int y = shape ? 10 : 8; y <= (shape ? 13 : 11); ++y)
        for (int x = shape ? 10 : 8; x <= (shape ? 13 : 11); ++x) im.setPixelColor(x, y, Qt::white);
    return im;
}
inline bool shapeIcon(const QString& name) { return name.startsWith("bloom") && name != "bloom-grid"; }
inline QJsonArray rows(const QString& set = "a") {
    QFile file(QString(FUGUE_TEST_DATA_DIR) + "/filter-types-world/stock.json");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object()["sets"].toObject()[set].toArray();
}
// Every row with captured wire records: set a and the bar sign's seven waves in set c.
inline QJsonArray recordRows() {
    QJsonArray out;
    for (const auto* set : {"a", "c"}) for (const auto& r : rows(set)) if (r.toObject().contains("records")) out.append(r);
    return out;
}
inline QJsonObject row(const QString& name, const QString& set = "a") {
    for (const auto& r : rows(set)) if (r.toObject()["case"].toString() == name) return r.toObject();
    return {};
}
inline shell::PlaneEffects effects(const QJsonObject& row) {
    QJsonObject fields{{"filters", row["records"].toArray()}};
    return shell::decodeSurfaceEffects(model::appearanceOf(QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString()));
}
// The case atom at `tile` (its icon's top-left), drawn through an atom-filter group like EntityMap's.
inline shell::MapSprite sprite(const QJsonObject& row, QPoint tile) {
    shell::MapSprite s;
    s.image = icon(shapeIcon(row["case"].toString()));
    s.position = tile; s.groupOrigin = tile; s.groupOccurrence = 1;
    auto group = std::make_shared<shell::AppearanceGroup>();
    group->effects = std::make_shared<const shell::PlaneEffects>(effects(row));
    group->filterOnly = true;
    s.appearanceGroup = group;
    return s;
}
}
