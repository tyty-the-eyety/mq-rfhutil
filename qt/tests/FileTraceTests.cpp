// FileTraceTests.cpp - the trace file (Windows CRfhutilApp::startTrace, logTraceEntry, dumpTraceData,
// rfhutil.cpp 1817-2003 and 2384-2528).
//
// Expected lines are built by hand from those functions:
//   entry line  = strftime("%y-%m-%d:%H.%M.%S") + " " + text + "\n", flushed at once;
//   dump        = a label line "<label> at <8 hex digits>" through the entry path, then lines without a
//                 time stamp: "%4.4X" offset, a blank, the characters in groups of 8 separated by a blank
//                 (non-printable as '.'), padded so that the hex starts at column 41, then each group of
//                 4 bytes as 8 upper case hex digits preceded by a blank;
//   without verbose trace a dump stops after 8192 bytes, after the line "Dumping out first 8K bytes of N".

#include <QtTest>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

#include <thread>

#include "fake_mq.h"
#include "FileTrace.h"
#include "SessionController.h"
#include "rfh_trace.h"

using rfhqt::FileTrace;
using rfhqt::SessionController;

namespace
{
    const QDateTime kNow(QDate(2026, 10, 18), QTime(14, 5, 9));
    const QByteArray kStamp = "26-10-18:14.05.09";

    QByteArray readAll(const QString & path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
    }

    int modeOf(const QString & path)
    {
        struct stat st;
        return (stat(path.toLocal8Bit().constData(), &st) == 0) ? (st.st_mode & 07777) : -1;
    }

    // The first line of the header that enabling writes: a blank line, then two entries (rfhutil.cpp 2418-2436,
    // 2521-2525; the Windows locale, screen and MQ install lines have no Linux counterpart).
    const QByteArray kHeader = "\n" + kStamp + " RFHUtil started\n" + kStamp + " verboseTrace=0\n";

    QByteArray hexLines(const QByteArray & trace)
    {
        // the dump lines: those that do not start with a time stamp
        QByteArray out;
        const QList<QByteArray> lines = trace.split('\n');
        for (const QByteArray & line : lines)
        {
            if (!line.isEmpty() && !line.startsWith("26-"))
            {
                out += line + "\n";
            }
        }
        return out;
    }
}

class FileTraceTests : public QObject
{
    Q_OBJECT

    QTemporaryDir sandbox;
    QString root;
    QString path;
    int counter = 0;

    FileTrace * make(qint64 maxBytes = FileTrace::DefaultMaxBytes)
    {
        FileTrace * t = new FileTrace(path, maxBytes);
        t->setClock([]() { return kNow; });
        return t;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(sandbox.isValid());
        root = QDir(sandbox.path()).canonicalPath();
        // Nothing below may touch the real home directory.
        qputenv("HOME", (root + "/home").toLocal8Bit());
        qunsetenv("XDG_STATE_HOME");
        qunsetenv("RFHUTIL_TRACE_FILE");
        qunsetenv("RFHUTIL_VERBOSE_TRACE");
    }

    void init()
    {
        counter++;
        path = root + "/t" + QString::number(counter) + "/sub/trace.log";
        rfhSetTrace(nullptr);
    }

    void cleanup()
    {
        rfhSetTrace(nullptr);
        QDir(root + "/t" + QString::number(counter)).removeRecursively();
    }

    void thePathUsedByTestsIsInsideTheTemporaryDirectory()
    {
        QVERIFY(path.startsWith(root + "/"));
        QVERIFY(root.startsWith(QDir::tempPath()) || root.startsWith("/tmp"));
    }

