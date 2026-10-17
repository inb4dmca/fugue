// Player macro lists (skin/usermacros.h): the Client > Macros layer.
// File format: the bytes stock DreamSeeker 516.1684 writes to cfg/macros.dmf parse, and what we write reads back identically in the same shape.
// Editing: the entry editor's commit rule, including that a blank key or command deletes.
// Precedence: skin set > this game's list > global list; a key the skin binds is in use.
// Storage: paths under the state dir, with a save/load round trip.
#include <QTemporaryDir>
#include <QtTest>

#include "skin/dmf.h"
#include "skin/usermacros.h"

using namespace fugue::skin;

namespace {
// Stock DreamSeeker's output after F6 -> hello then F1 -> zap user: "macro " entries with CRLF, newest first.
const char* kDsGlobal = "macro \r\n\telem \r\n\t\tname = \"F1\"\r\n\t\tcommand = \"zap user\"\r\n\telem \r\n\t\tname = \"F6\"\r\n"
                        "\t\tcommand = \"hello\"\r\n\r\n";

// Stock DreamSeeker's per-game file after F6 -> Rest then R -> zap userR: newest entry first, so the file is not sorted by key.
const char* kDsGame = "macro \r\n\telem \r\n\t\tname = \"R\"\r\n\t\tcommand = \"zap userR\"\r\n\telem \r\n\t\tname = \"F6\"\r\n"
                      "\t\tcommand = \"Rest\"\r\n\r\n";

MacroKey key(const char* spec, bool* rep = nullptr) {
    MacroKey k;
    bool r = false;
    parseMacroName(spec, k, r);
    if (rep) *rep = r;
    return k;
}

MacroSet skinSet(const char* dmf) {
    ParseResult r = parseDmf(dmf);
    if (!r.ok) qFatal("bad test skin: %s", r.error.c_str());
    return macroSetFromBlock(r.file.blocks.at(0));
}
}  // namespace

