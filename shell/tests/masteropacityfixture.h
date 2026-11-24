#pragma once
#include "shell/tests/stockinputfixture.h"

namespace fugue::shell::test {
struct MasterOpacityFixture : StockInputFixture {
    static QJsonArray rows() {
        QFile file(QString(FUGUE_TEST_DATA_DIR)+"/master-opacity-world/stock/result.json");
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object()["phases"].toArray();
    }
    bool load(const QString& phase) {
        return LocalRelayFixture::load("frames-"+phase+".txt","master-opacity-world/stock");
    }
    MapPointer at(QPoint point, const MapSprites& scene, bool defaultMap = true) const {
        return *mapPointerAt(point,frame.source.size(),{7,7},frame.source,frame.geometry,"main.map",defaultMap,scene);
    }
    // Transcribed from independent popup images, NOT Click ownership.
    static QStringList menu(const QString& phase, QPoint point) {
        return phase != "zero" && point.x() >= 32 ? QStringList{"member","floor"} : QStringList{"floor"};
    }
    QStringList menuNames(const MapPointer& p) const {
        QStringList result;
        for (const auto& hit : mapPointerStack(p)) result.push_back(QString::fromStdString(targetName(hit)));
        return result;
    }
};
}
