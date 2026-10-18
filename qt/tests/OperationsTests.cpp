// OperationsTests.cpp - the Main tab flows (Operations), driven through a real SessionController and AppDocument
// on the shared fake MQI (tests/core/fake_mq.h).
//
// Every expectation is written by hand from the Windows source (RFHUtil/, line numbers in the comments), never
// copied from a run:
//   GEN    General::On*              General.cpp (the button handlers)
//   DA     DataArea::*               DataArea.cpp (checkConnection 12489, connect2QM 10636, closeQ 12397, discQM 11706,
//                                    explicitConnect 11776, explicitCloseQ 12469, explicitDiscQM 11755, openQ 11811,
//                                    getCurrentDepth 18200, getQueueDepth 10246, setErrorMsg 9315)
//   VIEW   CRfhutilView::OnMq*       rfhutilView.cpp 2462-2600
// The status texts are those of DataArea::setErrorMsg. Texts that only the Qt program has are marked NEW.

#include <QtTest>

#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

#include "AppDocument.h"
#include "AppSettings.h"
#include "fake_mq.h"
#include "Operations.h"
#include "SessionController.h"
#include "StatusLog.h"
#include "rfh_format.h"
#include "rfh_platform.h"
#include "rfh_trace.h"

using namespace rfhqt;

namespace
{
    rfh_test::FakeMq & fake = rfh_test::fakeMq();

    // A test that hangs must fail the run, not freeze it.
    class Watchdog
    {
    public:
        explicit Watchdog(int seconds)
            : deadline(seconds), thread([this]() { run(); })
        {
        }
        ~Watchdog()
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                done = true;
            }
            cv.notify_all();
            thread.join();
        }

    private:
        void run()
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!cv.wait_for(lock, std::chrono::seconds(deadline), [this]() { return done; }))
            {
                printf("WATCHDOG: the test hung for %d s; aborting\n", deadline);
                fflush(stdout);
                std::_Exit(3);
            }
        }
        int deadline;
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        std::thread thread;
    };

    // Records what the flows (and the core) trace; the worker thread writes to it.
    class RecordingTrace : public RfhTrace
    {
    public:
        BOOL isTraceEnabled() override { return TRUE; }
        void logTraceEntry(const char * text) override
        {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back(text);
        }
        void dumpTraceData(const char * label, const unsigned char * data, unsigned int length) override
        {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back(label);
            lines.push_back(std::string((const char *)data, length));
        }
        std::string all() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            std::string joined;
            for (const std::string & l : lines)
            {
                joined += l + "\n";
            }
            return joined;
        }
        mutable std::mutex mutex;
        std::vector<std::string> lines;
    };

    class FakePrompter : public Prompter
    {
    public:
        bool confirm(const QString & text) override
        {
            asked << text;
            if (onAsk)
            {
                onAsk();
            }
            return answer;
        }
        std::function<void()> onAsk;
        bool answer = true;
        QStringList asked;
    };

    const char * kQm = "QM1";
    const char * kQueue = "RFHUTIL.TEST.OPS";

    // The status log as a list of entries, newest first, without the time stamps (DataArea::updateMsgText).
    QStringList entriesOf(const StatusLog & log)
    {
        static const QRegularExpression stamp("^\\d\\d\\.\\d\\d\\.\\d\\d ");
        QStringList entries;
        for (const QString & line : log.text().split("\r\n"))
        {
            if (stamp.match(line).hasMatch())
            {
                entries << line.mid(9);
            }
            else if (!entries.isEmpty())
            {
                entries.last() += "\r\n" + line;
            }
        }
        return entries;
    }


    // ---- messages for the fake queue ------------------------------------------------------------------------------

    rfh_test::FakeMessage fakeMessage(const std::string & body, const char * format = MQFMT_STRING, MQLONG ccsid = 1208,
                                      unsigned char idByte = 0x11, const std::vector<unsigned char> & headers = std::vector<unsigned char>())
    {
        rfh_test::FakeMessage m;
        memcpy(m.md.Format, format, 8);
        m.md.CodedCharSetId = ccsid;
        m.md.Encoding = 546;
        memset(m.md.MsgId, idByte, sizeof(m.md.MsgId));
        memset(m.md.CorrelId, idByte + 1, sizeof(m.md.CorrelId));
        m.md.Persistence = MQPER_PERSISTENT;
        m.data = headers;
        m.data.insert(m.data.end(), body.begin(), body.end());
        return m;
    }

    // An RFH2 header with no name/value data: 36 bytes, little endian (the layout of MQRFH2).
    std::vector<unsigned char> rfh2Header(MQLONG ccsid = 1208)
    {
        std::vector<unsigned char> h;
        auto put32 = [&](MQLONG v) { for (int i = 0; i < 4; i++) { h.push_back((unsigned char)((v >> (8 * i)) & 0xFF)); } };
        h.insert(h.end(), {'R', 'F', 'H', ' '});
        put32(2);			// Version
        put32(36);			// StrucLength
        put32(546);			// Encoding
        put32(ccsid);		// CodedCharSetId
        h.insert(h.end(), {'M', 'Q', 'S', 'T', 'R', ' ', ' ', ' '});
        put32(0);			// Flags
        put32(1208);		// NameValueCCSID
        return h;
    }

    void enqueue(const rfh_test::FakeMessage & m)
    {
        fake.queue(kQueue).messages.push_back(m);
    }

    std::string bodyOf(const rfh::Message & m)
    {
        return std::string(m.body.begin(), m.body.end());
    }

    rfh::Message bodyMessage(const std::string & body, const std::vector<unsigned char> & headers = std::vector<unsigned char>())
    {
        rfh::Message m;
        m.headers = headers;
        m.body.assign(body.begin(), body.end());
        return m;
    }

    void fillQueue(int count, bool persistent = false)
    {
        for (int i = 0; i < count; i++)
        {
            rfh_test::FakeMessage m = fakeMessage("m" + std::to_string(i));
            m.md.Persistence = persistent ? MQPER_PERSISTENT : MQPER_NOT_PERSISTENT;
            enqueue(m);
        }
    }


    // ---- files ----------------------------------------------------------------------------------------------------

    QByteArray golden(const char * name)
    {
        QFile f(QString(RFH_GOLDEN_DIR) + "/" + name);
        if (!f.open(QIODevice::ReadOnly))
        {
            return QByteArray();
        }
        return f.readAll();
    }

    QString goldenPath(const char * name)
    {
        return QString(RFH_GOLDEN_DIR) + "/" + name;
    }

    QByteArray readAll(const QString & path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<unreadable>");
    }

    // A document with a message from a queue, to see what a failed file operation leaves alone.
    void fillDocument(AppDocument & doc)
    {
        rfh::Message m;
        m.body = {'k', 'e', 'p', 't'};
        rfh::MqmdFields f;
        f.format = "MQSTR";
        f.priority = "6";
        doc.setMqmd(f);
        doc.setMessage(std::move(m), "Message Data (4) from SOME.QUEUE", true);
        doc.setFilePath("/tmp/previous.msg");
    }

    void expectDocumentUntouched(const AppDocument & doc)
    {
        QCOMPARE(std::string(doc.message().body.begin(), doc.message().body.end()), std::string("kept"));
        QCOMPARE(doc.dataSource(), std::string("Message Data (4) from SOME.QUEUE"));
        QCOMPARE(doc.mqmd().format, std::string("MQSTR"));
        QCOMPARE(doc.mqmd().priority, std::string("6"));
        QCOMPARE(doc.filePath(), QString("/tmp/previous.msg"));
        QVERIFY(doc.dirty());
    }

    struct Harness
    {
        Harness()
            : controller(rfh_test::fakeApi()), ops(doc, controller, log), finished(&ops, &Operations::flowFinished)
        {
            doc.setQueueManager(kQm);
            doc.setQueueName(kQueue);
            ops.setPrompter(&prompter);
        }

        // Runs a flow and waits for it to finish; false when it did not finish in time.
        bool run(const std::function<void()> & start)
        {
            finished.clear();
            start();
            return finished.count() > 0 || finished.wait(5000);
        }

        QString lastFlow() const { return finished.isEmpty() ? QString() : finished.last().at(0).toString(); }
        QStringList entries() const { return entriesOf(log); }
        QString newest() const { return entries().value(0); }

        // Connects through the flow and clears what the test does not look at.
        void connected()
        {
            QVERIFY(run([&]() { ops.connectQm(); }));
            QVERIFY(doc.connected());
            log.clear();
            finished.clear();
        }

        // Brings the document in line with a job the test ran on the controller itself.
        bool runJob(const std::function<bool()> & submit)
        {
            QSignalSpy done(&controller, &SessionController::finished);
            if (!submit())
            {
                return false;
            }
            if (done.count() == 0 && !done.wait(5000))
            {
                return false;
            }
            doc.applyJobResult(done.last().at(0).toString(), qvariant_cast<JobResult>(done.last().at(1)));
            return true;
        }

        AppDocument doc;
        StatusLog log;
        SessionController controller;
        Operations ops;
        FakePrompter prompter;
        QSignalSpy finished;
    };
}

