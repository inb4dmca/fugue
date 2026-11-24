#pragma once
#include "tests/appearancejson.h"
#include "shell/tests/replayfixture.h"
#include "shell/mapinput.h"
#include "model/verbmenu.h"
#include "model/statpanel.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
namespace fugue::shell::test {
struct MasterTreeFixture : ReplayFixture {
    static QString goldenFile(const QString& file) {
        // Byte-identical independent stock captures share one retained oracle.
        const QStringList aliases{"stock-g-overlay-group-apart-leafplane.png",
            "stock-g-overlay-group-apart-offset.png",
            "stock-g-overlay-group-apart-zero.png",
            "stock-g-overlay-group-apart.png",
            "stock-g-overlay-group-leafplane.png",
            "stock-g-overlay-leafplane.png",
            "stock-o-overlay-group-apart.png",
            "stock-q-overlay-group-apart-size2.png"};
        return aliases.contains(file) ? "stock-overlay.png" : file;
    }
    static QStringList outlinePhases(bool sizeTwo = false) {
        QFile file(QString(FUGUE_TEST_DATA_DIR)+"/master-tree-world/"+
            (sizeTwo ? "stock-outline2-observations.json" : "stock-outline-observations.json"));
        if (!file.open(QIODevice::ReadOnly)) return {};
        QStringList out;
        for (const auto& value : QJsonDocument::fromJson(file.readAll()).array())
            out.push_back(value.toObject()["phase"].toString());
        return out;
    }
    static QStringList wideOutlinePhases() {
        QFile file(QString(FUGUE_TEST_DATA_DIR)+"/master-tree-world/stock-outline-wide-observations.json");
        if (!file.open(QIODevice::ReadOnly)) return {};
        QStringList out;
        for (const auto& value : QJsonDocument::fromJson(file.readAll()).array())
            out.push_back(value.toObject()["phase"].toString());
        return out;
    }
    static QStringList phases() {
        return QStringList{"overlay","overlay-tint","overlay-under-tint","overlay-alpha","overlay-zero",
            "nested","nested-tint","nested-zero","nested-same-tint","nested-deep-tint",
            "nested-childzero","overlay-nested-tint",
            "overlay-group-tint","nested-group-tint","overlay-nested-group-tint",
            "g-overlay-group", "g-overlay-group-tint", "g-overlay-group-alpha",
            "g-overlay-group-zero", "g-overlay-group-ownicon", "g-overlay-group-leafplane",
            "g-overlay-group-offset", "g-overlay-group-apart-offset",
            "g-overlay-group-apart", "g-overlay-under-group-apart-tint",
            "g-overlay-group-apart-alpha", "g-overlay-group-apart-leafalpha",
            "g-overlay-group-apart-leafplane", "g-overlay-group-apart-leafoffset",
            "g-overlay-group-apart-zero", "g-overlay-nested-group-overlap",
            "g-overlay-nested-group-overlap-alpha", "g-overlay-nested-group-overlap-tint",
            "g-overlay-nested-group-apart-overlap-alpha", "g-overlay-nested-group-childapart-overlap-alpha",
            "g-overlay-nested-group-childgroup-overlap-alpha", "g-overlay-nested-group-same-overlap-alpha",
            "g-overlay-nested-group-deep-overlap-alpha", "g-overlay-nested-group-childzero-overlap",
            "g-overlay-leafplane", "g-overlay-leafplane-tint", "g-overlay-nested-group-leafplane-tint",
            "g-overlay-group-leafoffset", "g-overlay-group-apart-leafcenter-alpha",
            "g-overlay-group-leafcenter-alpha", "g-overlay-group-apart-clear",
            "g-overlay-nested-childgroup-overlap-alpha"} + outlinePhases() + outlinePhases(true) + wideOutlinePhases();
    }
    static QPoint point(int i) { return std::array<QPoint,4>{{{104,104},{136,104},{40,168},{60,168}}}[i]; }
    QStringList callbacks;
    QList<QPoint> points;
    QPoint clickPoint(int i) const { return points.at(i); }
    bool load(const QString& phase) {
        if (!ReplayFixture::load("frames-"+phase+".txt","master-tree-world")) return false;
        QFile file(QString(FUGUE_TEST_DATA_DIR)+"/master-tree-world/"+
            (phase.startsWith("w-") ? "stock-outline-wide-observations.json" :
             phase.startsWith("q-") ? "stock-outline2-observations.json" :
             phase.startsWith("o-") ? "stock-outline-observations.json" :
             phase.startsWith("g-") ? "stock-group-observations.json" : "stock-observations.json"));
        if (!file.open(QIODevice::ReadOnly)) return false;
        for (const auto& value : QJsonDocument::fromJson(file.readAll()).array()) {
            const auto row=value.toObject();
            if(row["phase"].toString()==phase) {
                for(const auto& point:row["pixelsTopLeft"].toArray()) {
                    const auto xy=point.toArray();points.push_back({xy[0].toInt(),xy[1].toInt()});
                }
                for(auto line:row["events"].toString().split('\n'))
                    if(line.startsWith("CLICK ")) callbacks.push_back(line.trimmed());
            }
        }
        return !points.empty() && callbacks.size()==points.size();
    }
    QString checkClick(const MapPointer& p,int i) const {
        const auto& ref=p.click.over; const auto& loc=p.click.loc;
        auto target=model::targetFromHit(session.world(),ref.kind,ref.ref,int(loc.x)+1,int(loc.y)+1,p.screen);
        const auto ap=session.world().appearances.find(target.appid);
        const QString name=ap==session.world().appearances.end() ? "" :
            QString::fromStdString(model::plainAtomName(model::parseJson(fieldsJson(ap->second))["name"].bytesOrStr()));
        const auto match=QRegularExpression("CLICK the (.*?) loc=(.*?) icon-x=(-?\\d+);icon-y=(-?\\d+);.*screen-loc=(.*)").match(callbacks[i]);
        if(!match.hasMatch()) return "invalid retained callback";
        if(name!=match.captured(1)) return "target: "+name+" expected "+match.captured(1);
        if(p.screen!=match.captured(2).isEmpty()) return "location ownership";
        if(p.click.iconX!=match.captured(3).toInt() || p.click.iconY!=match.captured(4).toInt())
            return QString("coordinates %1,%2 expected %3,%4").arg(p.click.iconX).arg(p.click.iconY).arg(match.captured(3),match.captured(4));
        if(p.click.hasVis) return "unexpected vis coordinates";
        const auto screen=QString("%1:%2,%3:%4").arg(p.click.screenX).arg(p.click.screenPX).arg(p.click.screenY).arg(p.click.screenPY);
        return screen==match.captured(5) ? QString() : "screen-loc: "+screen;
    }
};
}
