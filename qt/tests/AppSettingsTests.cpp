// AppSettingsTests.cpp - the settings file (QSettings, INI format).
//
// What Windows persists (rfhutil.cpp SaveLastUsedQNames 1575-1619, writeQMgr2Registry 2108-2165): the last queue
// manager, queue, user id, file path, the TLS and security exit values and the last 30 client queue managers.
// The password is stored there too (DPAPI) when "Save password" is on; here it never is (owner decision).

#include <QtTest>

#include <sys/stat.h>
#include <unistd.h>

#include "AppSettings.h"
#include "rfh_mq_session.h"

using rfhqt::AppSettings;

namespace
{
    const QByteArray kSecret = "Zq7!Canary-Pw-91xK";

    QByteArray readAll(const QString & path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
    }

    void writeFile(const QString & path, const QByteArray & content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(content);
    }

    int modeOf(const QString & path)
    {
        struct stat st;
        return (stat(path.toLocal8Bit().constData(), &st) == 0) ? (st.st_mode & 07777) : -1;
    }

    // Binary junk, a section header that never closes, lines without an equals sign and an empty key.
    QByteArray corruptFile()
    {
        return QByteArray("[[[\n") + QByteArray("\0\xff\xfe", 3) + " garbage without equals\n=\n[Display\ncodePage\n";
    }

    bool anyWarningMentions(const QStringList & warnings, const QString & text)
    {
        for (const QString & w : warnings)
        {
            if (w.contains(text))
            {
                return true;
            }
        }
        return false;
    }
}

class AppSettingsTests : public QObject
{
    Q_OBJECT

    QTemporaryDir sandbox;
    QString root;
    QString file;
    int counter = 0;

