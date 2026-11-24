#pragma once
#include "tests/appearancejson.h"
#include "shell/entitymap.h"
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <bit>

namespace fugue::shell::test {
// Synthetic assets/geometry from relay-gate-world; expectations in the tests
// are the independent stock observations, not a CPU rendering oracle.
struct PlaneSourceFixture {
    model::World world;
    EntityMap entities;
    TurfMap::Frame frame;
    MapSprites sprites;
    std::map<uint32_t,QByteArray> resources;
    PlaneSourceFixture() {
        world.bounds={true,11,11,0,5,3,5,3};
        world.resources={{123,0},{124,0},{125,0},{126,0},{127,0}};
        frame.image=QImage(160,96,QImage::Format_RGBA8888); frame.image.fill(QColor(20,70,90));
        frame.source=frame.image.rect(); frame.geometry={{3,4,5,3},{11,11},32,0,17};
        const std::array<QColor,8> patches{{{240,30,70,1},{73,149,211,3},{123,61,199,7},{91,173,47,17},
                                           {37,119,203,63},{211,53,107,128},{33,181,229,193},{177,93,41,255}}};
        for (int i=0;i<5;++i) {
            QImage im(32,32,QImage::Format_RGBA8888); im.fill(Qt::transparent);
            for (int y=0;y<32;++y) for (int x=0;x<32;++x) {
                if (i==4) { if (x<16 && y<16) im.setPixelColor(x,y,QColor(230,210,30)); }
                else if (i==3) im.setPixelColor(x,y,patches[(y/16)*4+x/8]);
                else if (i!=1) {
                    if (x>=2 && x<=25 && y>=2 && y<=27) im.setPixelColor(x,y,QColor(240,30,30));
                    if (x>=2 && x<=9 && y>=2 && y<=8) im.setPixelColor(x,y,QColor(255,220,20));
                    if (x>=12 && x<=19 && y>=12 && y<=19) im.setPixelColor(x,y,Qt::transparent);
                    if (i==2 && x>=22 && x<=25 && y>=2 && y<=27)
                        im.setPixelColor(x,y,QColor(240,30,30,y<=12 ? 1 : 128));
                } else if (x<16 && y<16) im.setPixelColor(x,y,QColor(30,220,240));
            }
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); im.save(&buffer,"PNG");
            resources[123+i]=bytes;
        }
        auto& ink=world.appearances[1]; ink.fields.iconResID=0; ink.fields.layerBits=std::bit_cast<uint32_t>(3.f);
        setFields(ink, R"({"hasPlane":true,"plane":60,"appearanceFlags":516})");
        auto& atom=world.atoms[100]; atom.appid=1; atom.x=atom.y=6; atom.hasPos=true;
        atom.haveKey=true; atom.key=7; atom.renderIdentity=1;
        world.hudOrder={200,201}; world.hudAppid={{200,2},{201,3}};
        world.hud={{200,proto::Hud::Record{}},{201,proto::Hud::Record{}}}; world.hudIdentity={{200,{2,8,2}},{201,{2,9,3}}};
        auto& master=world.appearances[2]; master.fields.iconResID=0xffff; master.fields.layerBits=std::bit_cast<uint32_t>(3.f);
        setFields(master, R"({"hasPlane":true,"plane":60,"appearanceFlags":268435972,"screenLoc":"1,1","renderTarget":"*relay"})");
        auto& child=world.appearances[4]; child.fields.iconResID=1; child.fields.layerBits=std::bit_cast<uint32_t>(-1.f);
        setFields(child, R"({"pixelX":68,"pixelY":32,"hasPlane":true,"plane":-32767})");
        world.appearances[3].fields.iconResID=0xffff; world.appearances[3].fields.layerBits=std::bit_cast<uint32_t>(3.f);
        relay({});
    }
    void relay(QJsonObject changes) {
        QJsonObject fields{{"hasPlane",true},{"plane",70},{"appearanceFlags",516},
                           {"screenLoc","1,1"},{"renderSource","*relay"}};
        for (auto it=changes.begin();it!=changes.end();++it) fields[it.key()]=it.value();
        setFields(world.appearances[3], QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
        refresh();
    }
    void observation(const QJsonObject& row) {
        setFields(world.appearances[4], QJsonDocument(row["child"].toObject()).toJson(QJsonDocument::Compact).toStdString());
        relay(row["root"].toObject());
    }
    static QJsonArray capturePixelObservations(const QByteArray& bytes, bool pixelParity=true) {
        const auto corpus=QJsonDocument::fromJson(bytes).object();
        const auto appearances=corpus["appearances"].toArray();
        QJsonArray rows;
        for (const auto& value:corpus["rows"].toArray()) {
            auto row=value.toObject(); if (pixelParity) row["pixelParity"]=true;
            for (const char* role:{"root","master","member","child","overlap"}) {
                if (!row.contains(role)) continue;
                auto fields=corpus["appearanceDefaults"].toObject();
                const auto overrides=appearances[row[role].toInt()].toObject();
                for (auto it=overrides.begin();it!=overrides.end();++it) fields[it.key()]=it.value();
                row[role]=fields;
            }
            for (const char* role:{"pixels","clicks"}) row[role]=corpus[role].toArray()[row[role].toInt()];
            if (corpus.contains("samples")) {
                QJsonArray pixels; const auto samples=corpus["samples"].toArray();
                for (const auto& index:row["pixels"].toArray()) pixels.append(samples[index.toInt()]);
                row["pixels"]=pixels;
            }
            rows.append(row);
        }
        return rows;
    }
    void sourceObservation(const QJsonObject& row) {
        for (const auto& [name,id]:std::initializer_list<std::pair<const char*,uint32_t>>{{"member",1},{"master",2},{"root",3}})
            setFields(world.appearances[id], QJsonDocument(row[name].toObject()).toJson(QJsonDocument::Compact).toStdString());
        world.appearances[1].fields.iconResID=row["yellow"].toBool() ? 4 : row["precision"].toBool() ? 3 : row["partial"].toBool() ? 2 : 0;
        if (row.contains("child"))
            setFields(world.appearances[4], QJsonDocument(row["child"].toObject()).toJson(QJsonDocument::Compact).toStdString());
        world.atoms.erase(101);
        if (row.contains("overlap")) {
            auto& ap=world.appearances[6]; ap.fields.iconResID=row["precision"].toBool() ? 3 : 1; ap.fields.layerBits=std::bit_cast<uint32_t>(4.f);
            setFields(ap, QJsonDocument(row["overlap"].toObject()).toJson(QJsonDocument::Compact).toStdString());
            auto& atom=world.atoms[101]; atom=world.atoms[100]; atom.appid=6; atom.key=10; atom.renderIdentity=4;
        }
        refresh();
    }
    void refresh(EntityMap::Stats* stats=nullptr) {
        sprites=entities.prepare(world,frame,[&](uint32_t id,QByteArray& out) {
            const auto it=resources.find(id); if(it==resources.end()) return false;
            out=it->second; return true;
        },0,stats,true);
    }
    MapPointer at(QPoint p) const {
        return *mapPointerAt(p,{160,96},{5,3},frame.source,frame.geometry,"main.map",true,sprites);
    }
    static QJsonArray blur(const char* bits) {
        return {QJsonObject{{"type",1},{"flags",1},{"paramMask",1},{"params",bits}}};
    }
};
}
