// SessionControllerTests.cpp - the worker thread that owns the MqSession, driven by the shared fake MQI.

#include <QtTest>

#include <dirent.h>
#include <stdio.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>

#include "fake_mq.h"
#include "SessionController.h"

using rfhqt::JobResult;
using rfhqt::SessionController;

namespace
{
    const char * kQueue = "RFHUTIL.TEST.QT";

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

    int threadCount()
    {
        int count = 0;
        DIR * dir = opendir("/proc/self/task");
        if (dir == nullptr)
        {
            return -1;
        }
        while (struct dirent * entry = readdir(dir))
        {
            if (entry->d_name[0] != '.')
            {
                count++;
            }
        }
        closedir(dir);
        return count;
    }

    // A QThread that has finished may linger in /proc for a moment: wait until the count stops changing.
    int settledThreadCount()
    {
        int last = threadCount();
        for (int stable = 0; stable < 5;)
        {
            QThread::msleep(20);
            const int now = threadCount();
            stable = (now == last) ? stable + 1 : 0;
            last = now;
        }
        return last;
    }

    rfh_test::FakeMq & fake = rfh_test::fakeMq();
}

class SessionControllerTests : public QObject
{
    Q_OBJECT

private:
    // Waits for the next finished signal; returns its result.
    static bool waitFinished(QSignalSpy & spy, int ms = 5000)
    {
        return spy.count() > 0 || spy.wait(ms);
    }

    static JobResult lastResult(QSignalSpy & spy)
    {
        return qvariant_cast<JobResult>(spy.last().at(1));
    }

    static void connectAndWait(SessionController & c, QSignalSpy & finished)
    {
        finished.clear();
        QVERIFY(c.connectTo(rfh::ConnectOptions()));
        QVERIFY(waitFinished(finished));
        QVERIFY(lastResult(finished).result.ok());
        QVERIFY(lastResult(finished).connected);
        finished.clear();
    }

    static rfh::GetOptions waiting(MQLONG ms)
    {
        rfh::GetOptions o;
        o.waitMs = ms;
        return o;
    }

private slots:
    void init()
    {
        fake.reset();
        fake.queue(kQueue);
    }