class OperationsTests : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        fake.reset();
        fake.queue(kQueue);
    }

    // ---- connect (explicitConnect DA 11776, connect2QM DA 10636) -------------------------------------------------

    void connectConnectsAndSaysConnectedTo()
    {
        Watchdog dog(60);
        Harness h;
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QCOMPARE(h.lastFlow(), QString("connect"));
        QCOMPARE(fake.connxCalls, 1);
        QCOMPARE(fake.connxLog.at(0).qmName, std::string(kQm));
        QVERIFY(h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
        // connect() learns the platform and level of the queue manager; the document mirrors them (DA 11230-11330)
        QCOMPARE(h.doc.queueManagerInfo().platform, (MQLONG)MQPL_UNIX);
        QCOMPARE(h.doc.queueManagerInfo().commandLevel, (MQLONG)940);
        QCOMPARE(h.entries(), QStringList() << "Connected to QM1");		// DA 11776-11800: sprintf "Connected to %s"
    }

    void connectToTheDefaultQueueManagerNamesNoQueueManager()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setQueueManager("");
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QCOMPARE(fake.connxLog.at(0).qmName, std::string());
        QVERIFY(h.doc.connected());
        QCOMPARE(h.newest(), QString("Connected to "));		// DA 11796: sprintf "Connected to %s" with an empty name
    }

    void connectIsDisabledWhileConnectedLikeTheMenuItem()
    {
        // OnUpdateMqMqconn (rfhutilView.cpp:2296) disables MQConn when a connection is active, so the flow does
        // nothing: no second MQCONNX, no text. Changing the queue manager name reconnects through checkConnection
        // (see queueInfoReconnectsWhenTheQueueManagerNameChanged), not through MQConn.
        Watchdog dog(60);
        Harness h;
        h.connected();
        h.doc.setQueueManager("QM2");
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QCOMPARE(fake.connxCalls, 1);
        QCOMPARE(fake.discCalls, 0);
        QVERIFY(h.log.text().isEmpty());
        QVERIFY(h.doc.connected());
    }

    void aFailedConnectShowsTheWindowsTextAndLeavesNoConnection()
    {
        Watchdog dog(60);
        fake.connxRc = MQRC_Q_MGR_NOT_AVAILABLE;
        Harness h;
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QVERIFY(!h.doc.connected());
        QCOMPARE(h.entries(), QStringList() << "2059 Queue manager not available (Connect) - may not be started");	// DA 9795
        QVERIFY(!h.doc.queueOpen());
        QCOMPARE(h.doc.queueManagerInfo().commandLevel, (MQLONG)0);
    }

    void aFailedInquiryOnTheQueueManagerIsShownAsAWarningLine()
    {
        // connect() still works when an inquiry on the queue manager fails and says so in a warning; no fact is lost
        Watchdog dog(60);
        fake.inqRcBySelector[MQIA_PLATFORM] = MQRC_NOT_AUTHORIZED;
        Harness h;
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QVERIFY(h.doc.connected());
        const QString entry = h.newest();
        QVERIFY2(entry.startsWith("Connected to QM1"), qPrintable(entry));
        QVERIFY2(entry.contains("\r\n"), "the core's warning follows on its own line");
        QCOMPARE(h.doc.queueManagerInfo().platform, (MQLONG)0);			// unknown stays unknown
        QCOMPARE(h.doc.queueManagerInfo().commandLevel, (MQLONG)940);
    }

    void connectIsRefusedWhileAnotherOperationRuns()
    {
        Watchdog dog(60);
        Harness h;
        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QCOMPARE(fake.connxCalls, 0);
        QVERIFY(!h.doc.connected());
        QCOMPARE(h.entries(), QStringList() << "Another operation is still running");		// NEW
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    void busyIsMirroredIntoTheShellWhileAJobRuns()
    {
        Watchdog dog(60);
        Harness h;
        QVERIFY(!h.doc.shell().busy);
        bool busyDuringJob = false;
        fake.onGet = nullptr;
        // the connect job itself is short: watch the document's shell from the first busyChanged to the last
        QObject::connect(&h.controller, &SessionController::busyChanged, &h.controller, [&](bool busy)
        {
            if (busy)
            {
                busyDuringJob = h.doc.shell().busy;
            }
        });
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QVERIFY(busyDuringJob);
        QVERIFY(!h.doc.shell().busy);
        QVERIFY(!h.doc.shell().cancellable);
    }

    // ---- passwords never leave the connect (ConnectOptions comment; Task 16) -------------------------------------

    void thePasswordReachesMqButNoTextSettingsOrTrace()
    {
        Watchdog dog(60);
        const std::string password = "S3cret-Distinct-Pw-7731";
        RecordingTrace trace;
        rfhSetTrace(&trace);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString settingsPath = dir.filePath("rfhutil.conf");
        {
            AppSettings settings(settingsPath);
            Harness h;
            h.ops.setSettings(&settings);
            MainOptions o;
            o.connect.userId = "alice";
            o.connect.password = password;
            h.ops.setOptions(o);

            QVERIFY(h.run([&]() { h.ops.connectQm(); }));
            QVERIFY(h.doc.connected());
            QCOMPARE(fake.connxLog.size(), size_t(1));
            QVERIFY(fake.connxLog.at(0).hasSecurityParms);
            QCOMPARE(std::string(fake.connxLog.at(0).password.begin(), fake.connxLog.at(0).password.end()), password);
            QCOMPARE(fake.connxLog.at(0).userId, std::string("alice"));

            QVERIFY(!h.log.text().contains(QString::fromStdString(password)));
            QVERIFY(!QString::fromStdString(trace.all()).contains(QString::fromStdString(password)));
            QVERIFY(!trace.all().empty());

            // The connect wiped the stored copy: a second connect, after a disconnect, sends no password
            QVERIFY(h.run([&]() { h.ops.disconnectQm(); }));
            QVERIFY(!h.doc.connected());
            QVERIFY(h.run([&]() { h.ops.connectQm(); }));
            QCOMPARE(fake.connxLog.size(), size_t(2));
            QVERIFY2(!fake.connxLog.at(1).hasSecurityParms, "the password was wiped after the first connect");
            QVERIFY(!h.log.text().contains(QString::fromStdString(password)));
        }
        rfhSetTrace(nullptr);

        QFile settingsFile(settingsPath);
        QVERIFY(settingsFile.open(QIODevice::ReadOnly));
        const QByteArray stored = settingsFile.readAll();
        QVERIFY(stored.contains("QM1"));
        QVERIFY(stored.contains("alice"));							// the user id is remembered, as in Windows
        QVERIFY2(!stored.contains(password.c_str()), "a password is never stored");
        QVERIFY(!QString::fromStdString(trace.all()).contains(QString::fromStdString(password)));
    }

    void aFailedConnectWipesThePasswordToo()
    {
        Watchdog dog(60);
        fake.connxRc = MQRC_Q_MGR_NOT_AVAILABLE;
        Harness h;
        MainOptions o;
        o.connect.userId = "alice";
        o.connect.password = "pw-for-a-failed-connect";
        h.ops.setOptions(o);
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.log.text().contains("pw-for-a-failed-connect"));

        fake.connxRc = MQRC_NONE;
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QVERIFY(h.doc.connected());
        QVERIFY2(!fake.connxLog.back().hasSecurityParms, "the failed connect already consumed the password");
    }

    void aSuccessfulConnectRemembersTheQueueManagerAndTheConnectionInTheSettings()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        AppSettings settings(dir.filePath("rfhutil.conf"));
        Harness h;
        h.ops.setSettings(&settings);
        MainOptions o;
        o.connect.mode = rfh::Mode::Bindings;
        o.connect.heartbeat = 33;
        h.ops.setOptions(o);
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QCOMPARE(settings.recentQueueManagers(), QStringList() << "QM1");
        QCOMPARE(settings.connection().heartbeat, (MQLONG)33);
        QCOMPARE(settings.connection().qmName, std::string("QM1"));
    }

    void aFailedConnectIsNotRemembered()
    {
        Watchdog dog(60);
        fake.connxRc = MQRC_Q_MGR_NOT_AVAILABLE;
        QTemporaryDir dir;
        AppSettings settings(dir.filePath("rfhutil.conf"));
        Harness h;
        h.ops.setSettings(&settings);
        QVERIFY(h.run([&]() { h.ops.connectQm(); }));
        QVERIFY(settings.recentQueueManagers().isEmpty());
    }

    // ---- disconnect (OnMqMqdisc VIEW 2477, explicitCloseQ DA 12469, explicitDiscQM DA 11755) ----------------------

    void disconnectClosesAnOpenQueueSaysQueueClosedAndDisconnected()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.runJob([&]() { return h.controller.get(kQueue, rfh::GetOptions()); }));
        QVERIFY(h.doc.queueOpen());
        QVERIFY(h.run([&]() { h.ops.disconnectQm(); }));
        QCOMPARE(h.lastFlow(), QString("disconnect"));
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
        QCOMPARE(fake.closeCalls, 1);
        QCOMPARE(fake.discCalls, 1);
        // appendError joins the two lines with CR LF; updateMsgText stamps them once
        QCOMPARE(h.entries(), QStringList() << "Queue closed\r\nDisconnected from queue manager");
        QVERIFY(fake.liveHandles.empty());
    }

    void disconnectWithoutAnOpenQueueOnlySaysDisconnected()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.run([&]() { h.ops.disconnectQm(); }));
        QVERIFY(!h.doc.connected());
        QCOMPARE(fake.closeCalls, 0);
        QCOMPARE(h.entries(), QStringList() << "Disconnected from queue manager");
    }

    void disconnectUsesTheCloseOptionOfTheMainTab()
    {
        Watchdog dog(60);
        Harness h;
        MainOptions o;
        o.closeOption = rfh::CloseOption::Delete;			// explicitCloseQ -> closeQ(m_close_option)
        h.ops.setOptions(o);
        h.connected();
        QVERIFY(h.runJob([&]() { return h.controller.get(kQueue, rfh::GetOptions()); }));
        fake.queue(kQueue);
        QVERIFY(h.run([&]() { h.ops.disconnectQm(); }));
        QCOMPARE(fake.closeLog.back().queue, std::string(kQueue));
        QCOMPARE(fake.closeLog.back().options, (MQLONG)MQCO_DELETE);
    }

    void aFailedCloseDuringDisconnectIsShownAndTheDisconnectGoesOn()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.runJob([&]() { return h.controller.get(kQueue, rfh::GetOptions()); }));
        fake.closeRc = MQRC_NOT_AUTHORIZED;
        QVERIFY(h.run([&]() { h.ops.disconnectQm(); }));
        QVERIFY(!h.doc.connected());
        QCOMPARE(fake.discCalls, 1);
        const QStringList entries = h.entries();
        QCOMPARE(entries.size(), 1);
        // setErrorMsg(cc, rc, "Close") (DA 12437), then explicitCloseQ's "Queue closed" and explicitDiscQM's text
        QVERIFY2(entries.at(0).startsWith("2035 Not authorized (Close)\r\nQueue closed\r\nDisconnected from queue manager"), qPrintable(entries.at(0)));
    }

    void aFailedDisconnectShowsTheWindowsDiscText()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        fake.discRc = MQRC_NOT_AUTHORIZED;
        QVERIFY(h.run([&]() { h.ops.disconnectQm(); }));
        QCOMPARE(h.entries().size(), 1);
        QVERIFY2(h.newest().startsWith("2035 Not authorized (Disc)"), qPrintable(h.newest()));	// DA 11722: setErrorMsg(cc, rc, "Disc")
    }

    void disconnectWithoutAConnectionDoesNothing()
    {
        Watchdog dog(60);
        Harness h;
        QVERIFY(h.run([&]() { h.ops.disconnectQm(); }));		// MQDisc is disabled (OnUpdateMqMqdisc): no call, no text
        QCOMPARE(fake.discCalls, 0);
        QVERIFY(h.log.text().isEmpty());
    }

    void disconnectIsRefusedWhileAnotherOperationRuns()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        QVERIFY(h.run([&]() { h.ops.disconnectQm(); }));
        QCOMPARE(fake.discCalls, 0);
        QVERIFY(h.doc.connected());
        QCOMPARE(h.entries(), QStringList() << "Another operation is still running");
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    // ---- Close Q button (OnCloseq GEN 1617): closeQ(m_close_options), then discQM -------------------------------

    void closeQClosesWithTheCloseOptionAndDisconnectsWithoutAText()
    {
        Watchdog dog(60);
        struct Case { rfh::CloseOption option; MQLONG mq; } cases[] =
        {
            { rfh::CloseOption::None, MQCO_NONE }, { rfh::CloseOption::Delete, MQCO_DELETE }, { rfh::CloseOption::Purge, MQCO_DELETE_PURGE }
        };
        for (const Case & c : cases)
        {
            fake.reset();
            fake.queue(kQueue);
            Harness h;
            MainOptions o;
            o.closeOption = c.option;
            h.ops.setOptions(o);
            h.connected();
            QVERIFY(h.runJob([&]() { return h.controller.get(kQueue, rfh::GetOptions()); }));
            QVERIFY(h.run([&]() { h.ops.closeQ(); }));
            QCOMPARE(h.lastFlow(), QString("closeQ"));
            QCOMPARE(fake.closeLog.back().options, c.mq);		// DA 12397-12467: MQCO_NONE / MQCO_DELETE / MQCO_DELETE_PURGE
            QCOMPARE(fake.discCalls, 1);
            QVERIFY(!h.doc.connected());
            QVERIFY(!h.doc.queueOpen());
            QVERIFY2(h.log.text().isEmpty(), "no text on success (inventory 3.2)");
        }
    }

    void closeQWithNoOpenQueueStillDisconnects()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.run([&]() { h.ops.closeQ(); }));
        QCOMPARE(fake.closeCalls, 0);
        QCOMPARE(fake.discCalls, 1);
        QVERIFY(!h.doc.connected());
    }

    void aFailedCloseShowsTheCloseTextAndStillDisconnects()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.runJob([&]() { return h.controller.get(kQueue, rfh::GetOptions()); }));
        fake.closeRc = MQRC_NOT_AUTHORIZED;
        QVERIFY(h.run([&]() { h.ops.closeQ(); }));
        QCOMPARE(h.entries(), QStringList() << "2035 Not authorized (Close)");
        QVERIFY(!h.doc.connected());
        QCOMPARE(fake.discCalls, 1);
    }

    void closeQNeedsAConnection()
    {
        Watchdog dog(60);
        Harness h;
        QVERIFY(h.run([&]() { h.ops.closeQ(); }));			// General.cpp:615 enables Close Q with the connection
        QCOMPARE(fake.discCalls, 0);
        QVERIFY(h.log.text().isEmpty());
    }

    void closeQIsRefusedWhileAnotherOperationRuns()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        QVERIFY(h.run([&]() { h.ops.closeQ(); }));
        QCOMPARE(fake.discCalls, 0);
        QCOMPARE(h.entries(), QStringList() << "Another operation is still running");
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    // ---- MQ > MQClose (OnMqMqclose VIEW 2575, explicitCloseQ DA 12469) --------------------------------------------

    void mqCloseClosesTheQueueKeepsTheConnectionAndSaysQueueClosed()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.runJob([&]() { return h.controller.get(kQueue, rfh::GetOptions()); }));
        QVERIFY(h.run([&]() { h.ops.closeQueueMenu(); }));
        QCOMPARE(h.lastFlow(), QString("closeQueue"));
        QVERIFY(h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
        QCOMPARE(fake.discCalls, 0);
        QCOMPARE(h.entries(), QStringList() << "Queue closed");
    }

    void mqCloseFailureShowsTheErrorThenQueueClosed()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.runJob([&]() { return h.controller.get(kQueue, rfh::GetOptions()); }));
        fake.closeRc = MQRC_NOT_AUTHORIZED;
        QVERIFY(h.run([&]() { h.ops.closeQueueMenu(); }));
        QCOMPARE(h.entries(), QStringList() << "2035 Not authorized (Close)\r\nQueue closed");	// explicitCloseQ appends it whatever happened
        QVERIFY(h.doc.connected());
    }

    // ---- queue information (getQueueDepth DA 10246, getCurrentDepth DA 18200, openQ DA 12293) --------------------

    void queueInfoRefreshesTypeDepthAndMaximumLength()
    {
        Watchdog dog(60);
        Harness h;
        fake.queue(kQueue).type = MQQT_ALIAS;
        fake.queue(kQueue).maxMsgLength = 12345;
        fake.queue(kQueue).hasDepthOverride = true;
        fake.queue(kQueue).depthOverride = 17;
        h.connected();
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(h.lastFlow(), QString("queueInfo"));
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)17);
        QCOMPARE(h.doc.queueInfo().type, (MQLONG)MQQT_ALIAS);
        QCOMPARE(h.doc.queueInfo().maxMsgLength, (MQLONG)12345);
        QVERIFY2(h.log.text().isEmpty(), "getQueueDepth says nothing when it works");
    }

    void queueInfoConnectsOnDemand()
    {
        Watchdog dog(60);
        Harness h;
        QVERIFY(!h.doc.connected());
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(fake.connxCalls, 1);			// checkConnection (DA 12489)
        QVERIFY(h.doc.connected());
        QCOMPARE(h.doc.queueManagerInfo().commandLevel, (MQLONG)940);
    }

    void queueInfoReconnectsWhenTheQueueManagerNameChanged()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.runJob([&]() { return h.controller.get(kQueue, rfh::GetOptions()); }));
        h.doc.setQueueManager("QM2");
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        // checkConnection: connected to another name -> closeQ(Q_CLOSE_NONE), discQM, then connect2QM (DA 12491-12510)
        QCOMPARE(fake.closeCalls, 2);			// the session's queue, then the inquiry's own handle on QM2
        QCOMPARE(fake.closeLog.at(0).queue, std::string(kQueue));
        QCOMPARE(fake.discCalls, 1);
        QCOMPARE(fake.connxCalls, 2);
        QCOMPARE(fake.connxLog.at(1).qmName, std::string("QM2"));
        QVERIFY(h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
    }

    void queueInfoDoesNotReconnectForTheSameName()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(fake.connxCalls, 1);
        QCOMPARE(fake.discCalls, 0);
    }

    void queueInfoNeedsAQueueName()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setQueueName("");
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(h.entries(), QStringList() << "*Queue Name required* ");		// DA 10270: note the trailing blank
        QCOMPARE(fake.connxCalls, 0);
    }

    void queueInfoOfAMissingQueueShowsNoSuchQueue()
    {
        Watchdog dog(60);
        fake.openRcByName[kQueue] = MQRC_UNKNOWN_OBJECT_NAME;
        Harness h;
        h.connected();
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(h.entries(), QStringList() << "2085 No such queue");		// DA 9855 area: case MQRC_UNKNOWN_OBJECT_NAME
        QVERIFY(h.doc.connected());
    }

    void aResolvedQueueNameIsLoggedAsItsOwnEntryOnce()
    {
        // openQ logs "Queue name resolved to %.48s" with updateMsgText, an entry of its own (DA 12293-12305), and
        // only when the queue is opened, so asking again for the same queue says it once.
        Watchdog dog(60);
        fake.queue(kQueue).type = MQQT_ALIAS;
        fake.queue(kQueue).resolvedQName = "RFHUTIL.TEST.BASE";
        Harness h;
        h.connected();
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(h.entries(), QStringList() << "Queue name resolved to RFHUTIL.TEST.BASE");
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(h.entries().size(), 1);
    }

    void aLostConnectionDuringTheInquiryRunsTheConnectionLostCleanup()
    {
        // setErrorMsg case 2009 (DA 9347-9356) calls connectionLostCleanup(): the queue is closed, the connection is gone
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.runJob([&]() { return h.controller.get(kQueue, rfh::GetOptions()); }));
        QVERIFY(h.doc.queueOpen());
        fake.openRcByName[kQueue] = MQRC_CONNECTION_BROKEN;
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(h.entries(), QStringList() << "2009 Connection to QM has been lost - Q closed");
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
        QCOMPARE(h.doc.queueManagerInfo().commandLevel, (MQLONG)0);

        // the next flow connects again, even for the same queue manager name
        fake.openRcByName.clear();
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(fake.connxCalls, 2);
        QVERIFY(h.doc.connected());
    }

    void queueInfoRefusesWhileAnotherOperationRuns()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        const int inqBefore = fake.inqCalls;
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(fake.inqCalls, inqBefore);
        QCOMPARE(h.entries(), QStringList() << "Another operation is still running");
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    void theQueueIsRememberedInTheSettingsOncePerQueueManager()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        AppSettings settings(dir.filePath("rfhutil.conf"));
        Harness h;
        h.ops.setSettings(&settings);
        h.connected();
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(settings.recentQueues(kQm), QStringList() << kQueue);		// DA 12214: initQname = Queue after a successful open
        h.doc.setQueueName("OTHER.QUEUE");
        fake.queue("OTHER.QUEUE");
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QCOMPARE(settings.recentQueues(kQm), QStringList() << "OTHER.QUEUE" << kQueue);
    }

    void aFailedInquiryDoesNotRememberTheQueue()
    {
        Watchdog dog(60);
        fake.openRcByName[kQueue] = MQRC_UNKNOWN_OBJECT_NAME;
        QTemporaryDir dir;
        AppSettings settings(dir.filePath("rfhutil.conf"));
        Harness h;
        h.ops.setSettings(&settings);
        h.connected();
        QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        QVERIFY(settings.recentQueues(kQm).isEmpty());
    }

    // ---- the trace (logTraceEntry in every flow) ------------------------------------------------------------------

    void everyFlowTracesItsEntry()
    {
        Watchdog dog(60);
        RecordingTrace trace;
        rfhSetTrace(&trace);
        {
            Harness h;
            h.connected();
            QVERIFY(h.run([&]() { h.ops.refreshQueueInfo(); }));
        }
        rfhSetTrace(nullptr);
        const std::string all = trace.all();
        QVERIFY2(all.find("Operations::connectQm") != std::string::npos, all.c_str());
        QVERIFY2(all.find("Operations::refreshQueueInfo QM=QM1 Q=RFHUTIL.TEST.OPS") != std::string::npos, all.c_str());
    }

    // ======================================================================================================
    // Read Q, Browse Q (getMessage DA 7947; General::OnReadq GEN 276, OnBrowse GEN 372)
    // ======================================================================================================

    void readQReadsOneMessageAndFillsTheDocument()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("hello"));
        enqueue(fakeMessage("second", MQFMT_STRING, 1208, 0x33));
        h.doc.setFilePath("/tmp/old.msg");
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(h.lastFlow(), QString("readQ"));
        // connect on demand, destructive get without syncpoint and without a wait (DA 7977, 8075)
        QCOMPARE(fake.connxCalls, 1);
        QCOMPARE(fake.getLog.size(), size_t(1));
        QCOMPARE(fake.getLog.at(0).options & (MQGMO_BROWSE_FIRST | MQGMO_BROWSE_NEXT), 0);
        QVERIFY(fake.getLog.at(0).options & MQGMO_NO_SYNCPOINT);
        QVERIFY(fake.getLog.at(0).options & MQGMO_FAIL_IF_QUIESCING);
        QCOMPARE(fake.lastGmoWaitInterval, (MQLONG)0);
        QCOMPARE(fake.queue(kQueue).messages.size(), size_t(1));

        QCOMPARE(bodyOf(h.doc.message()), std::string("hello"));
        QCOMPARE(h.doc.dataSource(), std::string("Message Data (5) from RFHUTIL.TEST.OPS"));	// DA 8120
        QVERIFY(h.doc.dirty());
        QVERIFY(!h.doc.hasFile());				// OnReadq: fileName[0] = 0
        QVERIFY(h.doc.connected());
        QVERIFY(h.doc.queueOpen());				// the queue stays open until Close Q
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)1);	// getCurrentDepth after the get
        QCOMPARE(h.entries(), QStringList() << "Msg read from RFHUTIL.TEST.OPS length=5");	// DA 8140: "Msg %s from %s length=%d%s"
        // the MQMD tab shows what MQ returned (fromGetMqmd) and the formats come from the message (defaultsFor)
        QCOMPARE(h.doc.mqmd().format, std::string("MQSTR   "));		// fromGetMqmd keeps the blanks (MQMDPAGE.cpp:1641)
        QCOMPARE(h.doc.mqmd().ccsid, std::string("1208"));
        QCOMPARE(h.doc.render().ccsid, 1208);
        QCOMPARE(h.doc.render().intFormat, (int)NUMERIC_PC);
        MQBYTE24 expectedId;
        memset(expectedId, 0x11, sizeof(expectedId));
        QVERIFY(memcmp(h.doc.mqmd().msgId, expectedId, sizeof(expectedId)) == 0);
        QCOMPARE(h.doc.message().mqmd.CodedCharSetId, (MQLONG)1208);
    }

    void readQShowsTheHeaderLengthsOfTheMessage()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("data", MQFMT_RF_HEADER_2, 1208, 0x11, rfh2Header()));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(h.doc.message().headers.size(), size_t(36));
        QCOMPARE(bodyOf(h.doc.message()), std::string("data"));
        // length is the data after the headers (fileSize after parseMsgHeaders); DA 8135-8145
        QCOMPARE(h.entries(), QStringList() << "Msg read from RFHUTIL.TEST.OPS length=4 (data 4 dlq 0 rfh 36 cics 0 ims 0)");
        QCOMPARE(h.doc.dataSource(), std::string("Message Data (4) from RFHUTIL.TEST.OPS"));
    }

    void readQWithoutAQueueNameSaysSoButStillClearsTheFlagsAndTheFileName()
    {
        // getMessage: "*Queue Name required*" (no blank, DA 7991); OnReadq goes on to clear Set Iden and the file name
        Watchdog dog(60);
        Harness h;
        h.doc.setQueueName("");
        h.doc.setFilePath("/tmp/old.msg");
        ShellState shell = h.doc.shell();
        shell.setIdentity = true;
        h.doc.setShell(shell);
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(h.entries(), QStringList() << "*Queue Name required*");
        QCOMPARE(fake.connxCalls, 0);
        QVERIFY(!h.doc.shell().setIdentity);
        QVERIFY(!h.doc.hasFile());
    }

    void readQOnAnEmptyQueueShowsNoMessagesAndSetsTheDepthToZero()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        rfh::QueueInfo before;
        before.depth = 9;
        h.doc.setQueueInfo(before);
        h.doc.setMessage(rfh::Message(), "Message Data", false);
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(h.entries(), QStringList() << "2033 No messages in queue");	// setErrorMsg case 2033 (DA 9425)
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)0);							// m_q_depth = 0
        QVERIFY(h.doc.connected());
        QVERIFY(h.doc.message().body.empty());
        QVERIFY(!h.doc.dirty());
    }

    void readQFailuresUseTheWindowsTextWithTheGetOperationWord()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        fake.getRc = MQRC_NOT_AUTHORIZED;
        enqueue(fakeMessage("kept"));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(h.entries(), QStringList() << "2035 Not authorized (Get)");	// DA 7821: setErrorMsg(cc, rc, getType = "Get")
        QVERIFY(h.doc.message().body.empty());
        QVERIFY(h.doc.connected());
    }

    void readQDefaultTextForAnUnlistedReason()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        fake.getRc = MQRC_BUFFER_ERROR + 5000;		// not a case of setErrorMsg: the default text
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(h.entries().size(), 1);
        QVERIFY2(h.newest().startsWith("*Error cc=2 rc=7004"), qPrintable(h.newest()));
        QVERIFY2(h.newest().endsWith("Cannot Get"), qPrintable(h.newest()));
    }

    void readQLosingTheConnectionRunsTheConnectionLostCleanup()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("one"));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QVERIFY(h.doc.queueOpen());
        h.log.clear();
        fake.getRc = MQRC_CONNECTION_BROKEN;
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(h.entries(), QStringList() << "2009 Connection to QM has been lost - Q closed");
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
        QCOMPARE(bodyOf(h.doc.message()), std::string("one"));		// the earlier message is still shown

        fake.getRc = MQRC_NONE;
        enqueue(fakeMessage("two"));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(fake.connxCalls, 2);						// reconnects on demand
        QCOMPARE(bodyOf(h.doc.message()), std::string("two"));
    }

    void readQWithAResourceLockedReasonClosesTheQueue()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        fake.getRc = MQRC_ATTRIBUTE_LOCKED;		// 6104
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(h.entries(), QStringList() << "6104 Resouce locked - queue has been closed");	// DA 9859, spelling kept
        QCOMPARE(fake.closeCalls, 1);			// closeQ(Q_CLOSE_NONE)
        QVERIFY(!h.doc.queueOpen());
        QVERIFY(h.doc.connected());
        QVERIFY(fake.liveHandles.empty());
    }

    void readQClearsSetIdentityContextAfterEveryRead()
    {
        // OnReadq 316: m_set_UserID = FALSE whatever the outcome
        Watchdog dog(60);
        for (bool succeed : { true, false })
        {
            fake.reset();
            fake.queue(kQueue);
            Harness h;
            if (succeed)
            {
                enqueue(fakeMessage("x"));
            }
            ShellState shell = h.doc.shell();
            shell.setIdentity = true;
            shell.setAll = true;
            h.doc.setShell(shell);
            QVERIFY(h.run([&]() { h.ops.readQ(); }));
            QVERIFY(!h.doc.shell().setIdentity);
            QVERIFY2(h.doc.shell().setAll, "only Set Iden Context clears itself");
        }
    }

    void readQGetByIdsAppliesOnlyANonZeroIdOfTheMqmdTab()
    {
        Watchdog dog(60);
        struct Case { bool msg, correl, group; bool zeroIds; MQLONG expected; } cases[] =
        {
            { true, false, false, false, MQMO_MATCH_MSG_ID },
            { false, true, false, false, MQMO_MATCH_CORREL_ID },
            { false, false, true, false, MQMO_MATCH_GROUP_ID },
            { true, true, true, false, MQMO_MATCH_MSG_ID | MQMO_MATCH_CORREL_ID | MQMO_MATCH_GROUP_ID },
            { true, true, true, true, 0 },			// DA 8043-8059: memcmp with MQMI_NONE etc. skips an all-zero id
            { false, false, false, false, 0 },
        };
        for (const Case & c : cases)
        {
            fake.reset();
            fake.queue(kQueue);
            enqueue(fakeMessage("x"));
            Harness h;
            MainOptions o;
            o.get.matchMsgId = c.msg;
            o.get.matchCorrelId = c.correl;
            o.get.matchGroupId = c.group;
            h.ops.setOptions(o);
            rfh::MqmdFields f;
            if (!c.zeroIds)
            {
                memset(f.msgId, 0x41, sizeof(f.msgId));
                memset(f.correlId, 0x42, sizeof(f.correlId));
                memset(f.groupId, 0x43, sizeof(f.groupId));
            }
            h.doc.setMqmd(f);
            QVERIFY(h.run([&]() { h.ops.readQ(); }));
            QCOMPARE(fake.getLog.size(), size_t(1));
            QCOMPARE(fake.getLog.at(0).matchOptions & (MQMO_MATCH_MSG_ID | MQMO_MATCH_CORREL_ID | MQMO_MATCH_GROUP_ID), c.expected);
            if (c.expected & MQMO_MATCH_MSG_ID)
            {
                QCOMPARE((int)fake.getLog.at(0).msgId[0], 0x41);
            }
            if (c.expected & MQMO_MATCH_CORREL_ID)
            {
                QCOMPARE((int)fake.getLog.at(0).correlId[0], 0x42);
            }
            if (c.expected & MQMO_MATCH_GROUP_ID)
            {
                QCOMPARE((int)fake.getLog.at(0).groupId[0], 0x43);
            }
        }
    }

    void readQConvertTakesTheCodePageAndTheIntegerFormatOfTheMqmdTab()
    {
        Watchdog dog(60);
        struct Case { int encoding; const char * ccsid; MQLONG expectedCcsid; MQLONG expectedEncoding; } cases[] =
        {
            { NUMERIC_PC, "1208", 1208, 546 },		// DEF_PC_ENCODING (DA 8011-8030)
            { NUMERIC_HOST, "37", 37, 273 },			// DEF_UNIX_ENCODING
            { NUMERIC_PC, "", MQCCSI_Q_MGR, 546 },	// no code page on the tab: the queue manager's
        };
        for (const Case & c : cases)
        {
            fake.reset();
            fake.queue(kQueue);
            enqueue(fakeMessage("x"));
            Harness h;
            MainOptions o;
            o.get.convert = true;
            h.ops.setOptions(o);
            rfh::MqmdFields f;
            f.ccsid = c.ccsid;
            f.encoding = c.encoding;
            h.doc.setMqmd(f);
            QVERIFY(h.run([&]() { h.ops.readQ(); }));
            QVERIFY(fake.getLog.at(0).options & MQGMO_CONVERT);
            QCOMPARE(fake.getLog.at(0).mdCcsid, c.expectedCcsid);
            QCOMPARE(fake.getLog.at(0).mdEncoding, c.expectedEncoding);
        }
    }

    void readQWithoutConvertSendsNoConvertOption()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(fake.getLog.at(0).options & MQGMO_CONVERT, 0);
    }

    void readQUsesTheWaitIntervalAndTheGroupOptionsOfTheMainTab()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        MainOptions o;
        o.get.waitMs = 500;
        o.get.logicalOrder = true;
        o.get.completeMsg = true;
        h.ops.setOptions(o);
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(fake.lastGmoWaitInterval, (MQLONG)500);
        QVERIFY(fake.getLog.at(0).options & MQGMO_WAIT);
        QVERIFY(fake.getLog.at(0).options & MQGMO_LOGICAL_ORDER);
        QVERIFY(fake.getLog.at(0).options & MQGMO_COMPLETE_MSG);
    }

    void readQAlternateUserComesFromTheUserIdOfTheMqmdTab()
    {
        // openQ 12126: "the alternate user id must be specified in the MQMD user id field"
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        ShellState shell = h.doc.shell();
        shell.alternateUser = true;
        h.doc.setShell(shell);
        rfh::MqmdFields f;
        f.userId = "ALTUSER";
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QVERIFY(fake.openLog.front().options & MQOO_ALTERNATE_USER_AUTHORITY);
        QCOMPARE(fake.openLog.front().alternateUser, std::string("ALTUSER"));
    }

    void readQSelectorAndPropertiesReachTheCore()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        MainOptions o;
        o.get.selector = "color = 'red'";
        o.get.properties = rfh::PropertiesMode::None;
        h.ops.setOptions(o);
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(fake.openLog.front().selector, std::string("color = 'red'"));
        QVERIFY(fake.getLog.at(0).options & MQGMO_NO_PROPERTIES);
    }

    void readQRemembersTheQueueInTheSettings()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        AppSettings settings(dir.filePath("rfhutil.conf"));
        Harness h;
        h.ops.setSettings(&settings);
        enqueue(fakeMessage("x"));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(settings.recentQueues(kQm), QStringList() << kQueue);
        QCOMPARE(settings.recentQueueManagers(), QStringList() << kQm);
    }

    void readQAnnouncesTheQueueAsTheWindowTitle()
    {
        Watchdog dog(60);
        Harness h;
        QSignalSpy title(&h.ops, &Operations::titleChanged);
        enqueue(fakeMessage("x"));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(title.count(), 1);
        QCOMPARE(title.at(0).at(0).toString(), QString(kQueue));	// setWindowTitle(Queue) (DA 8164)
    }

    void readQWithAConvertedTooBigWarningDeliversTheMessageAndTheWarning()
    {
        // Windows drops a warning get (cc != OK) although the message is gone from the queue; the core delivers it,
        // so the flow shows the data and the warning text.
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("kept"));
        fake.warnGetRc[1] = MQRC_CONVERTED_STRING_TOO_BIG;
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(bodyOf(h.doc.message()), std::string("kept"));
        QCOMPARE(h.entries().size(), 1);
        QVERIFY2(h.newest().contains("rc=2190"), qPrintable(h.newest()));
        QVERIFY2(h.newest().contains("Msg read from RFHUTIL.TEST.OPS length=4"), qPrintable(h.newest()));
    }

    void readQWithAnInvalidCodePageInTheMqmdSaysSoBeforeTheMessageLine()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x", MQFMT_STRING, 123456789));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(h.entries().size(), 1);
        // processMessage appends "Invalid code page in MQMD - %d\r\n" to m_error_msg before getMessage adds its line (DA 7124)
        QCOMPARE(h.newest(), QString("Invalid code page in MQMD - 123456789\r\nMsg read from RFHUTIL.TEST.OPS length=1"));
        QCOMPARE(h.doc.render().ccsid, 0);
    }

    void readQCanBeCancelledWhileItWaits()
    {
        Watchdog dog(60);
        Harness h;
        MainOptions o;
        o.get.waitMs = MQWI_UNLIMITED;
        h.ops.setOptions(o);
        h.connected();
        rfh::QueueInfo before;
        before.depth = 4;
        h.doc.setQueueInfo(before);
        bool cancellableWhileRunning = false;
        QTimer::singleShot(150, &h.ops, [&]()
        {
            cancellableWhileRunning = h.doc.shell().cancellable && h.doc.enables().cancel;
            h.ops.cancelGet();
        });
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QVERIFY(cancellableWhileRunning);
        QVERIFY(!h.doc.shell().cancellable);
        QVERIFY(!h.doc.shell().busy);
        QCOMPARE(h.entries(), QStringList() << "Get cancelled");				// NEW
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)4);						// a cancel is not a 2033 report
        QVERIFY(h.doc.connected());
        QVERIFY(h.doc.message().body.empty());
    }

    void cancelWhileIdleDoesNothing()
    {
        Watchdog dog(60);
        Harness h;
        h.ops.cancelGet();
        enqueue(fakeMessage("x"));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(bodyOf(h.doc.message()), std::string("x"));		// the flag was not left set for the next job
    }

    void readQIsRefusedWhileAnotherOperationRuns()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        enqueue(fakeMessage("x"));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(fake.getCalls, 0);
        QCOMPARE(h.entries(), QStringList() << "Another operation is still running");
        QVERIFY(h.doc.message().body.empty());
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    void readQIsDisabledWhileBrowsing()
    {
        // rfhutilView.cpp:2427: Read Q is disabled while a browse is active
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QVERIFY(h.doc.browse().active);
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QVERIFY(h.log.text().isEmpty());
        QCOMPARE(fake.getLog.size(), size_t(1));		// only the browse first
    }

    void browseQBrowsesOneMessageAndClosesTheQueueButNotTheConnection()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("first"));
        enqueue(fakeMessage("second", MQFMT_STRING, 1208, 0x33));
        QVERIFY(h.run([&]() { h.ops.browseQ(); }));
        QCOMPARE(h.lastFlow(), QString("browseQ"));
        QVERIFY(fake.getLog.at(0).options & MQGMO_BROWSE_FIRST);
        QCOMPARE(bodyOf(h.doc.message()), std::string("first"));
        QCOMPARE(fake.queue(kQueue).messages.size(), size_t(2));		// the message stays on the queue
        QVERIFY(h.doc.connected());
        QVERIFY(!h.doc.queueOpen());									// closeQ(Q_CLOSE_NONE) (DA 8151)
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)2);
        QCOMPARE(h.entries(), QStringList() << "Msg browse from RFHUTIL.TEST.OPS length=5");
        QCOMPARE(h.doc.dataSource(), std::string("Message Data (5) from RFHUTIL.TEST.OPS"));
        QVERIFY(!h.doc.browse().active);								// Browse Q starts no browse session
    }

    void browseQOnAnEmptyQueueShowsNoMessagesAndLeavesTheQueueOpen()
    {
        Watchdog dog(60);
        Harness h;
        QVERIFY(h.run([&]() { h.ops.browseQ(); }));
        QCOMPARE(h.entries(), QStringList() << "2033 No messages in queue");
        QVERIFY(h.doc.queueOpen());			// closeQ is reached only after a successful browse
    }

    void browseQClearsTheFileNameAndSetIdentityLikeRead()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setFilePath("/tmp/old.msg");
        ShellState shell = h.doc.shell();
        shell.setIdentity = true;
        h.doc.setShell(shell);
        QVERIFY(h.run([&]() { h.ops.browseQ(); }));		// even though the queue is empty
        QVERIFY(!h.doc.hasFile());
        QVERIFY(!h.doc.shell().setIdentity);
    }

    void browseQGetByMsgIdUsesTheMatchRule()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        MainOptions o;
        o.get.matchMsgId = true;
        h.ops.setOptions(o);
        rfh::MqmdFields f;
        memset(f.msgId, 0x55, sizeof(f.msgId));
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.browseQ(); }));
        QVERIFY(fake.getLog.at(0).matchOptions & MQMO_MATCH_MSG_ID);
        QCOMPARE((int)fake.getLog.at(0).msgId[0], 0x55);
    }

    // ======================================================================================================
    // Start Browse, Browse Next, Browse Prev, End Browse (startBrowse DA 8346, readFirstMessage DA 8180,
    // browseNext DA 8459, browsePrev DA 8895, endBrowse DA 8425)
    // ======================================================================================================

    void startBrowseShowsTheFirstMessageAndStartsTheBrowseSession()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("one"));
        enqueue(fakeMessage("two", MQFMT_STRING, 1208, 0x22));
        enqueue(fakeMessage("three", MQFMT_STRING, 1208, 0x33));
        h.doc.setFilePath("/tmp/old.msg");
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QCOMPARE(h.lastFlow(), QString("startBrowse"));
        QVERIFY(fake.getLog.at(0).options & MQGMO_BROWSE_FIRST);
        QCOMPARE(fake.getLog.at(0).matchOptions & MQMO_MATCH_MSG_ID, 0);
        QVERIFY(h.doc.browse().active);
        QCOMPARE(h.doc.browse().position, 1L);
        QVERIFY(!h.doc.browse().previousAllowed());
        QVERIFY(h.doc.queueOpen());
        QVERIFY(!h.doc.hasFile());
        QCOMPARE(h.entries(), QStringList() << "First message read from RFHUTIL.TEST.OPS length=3");
        QCOMPARE(h.doc.dataSource(), std::string("Message Data (3) msg 1 of 3 from RFHUTIL.TEST.OPS"));	// DA 8386
        const Enables e = h.doc.enables();
        QVERIFY(e.browseNext && e.endBrowse && !e.browsePrev && !e.readQ && !e.writeQ && !e.closeQ);
    }

    void startBrowseOnAnEmptyQueueSaysNoMessagesInTheQueueAndDisconnects()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setQueueName(kQueue);
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        // readFirstMessage replaces the pending text (DA 8263) and discQM follows (DA 8281)
        QCOMPARE(h.entries(), QStringList() << "No messages in RFHUTIL.TEST.OPS queue");
        QVERIFY(!h.doc.browse().active);
        QVERIFY(!h.doc.connected());
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)0);
        QCOMPARE(fake.discCalls, 1);
    }

    void startBrowseFailureShowsBothOperationWordsAndDisconnects()
    {
        // processMQGet reports with "StartBr" and readFirstMessage again with "Get (Browse)" (DA 7918, 8272)
        Watchdog dog(60);
        Harness h;
        fake.getRc = MQRC_NOT_AUTHORIZED;
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QCOMPARE(h.entries(), QStringList() << "2035 Not authorized (StartBr)\r\n2035 Not authorized (Get (Browse))");
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.doc.browse().active);
    }

    void startBrowseAFailureWhoseTextsAreTheSameIsShownOnce()
    {
        // an open failure is reported once by openQ ("2085 No such queue"); the core cannot tell it from a get failure,
        // so identical texts are not repeated
        Watchdog dog(60);
        Harness h;
        fake.openRcByName[kQueue] = MQRC_UNKNOWN_OBJECT_NAME;
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QCOMPARE(h.entries(), QStringList() << "2085 No such queue");
        QVERIFY(!h.doc.connected());
    }

    void startBrowseWithoutAQueueNameKeepsTheBlankOfTheWindowsText()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setQueueName("");
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QCOMPARE(h.entries(), QStringList() << "*Queue Name required* ");		// DA 8368
        QCOMPARE(fake.connxCalls, 0);
    }

    void startBrowseConnectFailureShowsTheConnectText()
    {
        Watchdog dog(60);
        fake.connxRc = MQRC_Q_MGR_NOT_AVAILABLE;
        Harness h;
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QCOMPARE(h.entries(), QStringList() << "2059 Queue manager not available (Connect) - may not be started");
        QVERIFY(!h.doc.browse().active);
    }

    void startBrowseLosingTheConnectionShowsTheLostText()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        fake.getRc = MQRC_CONNECTION_BROKEN;
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QVERIFY(h.newest().startsWith("2009 Connection to QM has been lost - Q closed"));
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.doc.browse().active);
        QVERIFY(!h.doc.queueOpen());
    }

    void startBrowseMatchesIdsLikeRead()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        MainOptions o;
        o.get.matchCorrelId = true;
        h.ops.setOptions(o);
        rfh::MqmdFields f;
        memset(f.correlId, 0x66, sizeof(f.correlId));
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QVERIFY(fake.getLog.at(0).matchOptions & MQMO_MATCH_CORREL_ID);
    }

    void startBrowseConvertKeepsTheNativeEncodingForHostIntegersAsWindowsDoes()
    {
        // readFirstMessage (DA 8226-8240) sets only the PC encoding; with host integers the MQMD keeps MQENC_NATIVE
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        MainOptions o;
        o.get.convert = true;
        h.ops.setOptions(o);
        rfh::MqmdFields f;
        f.ccsid = "37";
        f.encoding = NUMERIC_HOST;
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QCOMPARE(fake.getLog.at(0).mdCcsid, (MQLONG)37);
        QCOMPARE(fake.getLog.at(0).mdEncoding, (MQLONG)MQENC_NATIVE);
    }

    void browseNextShowsTheNextMessageAndAllowsBrowsePrev()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("one"));
        enqueue(fakeMessage("two", MQFMT_STRING, 1208, 0x22));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.browseNext(); }));
        QCOMPARE(h.lastFlow(), QString("browseNext"));
        QVERIFY(fake.getLog.back().options & MQGMO_BROWSE_NEXT);
        QCOMPARE(fake.getLog.back().matchOptions, (MQLONG)0);			// no match options on browse next (DA 8556)
        QCOMPARE(bodyOf(h.doc.message()), std::string("two"));
        QCOMPARE(h.doc.browse().position, 2L);
        QVERIFY(h.doc.browse().previousAllowed());
        QVERIFY(h.doc.enables().browsePrev);
        QCOMPARE(h.entries(), QStringList() << "Next message (2) read from RFHUTIL.TEST.OPS length=3");
        QCOMPARE(h.doc.dataSource(), std::string("Message Data (3) msg 2 of 2 from RFHUTIL.TEST.OPS"));
    }

    void browseNextAtTheEndShowsNoMoreMessagesRefreshesTheDepthAndEndsTheBrowse()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("only"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.browseNext(); }));
        // browseNext replaces the pending text (DA 8638), refreshes the depth and then disconnects (DA 8644-8648)
        QCOMPARE(h.entries(), QStringList() << "No more messages in queue");
        QVERIFY(!h.doc.browse().active);
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)1);			// the real depth, read before the disconnect, not the 2033 zero
        QCOMPARE(fake.discCalls, 1);
        QCOMPARE(bodyOf(h.doc.message()), std::string("only"));	// the last message stays shown
    }

    void browseNextFailureShowsBothOperationWordsAndEndsTheBrowse()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("one"));
        enqueue(fakeMessage("two"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        h.log.clear();
        fake.getRc = MQRC_NOT_AUTHORIZED;
        QVERIFY(h.run([&]() { h.ops.browseNext(); }));
        QCOMPARE(h.entries(), QStringList() << "2035 Not authorized (BrowseNext)\r\n2035 Not authorized (GetNext)");
        QVERIFY(!h.doc.browse().active);
        QVERIFY(!h.doc.connected());
    }

    void browseNextLosingTheConnectionEndsTheBrowse()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("one"));
        enqueue(fakeMessage("two"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        h.log.clear();
        fake.getRc = MQRC_CONNECTION_BROKEN;
        QVERIFY(h.run([&]() { h.ops.browseNext(); }));
        QVERIFY(h.newest().startsWith("2009 Connection to QM has been lost - Q closed"));
        QVERIFY(!h.doc.browse().active);
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
    }

    void browseNextIsDisabledWithoutABrowse()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        QVERIFY(h.run([&]() { h.ops.browseNext(); }));
        QVERIFY(h.log.text().isEmpty());
        QCOMPARE(fake.getCalls, 0);
    }

    void browseNextClearsSetIdentityContext()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("one"));
        enqueue(fakeMessage("two"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        ShellState shell = h.doc.shell();
        shell.setIdentity = true;
        h.doc.setShell(shell);
        QVERIFY(h.run([&]() { h.ops.browseNext(); }));
        QVERIFY(!h.doc.shell().setIdentity);
    }

    void startBrowseDoesNotClearSetIdentityContext()
    {
        // OnStartbr (GEN 780) does not touch m_set_UserID
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("one"));
        ShellState shell = h.doc.shell();
        shell.setIdentity = true;
        h.doc.setShell(shell);
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QVERIFY(h.doc.shell().setIdentity);
    }

    void browsePrevGoesBackOneMessage()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("one"));
        enqueue(fakeMessage("two", MQFMT_STRING, 1208, 0x22));
        enqueue(fakeMessage("three", MQFMT_STRING, 1208, 0x33));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QVERIFY(h.run([&]() { h.ops.browseNext(); }));
        QVERIFY(h.run([&]() { h.ops.browseNext(); }));
        QCOMPARE(h.doc.browse().position, 3L);
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.browsePrev(); }));
        QCOMPARE(h.lastFlow(), QString("browsePrev"));
        QCOMPARE(bodyOf(h.doc.message()), std::string("two"));
        QCOMPARE(h.doc.browse().position, 2L);
        QVERIFY(h.doc.browse().previousAllowed());
        QCOMPARE(h.entries(), QStringList() << "Previous message (2) read from RFHUTIL.TEST.OPS length=3");
        QCOMPARE(h.doc.dataSource(), std::string("Message Data (3) msg 2 of 3 from RFHUTIL.TEST.OPS"));

        QVERIFY(h.run([&]() { h.ops.browsePrev(); }));
        QCOMPARE(bodyOf(h.doc.message()), std::string("one"));
        QCOMPARE(h.doc.browse().position, 1L);
        QVERIFY(!h.doc.browse().previousAllowed());		// back at the first message: browsePrevActive = 0 (DA 9172)
        QVERIFY(!h.doc.enables().browsePrev);
        QVERIFY(h.doc.browse().active);
    }

    void browsePrevIsDisabledAtTheFirstMessage()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("one"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        const int gets = fake.getCalls;
        QVERIFY(h.run([&]() { h.ops.browsePrev(); }));
        QCOMPARE(fake.getCalls, gets);
    }

    void browsePrevFailureEndsTheBrowseWithTheBrowsePrevText()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("one"));
        enqueue(fakeMessage("two", MQFMT_STRING, 1208, 0x22));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        QVERIFY(h.run([&]() { h.ops.browseNext(); }));
        h.log.clear();
        fake.getRc = MQRC_NOT_AUTHORIZED;
        QVERIFY(h.run([&]() { h.ops.browsePrev(); }));
        QCOMPARE(h.entries(), QStringList() << "2035 Not authorized (BrowsePrevGet)");
        QVERIFY(!h.doc.browse().active);
        QVERIFY(!h.doc.connected());
    }

    void browsePrevCanBeCancelledAndKeepsTheBrowse()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        for (int i = 0; i < 6; i++)
        {
            enqueue(fakeMessage("m" + std::to_string(i), MQFMT_STRING, 1208, (unsigned char)(0x10 + i)));
        }
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        for (int i = 0; i < 5; i++)
        {
            QVERIFY(h.run([&]() { h.ops.browseNext(); }));
        }
        QCOMPARE(h.doc.browse().position, 6L);
        h.log.clear();
        const int base = fake.getCalls;
        fake.onGet = [&](int n)
        {
            if (n == base + 3)
            {
                const_cast<std::atomic<bool> *>(h.controller.cancelFlag())->store(true);
            }
        };
        QVERIFY(h.run([&]() { h.ops.browsePrev(); }));
        fake.onGet = nullptr;
        QCOMPARE(h.entries().size(), 1);
        QVERIFY2(h.newest().contains("cancelled"), qPrintable(h.newest()));
        QVERIFY(h.doc.connected());
        QVERIFY(h.doc.browse().active);			// a cancel does not end the browse
        QVERIFY(h.doc.browse().position >= 1);
        QCOMPARE(fake.discCalls, 0);
    }

    void endBrowseClosesDisconnectsAndSaysSo()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("one"));
        enqueue(fakeMessage("two"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.endBrowse(); }));
        QCOMPARE(h.lastFlow(), QString("endBrowse"));
        QCOMPARE(h.entries(), QStringList() << "Browse operation ended");
        QVERIFY(!h.doc.browse().active);
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)2);		// getCurrentDepth before the close (DA 8438)
        QCOMPARE(fake.closeCalls >= 1, true);
        QCOMPARE(fake.discCalls, 1);
        QVERIFY(fake.liveHandles.empty());
        QVERIFY(h.doc.enables().readQ);
    }

    void endBrowseWithoutABrowseDoesNothing()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        QVERIFY(h.run([&]() { h.ops.endBrowse(); }));
        QVERIFY(h.log.text().isEmpty());
        QCOMPARE(fake.discCalls, 0);
    }

    void browseFlowsAreRefusedWhileAnotherOperationRuns()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h;
        enqueue(fakeMessage("one"));
        enqueue(fakeMessage("two"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        h.log.clear();
        const int gets = fake.getCalls;
        for (const std::function<void()> & flow : std::vector<std::function<void()>>{
                 [&]() { h.ops.browseNext(); }, [&]() { h.ops.endBrowse(); }, [&]() { h.ops.startBrowse(); },
                 [&]() { h.ops.browseQ(); } })
        {
            QVERIFY(h.run(flow));
            QCOMPARE(h.newest(), QString("Another operation is still running"));
        }
        QCOMPARE(fake.getCalls, gets);
        QVERIFY(h.doc.browse().active);
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    // ======================================================================================================
    // Write Q (putMessage DA 6166; General::OnWriteq GEN 334)
    // ======================================================================================================

    void writeQPutsTheDataWithTheMqmdOfTheTabAndSaysMessageSent()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setMessage(bodyMessage("payload"), "Message Data", true);
        rfh::MqmdFields f;
        f.format = "MQSTR";
        f.ccsid = "1208";
        f.priority = "4";
        f.persistence = MQPER_PERSISTENT;
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(h.lastFlow(), QString("writeQ"));
        QCOMPARE(fake.putLog.size(), size_t(1));
        const rfh_test::PutRecord & put = fake.putLog.at(0);
        QCOMPARE(put.queue, std::string(kQueue));
        QCOMPARE(std::string(put.message.data.begin(), put.message.data.end()), std::string("payload"));
        QCOMPARE(std::string(put.message.md.Format, 8), std::string("MQSTR   "));		// Format as typed, blank filled
        QCOMPARE(put.message.md.CodedCharSetId, (MQLONG)1208);
        QCOMPARE(put.message.md.Priority, (MQLONG)4);
        QCOMPARE(put.message.md.Persistence, (MQLONG)MQPER_PERSISTENT);
        QVERIFY(put.pmoOptions & MQPMO_FAIL_IF_QUIESCING);
        QVERIFY(put.pmoOptions & MQPMO_NEW_MSG_ID);					// m_new_msg_id defaults on
        QVERIFY(put.pmoOptions & MQPMO_NO_SYNCPOINT);
        QCOMPARE(put.pmoOptions & (MQPMO_NEW_CORREL_ID | MQPMO_LOGICAL_ORDER | MQPMO_SET_ALL_CONTEXT | MQPMO_SET_IDENTITY_CONTEXT), 0);
        QVERIFY(fake.openLog.front().options & MQOO_OUTPUT);
        QCOMPARE(h.entries(), QStringList() << "Message sent to RFHUTIL.TEST.OPS length=7");	// DA 6454
        QCOMPARE(fake.queue(kQueue).messages.size(), size_t(1));
        QVERIFY(h.doc.connected());
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)1);			// getCurrentDepth after the put
        // updateMQMDafterPut: the new message id is shown on the tab
        QCOMPARE(std::string((const char *)h.doc.mqmd().msgId, 6), std::string("FAKEID"));
        QVERIFY(h.doc.queueOpen());
    }

    void writeQCountsTheHeadersInTheLength()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setMessage(bodyMessage("data", rfh2Header()), "Message Data", true);
        rfh::MqmdFields f;
        f.format = "MQHRF2";
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(h.entries(), QStringList() << "Message sent to RFHUTIL.TEST.OPS length=40");	// msgSize includes the headers
        QCOMPARE(fake.putLog.at(0).message.data.size(), size_t(40));
        QCOMPARE(std::string(fake.putLog.at(0).message.md.Format, 8), std::string("MQHRF2  "));
    }

    void writeQDoesNotChangeTheDataOfTheDocument()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setMessage(bodyMessage("payload"), "Message Data (7) from /tmp/f", false);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(bodyOf(h.doc.message()), std::string("payload"));
        QCOMPARE(h.doc.dataSource(), std::string("Message Data (7) from /tmp/f"));
    }

    void writeQWithoutAQueueNameKeepsTheBlank()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setQueueName("");
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(h.entries(), QStringList() << "*Queue Name required* ");		// DA 6218
        QCOMPARE(fake.connxCalls, 0);
    }

    void writeQSetIdentityAndSetAllReachTheOpenAndThePut()
    {
        Watchdog dog(60);
        struct Case { bool iden, all; MQLONG pmo, open; } cases[] =
        {
            { true, false, MQPMO_SET_IDENTITY_CONTEXT, MQOO_SET_IDENTITY_CONTEXT },
            { false, true, MQPMO_SET_ALL_CONTEXT, MQOO_SET_ALL_CONTEXT },
            { true, true, MQPMO_SET_ALL_CONTEXT, MQOO_SET_ALL_CONTEXT },	// Set All wins (DA 11935); the core sends the one MQ accepts
        };
        for (const Case & c : cases)
        {
            fake.reset();
            fake.queue(kQueue);
            Harness h;
            ShellState shell = h.doc.shell();
            shell.setIdentity = c.iden;
            shell.setAll = c.all;
            h.doc.setShell(shell);
            QVERIFY(h.run([&]() { h.ops.writeQ(); }));
            QCOMPARE(fake.putLog.at(0).pmoOptions & (MQPMO_SET_IDENTITY_CONTEXT | MQPMO_SET_ALL_CONTEXT), c.pmo);
            QVERIFY(fake.openLog.front().options & c.open);
            QVERIFY2(h.doc.shell().setIdentity == c.iden, "Write Q does not clear Set Iden Context");
        }
    }

    void writeQSetIdentityWithABlankUserIdPutsZerosNotTheOsUser()
    {
        // Pinned in Task 6: setMessageMQMD looks the OS user up and discards it (MQMDPAGE.cpp:2573-2604)
        Watchdog dog(60);
        Harness h;
        ShellState shell = h.doc.shell();
        shell.setIdentity = true;
        h.doc.setShell(shell);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        const MQMD & md = fake.putLog.at(0).message.md;
        for (size_t i = 0; i < sizeof(md.UserIdentifier); i++)
        {
            QCOMPARE((int)md.UserIdentifier[i], 0);
        }
    }

    void writeQSetIdentityPutsTheUserIdOfTheTab()
    {
        Watchdog dog(60);
        Harness h;
        ShellState shell = h.doc.shell();
        shell.setIdentity = true;
        h.doc.setShell(shell);
        rfh::MqmdFields f;
        f.userId = "TESTER";
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(std::string(fake.putLog.at(0).message.md.UserIdentifier, 6), std::string("TESTER"));
    }

    void writeQWithoutSetContextIgnoresTheUserIdOfTheTab()
    {
        Watchdog dog(60);
        Harness h;
        rfh::MqmdFields f;
        f.userId = "TESTER";
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QVERIFY(memcmp(fake.putLog.at(0).message.md.UserIdentifier, "TESTER", 6) != 0);		// the default is left alone
    }

    void writeQNewMsgIdNewCorrelIdAndLogicalOrderFollowTheMainTab()
    {
        Watchdog dog(60);
        Harness h;
        MainOptions o;
        o.put.newMsgId = false;
        o.put.newCorrelId = true;
        o.put.logicalOrder = true;
        h.ops.setOptions(o);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        const MQLONG pmo = fake.putLog.at(0).pmoOptions;
        QCOMPARE(pmo & MQPMO_NEW_MSG_ID, 0);
        QVERIFY(pmo & MQPMO_NEW_CORREL_ID);
        QVERIFY(pmo & MQPMO_LOGICAL_ORDER);
    }

    void writeQClusterBindOptionsReachTheOpenIncludingGroup()
    {
        // The Windows Group radio is dead (a defect fixed on the owner's decision): all four options reach MQOPEN
        Watchdog dog(60);
        struct Case { rfh::BindOption bind; MQLONG open; } cases[] =
        {
            { rfh::BindOption::AsQueueDefinition, MQOO_BIND_AS_Q_DEF },
            { rfh::BindOption::OnOpen, MQOO_BIND_ON_OPEN },
            { rfh::BindOption::NotFixed, MQOO_BIND_NOT_FIXED },
            { rfh::BindOption::OnGroup, MQOO_BIND_ON_GROUP },
        };
        for (const Case & c : cases)
        {
            fake.reset();
            fake.queue(kQueue);
            Harness h;
            MainOptions o;
            o.put.bind = c.bind;
            h.ops.setOptions(o);
            QVERIFY(h.run([&]() { h.ops.writeQ(); }));
            QCOMPARE(fake.openLog.front().options & (MQOO_BIND_AS_Q_DEF | MQOO_BIND_ON_OPEN | MQOO_BIND_NOT_FIXED | MQOO_BIND_ON_GROUP), c.open);
        }
    }

    void writeQAlternateUserComesFromTheMqmdTab()
    {
        Watchdog dog(60);
        Harness h;
        ShellState shell = h.doc.shell();
        shell.alternateUser = true;
        h.doc.setShell(shell);
        rfh::MqmdFields f;
        f.userId = "ALTUSER";
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QVERIFY(fake.openLog.front().options & MQOO_ALTERNATE_USER_AUTHORITY);
        QCOMPARE(fake.openLog.front().alternateUser, std::string("ALTUSER"));
    }

    void writeQRemoteQueueManagerIsUsedAtOpen()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setRemoteQueueManager("REMOTE.QM");
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(fake.openLog.front().name, std::string(kQueue));
        QVERIFY2(h.entries().size() == 1, "the put works");
    }

    // The four flags of the MQMD tab decide the syncpoint and the commit (putMessage DA 6342-6351, 6484-6503).
    // Columns: segment yes, segment last, group yes, group last; expected: under syncpoint, committed.
    void writeQDerivesTheSyncpointAndTheCommitFromTheFourFlags()
    {
        Watchdog dog(60);
        struct Row { bool segYes, segLast, grpYes, grpLast, syncpoint, commit; };
        const Row rows[] =
        {
            // no flags: no syncpoint
            { false, false, false, false, false, false },
            // group only
            { false, false, true,  false, true,  false },		// middle of a group: pending
            { false, false, false, true,  true,  true  },		// !segYes && grpLast: commit
            { false, false, true,  true,  true,  true  },
            // segment only
            { true,  false, false, false, true,  false },		// middle of a segment
            { false, true,  false, false, true,  false },		// segLast without segYes: syncpoint, but the else branch needs grpLast
            { true,  true,  false, false, true,  true  },		// segYes && segLast && !grpYes
            // segment and group
            { true,  false, true,  false, true,  false },
            { true,  true,  true,  false, true,  false },		// last segment of a message that is not the last of its group
            { true,  true,  true,  true,  true,  true  },		// last segment of the last message
            { true,  false, true,  true,  true,  false },		// grpLast but the segment goes on
            { true,  false, false, true,  true,  false },
            { true,  true,  false, true,  true,  true  },		// !grpYes
            { false, true,  true,  false, true,  false },
            { false, true,  true,  true,  true,  true  },
            { false, true,  false, true,  true,  true  },
        };
        for (const Row & row : rows)
        {
            QByteArray label = QByteArray("segYes=") + (row.segYes ? "1" : "0") + " segLast=" + (row.segLast ? "1" : "0")
                + " grpYes=" + (row.grpYes ? "1" : "0") + " grpLast=" + (row.grpLast ? "1" : "0");
            fake.reset();
            fake.queue(kQueue);
            Harness h;
            rfh::MqmdFields f;
            f.segmentYes = row.segYes;
            f.segmentLast = row.segLast;
            f.groupYes = row.grpYes;
            f.groupLast = row.grpLast;
            h.doc.setMqmd(f);
            QVERIFY2(h.run([&]() { h.ops.writeQ(); }), label.constData());
            QVERIFY2(h.entries().value(0).startsWith("Message sent"), label.constData());
            QVERIFY2(((fake.putLog.at(0).pmoOptions & MQPMO_SYNCPOINT) != 0) == row.syncpoint, label.constData());
            QVERIFY2(((fake.putLog.at(0).pmoOptions & MQPMO_NO_SYNCPOINT) != 0) == !row.syncpoint, label.constData());
            QVERIFY2((fake.commitCalls == 1) == row.commit, label.constData());
            // a message put under syncpoint is visible exactly when it was committed
            const size_t visible = fake.queue(kQueue).messages.size();
            QVERIFY2(visible == ((!row.syncpoint || row.commit) ? 1u : 0u), label.constData());
        }
    }

    void writeQLeavesAGroupPendingAndTheNextCloseBacksItOut()
    {
        // An incomplete group put under syncpoint is not published by Close Q: the unit of work is rolled back first
        // (Windows closeQ 12413 rolls back an open unit of work; inventory 3.2 Close Q)
        Watchdog dog(60);
        Harness h;
        rfh::MqmdFields f;
        f.groupYes = true;
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(fake.commitCalls, 0);
        QCOMPARE(fake.pendingPuts.size(), size_t(1));
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.closeQ(); }));
        QCOMPARE(fake.backCalls, 1);
        QCOMPARE(h.entries(), QStringList() << "Unit of work rolled back");		// rollbackUOW: appendError (DA 12618)
        QVERIFY(fake.queue(kQueue).messages.empty());
        QVERIFY(!h.doc.connected());
        // MQBACK came before MQDISC, which would have published it
        QCOMPARE(fake.callOrder, (std::vector<std::string>{ "back", "disc" }));
    }

    void writeQCompletingTheGroupCommitsEverythingAndClearsThePendingUnit()
    {
        Watchdog dog(60);
        Harness h;
        rfh::MqmdFields f;
        f.groupYes = true;
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        f.groupYes = false;
        f.groupLast = true;
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(fake.commitCalls, 1);
        QCOMPARE(fake.queue(kQueue).messages.size(), size_t(2));
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.closeQ(); }));
        QCOMPARE(fake.backCalls, 0);			// nothing pending any more
        QVERIFY(h.log.text().isEmpty());
    }

    void writeQDisconnectWithAPendingGroupBacksItOutToo()
    {
        Watchdog dog(60);
        Harness h;
        rfh::MqmdFields f;
        f.groupYes = true;
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.disconnectQm(); }));
        QCOMPARE(fake.backCalls, 1);
        QVERIFY(fake.queue(kQueue).messages.empty());
        QVERIFY2(h.newest().startsWith("Unit of work rolled back\r\nQueue closed\r\nDisconnected from queue manager"), qPrintable(h.newest()));
    }

    void aFailedCommitShowsTheCommitTextBacksTheUnitOutAndDisconnects()
    {
        // commitUOW (DA 12584-12588): setErrorMsg(cc, rc, "commit"), discQM. The core's MQDISC would commit what
        // the failed MQCMIT did not, so the unit is rolled back first.
        Watchdog dog(60);
        fake.cmitRc = MQRC_NOT_AUTHORIZED;
        Harness h;
        rfh::MqmdFields f;
        f.groupLast = true;
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        const QStringList entries = h.entries();
        QCOMPARE(entries.size(), 1);
        const QStringList lines = entries.at(0).split("\r\n");
        QCOMPARE(lines.at(0), QString("Message sent to RFHUTIL.TEST.OPS length=0"));
        QCOMPARE(lines.at(1), QString("2035 Not authorized (commit)"));
        QVERIFY2(entries.at(0).contains("Unit of work rolled back"), qPrintable(entries.at(0)));
        QVERIFY(!h.doc.connected());
        QCOMPARE(fake.backCalls, 1);
        QVERIFY(fake.queue(kQueue).messages.empty());
        QCOMPARE(fake.callOrder.back(), std::string("disc"));
    }

    void aCommitThatLosesTheConnectionSaysTheOutcomeIsUnknown()
    {
        Watchdog dog(60);
        fake.cmitRc = MQRC_CONNECTION_BROKEN;
        Harness h;
        rfh::MqmdFields f;
        f.groupLast = true;
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QVERIFY(!h.doc.connected());
        QVERIFY2(h.newest().contains("2009 Connection to QM has been lost - Q closed"), qPrintable(h.newest()));
        QVERIFY2(h.newest().contains("outcome of the unit of work is unknown"), qPrintable(h.newest()));	// the core's text, verbatim
        QVERIFY2(!h.newest().contains("remain on the queue"), qPrintable(h.newest()));
        QCOMPARE(fake.backCalls, 0);			// nothing to back out on a released connection
    }

    void aFailedPutShowsTheWindowsTextAndLeavesTheTabAlone()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        fake.putRc = MQRC_Q_FULL;
        rfh::MqmdFields f;
        f.seqNo = "7";
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(h.entries(), QStringList() << "2053 Queue full");		// DA 9561
        QCOMPARE(h.doc.mqmd().seqNo, std::string("7"));				// updateMQMDafterPut is not called after a failure
        QCOMPARE(fake.commitCalls, 0);
        QVERIFY(h.doc.connected());
    }

    void aPutThatLosesTheConnectionRunsTheCleanup()
    {
        Watchdog dog(60);
        Harness h;
        fake.putRc = MQRC_CONNECTION_BROKEN;
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(h.entries(), QStringList() << "2009 Connection to QM has been lost - Q closed");
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
    }

    void aPutToATooSmallQueueUsesTheMaximumLengthsInTheText()
    {
        Watchdog dog(60);
        Harness h;
        fake.queue(kQueue).maxMsgLength = 100;
        fake.putRc = MQRC_MSG_TOO_BIG_FOR_Q;		// 2030
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        // QMgr max comes from connect (4194304 in the fake). The queue's maximum is known only if an inquiry ran:
        // none ran before this put, so it is not claimed.
        QVERIFY2(h.newest().startsWith("2030 Message too large for queue (check q and qm settings) QMgr max=4194304 Queue max="), qPrintable(h.newest()));
    }

    void writeQAppliesTheReturnedValuesToTheSegmentFieldsOfTheTab()
    {
        // updateMQMDafterPut(&mqmd, msgSize): while a segment sequence continues the offset advances by the length put
        Watchdog dog(60);
        Harness h;
        h.doc.setMessage(bodyMessage("12345"), "Message Data", true);
        rfh::MqmdFields f;
        f.segmentYes = true;
        f.offset = "";
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(h.doc.mqmd().offset, std::string("5"));
        QVERIFY(h.doc.mqmd().segmentYes);
    }

    void writeQIsRefusedWhileBrowsingAndWhileBusy()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(fake.putCalls, 0);
        QVERIFY(h.log.text().isEmpty());
        QVERIFY(h.run([&]() { h.ops.endBrowse(); }));
        h.log.clear();

        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(fake.putCalls, 0);
        QCOMPARE(h.entries(), QStringList() << "Another operation is still running");
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    void writeQConnectsOnDemandAndReconnectsWhenTheQueueManagerChanged()
    {
        Watchdog dog(60);
        Harness h;
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(fake.connxCalls, 1);
        h.doc.setQueueManager("QM2");
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(fake.connxCalls, 2);
        QCOMPARE(fake.discCalls, 1);
        QCOMPARE(fake.connxLog.at(1).qmName, std::string("QM2"));
        QCOMPARE(fake.putLog.size(), size_t(2));
    }

    void writeQRemembersTheQueueAndAnnouncesTheTitle()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        AppSettings settings(dir.filePath("rfhutil.conf"));
        Harness h;
        h.ops.setSettings(&settings);
        QSignalSpy title(&h.ops, &Operations::titleChanged);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(settings.recentQueues(kQm), QStringList() << kQueue);
        QCOMPARE(title.count(), 1);
    }

    void aReadMessageCanBeWrittenBackWithTheTabsMqmd()
    {
        // read -> tab -> write: the descriptor travels through fromGetMqmd and toPutMqmd, headers and body unchanged
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("data", MQFMT_RF_HEADER_2, 1208, 0x11, rfh2Header()));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        QCOMPARE(h.doc.mqmd().format, std::string("MQHRF2  "));
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        const rfh_test::PutRecord & put = fake.putLog.at(0);
        QCOMPARE(put.message.data.size(), size_t(40));
        QCOMPARE(std::string(put.message.md.Format, 8), std::string("MQHRF2  "));
        QCOMPARE(put.message.md.CodedCharSetId, (MQLONG)1208);
    }

    // ======================================================================================================
    // Open File, Save File (ReadDataFile DA 928, ReadFileData DA 994, WriteDataFile DA 1703, WriteFile DA 1778)
    // The work is fileio's; Operations calls it with the Main options, off the GUI thread, and applies the result.
    // ======================================================================================================

    void mainOptionsDefaultToWindowsFileSettings()
    {
        MainOptions o;
        QVERIFY2(o.write.includeMqmd, "Write > Include MQMD is on by default (DataArea.cpp:373)");
        QVERIFY(!o.write.dataOnly);
        QVERIFY(!o.read.removeCrLf && !o.read.unixLineEnds && !o.read.ignoreHeaders);
        QCOMPARE(o.fileCodePage, 0);
    }

    void openFileReadsAPlainFileIntoTheDocument()
    {
        Watchdog dog(60);
        Harness h;
        const QString path = goldenPath("plain-crlf.txt");
        QVERIFY(h.run([&]() { h.ops.openFile(path); }));
        QCOMPARE(h.lastFlow(), QString("openFile"));
        QCOMPARE(QByteArray((const char *)h.doc.message().body.data(), (int)h.doc.message().body.size()), golden("plain-crlf.txt"));
        QCOMPARE(h.doc.dataSource(), std::string("Message Data (56) from ") + path.toStdString());
        QCOMPARE(h.doc.filePath(), path);
        QVERIFY(!h.doc.dirty());
        QCOMPARE(h.entries(), QStringList() << QString("56 bytes read from file %1").arg(path));
        QCOMPARE(h.doc.render().charFormat, (int)CHAR_ASCII);
        QVERIFY(h.doc.message().headers.empty());
        QCOMPARE(fake.connxCalls, 0);			// a file needs no queue manager
    }

    void openFileTakesTheMqmdTheHeadersAndTheFormatsFromTheFile()
    {
        Watchdog dog(60);
        Harness h;
        const QString path = goldenPath("mqmd-rfh2-text.bin");
        QVERIFY(h.run([&]() { h.ops.openFile(path); }));
        QCOMPARE(h.doc.mqmd().format, std::string("MQHRF2"));
        QVERIFY(!h.doc.message().headers.empty());
        QCOMPARE(h.doc.render().ccsid, h.doc.message().mqmd.CodedCharSetId);
        QCOMPARE(h.entries().size(), 1);
        QVERIFY2(h.newest().contains("bytes read (data 9 dlq 0 rfh1 0, rfh2 92 cics 0 ims 0) from file"), qPrintable(h.newest()));	// ReadFileData's breakdown of the headers found
        QVERIFY(h.doc.hasFile());
    }

    void openFileUsesTheCodePageAndTheReadOptionsOfTheMainTab()
    {
        Watchdog dog(60);
        {
            Harness h;
            MainOptions o;
            o.fileCodePage = 500;
            h.ops.setOptions(o);
            QVERIFY(h.run([&]() { h.ops.openFile(goldenPath("ebcdic-500.bin")); }));
            QCOMPARE(h.doc.render().ccsid, 500);
            QCOMPARE(h.doc.render().charFormat, (int)CHAR_EBCDIC);
        }
        {
            Harness h;
            MainOptions o;
            o.read.removeCrLf = true;
            h.ops.setOptions(o);
            QVERIFY(h.run([&]() { h.ops.openFile(goldenPath("plain-crlf.txt")); }));
            const QByteArray body((const char *)h.doc.message().body.data(), (int)h.doc.message().body.size());
            QVERIFY(!body.contains('\r') && !body.contains('\n'));
        }
        {
            Harness h;
            MainOptions o;
            o.read.unixLineEnds = true;
            h.ops.setOptions(o);
            QVERIFY(h.run([&]() { h.ops.openFile(goldenPath("plain-lf.txt")); }));
            const QByteArray body((const char *)h.doc.message().body.data(), (int)h.doc.message().body.size());
            QVERIFY(body.contains("\r\n"));
            QVERIFY(!body.contains("\n\n"));
        }
        {
            Harness h;
            MainOptions o;
            o.read.ignoreHeaders = true;
            h.ops.setOptions(o);
            QVERIFY(h.run([&]() { h.ops.openFile(goldenPath("rfh2-only.bin")); }));
            QVERIFY(h.doc.message().headers.empty());
            QCOMPARE((int)h.doc.message().body.size(), 127);
        }
    }

    void openFileOfAMissingOrUnreadableFileShowsTheWindowsTextAndKeepsTheDocument()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString missing = dir.filePath("nothing.msg");
        const QString directory = dir.path();
        for (const QString & path : { missing, directory })
        {
            Harness h;
            fillDocument(h.doc);
            QVERIFY(h.run([&]() { h.ops.openFile(path); }));
            QCOMPARE(h.entries(), QStringList() << QString("Could not open file %1").arg(path));		// DA 1046
            expectDocumentUntouched(h.doc);
        }
        if (geteuid() != 0)
        {
            const QString locked = dir.filePath("locked.msg");
            QFile f(locked);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("secret");
            f.close();
            QVERIFY(chmod(locked.toLocal8Bit().constData(), 0) == 0);
            Harness h;
            fillDocument(h.doc);
            QVERIFY(h.run([&]() { h.ops.openFile(locked); }));
            QCOMPARE(h.entries(), QStringList() << QString("Could not open file %1").arg(locked));
            expectDocumentUntouched(h.doc);
        }
    }

    void openFileOfAFileOverTheMessageLimitIsRefusedBeforeItIsReadAndKeepsTheDocument()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString big = dir.filePath("big.msg");
        QFile f(big);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.resize((qint64)rfh::format::kMaxRenderBody + 1));		// sparse: costs no disk
        f.close();
        Harness h;
        fillDocument(h.doc);
        QVERIFY(h.run([&]() { h.ops.openFile(big); }));
        QCOMPARE(h.entries(), QStringList() << QString("File %1 is too large: 104857601 bytes, the limit is 104857600 bytes (the largest MQ message)").arg(big));		// NEW
        expectDocumentUntouched(h.doc);
        QVERIFY(!h.controller.busy());
    }

    void openFileOfAFileAtTheLimitIsAccepted()
    {
        // the limit is the largest message the Data tab can show; a file of exactly that size is read
        Watchdog dog(60);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString big = dir.filePath("limit.msg");
        QFile f(big);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QVERIFY(f.resize((qint64)rfh::format::kMaxRenderBody));
        f.close();
        Harness h;
        QVERIFY(h.run([&]() { h.ops.openFile(big); }));
        QCOMPARE((qint64)h.doc.message().body.size(), (qint64)rfh::format::kMaxRenderBody);
        QVERIFY(h.newest().contains("bytes read from file"));
    }

    void openFileOfAnEmptyFileSucceedsWithAnEmptyBody()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        const QString path = dir.filePath("empty.msg");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        Harness h;
        fillDocument(h.doc);
        QVERIFY(h.run([&]() { h.ops.openFile(path); }));
        QCOMPARE(h.entries(), QStringList() << QString("0 bytes read from file %1").arg(path));
        QVERIFY(h.doc.message().body.empty());
        QCOMPARE(h.doc.filePath(), path);
    }

    void openFileOfAShortMqmdIsReadAsData()
    {
        // ReadFileData recognises an MQMD only when enough bytes were read for one (DA 1106); 10 bytes are data
        Watchdog dog(60);
        QTemporaryDir dir;
        const QString path = dir.filePath("short.msg");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("MD  \x02\x00\x00\x00 ab", 10);
        f.close();
        Harness h;
        QVERIFY(h.run([&]() { h.ops.openFile(path); }));
        QCOMPARE((int)h.doc.message().body.size(), 10);
        QVERIFY(h.doc.message().headers.empty());
        QVERIFY2(h.newest().startsWith("10 bytes read from file"), qPrintable(h.newest()));
    }

    void openFileKeepsTheNameInTheStatusWhenItIsLong()
    {
        Watchdog dog(60);
        Harness h;
        const QString path = goldenPath("plain-lf.txt");
        QVERIFY(h.run([&]() { h.ops.openFile(path); }));
        QVERIFY(h.newest().contains(path));
    }

    void openFileReturnsAtOnceAndReadsOnTheWorker()
    {
        Watchdog dog(60);
        Harness h;
        h.finished.clear();
        h.ops.openFile(goldenPath("plain-crlf.txt"));
        QCOMPARE(h.finished.count(), 0);			// the GUI thread was not held for the read
        QVERIFY(h.controller.busy());
        QVERIFY(h.doc.shell().busy);
        QVERIFY(h.finished.wait(5000));
        QVERIFY(h.doc.hasData());
        QVERIFY(!h.controller.busy());
    }

    void openFileIsRefusedWhileBusyAndWhileBrowsing()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.openFile(goldenPath("plain-crlf.txt")); }));
        QVERIFY(h.log.text().isEmpty());			// OnUpdateOpen: disabled while browsing
        QVERIFY(!h.doc.hasFile());

        QVERIFY(h.run([&]() { h.ops.endBrowse(); }));
        h.log.clear();
        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        QVERIFY(h.run([&]() { h.ops.openFile(goldenPath("plain-crlf.txt")); }));
        QCOMPARE(h.entries(), QStringList() << "Another operation is still running");
        QVERIFY(!h.doc.hasFile());
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    void goldenFilesRoundTripByteForByteThroughOpenAndSave()
    {
        Watchdog dog(60);
        const char * files[] = {
            "plain-crlf.txt", "plain-lf.txt", "binary-all-bytes.bin", "ebcdic-500.bin", "mqmd-v2-text.bin",
            "mqmd-rfh2-text.bin", "mqmd-dlh-rfh2-text.bin", "rfh2-only.bin"
        };
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const char * name : files)
        {
            QByteArray original = golden(name);
            QVERIFY2(!original.isEmpty(), name);
            Harness h;
            MainOptions o;
            o.write.includeMqmd = original.startsWith("MD  ");		// the file had an MQMD: write one (DA 1822)
            h.ops.setOptions(o);
            QVERIFY2(h.run([&]() { h.ops.openFile(goldenPath(name)); }), name);
            QVERIFY2(h.newest().contains("bytes read"), name);
            const QString out = dir.filePath(QString("out-") + name);
            QVERIFY2(h.run([&]() { h.ops.saveFile(out); }), name);
            QVERIFY2(h.newest().contains("written to file"), qPrintable(h.newest()));
            QCOMPARE(readAll(out), original);
            QCOMPARE(h.doc.filePath(), out);
            QVERIFY(!h.doc.dirty());
        }
    }

    void saveFileWritesTheMqmdOfTheTabWithTheDataAndSaysSo()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        Harness h;
        enqueue(fakeMessage("payload"));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        // edit the tab: the file gets the tab's descriptor, not the one MQ returned (buildMQMD, DA 1807)
        rfh::MqmdFields f = h.doc.mqmd();
        f.priority = "7";
        h.doc.setMqmd(f);
        const QString out = dir.filePath("saved.msg");
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.saveFile(out); }));
        const QByteArray bytes = readAll(out);
        QCOMPARE(bytes.size(), 364 + 7);
        QVERIFY(bytes.startsWith("MD  "));
        MQMD2 md;
        memcpy(&md, bytes.constData(), sizeof(md));
        QCOMPARE(md.Priority, (MQLONG)7);
        QCOMPARE(md.CodedCharSetId, (MQLONG)1208);
        QCOMPARE(bytes.mid(364), QByteArray("payload"));
        QCOMPARE(h.entries(), QStringList() << QString("371 bytes (data 7 mqmd 364) written to file %1").arg(out));
        QCOMPARE(h.doc.filePath(), out);
        QVERIFY(!h.doc.dirty());
    }

    void saveFileDataOnlyWritesNoMqmdAndNoHeaders()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        Harness h;
        MainOptions o;
        o.write.includeMqmd = false;
        o.write.dataOnly = true;
        h.ops.setOptions(o);
        enqueue(fakeMessage("data", MQFMT_RF_HEADER_2, 1208, 0x11, rfh2Header()));
        QVERIFY(h.run([&]() { h.ops.readQ(); }));
        const QString out = dir.filePath("data.msg");
        QVERIFY(h.run([&]() { h.ops.saveFile(out); }));
        QCOMPARE(readAll(out), QByteArray("data"));
    }

    void saveFileToAnUnwritablePathShowsTheCoreTextAndChangesNothing()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        Harness h;
        fillDocument(h.doc);
        const QString out = dir.filePath("no/such/dir/out.msg");
        QVERIFY(h.run([&]() { h.ops.saveFile(out); }));
        QCOMPARE(h.entries().size(), 1);
        QVERIFY2(h.newest().startsWith("Unable to open output file"), qPrintable(h.newest()));
        expectDocumentUntouched(h.doc);
    }

    void saveFileIsRefusedWhileBusy()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        Harness h;
        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        const QString out = dir.filePath("never.msg");
        QVERIFY(h.run([&]() { h.ops.saveFile(out); }));
        QCOMPARE(h.entries(), QStringList() << "Another operation is still running");
        QVERIFY(!QFileInfo::exists(out));
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    void saveFileIsAllowedWhileBrowsing()
    {
        // OnWriteFile is never disabled (General.cpp UpdatePageData leaves it on)
        Watchdog dog(60);
        QTemporaryDir dir;
        Harness h;
        enqueue(fakeMessage("browsed"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        const QString out = dir.filePath("browsed.msg");
        QVERIFY(h.run([&]() { h.ops.saveFile(out); }));
        QVERIFY(QFileInfo::exists(out));
        QVERIFY(h.doc.browse().active);
    }

    void fileDirectoriesAreRememberedOnSuccessOnly()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        AppSettings settings(dir.filePath("rfhutil.conf"));
        Harness h;
        h.ops.setSettings(&settings);
        QVERIFY(h.run([&]() { h.ops.openFile(dir.filePath("missing.msg")); }));
        QVERIFY(settings.lastDirectory("open").isEmpty());
        QVERIFY(h.run([&]() { h.ops.openFile(goldenPath("plain-crlf.txt")); }));
        QCOMPARE(settings.lastDirectory("open"), QString(RFH_GOLDEN_DIR));
        const QString out = dir.filePath("out.msg");
        QVERIFY(h.run([&]() { h.ops.saveFile(out); }));
        QCOMPARE(settings.lastDirectory("save"), dir.path());
    }

    // ======================================================================================================
    // Clear Data, Clear All (OnClearData GEN 636 -> clearFileData DA 10597; OnClearAll -> DeleteContents DA 1996)
    // ======================================================================================================

    void clearDataEmptiesOnlyTheBodyAndKeepsTheLog()
    {
        Watchdog dog(60);
        Harness h;
        fillDocument(h.doc);
        h.log.add("an earlier entry");
        QVERIFY(h.run([&]() { h.ops.clearData(); }));
        QCOMPARE(h.lastFlow(), QString("clearData"));
        QVERIFY(h.doc.message().body.empty());
        QCOMPARE(h.doc.dataSource(), std::string(AppDocument::EmptySource));
        QCOMPARE(h.doc.mqmd().format, std::string("MQSTR"));		// the MQMD tab and the file name stay
        QCOMPARE(h.doc.filePath(), QString("/tmp/previous.msg"));
        QCOMPARE(h.entries(), QStringList() << "an earlier entry");	// the status log is not cleared (inventory 3.2)
    }

    void clearAllEmptiesEverythingButTheLogAndTheConnection()
    {
        Watchdog dog(60);
        Harness h;
        h.connected();
        fillDocument(h.doc);
        rfh::QueueInfo info;
        info.depth = 12;
        h.doc.setQueueInfo(info);
        h.log.add("an earlier entry");
        QVERIFY(h.run([&]() { h.ops.clearAll(); }));
        QVERIFY(h.doc.message().body.empty());
        QVERIFY(!h.doc.hasFile());
        QCOMPARE(h.doc.mqmd().format, std::string());
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)0);				// m_q_depth = 0
        QVERIFY(h.doc.connected());
        QCOMPARE(h.entries(), QStringList() << "an earlier entry");
        QCOMPARE(fake.discCalls, 0);
    }

    void clearDataAndClearAllNeedNoConfirmation()
    {
        Watchdog dog(60);
        Harness h;
        fillDocument(h.doc);
        QVERIFY(h.run([&]() { h.ops.clearAll(); }));
        QVERIFY(h.prompter.asked.isEmpty());			// Windows asks only before a purge (General.cpp:900)
    }

    void clearIsDisabledWhileBrowsingAndRefusedWhileBusy()
    {
        Watchdog dog(60);
        Harness h;
        enqueue(fakeMessage("x"));
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        const std::string before = bodyOf(h.doc.message());
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.clearData(); }));
        QVERIFY(h.run([&]() { h.ops.clearAll(); }));
        QCOMPARE(bodyOf(h.doc.message()), before);
        QVERIFY(h.log.text().isEmpty());

        QVERIFY(h.run([&]() { h.ops.endBrowse(); }));
        h.log.clear();
        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        QVERIFY(h.run([&]() { h.ops.clearAll(); }));
        QCOMPARE(h.entries(), QStringList() << "Another operation is still running");
        QCOMPARE(bodyOf(h.doc.message()), before);
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    // ======================================================================================================
    // Purge Q (OnPurgeQ GEN 866, getQueueDepth DA 10246, purgeQueue DA 10065)
    // ======================================================================================================

    void purgeAsksOnceAfterConnectingThenPurgesAndCounts()
    {
        Watchdog dog(60);
        Harness h;
        fillQueue(3);
        ShellState shell = h.doc.shell();
        shell.setIdentity = true;
        h.doc.setShell(shell);
        int connectsWhenAsked = -1;
        h.prompter.onAsk = [&]() { connectsWhenAsked = fake.connxCalls; };
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QCOMPARE(h.lastFlow(), QString("purgeQ"));
        QCOMPARE(h.prompter.asked, QStringList() << "Purge all messages from queue RFHUTIL.TEST.OPS?");		// General.cpp:911
        QCOMPARE(connectsWhenAsked, 1);						// getQueueDepth connected first
        QVERIFY(fake.queue(kQueue).messages.empty());
        QCOMPARE(h.entries(), QStringList() << "3 messages purged from RFHUTIL.TEST.OPS");		// DA 10199
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)0);
        QVERIFY(!h.doc.shell().setIdentity);					// OnPurgeQ clears Set Iden Context
        QVERIFY(h.doc.connected());
        QVERIFY(fake.getLog.back().options & MQGMO_SYNCPOINT_IF_PERSISTENT);	// gmo.Options (DA 10117)
        QVERIFY(fake.getLog.back().options & MQGMO_ACCEPT_TRUNCATED_MSG);
    }

    void purgeAnsweredNoDoesNothingButClearSetIdentity()
    {
        Watchdog dog(60);
        Harness h;
        fillQueue(3);
        h.prompter.answer = false;
        ShellState shell = h.doc.shell();
        shell.setIdentity = true;
        h.doc.setShell(shell);
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QCOMPARE(h.prompter.asked.size(), 1);
        QCOMPARE(fake.getCalls, 0);
        QCOMPARE(fake.queue(kQueue).messages.size(), size_t(3));
        QVERIFY(h.log.text().isEmpty());
        QVERIFY(!h.doc.shell().setIdentity);
        QCOMPARE(h.doc.queueInfo().depth, (MQLONG)3);			// the depth query before the question stays
    }

    void purgeOfAnEmptyQueueSaysQueueEmpty()
    {
        Watchdog dog(60);
        Harness h;
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QCOMPARE(h.entries(), QStringList() << "Queue empty - no messages purged from RFHUTIL.TEST.OPS");	// DA 10208
    }

    void purgeWithoutAQueueNameDoesNotAskAndKeepsSetIdentity()
    {
        Watchdog dog(60);
        Harness h;
        h.doc.setQueueName("");
        ShellState shell = h.doc.shell();
        shell.setIdentity = true;
        h.doc.setShell(shell);
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QCOMPARE(h.entries(), QStringList() << "*Queue Name required");		// OnPurgeQ 880: no stars after, no blank
        QVERIFY(h.prompter.asked.isEmpty());
        QCOMPARE(fake.connxCalls, 0);
        QVERIFY(h.doc.shell().setIdentity);					// the handler returned before the clean-up
    }

    void purgeDoesNotAskWhenTheConnectionCannotBeMade()
    {
        // Windows asks anyway and fails again inside purgeQueue; here the failure is shown once and nothing is asked
        Watchdog dog(60);
        fake.connxRc = MQRC_Q_MGR_NOT_AVAILABLE;
        Harness h;
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QCOMPARE(h.entries(), QStringList() << "2059 Queue manager not available (Connect) - may not be started");
        QVERIFY(h.prompter.asked.isEmpty());
    }

    void purgeDoesNotAskForAMissingQueue()
    {
        Watchdog dog(60);
        fake.openRcByName[kQueue] = MQRC_UNKNOWN_OBJECT_NAME;
        Harness h;
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QCOMPARE(h.entries(), QStringList() << "2085 No such queue");
        QVERIFY(h.prompter.asked.isEmpty());
    }

    void purgeWithoutAPrompterIsNotStarted()
    {
        Watchdog dog(60);
        Harness h;
        h.ops.setPrompter(nullptr);
        fillQueue(2);
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QCOMPARE(fake.getCalls, 0);
        QCOMPARE(fake.queue(kQueue).messages.size(), size_t(2));
        QCOMPARE(h.entries(), QStringList() << "Purge was not started: there is no way to ask for the confirmation");		// NEW
    }

    void purgeUsesTheSelectorOfTheMainTab()
    {
        Watchdog dog(60);
        Harness h;
        MainOptions o;
        o.get.selector = "color = 'red'";
        h.ops.setOptions(o);
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        bool seen = false;
        for (const rfh_test::OpenRecord & open : fake.openLog)
        {
            seen = seen || (open.selector == "color = 'red'");
        }
        QVERIFY(seen);
    }

    void aPurgeThatFailsMidwayShowsTheReasonTheCountAndTheCodesInTheRightOrder()
    {
        // purgeQueue 10218 prints "rc=%d cc=%d" with cc and rc swapped; the owner approved fixing it
        Watchdog dog(60);
        Harness h;
        fillQueue(5);
        fake.failGetsAfter = 2;
        fake.failGetsRc = MQRC_NOT_AUTHORIZED;
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        const QStringList lines = h.newest().split("\r\n");
        QCOMPARE(lines.at(0), QString("2035 Not authorized (Get)"));						// setErrorMsg(cc, rc, "Get") DA 10172
        QCOMPARE(lines.at(1), QString("2 messages purged from RFHUTIL.TEST.OPS before error rc=2035 cc=2"));
        QVERIFY2(lines.size() >= 3 && lines.at(2).contains("after 2 messages"), qPrintable(h.newest()));	// the core's own text, verbatim
        QCOMPARE(fake.queue(kQueue).messages.size(), size_t(3));
        QVERIFY(h.doc.connected());
    }

    void aPurgeBackedOutByTheQueueManagerSaysTheLogMayBeTooSmall()
    {
        Watchdog dog(60);
        Harness h;
        fillQueue(6, true);
        fake.failGetsAfter = 4;
        fake.failGetsRc = MQRC_BACKED_OUT;
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QVERIFY2(h.newest().contains("Commit failed (rc=2003) - the MQ log may be too small"), qPrintable(h.newest()));	// DA 10212
        QVERIFY(h.doc.connected());
    }

    void aPurgeWhoseOutcomeIsUnknownSaysSoVerbatimAndNeverThatMessagesRemain()
    {
        Watchdog dog(60);
        Harness h;
        fillQueue(10, true);
        fake.failGetsAfter = 5;
        fake.failGetsRc = MQRC_Q_MGR_QUIESCING;
        fake.backRc = MQRC_CONNECTION_BROKEN;
        MainOptions o;
        h.ops.setOptions(o);
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        const QString text = h.newest();
        QVERIFY2(text.contains("unknown outcome"), qPrintable(text));
        QVERIFY2(text.contains("the queue manager may or may not have removed them"), qPrintable(text));
        QVERIFY2(!text.contains("remain on the queue"), qPrintable(text));
        QVERIFY(!h.doc.connected());
    }

    void aPurgeBackedOutByAQuiescingQueueManagerSaysWhichMessagesRemain()
    {
        Watchdog dog(60);
        Harness h;
        fillQueue(10, true);
        fake.failGetsAfter = 5;
        fake.failGetsRc = MQRC_Q_MGR_QUIESCING;
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QVERIFY2(h.newest().contains("remain on the queue"), qPrintable(h.newest()));		// the core knows: MQBACK worked
        QVERIFY(!h.doc.connected());
        QVERIFY2(h.newest().contains("2161 Queue manager is quiescing"), qPrintable(h.newest()));
    }

    void aPurgeThatLosesTheConnectionRunsTheCleanup()
    {
        Watchdog dog(60);
        Harness h;
        fillQueue(4);
        fake.failGetsAfter = 1;
        fake.failGetsRc = MQRC_CONNECTION_BROKEN;
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QVERIFY2(h.newest().contains("2009 Connection to QM has been lost - Q closed"), qPrintable(h.newest()));
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.doc.queueOpen());
    }

    void aPurgeCanBeCancelledAndSaysHowFarItGot()
    {
        Watchdog dog(60);
        Harness h;
        fillQueue(50);
        bool cancellable = false;
        fake.onGet = [&](int n)
        {
            if (n == 10)
            {
                cancellable = h.doc.shell().cancellable;		// read on the worker: the shell is only written by the GUI thread
                const_cast<std::atomic<bool> *>(h.controller.cancelFlag())->store(true);
            }
        };
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        fake.onGet = nullptr;
        QVERIFY(cancellable);
        QVERIFY2(h.newest().contains("Purge cancelled after"), qPrintable(h.newest()));
        QVERIFY(h.doc.connected());
        const size_t left = fake.queue(kQueue).messages.size();
        QVERIFY(left > 0 && left < 50);
        QVERIFY(!h.doc.shell().cancellable);
    }

    void aPurgeIsRefusedWhileAGroupPutWaitsForItsCommit()
    {
        // The core's syncpoint purge would commit the whole connection, publishing the unfinished group
        Watchdog dog(60);
        Harness h;
        rfh::MqmdFields f;
        f.groupYes = true;
        h.doc.setMqmd(f);
        QVERIFY(h.run([&]() { h.ops.writeQ(); }));
        QCOMPARE(fake.pendingPuts.size(), size_t(1));
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QVERIFY(h.prompter.asked.isEmpty());
        QCOMPARE(fake.getCalls, 0);
        QCOMPARE(fake.commitCalls, 0);
        QCOMPARE(fake.pendingPuts.size(), size_t(1));
        QCOMPARE(h.entries(), QStringList() << "Purge was not started: a group or segment put is waiting for its commit. Write its last message, or use Close Q to back it out");	// NEW
    }

    void purgeIsRefusedWhileBusyAndWhileBrowsing()
    {
        Watchdog dog(60);
        Harness h;
        fillQueue(2);
        QVERIFY(h.run([&]() { h.ops.startBrowse(); }));
        h.log.clear();
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QVERIFY(h.log.text().isEmpty());
        QVERIFY(h.prompter.asked.isEmpty());
        QVERIFY(h.run([&]() { h.ops.endBrowse(); }));
        h.log.clear();
        std::atomic<bool> release{false};
        QVERIFY(h.controller.submit("block", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QCOMPARE(h.entries(), QStringList() << "Another operation is still running");
        QVERIFY(h.prompter.asked.isEmpty());
        release = true;
        QSignalSpy done(&h.controller, &SessionController::finished);
        QVERIFY(done.count() > 0 || done.wait(5000));
    }

    void purgeRemembersTheQueueAndAnnouncesTheTitle()
    {
        Watchdog dog(60);
        QTemporaryDir dir;
        AppSettings settings(dir.filePath("rfhutil.conf"));
        Harness h;
        h.ops.setSettings(&settings);
        QSignalSpy title(&h.ops, &Operations::titleChanged);
        fillQueue(1);
        QVERIFY(h.run([&]() { h.ops.purgeQ(); }));
        QCOMPARE(settings.recentQueues(kQm), QStringList() << kQueue);
        QVERIFY(title.count() >= 1);
    }
};

QTEST_MAIN(OperationsTests)
#include "OperationsTests.moc"
