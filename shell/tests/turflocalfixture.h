#pragma once
#include "shell/tests/localrelayfixture.h"
#include "shell/mapinput.h"
namespace fugue::shell::test {
struct TurfLocalFixture : LocalRelayFixture {
    bool load() {
        if (!LocalRelayFixture::load("frames.txt","turf-local-world")) return false;
        auto& w=session.worldMut();
        for (auto& [cell,start]:w.tileIconStart) start.ns=0;
        for (auto& [id,atom]:w.atoms) atom.iconStartNs=0;
        refresh(0); return true;
    }
    void refresh(int64_t ns) {
        auto reader=[&](uint32_t id,QByteArray& out){return read(id,out);};
        frame=terrain.prepare(session.world(),reader,ns,false);
        sprites=entities.prepare(session.world(),frame,reader,ns,nullptr,true);
    }
    std::optional<MapPointer> at(QPoint point) const {
        return mapPointerAt(point,frame.source.size(),{9,9},frame.source,frame.geometry,"map",true,sprites);
    }
};
}