    void jobRunsOnAnotherThread()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        std::thread::id seen;
        QThread * seenQt = nullptr;
        QVERIFY(c.submit("probe", [&](rfh::MqSession &)
        {
            seen = std::this_thread::get_id();
            seenQt = QThread::currentThread();
            return JobResult();
        }));
        QVERIFY(waitFinished(finished));
        QVERIFY(seen != std::this_thread::get_id());
        QVERIFY(seenQt != QThread::currentThread());
        QVERIFY(seenQt != nullptr);
    }

    void mqCallsRunOnTheWorkerThreadOnly()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        std::set<std::thread::id> callers;
        std::mutex lock;
        fake.onGet = [&](int)
        {
            std::lock_guard<std::mutex> guard(lock);
            callers.insert(std::this_thread::get_id());
        };
        connectAndWait(c, finished);

        std::thread::id worker;
        QVERIFY(c.submit("who", [&](rfh::MqSession &) { worker = std::this_thread::get_id(); return JobResult(); }));
        QVERIFY(waitFinished(finished));
        finished.clear();

        QVERIFY(c.get(kQueue, rfh::GetOptions()));
        QVERIFY(waitFinished(finished));
        QCOMPARE(callers.size(), size_t(1));
        QVERIFY(*callers.begin() == worker);
        QVERIFY(worker != std::this_thread::get_id());
        QCOMPARE(fake.connxCalls, 1);
    }

    void resultsArriveOnTheUiThread()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QThread * slotThread = nullptr;
        QThread * busyThread = nullptr;
        connect(&c, &SessionController::finished, this, [&](const QString &, const JobResult &)
        {
            slotThread = QThread::currentThread();
        });
        connect(&c, &SessionController::busyChanged, this, [&](bool)
        {
            busyThread = QThread::currentThread();
        });
        QSignalSpy finished(&c, &SessionController::finished);
        QVERIFY(c.submit("noop", [](rfh::MqSession &) { return JobResult(); }));
        QVERIFY(waitFinished(finished));
        QCOMPARE(slotThread, QThread::currentThread());
        QCOMPARE(busyThread, QThread::currentThread());
        QCOMPARE(QThread::currentThread(), qApp->thread());
    }

    void submitWhileBusyIsRejected()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        std::atomic<bool> release{false};
        std::atomic<bool> secondRan{false};
        QVERIFY(c.submit("first", [&](rfh::MqSession &)
        {
            while (!release.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return JobResult();
        }));
        QVERIFY(c.busy());
        QVERIFY(!c.submit("second", [&](rfh::MqSession &) { secondRan = true; return JobResult(); }));
        QVERIFY(!c.get(kQueue, rfh::GetOptions()));
        release = true;
        QVERIFY(waitFinished(finished));
        QVERIFY(!c.busy());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), QString("first"));
        QTest::qWait(50);
        QVERIFY(!secondRan.load());
        QCOMPARE(finished.count(), 1);
    }

    void busyChangedEmitsTrueThenFalseOncePerJob()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy busy(&c, &SessionController::busyChanged);
        QSignalSpy finished(&c, &SessionController::finished);
        for (int i = 1; i <= 3; i++)
        {
            QVERIFY(c.submit("job", [](rfh::MqSession &) { return JobResult(); }));
            QVERIFY(waitFinished(finished));
            QCOMPARE(busy.count(), 2 * i);
            QCOMPARE(busy.at(2 * i - 2).at(0).toBool(), true);
            QCOMPARE(busy.at(2 * i - 1).at(0).toBool(), false);
            QVERIFY(!c.busy());
            finished.clear();
        }
    }

    void busyIsFalseWhenFinishedIsDeliveredSoASlotCanSubmitTheNextJob()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QStringList order;
        bool nextAccepted = false;
        connect(&c, &SessionController::finished, this, [&](const QString & name, const JobResult &)
        {
            order << name;
            if (name == "one")
            {
                nextAccepted = c.submit("two", [](rfh::MqSession &) { return JobResult(); });
            }
        });
        QVERIFY(c.submit("one", [](rfh::MqSession &) { return JobResult(); }));
        QTRY_COMPARE_WITH_TIMEOUT(order.size(), 2, 5000);
        QVERIFY(nextAccepted);
        QCOMPARE(order, (QStringList{"one", "two"}));
    }

    void connectAndDisconnectResultsAreDelivered()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        QVERIFY(c.connectTo(rfh::ConnectOptions()));
        QVERIFY(waitFinished(finished));
        QCOMPARE(finished.at(0).at(0).toString(), QString("connect"));
        JobResult r = lastResult(finished);
        QVERIFY(r.result.ok());
        QVERIFY(r.connected);
        QCOMPARE(r.queueManager.platform, fake.platform);
        QCOMPARE(r.queueManager.commandLevel, fake.commandLevel);
        finished.clear();

        QVERIFY(c.disconnectFromQm());
        QVERIFY(waitFinished(finished));
        QCOMPARE(finished.at(0).at(0).toString(), QString("disconnect"));
        QVERIFY(lastResult(finished).result.ok());
        QVERIFY(!lastResult(finished).connected);
        QCOMPARE(fake.discCalls, 1);
    }

    void aFailedConnectIsDelivered()
    {
        Watchdog dog(60);
        fake.connxRc = MQRC_Q_MGR_NOT_AVAILABLE;
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        QVERIFY(c.connectTo(rfh::ConnectOptions()));
        QVERIFY(waitFinished(finished));
        JobResult r = lastResult(finished);
        QVERIFY(!r.result.ok());
        QCOMPARE(r.result.rc, MQRC_Q_MGR_NOT_AVAILABLE);
        QVERIFY(!r.connected);
    }

    void thePasswordReachesMqButIsNeverReportedAndIsWiped()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        rfh::ConnectOptions options;
        options.userId = "alice";
        options.password = "s3cr3t-pw-value-that-is-long";
        QVERIFY(c.connectTo(options));
        QVERIFY(waitFinished(finished));
        JobResult r = lastResult(finished);
        QVERIFY(r.result.ok());
        QCOMPARE(fake.connxLog.size(), size_t(1));
        QCOMPARE(std::string(fake.connxLog[0].password.begin(), fake.connxLog[0].password.end()), options.password);
        QVERIFY(r.result.text.find("s3cr3t") == std::string::npos);
        for (const std::string & w : r.result.warnings)
        {
            QVERIFY(w.find("s3cr3t") == std::string::npos);
        }
        options.wipePassword();
    }

    void connectAndWipeClearsTheCallersPasswordOnSuccessAndFailure()
    {
        for (int pass = 0; pass < 2; pass++)
        {
            fake.reset();
            if (pass == 1)
            {
                fake.connxRc = MQRC_NOT_AUTHORIZED;
            }
            rfh::MqSession session(rfh_test::fakeApi());
            rfh::ConnectOptions options;
            options.userId = "bob";
            options.password = "hunter2-hunter2-hunter2";
            rfh::Result r = rfhqt::connectAndWipe(session, options);
            QCOMPARE(r.ok(), pass == 0);
            QVERIFY(options.password.empty());
        }
    }

    void cancelStopsAWaitingGet()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        connectAndWait(c, finished);

        QVERIFY(c.get(kQueue, waiting(60000)));
        QTest::qWait(200);
        QVERIFY(c.busy());
        QElapsedTimer timer;
        timer.start();
        c.cancel();
        QVERIFY(waitFinished(finished, 5000));
        QVERIFY2(timer.elapsed() < 2500, qPrintable(QString::number(timer.elapsed())));
        JobResult r = lastResult(finished);
        QVERIFY(!r.result.ok());
        QVERIFY(r.result.text.find("cancelled") != std::string::npos);
        QVERIFY(!c.busy());
        printf("cancel returned after %lld ms\n", (long long)timer.elapsed());
    }

    void cancelWhileIdleIsANoOp()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        connectAndWait(c, finished);
        c.cancel();
        c.cancel();
        QVERIFY(!c.cancelFlag()->load());
        QVERIFY(!c.busy());

        rfh_test::FakeMessage m;
        m.data = {'o', 'k'};
        fake.queue(kQueue).messages.push_back(m);
        QVERIFY(c.get(kQueue, waiting(5000)));
        QVERIFY(waitFinished(finished));
        QVERIFY(lastResult(finished).result.ok());
        QCOMPARE(lastResult(finished).message.body.size(), size_t(2));
    }

    void theCancelFlagIsClearedWhenTheNextJobStarts()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        connectAndWait(c, finished);

        QVERIFY(c.get(kQueue, waiting(60000)));
        QTest::qWait(100);
        c.cancel();
        QVERIFY(waitFinished(finished));
        QVERIFY(!lastResult(finished).result.ok());
        QVERIFY(c.cancelFlag()->load());		// still set until a job starts
        finished.clear();

        bool flagInJob = true;
        QVERIFY(c.submit("flag", [&](rfh::MqSession &)
        {
            flagInJob = c.cancelFlag()->load();
            return JobResult();
        }));
        QVERIFY(waitFinished(finished));
        QVERIFY(!flagInJob);
        finished.clear();

        // and a real get after the cancelled one waits normally and gets its message
        rfh_test::FakeMessage m;
        m.data = {'x'};
        fake.queue(kQueue).messages.push_back(m);
        QVERIFY(c.get(kQueue, waiting(5000)));
        QVERIFY(waitFinished(finished));
        QVERIFY(lastResult(finished).result.ok());
    }

    void destroyingWhileAWaitIsInProgressCancelsAndJoins()
    {
        Watchdog dog(60);
        const int before = settledThreadCount();
        std::unique_ptr<SessionController> c(new SessionController(rfh_test::fakeApi()));
        QSignalSpy finished(c.get(), &SessionController::finished);
        connectAndWait(*c, finished);
        QVERIFY(c->get(kQueue, waiting(MQWI_UNLIMITED)));
        QTest::qWait(200);
        QVERIFY(c->busy());
        QTRY_VERIFY(threadCount() > before);

        std::promise<void> destroyed;
        std::future<void> wait = destroyed.get_future();
        SessionController * raw = c.release();
        QElapsedTimer timer;
        timer.start();
        std::thread t([raw, &destroyed]() { delete raw; destroyed.set_value(); });
        const bool ok = wait.wait_for(std::chrono::seconds(10)) == std::future_status::ready;
        if (!ok)
        {
            printf("the destructor did not return within 10 s\n");
            fflush(stdout);
            std::_Exit(4);
        }
        t.join();
        QVERIFY2(timer.elapsed() < 3000, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(fake.discCalls, 1);
        QVERIFY(fake.liveHandles.empty());
        QTRY_COMPARE(threadCount(), before);
    }

    void destroyingAnIdleControllerLeavesNoThread()
    {
        Watchdog dog(60);
        const int before = settledThreadCount();
        {
            SessionController c(rfh_test::fakeApi());
            QTRY_VERIFY(threadCount() > before);
        }
        QTRY_COMPARE(threadCount(), before);
        QCOMPARE(fake.discCalls, 0);
    }

    void theSessionPersistsBetweenJobs()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        connectAndWait(c, finished);
        for (int i = 0; i < 3; i++)
        {
            bool connected = false;
            QVERIFY(c.submit("check", [&](rfh::MqSession & s) { connected = s.isConnected(); return JobResult(); }));
            QVERIFY(waitFinished(finished));
            finished.clear();
            QVERIFY(connected);
        }
        QCOMPARE(fake.connxCalls, 1);
    }

    void aJobThatThrowsBecomesAFailedResult()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        QVERIFY(c.submit("boom", [](rfh::MqSession &) -> JobResult { throw std::runtime_error("kaboom"); }));
        QVERIFY(waitFinished(finished));
        JobResult r = lastResult(finished);
        QVERIFY(!r.result.ok());
        QVERIFY(r.result.text.find("kaboom") != std::string::npos);
        QVERIFY(!c.busy());
        finished.clear();

        QVERIFY(c.submit("boom2", [](rfh::MqSession &) -> JobResult { throw 42; }));
        QVERIFY(waitFinished(finished));
        QVERIFY(!lastResult(finished).result.ok());
        finished.clear();

        QVERIFY(c.submit("after", [](rfh::MqSession &) { return JobResult(); }));
        QVERIFY(waitFinished(finished));
        QVERIFY(lastResult(finished).result.ok());
    }

    void aFailingMqCallIsDeliveredAsAResult()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        connectAndWait(c, finished);
        QVERIFY(c.get(kQueue, rfh::GetOptions()));
        QVERIFY(waitFinished(finished));
        JobResult r = lastResult(finished);
        QVERIFY(!r.result.ok());
        QCOMPARE(r.result.rc, MQRC_NO_MSG_AVAILABLE);
        QCOMPARE(r.result.cc, MQCC_FAILED);
    }

    void putGetBrowseRoundTripWithValueTypes()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        connectAndWait(c, finished);

        for (const char * text : {"one", "two", "three"})
        {
            rfh::Message m;
            m.body.assign(text, text + strlen(text));
            QVERIFY(c.put(kQueue, m, rfh::PutOptions()));
            QVERIFY(waitFinished(finished));
            QCOMPARE(finished.last().at(0).toString(), QString("put"));
            QVERIFY(lastResult(finished).result.ok());
            finished.clear();
        }
        QCOMPARE(fake.putCalls, 3);

        auto bodyOf = [](const JobResult & r) { return std::string(r.message.body.begin(), r.message.body.end()); };
        rfh::GetOptions browse;
        browse.mode = rfh::GetMode::BrowseFirst;
        QVERIFY(c.get(kQueue, browse));
        QVERIFY(waitFinished(finished));
        QCOMPARE(finished.last().at(0).toString(), QString("get"));
        QVERIFY(lastResult(finished).result.ok());
        QCOMPARE(bodyOf(lastResult(finished)), std::string("one"));
        QCOMPARE(lastResult(finished).browsePosition, 1L);
        finished.clear();

        QVERIFY(c.get(kQueue, rfh::GetOptions()));
        QVERIFY(waitFinished(finished));
        QVERIFY(lastResult(finished).result.ok());
        QCOMPARE(bodyOf(lastResult(finished)), std::string("one"));
        finished.clear();

        QVERIFY(c.inquireQueue(kQueue));
        QVERIFY(waitFinished(finished));
        QCOMPARE(finished.last().at(0).toString(), QString("queueInfo"));
        JobResult info = lastResult(finished);
        QVERIFY(info.result.ok());
        QVERIFY(info.hasQueueInfo);
        QCOMPARE(info.queue.depth, 2);
        QCOMPARE(info.queue.type, MQQT_LOCAL);
        finished.clear();

        QVERIFY(c.purge(kQueue, rfh::PurgeOptions()));
        QVERIFY(waitFinished(finished));
        QCOMPARE(finished.last().at(0).toString(), QString("purge"));
        QVERIFY(lastResult(finished).result.ok());
        QCOMPARE(lastResult(finished).count, 2L);
        finished.clear();

        QVERIFY(c.commit());
        QVERIFY(waitFinished(finished));
        QCOMPARE(finished.last().at(0).toString(), QString("commit"));
        QVERIFY(lastResult(finished).result.ok());
        QVERIFY(fake.commitCalls >= 1);
    }

    void aFailedCommitIsDelivered()
    {
        Watchdog dog(60);
        fake.cmitRc = MQRC_BACKED_OUT;
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        connectAndWait(c, finished);
        QVERIFY(c.commit());
        QVERIFY(waitFinished(finished));
        QVERIFY(!lastResult(finished).result.ok());
        QCOMPARE(lastResult(finished).result.rc, MQRC_BACKED_OUT);
    }

    void listQueuesDeliversTheFailureWhenTheCommandServerIsSilent()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        connectAndWait(c, finished);
        rfh::QueueListOptions options;
        options.replyWaitMs = 100;
        QVERIFY(c.listQueues(options));
        QVERIFY(waitFinished(finished));
        QCOMPARE(finished.last().at(0).toString(), QString("listQueues"));
        JobResult r = lastResult(finished);
        QVERIFY(!r.result.ok());
        QVERIFY(r.names.empty());
    }

    void aPurgeCanBeCancelled()
    {
        Watchdog dog(60);
        SessionController c(rfh_test::fakeApi());
        QSignalSpy finished(&c, &SessionController::finished);
        connectAndWait(c, finished);
        for (int i = 0; i < 50; i++)
        {
            rfh_test::FakeMessage m;
            m.data = {'p'};
            fake.queue(kQueue).messages.push_back(m);
        }
        fake.onGet = [&](int n)
        {
            if (n == 5)
            {
                const_cast<std::atomic<bool> *>(c.cancelFlag())->store(true);
            }
        };
        QVERIFY(c.purge(kQueue, rfh::PurgeOptions()));
        QVERIFY(waitFinished(finished));
        JobResult r = lastResult(finished);
        QVERIFY(!r.result.ok());
        QVERIFY(r.count >= 4 && r.count < 50);
    }
};

QTEST_MAIN(SessionControllerTests)
#include "SessionControllerTests.moc"
