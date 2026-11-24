#pragma once
#include "tests/appearancejson.h"
#include "shell/tests/replayfixture.h"
#include <bit>

namespace fugue::shell::test {
// Existing independent wire/assets plus the declared placement fixture inputs.
// Expected pixels and callbacks come only from stock, not from these mutations.
struct MasterPlacementFixture : ReplayFixture {
    uint32_t rootId = 0;
    static QJsonArray rows() {
        QFile file(QString(FUGUE_TEST_DATA_DIR)+"/master-tree-world/stock-placement.json");
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).array() : QJsonArray{};
    }
    void setLocation(const QString& location) {
        auto& ap=session.worldMut().appearances.at(rootId);
        auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(ap))).object();
        fields["screenLoc"]=location;
        setFields(ap, QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
        refresh();
    }
    bool load(const QJsonObject& row) {
        if(!ReplayFixture::load("frames-overlay.txt","master-tree-world")) return false;
        auto& w=session.worldMut();
        rootId=w.hudAppid.begin()->second;
        const auto original=w.appearances.at(rootId);
        auto root=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(original))).object();
        const auto childId=uint32_t(root["overlays"].toArray()[0].toInt());
        auto& child=w.appearances.at(childId);
        auto leaf=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(child))).object();
        const auto kind=row["kind"].toString();
        if(kind.contains("apart")) leaf["appearanceFlags"]=double(uint32_t(leaf["appearanceFlags"].toDouble())|0x08000000u);
        if(kind.contains("leafplane")) {leaf["hasPlane"]=true;leaf["plane"]=2;}
        setFields(child, QJsonDocument(leaf).toJson(QJsonDocument::Compact).toStdString());
        root["screenLoc"]=row["screenLoc"];
        root["overlays"]=QJsonArray{};root["underlays"]=QJsonArray{};
        if(kind.contains("group")) root["appearanceFlags"]=double(uint32_t(root["appearanceFlags"].toDouble())|0x04000000u);
        if(kind.contains("overlay")) root["overlays"]=QJsonArray{int(childId)};
        if(kind.contains("underlay")) root["underlays"]=QJsonArray{int(childId)};
        if(kind.contains("nested")) {
            const auto nestedId=w.appearances.rbegin()->first+1;
            auto nested=original;
            auto fields=QJsonDocument::fromJson(QByteArray::fromStdString(fieldsJson(original))).object();
            fields["screenLoc"]="";fields["hasPlane"]=true;fields["plane"]=2;
            fields["overlays"]=QJsonArray{};fields["underlays"]=QJsonArray{};
            nested.fields.layerBits=std::bit_cast<uint32_t>(-1.0f);
            setFields(nested, QJsonDocument(fields).toJson(QJsonDocument::Compact).toStdString());
            w.appearances[nestedId]=nested;
            auto children=root["overlays"].toArray();children.append(int(nestedId));root["overlays"]=children;
        }
        root["filters"]=kind.contains("blur") ? QJsonArray{QJsonObject{{"type",1},{"flags",1},{"paramMask",1},
            {"params",kind.contains("blur1") ? "0000803f" : "00000000"}}} : QJsonArray{};
        setFields(w.appearances.at(rootId), QJsonDocument(root).toJson(QJsonDocument::Compact).toStdString());
        refresh();return !sprites.empty();
    }
};
}