    void nothingIsWrittenOrCreatedWhileDisabled()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(!t->isTraceEnabled());
        t->logTraceEntry("hello");
        t->dumpTraceData("x", reinterpret_cast<const unsigned char *>("abc"), 3);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!QFileInfo::exists(QFileInfo(path).absolutePath()));
    }

    void enablingCreatesTheDirectoryAndWritesTheHeader()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        QVERIFY(t->isTraceEnabled());
        QVERIFY(t->lastError().isEmpty());
        QCOMPARE(readAll(path), kHeader);
    }

    void theFileIsOwnerOnlyWhateverTheUmask()
    {
        const mode_t old = umask(0);
        std::unique_ptr<FileTrace> t(make());
        const bool ok = t->setEnabled(true);
        umask(old);
        QVERIFY(ok);
        QCOMPARE(modeOf(path), 0600);
        // the directories this created for the trace are owner only too
        QCOMPARE(modeOf(QFileInfo(path).absolutePath()), 0700);
    }

    void anEntryIsOneLineWithTheWindowsTimeStampAndIsOnDiskAtOnce()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        t->logTraceEntry("MQOPEN cc=0 rc=0");
        // no close, no flush call: a second reader sees the line already
        QCOMPARE(readAll(path), kHeader + kStamp + " MQOPEN cc=0 rc=0\n");
    }

    void theTimeStampIsLocalTimeWithTwoDigitYear()
    {
        std::unique_ptr<FileTrace> t(make());
        t->setClock([]() { return QDateTime(QDate(2031, 1, 2), QTime(3, 4, 5)); });
        QVERIFY(t->setEnabled(true));
        t->logTraceEntry("x");
        QVERIFY(readAll(path).endsWith("31-01-02:03.04.05 x\n"));
    }

    void anExistingTraceIsAppendedToNotTruncated()
    {
        {
            std::unique_ptr<FileTrace> t(make());
            QVERIFY(t->setEnabled(true));
            t->logTraceEntry("first run");
        }
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        t->logTraceEntry("second run");
        QCOMPARE(readAll(path), kHeader + kStamp + " first run\n" + kHeader + kStamp + " second run\n");
    }

    void anExistingWorldReadableFileOfTheUserIsTightenedToOwnerOnly()
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("old\n");
        f.close();
        QVERIFY(QFile::setPermissions(path, QFile::Permissions(0x6644)));
        QCOMPARE(modeOf(path), 0644);
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        QCOMPARE(modeOf(path), 0600);
        QVERIFY(readAll(path).startsWith("old\n"));
    }

    void disablingStopsTheOutputAndEnablingAgainAppends()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        QVERIFY(t->setEnabled(false));
        QVERIFY(!t->isTraceEnabled());
        t->logTraceEntry("while off");
        QCOMPARE(readAll(path), kHeader);
        QVERIFY(t->setEnabled(true));
        t->logTraceEntry("on again");
        QVERIFY(readAll(path).endsWith(kStamp + " on again\n"));
        QVERIFY(!readAll(path).contains("while off"));
    }

    void aDumpOfThirtySixBytesHasTheWindowsLayout()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        const unsigned char data[36] = {
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
            'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5',
            0x00, 'a', 0x7F, 0xFF };
        t->dumpTraceData("buffer", data, sizeof(data));

        const QByteArray trace = readAll(path);
        QVERIFY(QRegularExpression("\\n26-10-18:14\\.05\\.09 buffer at [0-9A-F]{8}\\n").match(QString::fromLatin1(trace)).hasMatch());
        // ASCII part ends in column 39; Windows then blanks column 40 and puts one more blank before every
        // group of four bytes, so the first group follows two blanks.
        const QByteArray expected =
            QByteArray("0000 ABCDEFGH IJKLMNOP QRSTUVWX YZ012345") + "  41424344 45464748 494A4B4C 4D4E4F50"
            " 51525354 55565758 595A3031 32333435\n"
            "0020 .a.." + QByteArray(33, ' ') + "00617FFF\n";
        QCOMPARE(hexLines(trace), expected);
    }

    void aDumpWithAnOddNumberOfBytesPadsTheHexToColumn41()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        t->dumpTraceData("odd", reinterpret_cast<const unsigned char *>("abc"), 3);
        // offset, blank, "abc", blanks up to column 41 (34 of them from column 8), then "616263"
        QCOMPARE(hexLines(readAll(path)), QByteArray("0000 abc") + QByteArray(34, ' ') + "616263\n");
    }

    void aDumpWithoutALabelSaysMemory()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        t->dumpTraceData(nullptr, reinterpret_cast<const unsigned char *>("a"), 1);
        QVERIFY(QRegularExpression("26-10-18:14\\.05\\.09 memory at [0-9A-F]{8}\\n").match(QString::fromLatin1(readAll(path))).hasMatch());
    }

    void aDumpOfNothingSaysSo()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        t->dumpTraceData("empty", nullptr, 0);
        t->dumpTraceData("zero", reinterpret_cast<const unsigned char *>("a"), 0);
        const QByteArray trace = readAll(path);
        QCOMPARE(trace.count(kStamp + " data is NULL or zero length\n"), 2);
        QCOMPARE(hexLines(trace), QByteArray());
    }

    void withoutVerboseTraceADumpStopsAfter8KBytes()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        const QByteArray data(8 * 1024 + 1, 'z');
        t->dumpTraceData("big", reinterpret_cast<const unsigned char *>(data.constData()), (unsigned)data.size());
        const QByteArray trace = readAll(path);
        QVERIFY(trace.contains(kStamp + " Dumping out first 8K bytes of 8193\n"));
        const QByteArray lines = hexLines(trace);
        QCOMPARE(lines.count('\n'), 256);          // 8192 / 32
        QVERIFY(lines.contains("\n1FE0 "));
        QVERIFY(!lines.contains("2000 "));
    }

    void withVerboseTraceTheWholeAreaIsDumped()
    {
        std::unique_ptr<FileTrace> t(make());
        t->verboseTrace = TRUE;
        QVERIFY(t->setEnabled(true));
        const QByteArray data(8 * 1024 + 1, 'z');
        t->dumpTraceData("big", reinterpret_cast<const unsigned char *>(data.constData()), (unsigned)data.size());
        const QByteArray trace = readAll(path);
        QVERIFY(!trace.contains("Dumping out first 8K"));
        QVERIFY(trace.contains(kStamp + " verboseTrace=1\n"));
        const QByteArray lines = hexLines(trace);
        QCOMPARE(lines.count('\n'), 257);
        QVERIFY(lines.contains("\n2000 "));
    }

    void anUnwritablePathReturnsAnErrorAndDisablesTheTrace()
    {
        if (geteuid() == 0)
        {
            QSKIP("root can write anywhere");
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QVERIFY(QFile::setPermissions(QFileInfo(path).absolutePath(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(!t->setEnabled(true));
        QVERIFY(!t->isTraceEnabled());
        QVERIFY(t->lastError().contains(path));
        t->logTraceEntry("must not crash");            // operations go on
        t->dumpTraceData("x", reinterpret_cast<const unsigned char *>("a"), 1);
        QVERIFY(QFile::setPermissions(QFileInfo(path).absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        QVERIFY(!QFileInfo::exists(path));
    }

    void aPathUnderARegularFileFails()
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile blocker(root + "/blocker" + QString::number(counter));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        FileTrace t(blocker.fileName() + "/x/trace.log");
        QVERIFY(!t.setEnabled(true));
        QVERIFY(!t.lastError().isEmpty());
        QVERIFY(!t.isTraceEnabled());
    }

    void aSymbolicLinkAtThePathIsRefused()
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const QString target = root + "/victim" + QString::number(counter);
        QFile victim(target);
        QVERIFY(victim.open(QIODevice::WriteOnly));
        victim.write("keep");
        victim.close();
        QVERIFY(QFile::link(target, path));
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(!t->setEnabled(true));
        QVERIFY(!t->lastError().isEmpty());
        QCOMPARE(readAll(target), QByteArray("keep"));
    }

    void aDirectoryOrDeviceAtThePathIsRefused()
    {
        FileTrace dir(root);
        QVERIFY(!dir.setEnabled(true));
        FileTrace dev("/dev/null");
        QVERIFY(!dev.setEnabled(true));
        QVERIFY(dev.lastError().contains("regular file"));
    }

    void aFailedWriteDisablesTheTraceAndRecordsTheError()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        const off_t before = QFileInfo(path).size();
        signal(SIGXFSZ, SIG_IGN);
        struct rlimit old;
        QCOMPARE(getrlimit(RLIMIT_FSIZE, &old), 0);
        struct rlimit small = old;
        small.rlim_cur = (rlim_t)before + 10;
        QCOMPARE(setrlimit(RLIMIT_FSIZE, &small), 0);
        t->logTraceEntry("this line is longer than the ten bytes that are left");
        setrlimit(RLIMIT_FSIZE, &old);
        QVERIFY(!t->isTraceEnabled());
        QVERIFY(!t->lastError().isEmpty());
        t->logTraceEntry("ignored");
        QVERIFY(!readAll(path).contains("ignored"));
    }

    void theFileIsRotatedOnceItWouldPassTheLimitAndTwoGenerationsAreKept()
    {
        // the header is 67 bytes and every entry below 43; a new file starts with the header again
        const qint64 limit = 300;
        std::unique_ptr<FileTrace> t(make(limit));
        QVERIFY(t->setEnabled(true));
        for (int i = 0; i < 100; i++)
        {
            t->logTraceEntry(QString("entry %1 xxxxxxxxxxxxx").arg(i, 4, 10, QChar('0')).toLatin1().constData());
            QVERIFY2(QFileInfo(path).size() <= limit, "the current file passed the limit");
        }
        const QString older = path + ".1";
        QVERIFY(QFileInfo::exists(older));
        QVERIFY(!QFileInfo::exists(path + ".2"));
        QVERIFY(QFileInfo(older).size() <= limit);
        QVERIFY(readAll(path).contains("entry 0099"));
        QVERIFY(!readAll(path).contains("entry 0000"));
        QVERIFY(!readAll(older).contains("entry 0000"));
        QVERIFY(readAll(older).contains("entry 0098") || readAll(older).contains("entry 0090"));
        QCOMPARE(modeOf(path), 0600);
        QCOMPARE(modeOf(older), 0600);
        // every line of both files is whole
        for (const QString & f : { path, older })
        {
            const QByteArray content = readAll(f);
            QVERIFY(content.isEmpty() || content.endsWith('\n'));
        }
    }

    void theDefaultLimitIsTenMegabytes()
    {
        QCOMPARE(FileTrace::DefaultMaxBytes, qint64(10) * 1024 * 1024);
    }

    void theDefaultPathFollowsXdgStateHome()
    {
        qputenv("XDG_STATE_HOME", (root + "/xdgstate").toLocal8Bit());
        QCOMPARE(FileTrace::defaultPath(), root + "/xdgstate/rfhutil/trace.log");
        qunsetenv("XDG_STATE_HOME");
        QCOMPARE(FileTrace::defaultPath(), root + "/home/.local/state/rfhutil/trace.log");
        FileTrace t;
        QCOMPARE(t.path(), root + "/home/.local/state/rfhutil/trace.log");
    }

    void theEnvironmentVariableEnablesTheTraceLikeWindows()
    {
        QString error;
        QVERIFY(FileTrace::fromEnvironment(&error) == nullptr);
        QVERIFY(error.isEmpty());

        qputenv("RFHUTIL_TRACE_FILE", path.toLocal8Bit());
        std::unique_ptr<FileTrace> t = FileTrace::fromEnvironment(&error);
        QVERIFY2(t != nullptr, qPrintable(error));
        QVERIFY(t->isTraceEnabled());
        QVERIFY(!t->verboseTrace);
        QVERIFY(QFileInfo::exists(path));
        t.reset();

        // verbose unless the first character is N, n or 0 (rfhutil.cpp 2509)
        const char * off[] = { "N", "no", "n", "0", "0x" };
        for (const char * v : off)
        {
            qputenv("RFHUTIL_VERBOSE_TRACE", v);
            std::unique_ptr<FileTrace> u = FileTrace::fromEnvironment(&error);
            QVERIFY(u != nullptr);
            QVERIFY2(!u->verboseTrace, v);
        }
        const char * on[] = { "1", "Y", "yes", "true", "x" };
        for (const char * v : on)
        {
            qputenv("RFHUTIL_VERBOSE_TRACE", v);
            std::unique_ptr<FileTrace> u = FileTrace::fromEnvironment(&error);
            QVERIFY(u != nullptr);
            QVERIFY2(u->verboseTrace, v);
        }
        qunsetenv("RFHUTIL_VERBOSE_TRACE");
        qunsetenv("RFHUTIL_TRACE_FILE");
    }

    void theEnvironmentVariableWithAnUnusablePathReportsTheErrorAndGivesNoTrace()
    {
        qputenv("RFHUTIL_TRACE_FILE", "/dev/null");
        QString error;
        QVERIFY(FileTrace::fromEnvironment(&error) == nullptr);
        QVERIFY(!error.isEmpty());
        qputenv("RFHUTIL_TRACE_FILE", "");
        error.clear();
        QVERIFY(FileTrace::fromEnvironment(&error) == nullptr);
        qunsetenv("RFHUTIL_TRACE_FILE");
    }

    void linesFromTwoThreadsNeverMix()
    {
        std::unique_ptr<FileTrace> t(make());
        QVERIFY(t->setEnabled(true));
        auto worker = [&](char c)
        {
            const std::string line = std::string(60, c);
            for (int i = 0; i < 200; i++)
            {
                t->logTraceEntry(line.c_str());
            }
        };
        std::thread a(worker, 'a');
        std::thread b(worker, 'b');
        a.join();
        b.join();
        int whole = 0;
        const QList<QByteArray> lines = readAll(path).split('\n');
        for (const QByteArray & line : lines)
        {
            if (line == kStamp + " " + QByteArray(60, 'a') || line == kStamp + " " + QByteArray(60, 'b'))
            {
                whole++;
            }
        }
        QCOMPARE(whole, 400);
    }

    void aPasswordGivenToTheControllerNeverReachesTheTrace()
    {
        const QByteArray secret = "Zq7!Canary-Pw-91xK";
        rfh_test::FakeMq & fake = rfh_test::fakeMq();
        fake.reset();
        FileTrace trace(path);
        trace.verboseTrace = TRUE;
        QVERIFY(trace.setEnabled(true));
        rfhSetTrace(&trace);
        {
            SessionController controller(rfh_test::fakeApi());
            QSignalSpy finished(&controller, &SessionController::finished);
            rfh::ConnectOptions options;
            options.qmName = "QMTRACE";
            options.userId = "alice";
            options.password = secret.toStdString();
            QVERIFY(controller.connectTo(options));
            QVERIFY(finished.count() > 0 || finished.wait(5000));
            // the fake saw the password: the trace test is about what was written, not what was sent
            QCOMPARE(fake.connxLog.size(), size_t(1));
            QCOMPARE(QByteArray(fake.connxLog[0].password.data(), (int)fake.connxLog[0].password.size()), secret);
        }
        rfhSetTrace(nullptr);
        const QByteArray content = readAll(path);
        QVERIFY(content.contains("MqSession::connect mode=bindings qm=QMTRACE"));
        QVERIFY(content.contains("user=alice password=set"));
        QVERIFY(!content.contains(secret));
        QVERIFY(!content.contains(secret.toBase64()));
        QVERIFY(!content.contains(secret.toHex()));
        QVERIFY(!content.contains(secret.toHex().toUpper()));
    }
};

QTEST_MAIN(FileTraceTests)
#include "FileTraceTests.moc"
