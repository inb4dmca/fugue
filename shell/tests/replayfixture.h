#pragma once
#include "tests/appearancejson.h"
#include "shell/entitymap.h"
#include "shell/localtargets.h"
#include "shell/mapinput.h"
#include "model/statpanel.h"
#include "model/verbmenu.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace fugue::shell::test {
// Retained inputs through the production model and scene preparation. load()
// builds a snapshot; timed scenarios readFrames(), ingest() at recorded instants,
// then refresh() at each checkpoint. The wire reader does not interpret clocks.
struct ReplayFixture {
    model::Session session;
    TurfMap terrain;
    EntityMap entities;
    TurfMap::Frame frame;
    MapSprites sprites;
    std::map<uint32_t,QByteArray> resources;
    QString error;

    bool read(uint32_t id, QByteArray& out) const {
        const auto it=resources.find(id);
        if (it==resources.end()) return false;
        out=it->second;
        return true;
    }
    void refresh(int64_t sampleNs = 0) {
        auto reader=[&](uint32_t id,QByteArray& out){return read(id,out);};
        frame=terrain.prepare(session.world(),reader,sampleNs,false);
        sprites=entities.prepare(session.world(),frame,reader,sampleNs,nullptr,true);
    }
    template<class Consume>
    bool readFrames(const QString& frames, const QString& fixture, Consume consume) {
        error.clear();
        const QString dir=QString(FUGUE_TEST_DATA_DIR)+'/'+fixture;
        QFile wire(dir+'/'+frames);
        if (!wire.open(QIODevice::ReadOnly)) { error="Cannot read "+wire.fileName(); return false; }
        for (const auto& line:wire.readAll().split('\n')) {
            if (!line.startsWith("0x")) continue;
            const int at=int(line.indexOf(' ',line.indexOf("len=")));
            const auto body=at<0 ? QByteArray{} : QByteArray::fromHex(line.mid(at+1));
            consume(uint16_t(line.mid(2,4).toUInt(nullptr,16)),Bytes(body.begin(),body.end()));
        }
        return true;
    }
    void ingest(uint16_t type, const Bytes& body, int64_t arrivalNs) {
        session.setIngestClockNs(arrivalNs);
        session.ingest(type,body);
    }
    bool loadResources(const QString& fixture) {
        error.clear();
        const QString resDir=QString(FUGUE_TEST_DATA_DIR)+'/'+fixture+"/res";
        if (!QDir(resDir).exists()) { error="Missing resources: "+resDir; return false; }
        for (const auto& name:QDir(resDir).entryList(QDir::Files)) {
            QFile file(resDir+'/'+name);
            if (!file.open(QIODevice::ReadOnly)) { error="Cannot read "+file.fileName(); return false; }
            const auto data=file.readAll(); const auto id=name.toUInt(nullptr,16); resources[id]=data;
            session.noteResource(id,Bytes(data.begin(),data.end()),"");
        }
        return true;
    }
    bool load(const QString& frames = "frames.txt", const QString& fixture = "local-relay-world",
              const QString& assetsFixture = {}, int64_t sampleNs = 0) {
        if (!readFrames(frames,fixture,[&](uint16_t type,const Bytes& body) { session.ingest(type,body); })) return false;
        if (!loadResources(assetsFixture.isEmpty() ? fixture : assetsFixture)) return false;
        refresh(sampleNs);
        if (sprites.empty()) { error="Empty scene: "+fixture+'/'+frames; return false; }
        return true;
    }
    std::string targetName(const MapPointer& p) const {
        const auto& ref=p.click.over; const auto& loc=p.click.loc;
        const auto target=model::targetFromHit(session.world(),ref.kind,ref.ref,int(loc.x)+1,int(loc.y)+1,p.screen);
        const auto ap=session.world().appearances.find(target.appid);
        return ap==session.world().appearances.end() ? std::string() :
            model::plainAtomName(model::parseJson(fieldsJson(ap->second))["name"].bytesOrStr());
    }
    // Independent stock receipts supply the expected target and coordinates.
    QString checkClick(const MapPointer& p, const QJsonObject& expected) const {
        const auto& loc=p.click.loc;
        const auto name=QString::fromStdString(targetName(p));
        if (name!=expected["target"].toString()) return "target: "+name+" expected "+expected["target"].toString();
        if (p.screen!=expected["location"].isNull()) return "location ownership";
        if (!p.screen) {
            const auto xyz=expected["locationXYZ"].toArray();
            if (xyz!=QJsonArray{int(loc.x)+1,int(loc.y)+1,p.z+1}) return "world coordinates";
        }
        const auto params=expected["params"].toObject();
        if (p.click.iconX!=params["icon-x"].toString().toInt() || p.click.iconY!=params["icon-y"].toString().toInt()) return "icon coordinates";
        if (p.click.hasVis!=params.contains("vis-x")) return "vis presence";
        if (p.click.hasVis && (p.click.visX!=params["vis-x"].toString().toInt() || p.click.visY!=params["vis-y"].toString().toInt())) return "vis coordinates";
        const auto screen=QString("%1:%2,%3:%4").arg(p.click.screenX).arg(p.click.screenPX).arg(p.click.screenY).arg(p.click.screenPY);
        return screen==params["screen-loc"].toString() ? QString() : "screen coordinates";
    }
};
}