class UserMacros : public QObject {
    Q_OBJECT
private slots:
    void readsDreamSeekersOwnFile() {
        std::string err;
        MacroSet s = parseUserMacros(kDsGlobal, &err);
        QVERIFY2(err.empty(), err.c_str());
        QCOMPARE(int(s.entries.size()), 2);
        const MacroEntry* f1 = s.lookup(key("F1"), false);
        QVERIFY(f1);
        QCOMPARE(QString::fromStdString(f1->command), QString("zap user"));
        QCOMPARE(QString::fromStdString(s.lookup(key("F6"), false)->command), QString("hello"));
    }
    void readsAPerGameFileNewestFirst() {
        std::string err;
        MacroSet s = parseUserMacros(kDsGame, &err);
        QVERIFY2(err.empty(), err.c_str());
        QCOMPARE(int(s.entries.size()), 2);
        QCOMPARE(QString::fromStdString(s.lookup(key("F6"), false)->command), QString("Rest"));
        QCOMPARE(QString::fromStdString(s.lookup(key("R"), false)->command), QString("zap userR"));
        MacroSet r = parseUserMacros(formatUserMacros(s));  // what we write back reads the same
        QCOMPARE(int(r.entries.size()), 2);
        QCOMPARE(QString::fromStdString(r.lookup(key("F6"), false)->command), QString("Rest"));
    }
    void writesDreamSeekersShape() {
        MacroSet s;
        QVERIFY(editUserMacro(s, "", "F6", "hello"));
        QVERIFY(editUserMacro(s, "", "F1", "zap user"));
        std::string out = formatUserMacros(s);
        std::string ds = kDsGlobal;
        std::string lf;
        for (char c : ds) if (c != '\r') lf.push_back(c);
        QCOMPARE(QString::fromStdString(out), QString::fromStdString(lf));  // unnamed datum, same rows; ours is sorted by key, LF endings
    }
    void roundTripsEscapesIdsAndFlags() {
        MacroSet s;
        QVERIFY(editUserMacro(s, "", "CTRL+SHIFT+F8+REP", "say \"hi\\there\"", "myid"));
        QVERIFY(editUserMacro(s, "", "WEST+UP", "MoveKey 8 0"));
        MacroSet r = parseUserMacros(formatUserMacros(s));
        QCOMPARE(int(r.entries.size()), 2);
        bool rep = false;
        MacroKey k = key("SHIFT+CTRL+F8+REP", &rep);  // modifier order does not matter
        QVERIFY(rep);
        const MacroEntry* e = r.lookup(k, true);
        QVERIFY(e);
        QCOMPARE(QString::fromStdString(e->command), QString("say \"hi\\there\""));
        QCOMPARE(QString::fromStdString(e->id), QString("myid"));
        QVERIFY(r.lookup(key("WEST+UP"), false));
        QVERIFY(!r.lookup(key("WEST"), false));  // +UP is part of the identity
    }
    void editorCommitRule() {
        MacroSet s;
        QVERIFY(editUserMacro(s, "", "F6", "hello"));
        QVERIFY(editUserMacro(s, "", "f6", "zap"));  // re-binding the same key replaces it
        QCOMPARE(int(s.entries.size()), 1);
        QCOMPARE(QString::fromStdString(s.lookup(key("F6"), false)->command), QString("zap"));
        QVERIFY(editUserMacro(s, "F6", "F7", "zap"));  // editing the key moves the binding
        QVERIFY(!s.lookup(key("F6"), false));
        QVERIFY(s.lookup(key("F7"), false));
        QVERIFY(editUserMacro(s, "F7", "F7", "   "));  // blank command deletes
        QCOMPARE(int(s.entries.size()), 0);
        QVERIFY(editUserMacro(s, "", "F9", "x"));
        QVERIFY(editUserMacro(s, "F9", "", "x"));  // blank key deletes
        QCOMPARE(int(s.entries.size()), 0);
        QVERIFY(editUserMacro(s, "", "F9", "x"));
        QVERIFY(!editUserMacro(s, "F9", "+REP", "x"));  // names no key: refused, nothing changed
        QVERIFY(s.lookup(key("F9"), false));
    }
    void precedenceIsSkinThenGameThenGlobal() {
        // Layers: skin F1 -> hello; global F1 -> zap user, F6 -> hello; this game F6 -> zap game, F7 -> hello.
        MacroSet skin = skinSet("macro \"macro\"\n\telem\n\t\tname = \"F1\"\n\t\tcommand = \"hello\"\n"
                                "\telem\n\t\tname = \"F2\"\n\t\tcommand = \"x\"\n\t\tis-disabled = true\n");
        MacroSet global, game;
        editUserMacro(global, "", "F1", "zap user");
        editUserMacro(global, "", "F6", "hello");
        editUserMacro(global, "", "F2", "global f2");
        editUserMacro(game, "", "F6", "zap game");
        editUserMacro(game, "", "F7", "hello");
        auto fire = [&](const char* k) {
            const MacroEntry* e = lookupLayered(&skin, game, global, key(k), false);
            return e ? QString::fromStdString(e->command) : QString("<none>");
        };
        QCOMPARE(fire("F1"), QString("hello"));  // the skin's: the key is in use
        QCOMPARE(fire("F6"), QString("zap game"));  // this game's beats global
        QCOMPARE(fire("F7"), QString("hello"));  // this game's alone
        QCOMPARE(fire("F2"), QString("<none>"));  // a disabled skin binding still claims its key
        QCOMPARE(fire("F3"), QString("<none>"));
        MacroSet noGame;
        QCOMPARE(QString::fromStdString(lookupLayered(&skin, noGame, global, key("F6"), false)->command), QString("hello"));
        QCOMPARE(QString::fromStdString(lookupLayered(nullptr, noGame, global, key("F1"), false)->command), QString("zap user"));
    }
    void storage() {
        QCOMPARE(QString::fromStdString(userMacrosPath("/s", "")), QString("/s/macros.dmf"));
        QCOMPARE(QString::fromStdString(worldKeyFor("127.0.0.1", 45000)), QString("127.0.0.1_45000"));
        QCOMPARE(QString::fromStdString(worldKeyFor("a/../b:c", 1)), QString("a_.._b_c_1"));  // no path separators survive
        QCOMPARE(QString::fromStdString(userMacrosPath("/s", "h_1")), QString("/s/macros/h_1/macros.dmf"));
        QTemporaryDir d;
        QVERIFY(d.isValid());
        std::string p = userMacrosPath(d.path().toStdString(), worldKeyFor("127.0.0.1", 47915));
        QCOMPARE(int(loadUserMacros(p).entries.size()), 0);  // missing file = empty, not an error
        MacroSet s;
        editUserMacro(s, "", "F6", "hello");
        std::string err;
        QVERIFY2(saveUserMacros(p, s, &err), err.c_str());  // creates macros/<key>/
        MacroSet r = loadUserMacros(p);
        QCOMPARE(int(r.entries.size()), 1);
        QCOMPARE(QString::fromStdString(r.lookup(key("F6"), false)->command), QString("hello"));
        QVERIFY(!QFile::exists(QString::fromStdString(p + ".tmp")));
        QCOMPARE(int(parseUserMacros("window \"x\"\n\tjunk\n").entries.size()), 0);  // a damaged file reads as empty
    }
};

QTEST_GUILESS_MAIN(UserMacros)
#include "usermacros_test.moc"