    // Every test goes through here: the file must be inside the temporary directory, never the real home.
    void requireSandbox(const AppSettings & s)
    {
        QVERIFY2(s.filePath().startsWith(root + "/"), qPrintable(s.filePath()));
        QVERIFY(!s.filePath().startsWith(QDir::homePath() + "/.config"));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(sandbox.isValid());
        root = QDir(sandbox.path()).canonicalPath();
        qputenv("HOME", (root + "/home").toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", (root + "/config").toLocal8Bit());
    }

    void init()
    {
        counter++;
        file = root + "/s" + QString::number(counter) + "/rfhutil.conf";
    }

    void cleanup()
    {
        QFile::setPermissions(QFileInfo(file).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QDir(root + "/s" + QString::number(counter)).removeRecursively();
    }

    // ---- location ------------------------------------------------------------------------------------------

    void theDefaultFileIsUnderTheXdgConfigDirectory()
    {
        AppSettings s;
        QCOMPARE(s.filePath(), root + "/config/rfhutil/rfhutil.conf");
        QCOMPARE(AppSettings::defaultFilePath(), root + "/config/rfhutil/rfhutil.conf");
        requireSandbox(s);
        qunsetenv("XDG_CONFIG_HOME");
        QCOMPARE(AppSettings::defaultFilePath(), root + "/home/.config/rfhutil/rfhutil.conf");
        qputenv("XDG_CONFIG_HOME", (root + "/config").toLocal8Bit());
    }

    void readingCreatesNoFile()
    {
        AppSettings s(file);
        requireSandbox(s);
        s.recentQueueManagers();
        s.displayCodePage();
        QVERIFY(!QFileInfo::exists(file));
    }

    // ---- defaults ------------------------------------------------------------------------------------------

    void defaultsAreDocumented()
    {
        AppSettings s(file);
        QVERIFY(s.recentQueueManagers().isEmpty());
        QVERIFY(s.recentQueues("QM1").isEmpty());
        QCOMPARE(s.lastDirectory("open"), QString());
        QCOMPARE(s.lastDirectory("save"), QString());
        QCOMPARE(s.lastDirectory("copybook"), QString());
        QVERIFY(s.windowGeometry().isEmpty());
        QCOMPARE(s.displayCodePage(), 1252);
        QCOMPARE(s.showSystemQueues(), false);
        QCOMPARE(AppSettings::DefaultCodePage, 1252);
        QCOMPARE(AppSettings::MaxRecent, 30);
        QVERIFY(s.warnings().isEmpty());
    }

    // ---- recent lists --------------------------------------------------------------------------------------

    void recentQueueManagersAreMostRecentFirst()
    {
        AppSettings s(file);
        requireSandbox(s);
        s.addQueueManager("QM1");
        s.addQueueManager("QM2");
        s.addQueueManager("QM3");
        QCOMPARE(s.recentQueueManagers(), (QStringList{ "QM3", "QM2", "QM1" }));
    }

    void aDuplicateMovesToTheTopWithoutGrowingTheList()
    {
        AppSettings s(file);
        requireSandbox(s);
        s.addQueueManager("QM1");
        s.addQueueManager("QM2");
        s.addQueueManager("QM3");
        s.addQueueManager("QM1");
        QCOMPARE(s.recentQueueManagers(), (QStringList{ "QM1", "QM3", "QM2" }));
    }

    void queueManagerNamesAreCaseSensitiveLikeMq()
    {
        AppSettings s(file);
        requireSandbox(s);
        s.addQueueManager("qm1");
        s.addQueueManager("QM1");
        QCOMPARE(s.recentQueueManagers(), (QStringList{ "QM1", "qm1" }));
    }

    void emptyNamesAreNotRemembered()
    {
        AppSettings s(file);
        requireSandbox(s);
        s.addQueueManager("");
        s.addQueue("QM1", "");
        s.addQueue("", "Q1");
        QVERIFY(s.recentQueueManagers().isEmpty());
        QVERIFY(s.recentQueues("QM1").isEmpty());
        QVERIFY(s.recentQueues("").isEmpty());
    }

    void theQueueManagerListIsLimitedTo30DroppingTheOldest()
    {
        AppSettings s(file);
        requireSandbox(s);
        for (int i = 0; i < 35; i++)
        {
            s.addQueueManager(QString("QM%1").arg(i));
        }
        const QStringList list = s.recentQueueManagers();
        QCOMPARE(list.size(), 30);
        QCOMPARE(list.first(), QString("QM34"));
        QCOMPARE(list.last(), QString("QM5"));
        QVERIFY(!list.contains("QM4"));
    }

    void queueListsAreKeptPerQueueManager()
    {
        AppSettings s(file);
        requireSandbox(s);
        s.addQueue("QM1", "Q.A");
        s.addQueue("QM2", "Q.B");
        s.addQueue("QM1", "Q.C");
        s.addQueue("QM1", "Q.A");
        QCOMPARE(s.recentQueues("QM1"), (QStringList{ "Q.A", "Q.C" }));
        QCOMPARE(s.recentQueues("QM2"), (QStringList{ "Q.B" }));
        QVERIFY(s.recentQueues("QM3").isEmpty());
    }

    void theQueueListIsLimitedTo30PerQueueManager()
    {
        AppSettings s(file);
        requireSandbox(s);
        for (int i = 0; i < 35; i++)
        {
            s.addQueue("QM1", QString("Q%1").arg(i));
        }
        s.addQueue("QM2", "OTHER");
        const QStringList list = s.recentQueues("QM1");
        QCOMPARE(list.size(), 30);
        QCOMPARE(list.first(), QString("Q34"));
        QCOMPARE(list.last(), QString("Q5"));
        QCOMPARE(s.recentQueues("QM2"), (QStringList{ "OTHER" }));
    }

    void namesWithSlashesCommasAndEqualsSignsRoundTrip()
    {
        // An MQSERVER style string goes into the queue manager box: "CHAN/TCP/host(1414),host2(1415)".
        const QString odd = "CH.SVRCONN/TCP/host(1414),host2(1415)";
        const QString eq = "a=b;c\\d \"q\" #x";
        {
            AppSettings s(file);
            requireSandbox(s);
            s.addQueueManager(odd);
            s.addQueueManager(eq);
            s.addQueue(odd, "Q/1,2");
        }
        AppSettings again(file);
        QCOMPARE(again.recentQueueManagers(), (QStringList{ eq, odd }));
        QCOMPARE(again.recentQueues(odd), (QStringList{ "Q/1,2" }));
    }

    // ---- other values --------------------------------------------------------------------------------------

    void lastDirectoriesAreKeptPerKind()
    {
        AppSettings s(file);
        requireSandbox(s);
        s.setLastDirectory("open", "/data/in");
        s.setLastDirectory("save", "/data/out");
        s.setLastDirectory("copybook", "/data/cpy");
        QCOMPARE(s.lastDirectory("open"), QString("/data/in"));
        QCOMPARE(s.lastDirectory("save"), QString("/data/out"));
        QCOMPARE(s.lastDirectory("copybook"), QString("/data/cpy"));
    }

    void anUnknownDirectoryKindIsRefusedWithAWarning()
    {
        AppSettings s(file);
        requireSandbox(s);
        s.setLastDirectory("../evil/kind", "/x");
        QCOMPARE(s.lastDirectory("../evil/kind"), QString());
        QVERIFY(anyWarningMentions(s.takeWarnings(), "directory kind"));
        QVERIFY(!QFileInfo::exists(file));
    }

    void windowGeometryAndDisplayOptionsRoundTripAcrossInstances()
    {
        const QByteArray geometry("\x01\xd9\xd0\xcb\x00\x03\x00\x00\xff\x00zz", 11);
        {
            AppSettings s(file);
            requireSandbox(s);
            s.setWindowGeometry(geometry);
            s.setDisplayCodePage(437);
            s.setShowSystemQueues(true);
            s.addQueueManager("QM1");
            s.addQueue("QM1", "Q1");
            s.setLastDirectory("open", "/data/in");
        }
        AppSettings again(file);
        QCOMPARE(again.windowGeometry(), geometry);
        QCOMPARE(again.displayCodePage(), 437);
        QCOMPARE(again.showSystemQueues(), true);
        QCOMPARE(again.recentQueueManagers(), (QStringList{ "QM1" }));
        QCOMPARE(again.recentQueues("QM1"), (QStringList{ "Q1" }));
        QCOMPARE(again.lastDirectory("open"), QString("/data/in"));
        QVERIFY(again.warnings().isEmpty());
    }

    void aSettingIsOnDiskWhenTheSetterReturns()
    {
        AppSettings s(file);
        requireSandbox(s);
        s.setDisplayCodePage(850);
        // a second reader that never saw the first
        AppSettings other(file);
        QCOMPARE(other.displayCodePage(), 850);
    }

    void theSettingsFileIsOwnerOnlyAndNoTemporaryFileIsLeftBehind()
    {
        AppSettings s(file);
        requireSandbox(s);
        s.addQueueManager("QM1");
        QCOMPARE(modeOf(file), 0600);
        const QStringList names = QDir(QFileInfo(file).absolutePath()).entryList(QDir::Files | QDir::Hidden);
        QCOMPARE(names, (QStringList{ "rfhutil.conf" }));
    }

    // ---- bad values ----------------------------------------------------------------------------------------

    void outOfRangeOrUnparsableCodePagesFallBackToTheDefaultWithAWarning()
    {
        const char * bad[] = { "0", "-5", "70000", "abc", "12.5", "" };
        for (const char * value : bad)
        {
            writeFile(file, QByteArray("[Display]\ncodePage=") + value + "\n");
            AppSettings s(file);
            requireSandbox(s);
            QCOMPARE(s.displayCodePage(), 1252);
            if (QByteArray(value).isEmpty())
            {
                continue;
            }
            QVERIFY2(anyWarningMentions(s.warnings(), "Display/codePage"), value);
        }
    }

    void aValidCodePageAtTheEdgesOfTheRangeIsAccepted()
    {
        writeFile(file, "[Display]\ncodePage=65535\n");
        QCOMPARE(AppSettings(file).displayCodePage(), 65535);
        writeFile(file, "[Display]\ncodePage=1\n");
        QCOMPARE(AppSettings(file).displayCodePage(), 1);
    }

    void aBadBooleanFallsBackToFalseWithAWarning()
    {
        writeFile(file, "[Display]\nshowSystemQueues=maybe\n");
        AppSettings s(file);
        QCOMPARE(s.showSystemQueues(), false);
        QVERIFY(anyWarningMentions(s.warnings(), "Display/showSystemQueues"));
    }

    void trueFalseOneAndZeroAreAcceptedAsBooleans()
    {
        const char * yes[] = { "true", "TRUE", "1" };
        for (const char * v : yes)
        {
            writeFile(file, QByteArray("[Display]\nshowSystemQueues=") + v + "\n");
            AppSettings s(file);
            QCOMPARE(s.showSystemQueues(), true);
            QVERIFY(s.warnings().isEmpty());
        }
        const char * no[] = { "false", "FALSE", "0" };
        for (const char * v : no)
        {
            writeFile(file, QByteArray("[Display]\nshowSystemQueues=") + v + "\n");
            AppSettings s(file);
            QCOMPARE(s.showSystemQueues(), false);
            QVERIFY(s.warnings().isEmpty());
        }
    }

    void aPartiallyWrittenFileKeepsWhatIsReadableAndWarnsAboutTheRest()
    {
        // cut off in the middle of a value and of a section header
        writeFile(file, "[Display]\ncodePage=850\nshowSystemQueues=tr");
        AppSettings s(file);
        requireSandbox(s);
        QCOMPARE(s.displayCodePage(), 850);
        QCOMPARE(s.showSystemQueues(), false);
        QVERIFY(anyWarningMentions(s.warnings(), "Display/showSystemQueues"));

        writeFile(file, "[Display]\ncodePage=850\n[Direc");
        AppSettings t(file);
        QCOMPARE(t.displayCodePage(), 850);
    }

    void aHugeRecentListIsReadOnlyUpToTheLimit()
    {
        QByteArray text = "[RecentQueueManagers]\nsize=100000\n";
        for (int i = 1; i <= 100; i++)
        {
            text += "" + QByteArray::number(i) + "\\name=QM" + QByteArray::number(i) + "\n";
        }
        writeFile(file, text);
        AppSettings s(file);
        QCOMPARE(s.recentQueueManagers().size(), 30);
        QCOMPARE(s.recentQueueManagers().first(), QString("QM1"));
    }

    void anOversizedGeometryValueIsIgnoredWithAWarning()
    {
        writeFile(file, "[Window]\ngeometry=@ByteArray(" + QByteArray(5000, 'a') + ")\n");
        AppSettings s(file);
        QVERIFY(s.windowGeometry().isEmpty());
        QVERIFY(anyWarningMentions(s.warnings(), "Window/geometry"));
    }

    void aCorruptFileGivesDefaultsAndAWarningAndDoesNotCrash()
    {
        const QByteArray garbage = corruptFile();
        writeFile(file, garbage);
        AppSettings s(file);
        requireSandbox(s);
        QCOMPARE(s.displayCodePage(), 1252);
        QCOMPARE(s.showSystemQueues(), false);
        QVERIFY(s.recentQueueManagers().isEmpty());
        QVERIFY(!s.warnings().isEmpty());
        // nothing was written by reading
        QCOMPARE(readAll(file), garbage);
    }

    void aCorruptFileIsKeptAsDotCorruptBeforeTheFirstSave()
    {
        const QByteArray garbage = corruptFile();
        writeFile(file, garbage);
        AppSettings s(file);
        s.setDisplayCodePage(850);
        QCOMPARE(readAll(file + ".corrupt"), garbage);
        QCOMPARE(AppSettings(file).displayCodePage(), 850);
        QVERIFY(anyWarningMentions(s.warnings(), ".corrupt"));
    }

    void anUnreadableFileIsNeverOverwritten()
    {
        if (geteuid() == 0)
        {
            QSKIP("root reads every file");
        }
        const QByteArray original = "[Display]\ncodePage=850\n";
        writeFile(file, original);
        QVERIFY(QFile::setPermissions(file, QFileDevice::WriteOwner));
        AppSettings s(file);
        QCOMPARE(s.displayCodePage(), 1252);
        QVERIFY(!s.warnings().isEmpty());
        s.setDisplayCodePage(437);
        QVERIFY(anyWarningMentions(s.warnings(), "not saved"));
        QVERIFY(QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QCOMPARE(readAll(file), original);
        // a later instance, now that the file can be read, sees the original value
        QCOMPARE(AppSettings(file).displayCodePage(), 850);
    }

    void aFailedWriteLeavesTheExistingFileIntact()
    {
        if (geteuid() == 0)
        {
            QSKIP("root can write anywhere");
        }
        const QByteArray original = "[Display]\ncodePage=850\n";
        writeFile(file, original);
        const QString dir = QFileInfo(file).absolutePath();
        AppSettings s(file);
        QCOMPARE(s.displayCodePage(), 850);
        // no write permission on the directory: the atomic replace cannot be made
        QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        s.setDisplayCodePage(437);
        QVERIFY(anyWarningMentions(s.takeWarnings(), "not saved"));
        QVERIFY(QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QCOMPARE(readAll(file), original);
        QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden).size(), 1);
        // once the directory is writable again the next save works
        s.setDisplayCodePage(437);
        QCOMPARE(AppSettings(file).displayCodePage(), 437);
    }

    void warningsAreTakenOnce()
    {
        writeFile(file, "[Display]\ncodePage=zzz\n");
        AppSettings s(file);
        QCOMPARE(s.displayCodePage(), 1252);
        QCOMPARE(s.displayCodePage(), 1252);
        QStringList first = s.takeWarnings();
        QCOMPARE(first.size(), 1);                 // the same bad value warns once, not on every read
        QVERIFY(s.takeWarnings().isEmpty());
    }

    // ---- unknown keys --------------------------------------------------------------------------------------

    void unknownKeysAndSectionsSurviveASave()
    {
        writeFile(file, "[Future]\nfeature=on\nlist=a, b\n\n[Display]\ncodePage=850\nnewOption=42\n");
        {
            AppSettings s(file);
            s.setDisplayCodePage(437);
            s.addQueueManager("QM1");
        }
        const QByteArray content = readAll(file);
        QVERIFY(content.contains("feature=on"));
        QVERIFY(content.contains("newOption=42"));
        QVERIFY(content.contains("codePage=437"));
        QVERIFY(content.contains("QM1"));
    }

    // ---- connection settings and passwords -----------------------------------------------------------------

    void theConnectionSettingsRoundTripWithoutThePasswordAndSecurityData()
    {
        rfh::ConnectOptions o;
        o.mode = rfh::Mode::Client;
        o.qmName = "QM1";
        o.channel = "DEV.APP.SVRCONN";
        o.connName = "host(1414)";
        o.heartbeat = 30;
        o.userId = "alice";
        o.password = kSecret.toStdString();
        o.sslEnabled = true;
        o.cipherSpec = "ANY_TLS12_OR_HIGHER";
        o.keyRepository = "/var/mqm/ssl/key";
        o.sslPeerName = "CN=qm1";
        o.sslClientValidation = true;
        o.fipsRequired = true;
        o.sslResetCount = 1000;
        o.certificateLabel = "ibmwebspheremqalice";
        o.securityExit = "SECEXIT(secExit)";
        o.securityData = "exit-data-token";
        o.localAddress = "10.0.0.5(5000)";
        {
            AppSettings s(file);
            requireSandbox(s);
            s.setConnection(o);
        }
        AppSettings again(file);
        const rfh::ConnectOptions r = again.connection();
        QVERIFY(r.mode == rfh::Mode::Client);
        QCOMPARE(r.qmName, o.qmName);
        QCOMPARE(r.channel, o.channel);
        QCOMPARE(r.connName, o.connName);
        QCOMPARE((int)r.heartbeat, 30);
        QCOMPARE(r.userId, o.userId);
        QCOMPARE(r.sslEnabled, true);
        QCOMPARE(r.cipherSpec, o.cipherSpec);
        QCOMPARE(r.keyRepository, o.keyRepository);
        QCOMPARE(r.sslPeerName, o.sslPeerName);
        QCOMPARE(r.sslClientValidation, true);
        QCOMPARE(r.fipsRequired, true);
        QCOMPARE(r.sslResetCount, 1000);
        QCOMPARE(r.certificateLabel, o.certificateLabel);
        QCOMPARE(r.securityExit, o.securityExit);
        QCOMPARE(r.localAddress, o.localAddress);
        QCOMPARE(r.password, std::string());
        QCOMPARE(r.securityData, std::string());       // exit user data may carry a secret: not stored
        QVERIFY(!readAll(file).contains("exit-data-token"));
    }

    void aCcdtUrlWithACredentialIsNotStored()
    {
        rfh::ConnectOptions o;
        o.mode = rfh::Mode::Client;
        o.ccdtUrl = "https://alice:" + kSecret.toStdString() + "@host/ccdt.json";
        AppSettings s(file);
        s.setConnection(o);
        QVERIFY(!readAll(file).contains(kSecret));
        QCOMPARE(s.connection().ccdtUrl, std::string());
        o.ccdtUrl = "https://host/ccdt.json";
        s.setConnection(o);
        QCOMPARE(s.connection().ccdtUrl, std::string("https://host/ccdt.json"));
    }

    void badStoredConnectionValuesFallBackToTheDefaultsWithAWarning()
    {
        writeFile(file, "[Connection]\nmode=carrier-pigeon\nsslResetCount=-3\nheartbeat=99999999\nsslEnabled=perhaps\n");
        AppSettings s(file);
        const rfh::ConnectOptions r = s.connection();
        const rfh::ConnectOptions d;
        QVERIFY(r.mode == rfh::Mode::Bindings);
        QCOMPARE(r.sslResetCount, 0);
        QCOMPARE((int)r.heartbeat, (int)d.heartbeat);
        QCOMPARE(r.sslEnabled, false);
        const QStringList w = s.takeWarnings();
        QVERIFY(anyWarningMentions(w, "Connection/mode"));
        QVERIFY(anyWarningMentions(w, "Connection/sslResetCount"));
        QVERIFY(anyWarningMentions(w, "Connection/heartbeat"));
        QVERIFY(anyWarningMentions(w, "Connection/sslEnabled"));
    }

    void secretKeysAreRecognisedByNameCaseInsensitively()
    {
        const char * secret[] = { "password", "Password", "connPW", "savePW", "userPassword", "Passwd", "passphrase",
                                  "pwd", "secret", "clientSecret", "token", "apiToken", "Connection/PASSWORD" };
        for (const char * k : secret)
        {
            QVERIFY2(AppSettings::isSecretKey(k), k);
        }
        const char * plain[] = { "codePage", "userId", "qmName", "geometry", "keyRepository", "showSystemQueues",
                                 "sslPeerName", "channel" };
        for (const char * k : plain)
        {
            QVERIFY2(!AppSettings::isSecretKey(k), k);
        }
    }

    void savingEverySettingLeavesNoTraceOfAPasswordInTheFile()
    {
        rfh::ConnectOptions o;
        o.mode = rfh::Mode::Client;
        o.qmName = "QM1";
        o.channel = "DEV.APP.SVRCONN";
        o.connName = "host(1414)";
        o.userId = "alice";
        o.password = kSecret.toStdString();               // in the in-memory model only
        o.securityData = kSecret.toStdString();
        o.ccdtUrl = "https://alice:" + kSecret.toStdString() + "@host/ccdt.json";
        {
            AppSettings s(file);
            requireSandbox(s);
            s.setConnection(o);
            s.addQueueManager("QM1");
            s.addQueue("QM1", "Q1");
            s.setLastDirectory("open", "/in");
            s.setLastDirectory("save", "/out");
            s.setLastDirectory("copybook", "/cpy");
            s.setWindowGeometry("geometry bytes");
            s.setDisplayCodePage(850);
            s.setShowSystemQueues(true);
        }
        // the whole settings directory, so a backup or a temporary file cannot hide a copy
        const QStringList names = QDir(QFileInfo(file).absolutePath()).entryList(QDir::Files | QDir::Hidden);
        QVERIFY(names.contains("rfhutil.conf"));
        for (const QString & name : names)
        {
            const QByteArray content = readAll(QFileInfo(file).absolutePath() + "/" + name);
            QVERIFY2(!content.contains(kSecret), qPrintable(name));
            QVERIFY2(!content.contains(kSecret.toBase64()), qPrintable(name));
            QVERIFY2(!content.contains(kSecret.toHex()), qPrintable(name));
            QVERIFY2(!content.contains(kSecret.toHex().toUpper()), qPrintable(name));
            QVERIFY2(!content.contains(QUrl::toPercentEncoding(QString::fromLatin1(kSecret))), qPrintable(name));
        }
        // and no key is called a password
        const QByteArray content = readAll(file).toLower();
        QVERIFY(!content.contains("password"));
        QVERIFY(!content.contains("passwd"));
        QVERIFY(!content.contains("pw="));
        // the in-memory model still holds it: the test did not simply lose the value
        QCOMPARE(QByteArray::fromStdString(o.password), kSecret);
    }

    void aSettingWhoseKeyIsNamedLikeAPasswordIsRefused()
    {
        AppSettings s(file);
        QVERIFY(!s.setValue("Connection/password", "x"));
        QVERIFY(!s.setValue("savePW", "1"));
        QVERIFY(!QFileInfo::exists(file) || !readAll(file).toLower().contains("password"));
        QVERIFY(s.setValue("Display/codePage", "850"));
        QVERIFY(anyWarningMentions(s.takeWarnings(), "password"));
    }
};

QTEST_MAIN(AppSettingsTests)
#include "AppSettingsTests.moc"
