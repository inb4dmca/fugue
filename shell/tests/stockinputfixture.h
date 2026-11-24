#pragma once
#include "tests/appearancejson.h"
#include "shell/tests/localrelayfixture.h"
#include "shell/mapinput.h"
#include "model/statpanel.h"
#include "model/verbmenu.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace fugue::shell::test {
// Shared assertions for independent stock input receipts; no case registry.
struct StockInputFixture : LocalRelayFixture {
    std::string targetName(const MapPointer& p) const {
        const auto& ref=p.click.over; const auto& loc=p.click.loc;
        const auto target=model::targetFromHit(session.world(),ref.kind,ref.ref,int(loc.x)+1,int(loc.y)+1,p.screen);
        const auto ap=session.world().appearances.find(target.appid);
        return ap==session.world().appearances.end() ? std::string() :
            model::plainAtomName(model::parseJson(fieldsJson(ap->second))["name"].bytesOrStr());
    }
    QString checkClick(const MapPointer& p,const QJsonObject& expected) const {
        const auto& ref=p.click.over; const auto& loc=p.click.loc;
        const auto target=model::targetFromHit(session.world(),ref.kind,ref.ref,int(loc.x)+1,int(loc.y)+1,p.screen);
        const auto ap=session.world().appearances.find(target.appid);
        const auto name=ap==session.world().appearances.end() ? QString() :
            QString::fromStdString(model::plainAtomName(model::parseJson(fieldsJson(ap->second))["name"].bytesOrStr()));
        if(name!=expected["target"].toString()) return "target: "+name+" expected "+expected["target"].toString();
        if(p.screen!=expected["location"].isNull()) return "location ownership";
        if(!p.screen) {
            const auto xyz=expected["locationXYZ"].toArray();
            if(xyz!=QJsonArray{int(loc.x)+1,int(loc.y)+1,p.z+1}) return "world coordinates";
        }
        const auto params=expected["params"].toObject();
        if(p.click.iconX!=params["icon-x"].toString().toInt() || p.click.iconY!=params["icon-y"].toString().toInt()) return "icon coordinates";
        if(p.click.hasVis!=params.contains("vis-x")) return "vis presence";
        if(p.click.hasVis && (p.click.visX!=params["vis-x"].toString().toInt() || p.click.visY!=params["vis-y"].toString().toInt())) return "vis coordinates";
        const auto screen=QString("%1:%2,%3:%4").arg(p.click.screenX).arg(p.click.screenPX).arg(p.click.screenY).arg(p.click.screenPY);
        return screen==params["screen-loc"].toString() ? QString() : "screen coordinates";
    }
};
}
