#pragma once
#include "shell/entitymap.h"
#include "shell/localtargets.h"
#include <QFile>
#include <QDir>
namespace fugue::shell::test {
struct LocalRelayFixture {
    model::Session session;
    TurfMap terrain;
    EntityMap entities;
    TurfMap::Frame frame;
    MapSprites sprites;
    std::map<uint32_t,QByteArray> resources;
    bool read(uint32_t id,QByteArray& out) const {
        const auto it=resources.find(id); if(it==resources.end()) return false; out=it->second; return true;
    }
    void refresh() {
        auto reader=[&](uint32_t id,QByteArray& out){return read(id,out);};
        frame=terrain.prepare(session.world(),reader,0,false);
        sprites=entities.prepare(session.world(),frame,reader,0,nullptr,true);
    }
    bool load(const QString& frames = "frames.txt", const QString& fixture = "local-relay-world", const QString& assetsFixture = {}) {
        const QString dir=QString(FUGUE_TEST_DATA_DIR)+'/'+fixture;
        const QString resDir=QString(FUGUE_TEST_DATA_DIR)+'/'+(assetsFixture.isEmpty() ? fixture : assetsFixture)+"/res";
        QFile wire(dir+'/'+frames); if(!wire.open(QIODevice::ReadOnly)) return false;
        for(auto line:wire.readAll().split('\n')) {
            if(!line.startsWith("0x")) continue;
            const int at=int(line.indexOf(' ',line.indexOf("len=")));
            const auto body=QByteArray::fromHex(line.mid(at+1));
            session.ingest(uint16_t(line.mid(2,4).toUInt(nullptr,16)),Bytes(body.begin(),body.end()));
        }
        for(const auto& name:QDir(resDir).entryList(QDir::Files)) {
            QFile file(resDir+'/'+name); if(!file.open(QIODevice::ReadOnly)) return false;
            const auto data=file.readAll(); const auto id=name.toUInt(nullptr,16); resources[id]=data;
            session.noteResource(id,Bytes(data.begin(),data.end()),"");
        }
        refresh(); return !sprites.empty();
    }
};
}
