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
            return answer;
        }
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
};

QTEST_MAIN(OperationsTests)
#include "OperationsTests.moc"
