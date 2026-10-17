// Clear cache (pager/cache.h) removes exactly the identity-<ckey> files, the remembered-guest flag, the resource cache and ~/.fugue/cauth.txt,
// and keeps everything else (pager.json, the sid files, client.log, ~/.byond). Temp HOME only.
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include "account/store.h"
#include "pager/cache.h"
#include "pager/launch.h"

using namespace fugue;
using namespace fugue::pager;

namespace {
struct Home {
    QTemporaryDir dir;
    Home() {
        qputenv("HOME", dir.path().toUtf8());
        for (auto v : {"FUGUE_PAGER_CONFIG", "FUGUE_IDENTITY", "FUGUE_BYOND_SID", "FUGUE_KEYTXT", "XDG_STATE_HOME"}) qunsetenv(v);
    }
    QString p(const QString& rel) const { return dir.path() + "/" + rel; }
    void put(const QString& rel, const QByteArray& b) {
        QDir().mkpath(QFileInfo(p(rel)).absolutePath());
        QFile f(p(rel));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(b);
    }
    bool has(const QString& rel) const { return QFileInfo::exists(p(rel)); }
};
}

class PagerCache : public QObject {
    Q_OBJECT
private slots:
    void emptyHomeHasNothingToClear() {
        Home h;
        CachePlan p = planClearCache();
        QVERIFY(p.empty());
        QVERIFY(QString::fromStdString(describeClearCache(p)).startsWith("There is nothing cached to remove."));
        std::string err;
        QVERIFY(clearCache(&err).empty());
        QVERIFY(err.empty());
    }
    // The Sign in window's account list: the ckey of every regular identity file, sorted; nothing else in ~/.fugue counts.
    void savedSignInNamesAreTheIdentityFiles() {
        Home h;
        QVERIFY(savedSignInNames().empty());
        QVERIFY(account::saveAccount("Some Key", account::Account{Bytes(21, 1), 1, 2}));
        QVERIFY(account::saveAccount("Other", account::Account{Bytes(21, 2), 3, 4}));
        h.put(".fugue/identity-", "x");                 // no name
        h.put(".fugue/identities/identity-x", "x");     // not at the top level
        QDir().mkpath(h.p(".fugue/identity-adir"));      // not a file
        QCOMPARE(savedSignInNames(), (std::vector<std::string>{"other", "somekey"}));
    }
    void removesExactlyIdentitiesGuestFlagAndResources() {
        Home h;
        QVERIFY(account::saveAccount("Some Key", account::Account{Bytes(21, 1), 1, 2}));   // identity-somekey
        QVERIFY(account::saveAccount("Other", account::Account{Bytes(21, 2), 3, 4}));      // identity-other
        QVERIFY(rememberGuest(true));
        h.put(".fugue/rsc/ab/abcdef01", QByteArray(1000, 'a'));
        h.put(".fugue/rsc/ab/abcdef02", QByteArray(24, 'b'));
        h.put(".fugue/rsc/0c/0c000001", QByteArray(2048, 'c'));
        h.put(".fugue/rsc/names", "abcdef01 some.dmi\n");
        // kept: settings, machine identity, logs, macros, BYOND's own files, and anything not in the cache layout
        h.put(".fugue/pager.json", "{\"servers\":[]}\n");
        h.put(".fugue/byond-sid", "0123456789abcdef01234567\n");
        h.put(".fugue/sid", "0123456789abcdef01234567");
        h.put(".fugue/client.log", "log\n");
        h.put(".fugue/qt/other-setting", "x");
        h.put(".fugue/cauth.txt", "guest@127.0.0.1:52516 key=644788f;pwd=03000000010000000200000003;ctime=6ad240c8\n");   // removed
        h.put(".fugue/macros.dmf", "x");
        h.put(".byond/cfg/key.txt", "BEGIN KEY x\n`\nEND KEY\n");
        h.put(".fugue/rsc/ab/not-a-resource", "keep");
        h.put(".fugue/identity", "not identity-<ckey>");

        CachePlan p = planClearCache();
        QCOMPARE(int(p.items.size()), 5);
        QCOMPARE(QString::fromStdString(p.items[0].path), h.p(".fugue/identity-other"));
        QCOMPARE(QString::fromStdString(p.items[1].path), h.p(".fugue/identity-somekey"));
        QCOMPARE(QString::fromStdString(p.items[1].what), QString("Saved sign-in for somekey"));
        QCOMPARE(QString::fromStdString(p.items[2].path), h.p(".fugue/qt/pager-guest"));
        QCOMPARE(QString::fromStdString(p.items[3].path), h.p(".fugue/rsc"));
        QCOMPARE(p.items[3].files, uint64_t(4));   // three resources + the names log
        QCOMPARE(p.items[3].bytes, uint64_t(1000 + 24 + 2048 + 18));
        QCOMPARE(QString::fromStdString(p.items[4].path), h.p(".fugue/cauth.txt"));
        QCOMPARE(QString::fromStdString(p.items[4].what), QString("Saved server login keys"));
        const QString text = QString::fromStdString(describeClearCache(p));
        QVERIFY2(text.contains("Downloaded game resources — 3.0 KiB in 4 files"), qPrintable(text));
        QVERIFY2(text.contains("Remembered \"Play as guest\" choice"), qPrintable(text));
        QVERIFY2(text.contains("Kept: your saved servers and settings (pager.json)"), qPrintable(text));

        std::string err;
        CachePlan done = clearCache(&err);
        QVERIFY2(err.empty(), err.c_str());
        QCOMPARE(done.files(), p.files());
        QCOMPARE(done.bytes(), p.bytes());
        for (auto rel : {".fugue/identity-other", ".fugue/identity-somekey", ".fugue/qt/pager-guest", ".fugue/rsc/ab/abcdef01",
                         ".fugue/rsc/ab/abcdef02", ".fugue/rsc/0c/0c000001", ".fugue/rsc/names", ".fugue/rsc/0c", ".fugue/cauth.txt"})
            QVERIFY2(!h.has(rel), rel);
        for (auto rel : {".fugue/pager.json", ".fugue/byond-sid", ".fugue/sid", ".fugue/client.log", ".fugue/qt/other-setting",
                         ".fugue/macros.dmf", ".byond/cfg/key.txt", ".fugue/rsc/ab/not-a-resource", ".fugue/identity"})
            QVERIFY2(h.has(rel), rel);
        QVERIFY(!guestRemembered());
        QVERIFY(planClearCache().empty());
    }
    // An explicit $FUGUE_IDENTITY is a file someone pointed us at, not our cache: never listed, never removed.
    void explicitIdentityOverrideIsNotOurs() {
        Home h;
        h.put("elsewhere/id", "identity=00\ninstall_id=0x1\ninner_key=0x2\n");
        qputenv("FUGUE_IDENTITY", h.p("elsewhere/id").toUtf8());
        QVERIFY(planClearCache().empty());
        clearCache();
        QVERIFY(h.has("elsewhere/id"));
        qunsetenv("FUGUE_IDENTITY");
    }
    void humanSizes() {
        QCOMPARE(humanBytes(0), std::string("0 bytes"));
        QCOMPARE(humanBytes(1023), std::string("1023 bytes"));
        QCOMPARE(humanBytes(1536), std::string("1.5 KiB"));
        QCOMPARE(humanBytes(5u << 20), std::string("5.0 MiB"));
        QCOMPARE(humanBytes(3ULL << 30), std::string("3.00 GiB"));
    }
};
QTEST_MAIN(PagerCache)
#include "pager_cache_test.moc"
