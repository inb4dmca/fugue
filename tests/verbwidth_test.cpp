// Stock 516.1688 narrow/wide advertisements, appearance membership and commands.
#include <QtTest>
#include "tests/testmain.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "net/handshake.h"
#include "net/input.h"
#include "model/world.h"
#include "model/verbmenu.h"
#include "model/statpanel.h"

using namespace fugue;
namespace {
Bytes bytes(const QString& hex) {
    const auto b = QByteArray::fromHex(hex.toLatin1());
    return Bytes(b.begin(), b.end());
}
QJsonObject corpus() {
    QFile f(QStringLiteral(FUGUE_TEST_DATA_DIR "/verb-lookup-world/stock-observations.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
struct Replay {
    net::InputModel input;
    model::Session model;
    bool wide = false;
    std::vector<Bytes> commands;
    void load(const QJsonObject& data) {
        for (const auto& value : data.value("rows").toArray()) {
            const auto row = value.toObject();
            const auto type = row.value("type").toString();
            const auto body = bytes(row.value("body").toString());
            if (type == "0001-header") {
                net::HandshakeReply reply;
                QVERIFY(net::parseHandshakeReply(body, reply));
                wide = reply.wideVerbIds();
                input.setWideVerbIds(wide);
                model.setWideVerbIds(wide);
            } else if (row.value("dir").toString() == "s2c") {
                const auto id = uint16_t(type.toUInt(nullptr, 16));
                input.onFrame(id, body);
                model.ingest(id, body);
            } else {
                auto normalized = body;
                // Stock's /client src-ref is unused; compare all other bytes exactly.
                const size_t ref = 2 + (wide ? 4 : 2) + 1;
                QVERIFY(normalized.size() >= ref + 4);
                std::fill(normalized.begin() + ref, normalized.begin() + ref + 4, 0);
                commands.push_back(std::move(normalized));
            }
        }
    }
    net::CommandResult command(const char* text) {
        return input.verbCommand(text, model.world().haveSelf, [&](uint32_t id) {
            std::vector<net::SrcCandidate> out;
            for (const auto& candidate : model::verbSrcCandidates(model.world(), id, true))
                out.push_back({{candidate.src.kind, candidate.src.ref}, candidate.name, false});
            return out;
        });
    }
};
}
class VerbWidthTest : public QObject {
    Q_OBJECT
private slots:
    void stock_data() {
        QTest::addColumn<QString>("name");
        for (const char* name : {"small", "mid", "wide_low", "wide_high"})
            QTest::newRow(name) << QString(name);
    }
    void stock() {
        QFETCH(QString, name);
        const auto data = corpus();
        QVERIFY(data.contains(name));
        Replay replay;
        replay.load(data.value(name).toObject());
        QCOMPARE(replay.wide, name.startsWith("wide_"));
        QCOMPARE(replay.model.ingestErrors(), 0);
        QCOMPARE(replay.input.verbs().size(), size_t(3));
        const auto* action = replay.input.verbByName("Probe Action");
        QVERIFY(action);
        QCOMPARE(action->wideId, replay.wide);
        QCOMPARE(action->id, name == "small" || name == "wide_low" ? uint32_t(3) : name == "mid" ? uint32_t(33003) : uint32_t(66004));
        const auto& world = replay.model.world();
        QVERIFY(world.haveClientAppid && world.haveSelf);
        const auto& appearance = world.appearances.at(world.clientAppid);
        const auto& verbs = appearance.fields.verbs;
        QVERIFY(std::find(verbs.begin(), verbs.end(), action->id) != verbs.end());
        const auto candidates = model::verbSrcCandidates(world, action->id, true);
        QCOMPARE(candidates.size(), size_t(1));
        QCOMPARE(candidates[0].src.kind, uint8_t(8));
        const model::VerbDef def = model::verbDefOf(world.verbById.at(action->id));
        QCOMPARE(def.id, action->id);
        std::vector<uint32_t> scope;
        QVERIFY(model::verbScope(world, scope));
        QVERIFY(std::find(scope.begin(), scope.end(), action->id) != scope.end());
        QCOMPARE(replay.commands.size(), size_t(5));
        if (name == "wide_high") {
            // Stock truncated these high IDs and invoked nothing. Do not alias to
            // a different low-ID verb, or silently claim high-ID dispatch support.
            QVERIFY(model::verbSrcCandidates(world, uint16_t(action->id), true).empty());
            QVERIFY(!replay.input.verbs().count(uint16_t(action->id)));
            const auto result = replay.command("Probe-Action");
            QVERIFY(result.frames.empty());
            QVERIFY(result.error.find("Unsupported verb ID") != std::string::npos);
        } else {
            size_t index = 0;
            for (const char* cmd : {"Probe-Action", "Probe-Action", "Probe-Action", "Late-Probe", "Probe-Text hello"}) {
                const auto result = replay.command(cmd);
                QVERIFY2(result.error.empty(), result.error.c_str());
                QCOMPARE(result.frames.size(), size_t(1));
                QCOMPARE(result.frames[0].body, replay.commands[index++]);
            }
            const auto pending = replay.command("Probe-Text");
            QVERIFY(pending.missing.has_value());
            const auto completed = net::InputModel::completeArg(*pending.missing, "hello", true);
            QCOMPARE(completed.frames.size(), size_t(1));
            QCOMPARE(completed.frames[0].body, replay.commands.back());
        }
        QVERIFY(replay.command("Missing-Probe").frames.empty());
    }
    void negotiatedModeAndMalformedFrames() {
        const auto data = corpus();
        Replay replay;
        // The same consumer can be reconfigured; no width guessed from the verb
        // ID, empty-name prefix, or the unrelated 0x003b high flag.
        for (const char* name : {"wide_low", "small", "wide_low", "mid"}) {
            replay.load(data.value(name).toObject());
            const auto result = replay.command("Probe-Action");
            QVERIFY2(result.error.empty(), result.error.c_str());
            const size_t expected = replay.wide ? 12 : 10;
            QCOMPARE(result.frames.at(0).body.size(), expected);
        }
        net::InputModel input;
        input.setWideVerbIds(true);
        const auto good = bytes("0300000050726f626520416374696f6e000000ff000400");
        for (size_t size = 0; size < good.size(); ++size) {
            input.onFrame(0x11, ByteSpan(good).first(size));
            QVERIFY(input.verbs().empty());
        }
        auto trailing = good;
        trailing.push_back(0);
        input.onFrame(0x11, trailing);
        QVERIFY(input.verbs().empty());
        input.onFrame(0x11, good);
        QCOMPARE(input.verbs().size(), size_t(1));
    }
};
FUGUE_TEST_GUILESS_MAIN(VerbWidthTest)
#include "verbwidth_test.moc"
