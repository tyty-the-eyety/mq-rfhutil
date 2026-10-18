// MqSessionFakeTests.cpp - MqSession paths that need MQ to misbehave, driven by a
// fake MQI so they run without a queue manager.

#include <gtest/gtest.h>

#include <atomic>

#include "rfh_mq_session.h"

using namespace rfh;

#include "fake_mq.h"

namespace
{
    using rfh_test::fakeApi;

    rfh_test::FakeMq & fake = rfh_test::fakeMq();

    class MqSessionFakeTest : public ::testing::Test
    {
    protected:
        void SetUp() override { fake.reset(); }

        MqSessionFakeTest() : session(fakeApi()) {}

        void connect()
        {
            ConnectOptions options;
            options.mode = Mode::Bindings;
            Result r = session.connect(options);
            ASSERT_TRUE(r.ok()) << r.text;
        }

        MqSession session;
    };
}

TEST_F(MqSessionFakeTest, DropIfBrokenOnConnectionBroken)
{
    connect();
    fake.openRc = MQRC_CONNECTION_BROKEN;

    Message m;
    Result r = session.get("Q", m, GetOptions());
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ(1, fake.discCalls);

    fake.openRc = MQRC_NONE;
    connect();
    EXPECT_TRUE(session.isConnected());
    EXPECT_EQ(2, fake.connxCalls);
}

TEST_F(MqSessionFakeTest, DisconnectAfterCloseFailureClearsHandle)
{
    connect();
    Message m;
    ASSERT_TRUE(session.get("Q", m, GetOptions()).ok());
    EXPECT_EQ(1, fake.openCalls);

    fake.closeRc = MQRC_OPTION_NOT_VALID_FOR_TYPE;
    Result d = session.disconnect();
    EXPECT_TRUE(d.ok()) << d.text;
    EXPECT_EQ(1u, d.warnings.size());
    EXPECT_FALSE(session.isConnected());

    fake.closeRc = MQRC_NONE;
    connect();
    ASSERT_TRUE(session.get("Q", m, GetOptions()).ok());
    EXPECT_EQ(2, fake.openCalls) << "the old queue handle must not be reused";
}

TEST_F(MqSessionFakeTest, DisconnectCallInProgressKeepsConnection)
{
    connect();
    fake.discRc = MQRC_CALL_IN_PROGRESS;

    Result r = session.disconnect();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_CALL_IN_PROGRESS, r.rc);
    EXPECT_TRUE(session.isConnected());

    fake.discRc = MQRC_NONE;
    EXPECT_TRUE(session.disconnect().ok());
    EXPECT_FALSE(session.isConnected());
}

TEST_F(MqSessionFakeTest, QueueManagerQuiescingOnPutDropsSession)
{
    connect();
    fake.putRc = MQRC_Q_MGR_QUIESCING;

    Message m;
    Result r = session.put("Q", m, PutOptions());
    EXPECT_EQ(MQRC_Q_MGR_QUIESCING, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ(1, fake.discCalls);
}

TEST(MqSessionFakeApi, IncompleteInjectedApiIsRejected)
{
    fake.reset();
    MqApi api = fakeApi();
    api.inq = nullptr;
    MqSession session(api);

    ConnectOptions options;
    options.mode = Mode::Bindings;
    Result r = session.connect(options);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("Injected MQ API is incomplete", r.text);
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ(0, fake.connxCalls);
}

namespace
{
    // A queue handle for the fake, opened through the fake's own open entry point.
    MQHOBJ openFake(rfh::MqApi & api, const char * name, MQLONG objectType = MQOT_Q)
    {
        MQOD od = {MQOD_DEFAULT};
        od.ObjectType = objectType;
        strncpy(od.ObjectName, name, sizeof(od.ObjectName));
        MQHOBJ h = MQHO_UNUSABLE_HOBJ;
        MQLONG cc = MQCC_OK;
        MQLONG rc = MQRC_NONE;
        api.open(1, &od, MQOO_INPUT_AS_Q_DEF | MQOO_INQUIRE, &h, &cc, &rc);
        return h;
    }
}

TEST(FakeMq, AnEmptyQueueReportsNoMessageAvailable)
{
    fake.reset();
    fake.queue("RFHUTIL.TEST.EMPTY");

    MqSession session(fakeApi());
    ConnectOptions options;
    options.mode = Mode::Bindings;
    ASSERT_TRUE(session.connect(options).ok());

    Message m;
    Result r = session.get("RFHUTIL.TEST.EMPTY", m, GetOptions());
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_EQ(MQCC_FAILED, r.cc);
}

TEST(FakeMq, InquiryReturnsConfiguredAttributes)
{
    fake.reset();
    rfh_test::FakeQueue & q = fake.queue("RFHUTIL.TEST.ALIAS");
    q.type = MQQT_ALIAS;
    q.maxMsgLength = 1234;
    q.baseQName = "RFHUTIL.TEST.BASE";
    q.hasDepthOverride = true;
    q.depthOverride = 42;

    rfh::MqApi api = fakeApi();
    MQHOBJ h = openFake(api, "RFHUTIL.TEST.ALIAS");
    MQLONG selectors[4] = {MQIA_Q_TYPE, MQIA_CURRENT_Q_DEPTH, MQIA_MAX_MSG_LENGTH, MQCA_BASE_Q_NAME};
    MQLONG ints[3] = {0, 0, 0};
    char chars[MQ_Q_NAME_LENGTH + 1];
    memset(chars, 0, sizeof(chars));
    MQLONG cc = MQCC_FAILED;
    MQLONG rc = MQRC_UNEXPECTED_ERROR;
    api.inq(1, h, 4, selectors, 3, ints, MQ_Q_NAME_LENGTH, chars, &cc, &rc);

    EXPECT_EQ(MQCC_OK, cc);
    EXPECT_EQ(MQQT_ALIAS, ints[0]);
    EXPECT_EQ(42, ints[1]);
    EXPECT_EQ(1234, ints[2]);
    EXPECT_EQ("RFHUTIL.TEST.BASE", std::string(chars, 17));
}

TEST(FakeMq, QueueManagerInquiryReturnsPlatformAndLevel)
{
    fake.reset();
    fake.platform = MQPL_WINDOWS_NT;
    fake.commandLevel = 912;

    rfh::MqApi api = fakeApi();
    MQHOBJ h = openFake(api, "QM1", MQOT_Q_MGR);
    MQLONG selectors[2] = {MQIA_PLATFORM, MQIA_COMMAND_LEVEL};
    MQLONG ints[2] = {0, 0};
    MQLONG cc = MQCC_FAILED;
    MQLONG rc = MQRC_UNEXPECTED_ERROR;
    api.inq(1, h, 2, selectors, 2, ints, 0, nullptr, &cc, &rc);

    EXPECT_EQ(MQCC_OK, cc);
    EXPECT_EQ(MQPL_WINDOWS_NT, ints[0]);
    EXPECT_EQ(912, ints[1]);
}

TEST(FakeMq, AGetThatWaitsHonoursTheWaitInterval)
{
    fake.reset();
    fake.queue("RFHUTIL.TEST.EMPTY");

    rfh::MqApi api = fakeApi();
    MQHOBJ h = openFake(api, "RFHUTIL.TEST.EMPTY");
    MQMD md = {MQMD_DEFAULT};
    MQGMO gmo = {MQGMO_DEFAULT};
    gmo.Options = MQGMO_WAIT;
    gmo.WaitInterval = 120;
    char buffer[16];
    MQLONG length = 0;
    MQLONG cc = MQCC_OK;
    MQLONG rc = MQRC_NONE;

    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    api.get(1, h, &md, &gmo, sizeof(buffer), buffer, &length, &cc, &rc);
    long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, rc);
    EXPECT_GE(elapsed, 120);
    EXPECT_EQ(120, fake.lastWaitedMs);

    gmo.Options = MQGMO_NO_WAIT;
    api.get(1, h, &md, &gmo, sizeof(buffer), buffer, &length, &cc, &rc);
    EXPECT_EQ(0, fake.lastWaitedMs) << "no wait option, no wait";
}

TEST(FakeMq, PutAndGetRecordTheirOptions)
{
    fake.reset();
    fake.queue("RFHUTIL.TEST.Q");

    MqSession session(fakeApi());
    ConnectOptions options;
    options.mode = Mode::Bindings;
    ASSERT_TRUE(session.connect(options).ok());

    Message m;
    m.body.assign({'a', 'b'});
    ASSERT_TRUE(session.put("RFHUTIL.TEST.Q", m, PutOptions()).ok());
    EXPECT_EQ(1, fake.putCalls);
    EXPECT_NE(0, fake.lastPmoOptions);

    GetOptions get;
    get.mode = GetMode::BrowseFirst;
    Message back;
    ASSERT_TRUE(session.get("RFHUTIL.TEST.Q", back, get).ok());
    EXPECT_TRUE((fake.lastGmoOptions & (MQGMO_BROWSE_FIRST | MQGMO_BROWSE_NEXT)) != 0);
    EXPECT_EQ(2u, back.body.size());
}

TEST(FakeMq, CommitIsCounted)
{
    fake.reset();
    MQLONG cc = MQCC_FAILED;
    MQLONG rc = MQRC_UNEXPECTED_ERROR;
    rfh_test::fakeCmit(1, &cc, &rc);
    rfh_test::fakeCmit(1, &cc, &rc);
    EXPECT_EQ(MQCC_OK, cc);
    EXPECT_EQ(2, fake.commitCalls);
}

// ---- the fake's unit of work (see the top of fake_mq.h) ----

TEST(FakeMq, ASyncpointPutIsInvisibleUntilTheCommit)
{
    fake.reset();
    fake.queue("RFHUTIL.TEST.Q");
    MqSession session(fakeApi());
    ConnectOptions options;
    options.mode = Mode::Bindings;
    ASSERT_TRUE(session.connect(options).ok());

    PutOptions put;
    put.syncpoint = true;
    Message m;
    m.body.assign({'a'});
    ASSERT_TRUE(session.put("RFHUTIL.TEST.Q", m, put).ok());
    EXPECT_EQ(0u, fake.queue("RFHUTIL.TEST.Q").messages.size()) << "not visible before the commit";
    MQLONG depth = -1;
    ASSERT_TRUE(session.depth("RFHUTIL.TEST.Q", depth).ok());
    EXPECT_EQ(1, depth) << "CURDEPTH counts uncommitted puts";

    ASSERT_TRUE(session.commit().ok());
    EXPECT_EQ(1u, fake.queue("RFHUTIL.TEST.Q").messages.size());
}

TEST(FakeMq, ADisconnectCommitsWhatIsPending)
{
    fake.reset();
    fake.queue("RFHUTIL.TEST.Q");
    MqSession session(fakeApi());
    ConnectOptions options;
    options.mode = Mode::Bindings;
    ASSERT_TRUE(session.connect(options).ok());

    PutOptions put;
    put.syncpoint = true;
    Message m;
    m.body.assign({'a'});
    ASSERT_TRUE(session.put("RFHUTIL.TEST.Q", m, put).ok());
    ASSERT_EQ(0u, fake.queue("RFHUTIL.TEST.Q").messages.size());
    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_EQ(1u, fake.queue("RFHUTIL.TEST.Q").messages.size()) << "MQDISC commits";
}

TEST(FakeMq, MessagesGotUnderSyncpointReturnWhenTheCommitBacksOut)
{
    fake.reset();
    for (int i = 0; i < 3; i++)
    {
        rfh_test::FakeMessage m;
        m.md.Persistence = MQPER_PERSISTENT;
        fake.queue("RFHUTIL.TEST.Q").messages.push_back(m);
    }
    fake.cmitRc = MQRC_BACKED_OUT;
    MqSession session(fakeApi());
    ConnectOptions options;
    options.mode = Mode::Bindings;
    ASSERT_TRUE(session.connect(options).ok());

    PurgeOptions purge;
    purge.syncpoint = true;
    MQLONG count = 0;
    EXPECT_FALSE(session.purge("RFHUTIL.TEST.Q", count, purge).ok());
    EXPECT_EQ(3u, fake.queue("RFHUTIL.TEST.Q").messages.size()) << "the backed out gets are on the queue again";
}

// ---- MqSession::queueInfo (Windows: getQueueType 18244, getCurrentDepth 18200, openQ 12268-12310) ----

namespace
{
    const char * kInfoQueue = "RFHUTIL.TEST.INFO";

    void addMessages(const char * queue, int count)
    {
        for (int i = 0; i < count; i++)
        {
            rfh_test::FakeMessage m;
            m.data.assign({'a', 'b', 'c'});
            fake.queue(queue).messages.push_back(m);
        }
    }

    void expectNoHandleLeaked()
    {
        EXPECT_EQ(fake.openCalls, fake.closeCalls) << "an opened handle was not closed";
    }
}

TEST_F(MqSessionFakeTest, QueueInfoReturnsTypeDepthAndMaximum)
{
    connect();
    fake.queue(kInfoQueue).maxMsgLength = 1234;
    addMessages(kInfoQueue, 3);

    QueueInfo info;
    Result r = session.queueInfo(kInfoQueue, info);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());
    EXPECT_EQ(MQQT_LOCAL, info.type);
    EXPECT_EQ(3, info.depth);
    EXPECT_EQ(1234, info.maxMsgLength);
    EXPECT_EQ("", info.resolvedName);   // a local queue resolves to itself
    EXPECT_EQ(1, fake.openCalls);
    EXPECT_NE(0, fake.lastOpenOptions & MQOO_INQUIRE);
    expectNoHandleLeaked();
}

TEST_F(MqSessionFakeTest, QueueInfoReportsAResolvedAliasName)
{
    connect();
    rfh_test::FakeQueue & alias = fake.queue("RFHUTIL.TEST.ALIAS");
    alias.type = MQQT_ALIAS;
    alias.resolvedQName = "RFHUTIL.TEST.BASE";

    QueueInfo info;
    Result r = session.queueInfo("RFHUTIL.TEST.ALIAS", info);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(MQQT_ALIAS, info.type);   // Windows records the alias type; its resolution loop is disabled (while (... && 0))
    EXPECT_EQ("RFHUTIL.TEST.BASE", info.resolvedName);
    EXPECT_EQ(1, fake.openCalls) << "the base queue is not opened";
    expectNoHandleLeaked();
}

TEST_F(MqSessionFakeTest, QueueInfoResolvedNameIsEmptyWhenItEqualsTheQueueName)
{
    connect();
    fake.queue(kInfoQueue).resolvedQName = kInfoQueue;

    QueueInfo info;
    ASSERT_TRUE(session.queueInfo(kInfoQueue, info).ok());
    EXPECT_EQ("", info.resolvedName);
}

TEST_F(MqSessionFakeTest, QueueInfoHasUnknownDefaultsBeforeAnyInquiry)
{
    QueueInfo info;
    EXPECT_EQ(0, info.type);
    EXPECT_EQ(-1, info.depth);
    EXPECT_EQ(-1, info.maxMsgLength);
    EXPECT_EQ("", info.resolvedName);
}

TEST_F(MqSessionFakeTest, QueueInfoOfAMissingQueueFails)
{
    connect();
    fake.openRc = MQRC_UNKNOWN_OBJECT_NAME;

    QueueInfo info;
    info.type = MQQT_ALIAS;   // stale facts from an earlier call must not survive
    info.depth = 5;
    info.maxMsgLength = 99;
    info.resolvedName = "OLD";
    Result r = session.queueInfo("RFHUTIL.TEST.NO.SUCH", info);

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_UNKNOWN_OBJECT_NAME, r.rc);
    EXPECT_NE(std::string::npos, r.text.find("MQOPEN"));
    EXPECT_EQ(0, info.type);
    EXPECT_EQ(-1, info.depth);
    EXPECT_EQ(-1, info.maxMsgLength);
    EXPECT_EQ("", info.resolvedName);
    EXPECT_EQ(0, fake.inqCalls);
    EXPECT_EQ(0, fake.closeCalls) << "there is no handle to close after a failed open";
}

TEST_F(MqSessionFakeTest, QueueInfoOfARemoteQueueDefinitionReportsTheOpenFailureWithoutInquiring)
{
    // Windows retries such an open without MQOO_INQUIRE (openQ 12164-12176) and then
    // records type 0 and depth 0 silently; queueInfo reports the reason instead.
    connect();
    fake.openRc = MQRC_OPTION_NOT_VALID_FOR_TYPE;

    QueueInfo info;
    Result r = session.queueInfo("RFHUTIL.TEST.REMOTE", info);

    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(MQRC_OPTION_NOT_VALID_FOR_TYPE, r.rc);
    EXPECT_EQ(0, info.type);
    EXPECT_EQ(-1, info.depth);
    EXPECT_EQ(0, fake.inqCalls);
    EXPECT_EQ(1, fake.openCalls) << "no second open without MQOO_INQUIRE: it would only prove the name exists";
}

TEST_F(MqSessionFakeTest, QueueInfoKeepsTheTypeOfARemoteQueueWhenMqAnswersIt)
{
    connect();
    fake.queue(kInfoQueue).type = MQQT_REMOTE;

    QueueInfo info;
    ASSERT_TRUE(session.queueInfo(kInfoQueue, info).ok());
    EXPECT_EQ(MQQT_REMOTE, info.type);
}

TEST_F(MqSessionFakeTest, QueueInfoWithAnUnknownQueueTypeKeepsTheValueAndWarns)
{
    // Windows leaves the type text empty for a value outside local/model/alias/remote/cluster (setQueueType 10354).
    connect();
    fake.queue(kInfoQueue).type = 99;
    addMessages(kInfoQueue, 1);

    QueueInfo info;
    Result r = session.queueInfo(kInfoQueue, info);

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(99, info.type);
    EXPECT_EQ(1, info.depth);
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_NE(std::string::npos, r.warnings[0].find("99"));
    expectNoHandleLeaked();
}

TEST_F(MqSessionFakeTest, QueueInfoTypeInquiryFailureKeepsTheLaterFacts)
{
    connect();
    fake.queue(kInfoQueue).maxMsgLength = 500;
    addMessages(kInfoQueue, 2);
    fake.inqRcBySelector[MQIA_Q_TYPE] = MQRC_SELECTOR_NOT_FOR_TYPE;

    QueueInfo info;
    Result r = session.queueInfo(kInfoQueue, info);

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_SELECTOR_NOT_FOR_TYPE, r.rc);
    EXPECT_NE(std::string::npos, r.text.find("MQINQ"));
    EXPECT_EQ(0, info.type);            // Windows: queueType = 0 (18269)
    EXPECT_EQ(2, info.depth);
    EXPECT_EQ(500, info.maxMsgLength);
    expectNoHandleLeaked();
}

TEST_F(MqSessionFakeTest, QueueInfoDepthInquiryFailureKeepsTheOtherFacts)
{
    connect();
    fake.queue(kInfoQueue).type = MQQT_MODEL;
    fake.queue(kInfoQueue).maxMsgLength = 500;
    fake.inqRcBySelector[MQIA_CURRENT_Q_DEPTH] = MQRC_NOT_AUTHORIZED;

    QueueInfo info;
    Result r = session.queueInfo(kInfoQueue, info);

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_NOT_AUTHORIZED, r.rc);
    EXPECT_EQ(MQQT_MODEL, info.type);
    EXPECT_EQ(-1, info.depth);          // Windows stores 0; the core says unknown
    EXPECT_EQ(500, info.maxMsgLength);
    expectNoHandleLeaked();
}

TEST_F(MqSessionFakeTest, QueueInfoMaximumLengthInquiryFailureKeepsTheOtherFacts)
{
    // Windows tests cc, not cc2, after this inquiry (12276) and so uses whatever
    // the failed call left in its buffer; queueInfo reports the failure.
    connect();
    addMessages(kInfoQueue, 4);
    fake.inqRcBySelector[MQIA_MAX_MSG_LENGTH] = MQRC_HOBJ_ERROR;

    QueueInfo info;
    Result r = session.queueInfo(kInfoQueue, info);

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_HOBJ_ERROR, r.rc);
    EXPECT_EQ(MQQT_LOCAL, info.type);
    EXPECT_EQ(4, info.depth);
    EXPECT_EQ(-1, info.maxMsgLength);
    expectNoHandleLeaked();
}

TEST_F(MqSessionFakeTest, QueueInfoReportsTheFirstOfSeveralInquiryFailuresAndNotesTheRest)
{
    connect();
    fake.inqRcBySelector[MQIA_Q_TYPE] = MQRC_NOT_AUTHORIZED;
    fake.inqRcBySelector[MQIA_MAX_MSG_LENGTH] = MQRC_HOBJ_ERROR;

    QueueInfo info;
    Result r = session.queueInfo(kInfoQueue, info);

    EXPECT_EQ(MQRC_NOT_AUTHORIZED, r.rc);
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_NE(std::string::npos, r.warnings[0].find("2019"));
    EXPECT_EQ(7, info.depth) << "the fake answers 7 for the depth of a queue it does not know";
    expectNoHandleLeaked();
}

TEST_F(MqSessionFakeTest, QueueInfoStopsWhenTheConnectionIsLostDuringAnInquiry)
{
    connect();
    addMessages(kInfoQueue, 2);
    fake.inqRcBySelector[MQIA_CURRENT_Q_DEPTH] = MQRC_CONNECTION_BROKEN;

    QueueInfo info;
    Result r = session.queueInfo(kInfoQueue, info);

    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ(MQQT_LOCAL, info.type);   // learned before the connection broke
    EXPECT_EQ(-1, info.depth);
    EXPECT_EQ(-1, info.maxMsgLength);
    EXPECT_EQ(2, fake.inqCalls) << "no inquiry after the connection is gone";
    EXPECT_EQ(1, fake.discCalls);
    EXPECT_EQ(0, fake.closeCalls) << "MQDISC released the handle";
}

TEST_F(MqSessionFakeTest, QueueInfoCloseFailureIsAWarningAndKeepsTheFacts)
{
    connect();
    addMessages(kInfoQueue, 1);
    fake.closeRc = MQRC_HOBJ_ERROR;

    QueueInfo info;
    Result r = session.queueInfo(kInfoQueue, info);

    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_NE(std::string::npos, r.warnings[0].find("MQCLOSE"));
    EXPECT_EQ(1, info.depth);
}

TEST_F(MqSessionFakeTest, QueueInfoRequiresAConnection)
{
    QueueInfo info;
    info.depth = 3;
    Result r = session.queueInfo(kInfoQueue, info);

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(-1, info.depth);
    EXPECT_EQ(0, fake.openCalls);
}

TEST_F(MqSessionFakeTest, QueueInfoRejectsAQueueNameLongerThan48Characters)
{
    connect();
    QueueInfo info;
    Result r = session.queueInfo(std::string(49, 'Q'), info);

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(0, fake.openCalls);
}

TEST_F(MqSessionFakeTest, QueueInfoDoesNotDisturbAnOpenBrowse)
{
    connect();
    addMessages(kInfoQueue, 3);

    GetOptions first;
    first.mode = GetMode::BrowseFirst;
    Message m;
    ASSERT_TRUE(session.get(kInfoQueue, m, first).ok());
    ASSERT_EQ(1, fake.openCalls);

    QueueInfo info;
    ASSERT_TRUE(session.queueInfo(kInfoQueue, info).ok());
    EXPECT_EQ(3, info.depth);
    EXPECT_EQ(2, fake.openCalls);
    EXPECT_EQ(1, fake.closeCalls) << "only the inquiry handle is closed";

    GetOptions next;
    next.mode = GetMode::BrowseNext;
    ASSERT_TRUE(session.get(kInfoQueue, m, next).ok());
    EXPECT_EQ(2, fake.openCalls) << "the browse handle is still the one in use";
    EXPECT_NE(0, fake.lastGmoOptions & MQGMO_BROWSE_NEXT);
}

// ---- browsePosition (Plan 3 Task 10) ----
//
// Windows counts the browsed messages in DataArea::browseCount: set to 1 by
// readFirstMessage (:8283-8292), +1 by browseNext (:8670), and recomputed by
// browsePrev from a fresh browse first (:8958, :9002). The core keeps the cursor
// position of the open queue handle and browsePosition() reports it, 0 for none.

namespace
{
    const char * kBrowseQueue = "RFHUTIL.TEST.BROWSE";
    const char * kOtherQueue = "RFHUTIL.TEST.OTHER";

    void addNamed(const char * queue, const std::string & text)
    {
        rfh_test::FakeMessage m;
        m.data.assign(text.begin(), text.end());
        fake.queue(queue).messages.push_back(m);
    }

    void addNamedMessages(const char * queue, int count)
    {
        for (int i = 1; i <= count; i++)
        {
            addNamed(queue, "m" + std::to_string(i));
        }
    }

    std::string bodyOf(const Message & m)
    {
        return std::string(m.body.begin(), m.body.end());
    }

    GetOptions modeOptions(GetMode mode)
    {
        GetOptions options;
        options.mode = mode;
        return options;
    }
}

TEST_F(MqSessionFakeTest, BrowsePositionCountsFirstNextAndPrevious)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 3);
    Message m;

    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    EXPECT_EQ(1, session.browsePosition());
    EXPECT_EQ("m1", bodyOf(m));

    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    EXPECT_EQ(2, session.browsePosition());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    EXPECT_EQ(3, session.browsePosition());
    EXPECT_EQ("m3", bodyOf(m));

    // end of queue: Windows closes the queue and disconnects (DataArea.cpp:8645);
    // the core keeps the cursor on the last message
    Result end = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext));
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, end.rc);
    EXPECT_EQ(3, session.browsePosition());
    EXPECT_EQ("m3", bodyOf(m)) << "a failed browse leaves the caller's message alone";

    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowsePrevious)).ok());
    EXPECT_EQ(2, session.browsePosition());
    EXPECT_EQ("m2", bodyOf(m));
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowsePrevious)).ok());
    EXPECT_EQ(1, session.browsePosition());
    EXPECT_EQ("m1", bodyOf(m));

    // already at the first message
    Result first = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowsePrevious));
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, first.rc);
    EXPECT_EQ(1, session.browsePosition());
    EXPECT_EQ("m1", bodyOf(m));

    // next after previous continues from the browsed message
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    EXPECT_EQ(2, session.browsePosition());
    EXPECT_EQ("m2", bodyOf(m));

    EXPECT_EQ(1, fake.openCalls) << "one handle serves the whole browse";
    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_EQ(fake.openCalls, fake.closeCalls);
    EXPECT_TRUE(fake.liveHandles.empty());
    EXPECT_EQ(0, session.browsePosition());
}

TEST_F(MqSessionFakeTest, BrowsePositionIsZeroWithoutABrowse)
{
    fake.browseCursors = true;
    EXPECT_EQ(0, session.browsePosition()) << "not connected";
    connect();
    EXPECT_EQ(0, session.browsePosition()) << "connected, nothing opened";
    fake.queue(kBrowseQueue);   // declared and empty

    // queue empty: the first browse finds nothing
    Message m;
    Result empty = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst));
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, empty.rc);
    EXPECT_EQ(0, session.browsePosition());
    empty = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext));
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, empty.rc);
    EXPECT_EQ(0, session.browsePosition());

    // browse previous with no browse: refused, still no position
    Result prev = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowsePrevious));
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, prev.rc);
    EXPECT_EQ(0, session.browsePosition());

    // a destructive get that is the first use of the handle starts no browse
    addNamedMessages(kBrowseQueue, 1);
    ASSERT_TRUE(session.get(kBrowseQueue, m, GetOptions()).ok());
    EXPECT_EQ(0, session.browsePosition());
}

TEST_F(MqSessionFakeTest, BrowsePositionResetsWhenTheQueueChanges)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 3);
    addNamedMessages(kOtherQueue, 2);
    Message m;

    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_EQ(2, session.browsePosition());

    // browsing another queue closes the first handle and starts counting again
    ASSERT_TRUE(session.get(kOtherQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    EXPECT_EQ(1, session.browsePosition());
    EXPECT_EQ(1u, fake.liveHandles.size());

    // browse previous on a queue other than the one under the cursor is refused
    Result wrong = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowsePrevious));
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, wrong.rc);

    ASSERT_TRUE(session.get(kOtherQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_EQ(2, session.browsePosition());

    // a put reopens the handle for output
    Message out;
    out.body.assign({'x'});
    ASSERT_TRUE(session.put(kOtherQueue, out, PutOptions()).ok());
    EXPECT_EQ(0, session.browsePosition());
    EXPECT_EQ(1u, fake.liveHandles.size());

    // closing the queue
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    ASSERT_EQ(1, session.browsePosition());
    ASSERT_TRUE(session.closeQueue().ok());
    EXPECT_EQ(0, session.browsePosition());
    EXPECT_TRUE(fake.liveHandles.empty());

    // a browse next on a new handle returns the first message, which is position 1
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    EXPECT_EQ(1, session.browsePosition());
    EXPECT_EQ("m1", bodyOf(m));

    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_EQ(fake.openCalls, fake.closeCalls);
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, BrowsePositionAfterADestructiveGetIsZero)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 4);
    Message m;

    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_EQ(2, session.browsePosition());

    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::Destructive)).ok());
    EXPECT_EQ("m1", bodyOf(m));
    EXPECT_EQ(0, session.browsePosition()) << "the cursor no longer maps to a position";
    EXPECT_EQ(1, fake.openCalls) << "the destructive get used the browse handle";

    // browse next carries on from the cursor but cannot know where that is
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    EXPECT_EQ("m3", bodyOf(m));
    EXPECT_EQ(0, session.browsePosition());

    // browse previous needs a known position
    Result prev = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowsePrevious));
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, prev.rc);
    EXPECT_EQ("m3", bodyOf(m));
    EXPECT_EQ(0, session.browsePosition());

    // browse first makes it known again
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    EXPECT_EQ("m2", bodyOf(m));
    EXPECT_EQ(1, session.browsePosition());
}

TEST_F(MqSessionFakeTest, BrowsePositionIsZeroAfterAFailedBrowseFirst)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 3);
    Message m;

    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_EQ(3, session.browsePosition());

    // somebody else empties the queue; the restarted browse finds nothing and the
    // cursor is back before the first message, so a stale 3 would be wrong
    fake.queue(kBrowseQueue).messages.clear();
    Result r = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst));
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_EQ(0, session.browsePosition());

    addNamedMessages(kBrowseQueue, 2);
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    EXPECT_EQ("m1", bodyOf(m));
    EXPECT_EQ(1, session.browsePosition());
}

TEST_F(MqSessionFakeTest, BrowsePositionKeepsItsValueOnOtherFailures)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 3);
    Message m;
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());

    fake.getRc = MQRC_GET_INHIBITED;
    Result r = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext));
    EXPECT_EQ(MQRC_GET_INHIBITED, r.rc);
    EXPECT_EQ(2, session.browsePosition()) << "the cursor did not move";
    EXPECT_TRUE(session.isConnected());

    fake.getRc = MQRC_NONE;
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    EXPECT_EQ(3, session.browsePosition());
}

TEST_F(MqSessionFakeTest, BrowsePositionIsZeroAfterTheConnectionIsLost)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 3);
    Message m;
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_EQ(2, session.browsePosition());

    fake.getRc = MQRC_CONNECTION_BROKEN;
    Result r = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext));
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ(0, session.browsePosition());
    EXPECT_EQ(1, fake.discCalls) << "the broken connection is released";
    EXPECT_TRUE(fake.liveHandles.empty()) << "the disconnect released the handle";

    // browse previous on the dead session fails cleanly
    Result prev = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowsePrevious));
    EXPECT_FALSE(prev.ok());
    EXPECT_EQ(0, session.browsePosition());

    // a new connection starts again at nothing
    fake.getRc = MQRC_NONE;
    connect();
    EXPECT_EQ(0, session.browsePosition());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    EXPECT_EQ(1, session.browsePosition());
    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, BrowsePreviousThatCannotReachItsTargetReportsTheCursor)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 3);
    Message m;
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_EQ("m3", bodyOf(m));

    // two messages vanish while the user looks at the third: the walk to position 2 ends after 1
    fake.queue(kBrowseQueue).messages.pop_front();
    fake.queue(kBrowseQueue).messages.pop_front();
    Result r = session.get(kBrowseQueue, m, modeOptions(GetMode::BrowsePrevious));
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_EQ("m3", bodyOf(m)) << "the caller's message stays";
    EXPECT_EQ(1, session.browsePosition()) << "the cursor is where the walk stopped";
}

TEST_F(MqSessionFakeTest, CancelledBrowsePreviousKeepsThePosition)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 3);
    Message m;
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());

    std::atomic<bool> cancel(true);
    GetOptions prev = modeOptions(GetMode::BrowsePrevious);
    prev.cancel = &cancel;
    const int getsBefore = fake.getCalls;
    Result r = session.get(kBrowseQueue, m, prev);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(getsBefore, fake.getCalls) << "cancelled before the first MQGET";
    EXPECT_EQ(3, session.browsePosition());
    EXPECT_EQ("m3", bodyOf(m));
}

TEST_F(MqSessionFakeTest, PurgeOfTheBrowsedQueueResetsThePosition)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 3);
    Message m;
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_EQ(2, session.browsePosition());

    MQLONG purged = 0;
    ASSERT_TRUE(session.purge(kBrowseQueue, purged).ok());
    EXPECT_EQ(3, purged);
    EXPECT_EQ(0, session.browsePosition());
    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_EQ(fake.openCalls, fake.closeCalls);
    EXPECT_TRUE(fake.liveHandles.empty());
}

// ---- put options and commit (Windows: putMessage 6290-6351, openQ 11973-11991, commitUOW 12558) ----

namespace
{
    Message putText()
    {
        Message m;
        m.body.assign({'a', 'b'});
        return m;
    }
}

TEST_F(MqSessionFakeTest, PutDefaultsAreUnchanged)
{
    connect();
    Message m = putText();
    ASSERT_TRUE(session.put("Q", m, PutOptions()).ok());

    // the options the put used before Task 14: no syncpoint, no logical order, no alternate user
    EXPECT_EQ((MQLONG)(MQPMO_NO_SYNCPOINT | MQPMO_FAIL_IF_QUIESCING | MQPMO_NEW_MSG_ID), fake.lastPmoOptions);
    EXPECT_EQ(MQPMO_VERSION_1, fake.lastPmoVersion);
    ASSERT_EQ(1u, fake.openLog.size());
    EXPECT_EQ((MQLONG)(MQOO_OUTPUT | MQOO_FAIL_IF_QUIESCING), fake.openLog[0].options);
    EXPECT_EQ("", fake.openLog[0].alternateUser);
    EXPECT_EQ(0, fake.commitCalls);
}

TEST_F(MqSessionFakeTest, PutLogicalOrderSetsItsFlag)
{
    connect();
    PutOptions options;
    options.logicalOrder = true;
    Message m = putText();
    ASSERT_TRUE(session.put("Q", m, options).ok());

    EXPECT_NE(0, fake.lastPmoOptions & MQPMO_LOGICAL_ORDER);
    EXPECT_NE(0, fake.lastPmoOptions & MQPMO_NO_SYNCPOINT) << "logical order does not imply syncpoint (Windows 6342-6351 decides that from the group and segment flags)";
    EXPECT_EQ(MQPMO_VERSION_2, fake.lastPmoVersion) << "Windows 6293 sets version 2";
}

TEST_F(MqSessionFakeTest, PutSyncpointReplacesNoSyncpoint)
{
    connect();
    PutOptions options;
    options.syncpoint = true;
    Message m = putText();
    ASSERT_TRUE(session.put("Q", m, options).ok());

    EXPECT_NE(0, fake.lastPmoOptions & MQPMO_SYNCPOINT);
    EXPECT_EQ(0, fake.lastPmoOptions & MQPMO_NO_SYNCPOINT);
    EXPECT_NE(0, fake.lastPmoOptions & MQPMO_FAIL_IF_QUIESCING);
    EXPECT_EQ(0, fake.commitCalls) << "a put never commits by itself";
}

TEST_F(MqSessionFakeTest, PutAlternateUserIsPassedAtOpen)
{
    connect();
    PutOptions options;
    options.alternateUser = "ALTUSER";
    Message m = putText();
    ASSERT_TRUE(session.put("Q", m, options).ok());

    ASSERT_EQ(1u, fake.openLog.size());
    EXPECT_NE(0, fake.openLog[0].options & MQOO_ALTERNATE_USER_AUTHORITY);
    EXPECT_NE(0, fake.openLog[0].options & MQOO_OUTPUT);
    EXPECT_EQ("ALTUSER", fake.openLog[0].alternateUser);
}

TEST_F(MqSessionFakeTest, PutAlternateUserLongerThan12CharactersIsCutAndSaid)
{
    connect();
    PutOptions options;
    options.alternateUser = "ABCDEFGHIJKLMNOP";
    Message m = putText();
    Result r = session.put("Q", m, options);
    ASSERT_TRUE(r.ok()) << r.text;

    ASSERT_EQ(1u, fake.openLog.size());
    EXPECT_EQ("ABCDEFGHIJKL", fake.openLog[0].alternateUser) << "openQ 11978-11990 cuts to MQ_USER_ID_LENGTH";
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_NE(std::string::npos, r.warnings[0].find("cut to 12 characters")) << r.warnings[0];
}

TEST_F(MqSessionFakeTest, PutReopensWhenTheAlternateUserChanges)
{
    connect();
    Message m = putText();
    PutOptions plain;
    ASSERT_TRUE(session.put("Q", m, plain).ok());
    ASSERT_TRUE(session.put("Q", m, plain).ok());
    EXPECT_EQ(1, fake.openCalls) << "the same options reuse the handle";

    PutOptions alt;
    alt.alternateUser = "OTHER";
    ASSERT_TRUE(session.put("Q", m, alt).ok());
    EXPECT_EQ(2, fake.openCalls);
    EXPECT_EQ(1, fake.closeCalls) << "the earlier handle is closed, not leaked";
    ASSERT_TRUE(session.put("Q", m, alt).ok());
    EXPECT_EQ(2, fake.openCalls);

    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PutSyncpointDoesNotChangeTheOpenHandle)
{
    connect();
    Message m = putText();
    PutOptions options;
    ASSERT_TRUE(session.put("Q", m, options).ok());
    options.syncpoint = true;
    options.logicalOrder = true;
    ASSERT_TRUE(session.put("Q", m, options).ok());
    EXPECT_EQ(1, fake.openCalls) << "syncpoint and logical order are put options, not open options";
}

TEST_F(MqSessionFakeTest, CommitCallsMqcmit)
{
    connect();
    Result r = session.commit();
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(MQRC_NONE, r.rc);
    EXPECT_EQ(1, fake.commitCalls);
    EXPECT_TRUE(session.isConnected());
}

TEST_F(MqSessionFakeTest, CommitWithNothingPendingSucceeds)
{
    connect();
    ASSERT_TRUE(session.commit().ok());
    ASSERT_TRUE(session.commit().ok());
    EXPECT_EQ(2, fake.commitCalls) << "MQ decides that there is nothing to commit; the session does not track a unit of work";
    EXPECT_EQ(0, fake.putCalls);
}

TEST_F(MqSessionFakeTest, CommitAfterSyncpointPutsKeepsTheQueueHandle)
{
    connect();
    Message m = putText();
    PutOptions options;
    options.syncpoint = true;
    ASSERT_TRUE(session.put("Q", m, options).ok());
    ASSERT_TRUE(session.put("Q", m, options).ok());
    ASSERT_TRUE(session.commit().ok());
    EXPECT_EQ(1, fake.commitCalls);
    ASSERT_TRUE(session.put("Q", m, options).ok());
    EXPECT_EQ(1, fake.openCalls) << "a commit does not close the queue";

    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, CommitWithoutAConnectionFails)
{
    Result r = session.commit();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("Not connected to a queue manager", r.text);
    EXPECT_EQ(0, fake.commitCalls) << "MQCMIT is not called without a connection handle";
}

TEST_F(MqSessionFakeTest, CommitFailureIsReportedAndKeepsTheConnection)
{
    connect();
    fake.cmitRc = MQRC_SYNCPOINT_NOT_AVAILABLE;
    Result r = session.commit();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(MQRC_SYNCPOINT_NOT_AVAILABLE, r.rc);
    EXPECT_NE(std::string::npos, r.text.find("MQCMIT")) << r.text;
    EXPECT_TRUE(session.isConnected()) << "the session does not disconnect on its own; Windows' discQM (12587) is the caller's flow";
    EXPECT_EQ(0, fake.discCalls);
}

TEST_F(MqSessionFakeTest, CommitWarningIsASuccessWithItsReason)
{
    connect();
    fake.cmitRc = MQRC_OUTCOME_MIXED;
    fake.cmitCc = MQCC_WARNING;
    Result r = session.commit();
    EXPECT_TRUE(r.ok()) << "Windows 12573 treats any cc but OK as a failure; here the cc and rc are for the caller to read";
    EXPECT_EQ(MQCC_WARNING, r.cc);
    EXPECT_EQ(MQRC_OUTCOME_MIXED, r.rc);
    EXPECT_TRUE(session.isConnected());
}

TEST_F(MqSessionFakeTest, CommitOnALostConnectionReleasesTheSession)
{
    connect();
    Message m = putText();
    PutOptions options;
    options.syncpoint = true;
    ASSERT_TRUE(session.put("Q", m, options).ok());

    fake.cmitRc = MQRC_CONNECTION_BROKEN;
    Result r = session.commit();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ(1, fake.discCalls);
    EXPECT_TRUE(fake.liveHandles.empty()) << "the queue handle went with the connection";

    Result again = session.commit();
    EXPECT_FALSE(again.ok());
    EXPECT_EQ(1, fake.commitCalls) << "no second MQCMIT on a dead connection";
}

TEST(MqSessionFakeApi, ApiWithoutACommitEntryIsRejected)
{
    fake.reset();
    MqApi api = fakeApi();
    api.cmit = nullptr;
    MqSession session(api);

    ConnectOptions options;
    options.mode = Mode::Bindings;
    Result r = session.connect(options);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("Injected MQ API is incomplete", r.text);
    EXPECT_EQ(0, fake.connxCalls);
}

// ---- purge options (Windows: DataArea::purgeQueue 10065-10244, openQ 12056-12105) ----
//
// Windows reads every message with a zero-length buffer and MQGMO_ACCEPT_TRUNCATED_MSG, with
// MQGMO_SYNCPOINT_IF_PERSISTENT, commits every maxUOW - 1 messages (and after 2000 at most) and at the
// end (except after a 2003), and opens the queue with the filter as a selector.

namespace
{
    const char * kPurgeQueue = "RFHUTIL.TEST.PURGE";

    void addPurgeMessages(int count, bool persistent, size_t size = 4)
    {
        for (int i = 0; i < count; i++)
        {
            rfh_test::FakeMessage m;
            m.data.assign(size, 'x');
            m.md.Persistence = persistent ? MQPER_PERSISTENT : MQPER_NOT_PERSISTENT;
            fake.queue(kPurgeQueue).messages.push_back(m);
        }
    }

    size_t queued()
    {
        return fake.queue(kPurgeQueue).messages.size();
    }

    PurgeOptions syncpointOptions(MQLONG batch)
    {
        PurgeOptions options;
        options.syncpoint = true;
        options.batchSize = batch;
        return options;
    }
}

TEST_F(MqSessionFakeTest, PurgeWithoutOptionsBehavesAsBefore)
{
    connect();
    addPurgeMessages(3, true);

    MQLONG purged = -1;
    Result r = session.purge(kPurgeQueue, purged);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(3, purged);
    EXPECT_EQ(0u, queued());
    EXPECT_EQ(0, fake.commitCalls) << "no syncpoint, nothing to commit";
    ASSERT_EQ(4u, fake.getLog.size());
    EXPECT_EQ(0, fake.getLog[0].options & (MQGMO_SYNCPOINT | MQGMO_SYNCPOINT_IF_PERSISTENT));
    EXPECT_NE(0, fake.getLog[0].options & MQGMO_NO_SYNCPOINT);
    EXPECT_NE(0, fake.getLog[0].options & MQGMO_ACCEPT_TRUNCATED_MSG);
    ASSERT_EQ(1u, fake.openLog.size());
    EXPECT_LT(fake.openLog[0].odVersion, MQOD_VERSION_4) << "no selector, no version 4 descriptor";
    EXPECT_EQ(fake.openCalls, fake.closeCalls);
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeOfAnEmptyQueueSucceedsWithCountZero)
{
    connect();
    fake.queue(kPurgeQueue);
    MQLONG purged = 9;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(10));
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(0, purged);
    EXPECT_EQ(0, fake.commitCalls) << "nothing was got, so there is no unit of work to commit";
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeSendsTheSelectorInAVersion4Open)
{
    connect();
    addPurgeMessages(2, false);
    PurgeOptions options;
    options.selector = "color = 'blue'";
    MQLONG purged = 0;
    ASSERT_TRUE(session.purge(kPurgeQueue, purged, options).ok());
    ASSERT_EQ(1u, fake.openLog.size());
    EXPECT_EQ(MQOD_VERSION_4, fake.openLog[0].odVersion);
    EXPECT_EQ("color = 'blue'", fake.openLog[0].selector);
    EXPECT_GT(fake.openLog[0].resObjectStringBufSize, 0) << "version 7 answers 2520 without a buffer (openQ 12097-12100)";
    EXPECT_EQ(MQOO_INPUT_AS_Q_DEF | MQOO_FAIL_IF_QUIESCING, fake.openLog[0].options & ~MQOO_INQUIRE);
}

TEST_F(MqSessionFakeTest, PurgeRefusesASelectorTheQueueManagerCannotUse)
{
    fake.commandLevel = 600;
    connect();
    addPurgeMessages(2, false);
    PurgeOptions options;
    options.selector = "a = 1";
    MQLONG purged = 5;
    Result r = session.purge(kPurgeQueue, purged, options);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("command level 700")) << r.text;
    EXPECT_EQ(0, purged);
    EXPECT_EQ(0, fake.openCalls) << "refused before any open";
    EXPECT_EQ(2u, queued());

    fake.reset();
    ASSERT_TRUE(session.disconnect().ok());
    connect();
    options.selector = std::string(MQ_SELECTOR_LENGTH + 1, 'a');
    r = session.purge(kPurgeQueue, purged, options);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("longer than " + std::to_string(MQ_SELECTOR_LENGTH) + " bytes")) << r.text;
    EXPECT_EQ(0, fake.openCalls);
}

TEST_F(MqSessionFakeTest, ASelectorOfExactlyTheHeaderLengthReachesTheOpen)
{
    // cmqc.h: MQ_SELECTOR_LENGTH is 10240 and the selector travels in an MQCHARV with a length, so no byte is
    // kept for a terminator. Windows cut at one byte less because it copied into a buffer with a terminator.
    connect();
    addPurgeMessages(1, false);
    PurgeOptions options;
    options.selector = std::string(MQ_SELECTOR_LENGTH, 'a');
    MQLONG purged = 0;
    ASSERT_TRUE(session.purge(kPurgeQueue, purged, options).ok());
    ASSERT_EQ(1u, fake.openLog.size());
    EXPECT_EQ((size_t)MQ_SELECTOR_LENGTH, fake.openLog[0].selector.size());

    GetOptions get;
    get.selector = std::string(MQ_SELECTOR_LENGTH, 'b');
    Message m;
    session.get(kPurgeQueue, m, get);
    ASSERT_EQ(2u, fake.openLog.size());
    EXPECT_EQ((size_t)MQ_SELECTOR_LENGTH, fake.openLog[1].selector.size());

    get.selector = std::string(MQ_SELECTOR_LENGTH + 1, 'b');
    Result refused = session.get(kPurgeQueue, m, get);
    EXPECT_FALSE(refused.ok());
    EXPECT_EQ("The selector is longer than " + std::to_string(MQ_SELECTOR_LENGTH) + " bytes", refused.text);
    EXPECT_EQ(2u, fake.openLog.size()) << "refused before any open";
}

TEST_F(MqSessionFakeTest, PurgeSyncpointCommitsInBatchesAndAtTheEnd)
{
    connect();
    addPurgeMessages(250, true);
    MQLONG purged = 0;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(100));
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(250, purged);
    EXPECT_EQ(3, fake.commitCalls);
    EXPECT_EQ((std::vector<int>{ 100, 200, 251 }), fake.commitAtGet) << "after the 100th, the 200th and the last (the 251st get found the queue empty)";
    EXPECT_NE(0, fake.getLog[0].options & MQGMO_SYNCPOINT_IF_PERSISTENT);
    EXPECT_EQ(0, fake.getLog[0].options & MQGMO_NO_SYNCPOINT);
    EXPECT_EQ(MQGMO_VERSION_2, fake.getLog[0].gmoVersion);
    EXPECT_EQ(MQMO_NONE, fake.getLog[0].matchOptions);
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeSyncpointBatchIsCappedAtTheWindowsLimit)
{
    connect();
    addPurgeMessages(2500, false, 1);
    MQLONG purged = 0;
    ASSERT_TRUE(session.purge(kPurgeQueue, purged, syncpointOptions(100000)).ok());
    EXPECT_EQ(2500, purged);
    EXPECT_EQ((std::vector<int>{ 2001, 2501 }), fake.commitAtGet) << "uowCount > 2000 (purgeQueue 10160)";

    ASSERT_TRUE(session.disconnect().ok());
    fake.reset();
    connect();
    addPurgeMessages(2500, false, 1);
    ASSERT_TRUE(session.purge(kPurgeQueue, purged, syncpointOptions(0)).ok());
    EXPECT_EQ((std::vector<int>{ 2001, 2501 }), fake.commitAtGet) << "a batch size of zero means the cap";
}

TEST_F(MqSessionFakeTest, PurgeWithoutSyncpointNeverCommits)
{
    connect();
    addPurgeMessages(250, true);
    PurgeOptions options;
    options.batchSize = 10;
    MQLONG purged = 0;
    ASSERT_TRUE(session.purge(kPurgeQueue, purged, options).ok());
    EXPECT_EQ(250, purged);
    EXPECT_EQ(0, fake.commitCalls);
}

TEST_F(MqSessionFakeTest, PurgeRemovesTruncatedAndEmptyMessagesAndCountsThem)
{
    connect();
    addPurgeMessages(2, true, 100000);
    addPurgeMessages(1, true, 0);
    addPurgeMessages(1, false, 1);
    MQLONG purged = 0;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(100));
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());
    EXPECT_EQ(4, purged);
    EXPECT_EQ(0u, queued());
    EXPECT_EQ(0, r.rc) << "the truncation warning of the last get is not the result";
}

TEST_F(MqSessionFakeTest, PurgeKeepsThePartialCountWhenAGetFails)
{
    connect();
    addPurgeMessages(10, true);
    fake.failGetsAfter = 3;
    fake.failGetsRc = MQRC_UNEXPECTED_ERROR;
    MQLONG purged = 0;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(100));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_UNEXPECTED_ERROR, r.rc);
    EXPECT_EQ(3, purged);
    EXPECT_NE(std::string::npos, r.text.find("after 3 messages")) << r.text;
    EXPECT_EQ(7u, queued());
    EXPECT_EQ(1, fake.commitCalls) << "the three got are committed after a failed get, as Windows does";
    EXPECT_TRUE(session.isConnected());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeWithoutSyncpointKeepsTheCountOfAFailedGet)
{
    connect();
    addPurgeMessages(10, true);
    fake.failGetsAfter = 4;
    fake.failGetsRc = MQRC_UNEXPECTED_ERROR;
    MQLONG purged = 0;
    Result r = session.purge(kPurgeQueue, purged);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(4, purged);
    EXPECT_NE(std::string::npos, r.text.find("after 4 messages")) << r.text;
    EXPECT_EQ(0, fake.commitCalls);
}

TEST_F(MqSessionFakeTest, PurgeAfterALostConnectionKeepsOnlyWhatMQRemoved)
{
    connect();
    addPurgeMessages(10, true);
    fake.failGetsAfter = 5;
    fake.failGetsRc = MQRC_CONNECTION_BROKEN;
    MQLONG purged = -1;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(3));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_EQ(3, purged) << "the commit after 3 worked; the 2 got after it were rolled back with the connection";
    EXPECT_NE(std::string::npos, r.text.find("after 3 messages")) << r.text;
    EXPECT_NE(std::string::npos, r.text.find("2 persistent messages")) << r.text;
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ(1, fake.commitCalls) << "no commit on a lost connection";
    EXPECT_EQ(1, fake.discCalls);
    EXPECT_EQ(0, fake.closeCalls) << "the queue handle went with the connection";
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeAfterALostConnectionCountsNonPersistentMessagesAsRemoved)
{
    connect();
    addPurgeMessages(10, false);
    fake.failGetsAfter = 5;
    fake.failGetsRc = MQRC_CONNECTION_BROKEN;
    MQLONG purged = -1;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(100));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(5, purged) << "non-persistent messages are not in the unit of work (MQGMO_SYNCPOINT_IF_PERSISTENT)";
    EXPECT_EQ(5u, queued());
    EXPECT_EQ(0, fake.commitCalls);
}

TEST_F(MqSessionFakeTest, PurgeWithoutSyncpointAfterALostConnectionKeepsTheCount)
{
    connect();
    addPurgeMessages(10, true);
    fake.failGetsAfter = 5;
    fake.failGetsRc = MQRC_CONNECTION_BROKEN;
    MQLONG purged = -1;
    Result r = session.purge(kPurgeQueue, purged);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(5, purged);
}

TEST_F(MqSessionFakeTest, PurgeBackedOutTakesTheUnitOfWorkOffTheCountAndSkipsTheCommit)
{
    connect();
    addPurgeMessages(10, true);
    fake.failGetsAfter = 3;
    fake.failGetsRc = MQRC_BACKED_OUT;
    MQLONG purged = -1;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(2));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_BACKED_OUT, r.rc);
    EXPECT_EQ(2, purged) << "committed after 2; the 3rd was backed out (purgeQueue 10185 skips the commit)";
    EXPECT_NE(std::string::npos, r.text.find("1 persistent messages")) << r.text;
    EXPECT_EQ(1, fake.commitCalls);
    EXPECT_TRUE(session.isConnected());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeStopsWhenACommitInTheLoopFails)
{
    connect();
    addPurgeMessages(10, true);
    fake.cmitRc = MQRC_BACKED_OUT;
    MQLONG purged = -1;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(2));
    EXPECT_FALSE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("MQCMIT")) << r.text;
    EXPECT_EQ(0, purged) << "the two got were not committed";
    EXPECT_NE(std::string::npos, r.text.find("2 persistent messages")) << r.text;
    EXPECT_EQ(1, fake.commitCalls);
    EXPECT_EQ(2, fake.getCalls) << "no further gets after a failed commit";
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeFailsWhenTheFinalCommitFails)
{
    connect();
    addPurgeMessages(3, true);
    fake.cmitRc = MQRC_BACKED_OUT;
    MQLONG purged = -1;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(100));
    EXPECT_FALSE(r.ok()) << "Windows reports success here (commitUOW's error is overwritten)";
    EXPECT_EQ(0, purged);
    EXPECT_NE(std::string::npos, r.text.find("MQCMIT")) << r.text;
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeRetriesAGetOnceAfterTheSyncpointLimitWasReached)
{
    connect();
    addPurgeMessages(10, true);
    fake.maxUncommitted = 3;
    MQLONG purged = 0;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(100));
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(10, purged);
    EXPECT_EQ(4, fake.commitCalls) << "three commits for the limit (after 3, 6, 9) and the last one for the tenth";
    EXPECT_EQ(0u, queued());
}

TEST_F(MqSessionFakeTest, PurgeEndsWhenTheSyncpointLimitIsStillReachedAfterTheCommit)
{
    connect();
    addPurgeMessages(10, true);
    fake.maxUncommitted = 3;
    fake.cmitRc = MQRC_NONE;
    fake.failGetsAfter = 4;   // the retried get (the 5th call) fails as well
    fake.failGetsRc = MQRC_SYNCPOINT_LIMIT_REACHED;
    MQLONG purged = 0;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(100));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_SYNCPOINT_LIMIT_REACHED, r.rc);
    EXPECT_EQ(3, purged);
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeStopsAtAWarningOtherThanTruncationAndCountsTheMessageItRemoved)
{
    connect();
    addPurgeMessages(5, false, 0);   // empty messages: the fake's truncation warning does not take the place of the one under test
    fake.warnGetRc[3] = MQRC_NOT_CONVERTED;
    MQLONG purged = 0;
    Result r = session.purge(kPurgeQueue, purged);
    EXPECT_FALSE(r.ok()) << "Windows ends the loop on any cc other than OK";
    EXPECT_EQ(MQRC_NOT_CONVERTED, r.rc);
    EXPECT_EQ(3, purged) << "the warning get did remove its message (Windows does not count it)";
    EXPECT_EQ(2u, queued());
}

TEST_F(MqSessionFakeTest, PurgeStopsWhenCancelledAndKeepsTheCount)
{
    connect();
    addPurgeMessages(10, true);
    std::atomic<bool> cancel(false);
    fake.onGet = [&cancel](int number)
    {
        if (number == 4)
        {
            cancel = true;
        }
    };
    PurgeOptions options = syncpointOptions(100);
    options.cancel = &cancel;
    MQLONG purged = 0;
    Result r = session.purge(kPurgeQueue, purged, options);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("cancelled")) << r.text;
    EXPECT_NE(std::string::npos, r.text.find("after 4 messages")) << r.text;
    EXPECT_EQ(4, purged) << "the 4th get ran; the flag is looked at before the 5th";
    EXPECT_EQ(6u, queued());
    EXPECT_EQ(1, fake.commitCalls) << "what was got is committed";
    EXPECT_EQ((std::vector<int>{ 4 }), fake.commitAtGet);
    EXPECT_TRUE(session.isConnected());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeCancelledBeforeTheFirstGetRemovesNothing)
{
    connect();
    addPurgeMessages(3, true);
    std::atomic<bool> cancel(true);
    PurgeOptions options;
    options.cancel = &cancel;
    MQLONG purged = 5;
    Result r = session.purge(kPurgeQueue, purged, options);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(0, purged);
    EXPECT_EQ(0, fake.getCalls);
    EXPECT_EQ(3u, queued());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeWhenAnotherApplicationHoldsTheQueueExclusively)
{
    connect();
    addPurgeMessages(3, true);
    fake.openRcByName[kPurgeQueue] = MQRC_OBJECT_IN_USE;
    MQLONG purged = 5;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(10));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_OBJECT_IN_USE, r.rc);
    EXPECT_EQ(std::string::npos, r.text.find("this session already has the queue open")) << "another application, not this session";
    EXPECT_EQ(0, purged);
    EXPECT_EQ(0, fake.getCalls);
    EXPECT_EQ(3u, queued());
    EXPECT_TRUE(fake.liveHandles.empty());
    EXPECT_TRUE(session.isConnected());
}

TEST_F(MqSessionFakeTest, PurgeOfAQueueThatCannotBeOpened)
{
    connect();
    fake.openRcByName[kPurgeQueue] = MQRC_UNKNOWN_OBJECT_NAME;
    MQLONG purged = 5;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(10));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_UNKNOWN_OBJECT_NAME, r.rc);
    EXPECT_EQ(0, purged);
    EXPECT_EQ(0, fake.commitCalls);
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(MqSessionFakeTest, PurgeWithoutAConnectionFails)
{
    MQLONG purged = 5;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(10));
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(0, purged);
    EXPECT_EQ(0, fake.openCalls);
}

TEST_F(MqSessionFakeTest, PurgeReportsACloseFailureAsAWarningAndKeepsTheCount)
{
    connect();
    addPurgeMessages(3, true);
    fake.closeRc = MQRC_UNEXPECTED_ERROR;
    MQLONG purged = 0;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(10));
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(3, purged);
    EXPECT_EQ(1u, r.warnings.size());
}

// ---- outcomes of interrupted units of work (cleanup of Tasks 13/14/19) ----
//
// MQDISC commits what a connection left pending, and a connection that is quiescing is still healthy, so
// disconnecting it publishes the unit of work. The session therefore backs the unit out before it releases a
// connection that reported 2161, 2162 or 2202, and it says "remain on the queue" only when it knows. When it
// does not (the connection broke, the back-out failed, MQCMIT lost the connection or ended with a warning) the
// outcome is reported as unknown and the persistent messages are counted apart, as `uncertain`.

namespace
{
    // The text the session uses when it cannot tell; it must never contain the certain phrase.
    const char * kRemain = "remain on the queue";

    const MQLONG kQuiescing[] = { MQRC_Q_MGR_QUIESCING, MQRC_Q_MGR_STOPPING, MQRC_CONNECTION_QUIESCING };

    bool contains(const std::string & text, const char * part)
    {
        return text.find(part) != std::string::npos;
    }
}

TEST_F(MqSessionFakeTest, AQuiescingConnectionIsBackedOutBeforeItIsReleasedAndSaysWhatRemains)
{
    for (MQLONG quiescing : kQuiescing)
    {
        SCOPED_TRACE(quiescing);
        fake.reset();
        MqSession local(fakeApi());
        ConnectOptions options;
        options.mode = Mode::Bindings;
        ASSERT_TRUE(local.connect(options).ok());
        addPurgeMessages(10, true);
        fake.failGetsAfter = 5;
        fake.failGetsRc = quiescing;

        MQLONG purged = -1;
        MQLONG uncertain = -1;
        Result r = local.purge(kPurgeQueue, purged, syncpointOptions(3), uncertain);
        EXPECT_FALSE(r.ok());
        EXPECT_EQ(quiescing, r.rc);
        EXPECT_FALSE(local.isConnected());
        EXPECT_EQ(3, purged) << "the commit after 3 worked";
        EXPECT_EQ(0, uncertain);
        EXPECT_EQ((std::vector<std::string>{ "cmit", "back", "disc" }), fake.callOrder) << "MQBACK before MQDISC, which would commit";
        EXPECT_EQ(7u, queued()) << "the two got after the commit went back to the queue";
        EXPECT_TRUE(contains(r.text, "2 persistent messages")) << r.text;
        EXPECT_TRUE(contains(r.text, kRemain)) << r.text;
        EXPECT_TRUE(fake.liveHandles.empty());
    }
}

TEST_F(MqSessionFakeTest, AFailedBackOutOfAQuiescingConnectionMakesTheOutcomeUnknown)
{
    for (bool queueManagerCommitted : { false, true })
    {
        SCOPED_TRACE(queueManagerCommitted);
        fake.reset();
        MqSession local(fakeApi());
        ConnectOptions options;
        options.mode = Mode::Bindings;
        ASSERT_TRUE(local.connect(options).ok());
        addPurgeMessages(10, true);
        fake.failGetsAfter = 5;
        fake.failGetsRc = MQRC_Q_MGR_QUIESCING;
        fake.backRc = MQRC_CONNECTION_BROKEN;
        fake.lostUnitCommits = queueManagerCommitted;

        MQLONG purged = -1;
        MQLONG uncertain = -1;
        Result r = local.purge(kPurgeQueue, purged, syncpointOptions(3), uncertain);
        EXPECT_FALSE(r.ok());
        EXPECT_EQ(3, purged) << "only what the commit made certain";
        EXPECT_EQ(2, uncertain);
        EXPECT_EQ(queueManagerCommitted ? 5u : 7u, queued()) << "the fake settled the unit as the queue manager might have";
        EXPECT_FALSE(contains(r.text, kRemain)) << r.text;
        EXPECT_TRUE(contains(r.text, "2 persistent messages")) << r.text;
        EXPECT_TRUE(contains(r.text, "unknown outcome")) << r.text;
        EXPECT_FALSE(local.isConnected());
    }
}

TEST_F(MqSessionFakeTest, ALostConnectionLeavesThePurgedMessagesUnknownWhicheverWayTheQueueManagerWent)
{
    for (bool queueManagerCommitted : { false, true })
    {
        SCOPED_TRACE(queueManagerCommitted);
        fake.reset();
        MqSession local(fakeApi());
        ConnectOptions options;
        options.mode = Mode::Bindings;
        ASSERT_TRUE(local.connect(options).ok());
        addPurgeMessages(10, true);
        fake.failGetsAfter = 5;
        fake.failGetsRc = MQRC_CONNECTION_BROKEN;
        fake.lostUnitCommits = queueManagerCommitted;

        MQLONG purged = -1;
        MQLONG uncertain = -1;
        Result r = local.purge(kPurgeQueue, purged, syncpointOptions(3), uncertain);
        EXPECT_FALSE(r.ok());
        EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
        EXPECT_EQ(3, purged);
        EXPECT_EQ(2, uncertain);
        EXPECT_EQ(queueManagerCommitted ? 5u : 7u, queued());
        EXPECT_FALSE(contains(r.text, kRemain)) << "the old text claimed this and was wrong when the queue manager committed: " << r.text;
        EXPECT_TRUE(contains(r.text, "unknown outcome")) << r.text;
        EXPECT_TRUE(contains(r.text, "after 3 messages")) << r.text;
        EXPECT_EQ((std::vector<std::string>{ "cmit", "disc" }), fake.callOrder) << "a broken connection is not backed out, only released";
    }
}

TEST_F(MqSessionFakeTest, AWarningFromTheCommitInThePurgeLoopStopsThePurgeWithAnUnknownOutcome)
{
    for (MQLONG warning : { MQRC_OUTCOME_MIXED, MQRC_OUTCOME_PENDING })
    {
        SCOPED_TRACE(warning);
        fake.reset();
        MqSession local(fakeApi());
        ConnectOptions options;
        options.mode = Mode::Bindings;
        ASSERT_TRUE(local.connect(options).ok());
        addPurgeMessages(10, true);
        fake.cmitRc = warning;
        fake.cmitCc = MQCC_WARNING;

        MQLONG purged = -1;
        MQLONG uncertain = -1;
        Result r = local.purge(kPurgeQueue, purged, syncpointOptions(2), uncertain);
        EXPECT_FALSE(r.ok()) << "a purge cannot go on over a batch whose fate it does not know";
        EXPECT_EQ(0, purged);
        EXPECT_EQ(2, uncertain);
        EXPECT_EQ(2, fake.getCalls);
        EXPECT_EQ(1, fake.commitCalls);
        EXPECT_FALSE(contains(r.text, kRemain)) << r.text;
        EXPECT_TRUE(contains(r.text, "unknown outcome")) << r.text;
        EXPECT_TRUE(contains(r.text, "after 0 messages")) << r.text;
        EXPECT_TRUE(local.isConnected());
    }
}

TEST_F(MqSessionFakeTest, AConnectionLostDuringTheCommitLeavesTheBatchUnknown)
{
    for (bool queueManagerCommitted : { false, true })
    {
        SCOPED_TRACE(queueManagerCommitted);
        fake.reset();
        MqSession local(fakeApi());
        ConnectOptions options;
        options.mode = Mode::Bindings;
        ASSERT_TRUE(local.connect(options).ok());
        addPurgeMessages(3, true);
        fake.cmitRc = MQRC_CONNECTION_BROKEN;
        fake.lostUnitCommits = queueManagerCommitted;

        // the final commit, after the queue was found empty
        MQLONG purged = -1;
        MQLONG uncertain = -1;
        Result r = local.purge(kPurgeQueue, purged, syncpointOptions(100), uncertain);
        EXPECT_FALSE(r.ok());
        EXPECT_EQ(0, purged);
        EXPECT_EQ(3, uncertain);
        EXPECT_EQ(queueManagerCommitted ? 0u : 3u, queued());
        EXPECT_FALSE(contains(r.text, kRemain)) << r.text;
        EXPECT_TRUE(contains(r.text, "unknown outcome")) << r.text;
        EXPECT_FALSE(local.isConnected());
    }
}

TEST_F(MqSessionFakeTest, AConnectionLostDuringACommitInTheLoopLeavesThatBatchUnknown)
{
    connect();
    addPurgeMessages(10, true);
    fake.cmitRc = MQRC_CONNECTION_BROKEN;
    MQLONG purged = -1;
    MQLONG uncertain = -1;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(2), uncertain);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(0, purged);
    EXPECT_EQ(2, uncertain);
    EXPECT_EQ(2, fake.getCalls);
    EXPECT_FALSE(contains(r.text, kRemain)) << r.text;
    EXPECT_FALSE(session.isConnected());
}

TEST_F(MqSessionFakeTest, ACommitThatFailsOtherwiseLeavesTheUnitOpenAndTheOutcomeUnknownUntilRolledBack)
{
    connect();
    addPurgeMessages(10, true);
    fake.cmitRc = MQRC_STORAGE_NOT_AVAILABLE;
    MQLONG purged = -1;
    MQLONG uncertain = -1;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(2), uncertain);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(0, purged);
    EXPECT_EQ(2, uncertain);
    EXPECT_FALSE(contains(r.text, kRemain)) << r.text;
    EXPECT_TRUE(contains(r.text, "unknown outcome")) << r.text;
    EXPECT_TRUE(session.isConnected());
    EXPECT_EQ(2u, fake.pendingGets.size()) << "MQ keeps the unit of work open";
    EXPECT_EQ(8u, queued());

    Result back = session.rollback();
    EXPECT_TRUE(back.ok()) << back.text;
    EXPECT_EQ(10u, queued()) << "rollback() gives the caller the means to settle it";
}

TEST_F(MqSessionFakeTest, ABackedOutUnitOfWorkReallyLeavesTheMessagesOnTheQueue)
{
    // 2003 is MQ's own word that the unit was backed out, so "remain on the queue" is certain here
    connect();
    addPurgeMessages(10, true);
    fake.cmitRc = MQRC_BACKED_OUT;
    MQLONG purged = -1;
    MQLONG uncertain = -1;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(2), uncertain);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(0, purged);
    EXPECT_EQ(0, uncertain);
    EXPECT_EQ(10u, queued());
    EXPECT_TRUE(contains(r.text, "2 persistent messages")) << r.text;
    EXPECT_TRUE(contains(r.text, kRemain)) << r.text;

    // and a get that is told 2003 has had its unit backed out by MQ
    fake.reset();
    MqSession local(fakeApi());
    ConnectOptions options;
    options.mode = Mode::Bindings;
    ASSERT_TRUE(local.connect(options).ok());
    addPurgeMessages(10, true);
    fake.failGetsAfter = 3;
    fake.failGetsRc = MQRC_BACKED_OUT;
    r = local.purge(kPurgeQueue, purged, syncpointOptions(2), uncertain);
    EXPECT_EQ(2, purged);
    EXPECT_EQ(8u, queued());
    EXPECT_TRUE(contains(r.text, kRemain)) << r.text;
}

TEST_F(MqSessionFakeTest, PurgeWithoutAnOutcomeOutParameterStillReportsTheUncertainBatch)
{
    connect();
    addPurgeMessages(10, true);
    fake.failGetsAfter = 5;
    fake.failGetsRc = MQRC_CONNECTION_BROKEN;
    MQLONG purged = -1;
    Result r = session.purge(kPurgeQueue, purged, syncpointOptions(3));
    EXPECT_EQ(3, purged);
    EXPECT_TRUE(contains(r.text, "2 persistent messages")) << r.text;
    EXPECT_TRUE(contains(r.text, "unknown outcome")) << r.text;
}

// ---- commit() and rollback() ----

TEST_F(MqSessionFakeTest, CommitAfterALostConnectionSaysTheOutcomeIsUnknown)
{
    connect();
    fake.cmitRc = MQRC_CONNECTION_BROKEN;
    Result r = session.commit();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_TRUE(contains(r.text, "outcome of the unit of work is unknown")) << r.text;
}

TEST_F(MqSessionFakeTest, CommitThatEndsWithAWarningIsOkButSaysTheOutcomeIsUnknown)
{
    for (MQLONG warning : { MQRC_OUTCOME_MIXED, MQRC_OUTCOME_PENDING })
    {
        SCOPED_TRACE(warning);
        fake.reset();
        MqSession local(fakeApi());
        ConnectOptions options;
        options.mode = Mode::Bindings;
        ASSERT_TRUE(local.connect(options).ok());
        fake.cmitRc = warning;
        fake.cmitCc = MQCC_WARNING;
        Result r = local.commit();
        EXPECT_TRUE(r.ok());
        EXPECT_EQ(warning, r.rc);
        ASSERT_EQ(1u, r.warnings.size());
        EXPECT_TRUE(contains(r.warnings[0], "outcome of the unit of work is unknown")) << r.warnings[0];
        EXPECT_TRUE(local.isConnected());
    }
}

TEST_F(MqSessionFakeTest, CommitThatFindsTheConnectionQuiescingBacksTheUnitOutAndSaysSo)
{
    connect();
    fake.cmitRc = MQRC_Q_MGR_QUIESCING;
    Result r = session.commit();
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ((std::vector<std::string>{ "cmit", "back", "disc" }), fake.callOrder);
    EXPECT_TRUE(contains(r.text, "backed out")) << r.text;
}

TEST_F(MqSessionFakeTest, RollbackDiscardsThePendingSyncpointPuts)
{
    connect();
    fake.queue("RFHUTIL.TEST.OTHER");
    PutOptions put;
    put.syncpoint = true;
    Message m = putText();
    ASSERT_TRUE(session.put("RFHUTIL.TEST.OTHER", m, put).ok());
    ASSERT_EQ(1u, fake.pendingPuts.size());

    Result r = session.rollback();
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(1, fake.backCalls);
    EXPECT_EQ(0u, fake.pendingPuts.size());
    EXPECT_TRUE(session.isConnected());
    ASSERT_TRUE(session.commit().ok());
    EXPECT_EQ(0u, fake.queue("RFHUTIL.TEST.OTHER").messages.size()) << "nothing was left to publish";
}

TEST_F(MqSessionFakeTest, RollbackWithoutAConnectionFails)
{
    Result r = session.rollback();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("Not connected to a queue manager", r.text);
    EXPECT_EQ(0, fake.backCalls);
}

TEST_F(MqSessionFakeTest, RollbackThatFindsTheConnectionLostSaysTheOutcomeIsUnknown)
{
    connect();
    fake.backRc = MQRC_CONNECTION_BROKEN;
    Result r = session.rollback();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_TRUE(contains(r.text, "outcome of the unit of work is unknown")) << r.text;
}

TEST_F(MqSessionFakeTest, RollbackRefusedWithQuiescingDoesNotBackOutAgain)
{
    connect();
    fake.backRc = MQRC_Q_MGR_QUIESCING;
    Result r = session.rollback();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(1, fake.backCalls) << "the failing MQBACK is not repeated by the release";
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ(1, fake.discCalls);
    EXPECT_TRUE(contains(r.text, "outcome of the unit of work is unknown")) << r.text;
}

TEST_F(MqSessionFakeTest, RollbackThatFailsOtherwiseKeepsTheConnection)
{
    connect();
    fake.backRc = MQRC_UNEXPECTED_ERROR;
    Result r = session.rollback();
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(session.isConnected());
}

TEST(MqSessionFakeApi, ApiWithoutABackEntryIsRejected)
{
    fake.reset();
    MqApi api = fakeApi();
    api.back = nullptr;
    MqSession session(api);

    ConnectOptions options;
    options.mode = Mode::Bindings;
    Result r = session.connect(options);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("Injected MQ API is incomplete", r.text);
    EXPECT_EQ(0, fake.connxCalls);
}

// ---- puts under syncpoint on a connection that is lost ----

TEST_F(MqSessionFakeTest, ASyncpointPutOnAQuiescingConnectionSaysTheEarlierWorkWasBackedOut)
{
    connect();
    fake.queue(kPurgeQueue);
    PutOptions put;
    put.syncpoint = true;
    Message first = putText();
    ASSERT_TRUE(session.put(kPurgeQueue, first, put).ok());
    fake.failPutsAfter = 1;
    fake.failPutsRc = MQRC_Q_MGR_QUIESCING;
    Message second = putText();
    Result r = session.put(kPurgeQueue, second, put);
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ((std::vector<std::string>{ "back", "disc" }), fake.callOrder);
    EXPECT_EQ(0u, queued()) << "the first put was backed out, not published by MQDISC";
    EXPECT_TRUE(contains(r.text, "backed out")) << r.text;
    EXPECT_FALSE(contains(r.text, "unknown")) << r.text;
}

TEST_F(MqSessionFakeTest, ASyncpointPutOnABrokenConnectionSaysTheEarlierWorkIsUnknown)
{
    for (bool queueManagerCommitted : { false, true })
    {
        SCOPED_TRACE(queueManagerCommitted);
        fake.reset();
        MqSession local(fakeApi());
        ConnectOptions options;
        options.mode = Mode::Bindings;
        ASSERT_TRUE(local.connect(options).ok());
        fake.queue(kPurgeQueue);
        PutOptions put;
        put.syncpoint = true;
        Message first = putText();
        ASSERT_TRUE(local.put(kPurgeQueue, first, put).ok());
        fake.failPutsAfter = 1;
        fake.failPutsRc = MQRC_CONNECTION_BROKEN;
        fake.lostUnitCommits = queueManagerCommitted;
        Message second = putText();
        Result r = local.put(kPurgeQueue, second, put);
        EXPECT_FALSE(r.ok());
        EXPECT_EQ(queueManagerCommitted ? 1u : 0u, queued());
        EXPECT_TRUE(contains(r.text, "unknown")) << r.text;
        EXPECT_FALSE(contains(r.text, kRemain)) << r.text;
        EXPECT_EQ(0, fake.backCalls);
    }
}

TEST_F(MqSessionFakeTest, ASyncpointPutWhoseOpenFindsTheConnectionQuiescingSaysTheEarlierWorkWasBackedOut)
{
    connect();
    fake.queue(kPurgeQueue);
    PutOptions put;
    put.syncpoint = true;
    Message first = putText();
    ASSERT_TRUE(session.put(kPurgeQueue, first, put).ok());
    fake.openRcByName["RFHUTIL.TEST.OTHER"] = MQRC_CONNECTION_QUIESCING;
    Message second = putText();
    Result r = session.put("RFHUTIL.TEST.OTHER", second, put);
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ(0u, queued()) << "backed out, not published by MQDISC";
    EXPECT_TRUE(contains(r.text, "backed out")) << r.text;
}

TEST_F(MqSessionFakeTest, APutWithoutSyncpointAddsNoUnitOfWorkNoteWhenTheConnectionIsLost)
{
    connect();
    fake.putRc = MQRC_Q_MGR_QUIESCING;
    Message m = putText();
    Result r = session.put(kPurgeQueue, m, PutOptions());
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(contains(r.text, "unit of work")) << r.text;
    EXPECT_FALSE(contains(r.text, "syncpoint")) << r.text;
}

TEST(FakeMq, AnUnusedSessionCommitsPendingPutsWhenItIsDestroyed)
{
    fake.reset();
    fake.queue("RFHUTIL.TEST.Q");
    {
        MqSession session(fakeApi());
        ConnectOptions options;
        options.mode = Mode::Bindings;
        ASSERT_TRUE(session.connect(options).ok());
        PutOptions put;
        put.syncpoint = true;
        Message m;
        m.body.assign({'a'});
        ASSERT_TRUE(session.put("RFHUTIL.TEST.Q", m, put).ok());
    }
    EXPECT_EQ(1u, fake.queue("RFHUTIL.TEST.Q").messages.size()) << "~MqSession disconnects, and MQDISC commits";
}

// ---- reasons that release the connection, and closeQueue ----

TEST_F(MqSessionFakeTest, ConnectionStoppingAndAHandleErrorReleaseTheConnection)
{
    for (MQLONG rc : { MQRC_CONNECTION_STOPPING, MQRC_HCONN_ERROR })
    {
        SCOPED_TRACE(rc);
        fake.reset();
        MqSession local(fakeApi());
        ConnectOptions options;
        options.mode = Mode::Bindings;
        ASSERT_TRUE(local.connect(options).ok());
        fake.openRc = rc;
        Message m;
        Result r = local.get("Q", m, GetOptions());
        EXPECT_EQ(rc, r.rc);
        EXPECT_FALSE(local.isConnected());
        EXPECT_EQ(1, fake.discCalls);
        EXPECT_EQ(0, fake.backCalls) << "neither says the connection can still be used";
    }
}

TEST_F(MqSessionFakeTest, ACloseThatFailsBecauseTheQueueManagerIsQuiescingReleasesTheConnection)
{
    connect();
    Message m;
    ASSERT_TRUE(session.get("Q", m, GetOptions()).ok());
    fake.closeRc = MQRC_Q_MGR_QUIESCING;
    Result r = session.closeQueue();
    EXPECT_EQ(MQRC_Q_MGR_QUIESCING, r.rc);
    EXPECT_FALSE(session.isConnected()) << "the early return for a live handle skipped the release";
    EXPECT_EQ(1, fake.discCalls);
    EXPECT_EQ((std::vector<std::string>{ "back", "disc" }), fake.callOrder);
}

// ---- the browse handle of a purged queue ----

TEST_F(MqSessionFakeTest, PurgeOfTheBrowsedQueueClosesTheBrowseHandleSoTheNextBrowseStartsClean)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 3);
    Message m;
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    ASSERT_EQ(2, session.browsePosition());
    const int opensBefore = fake.openCalls;

    MQLONG purged = 0;
    ASSERT_TRUE(session.purge(kBrowseQueue, purged).ok());
    EXPECT_EQ(opensBefore + 1, fake.openCalls);
    EXPECT_EQ(2, fake.closeCalls) << "the purge's own handle and the browse handle";
    EXPECT_EQ(0u, fake.liveHandles.size()) << "the browse handle is closed, not just forgotten";
    EXPECT_EQ(0, session.browsePosition());

    addNamedMessages(kBrowseQueue, 2);
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseNext)).ok());
    EXPECT_EQ(opensBefore + 2, fake.openCalls) << "a new handle, whose cursor is before the first message";
    EXPECT_EQ(1, session.browsePosition());
}

TEST_F(MqSessionFakeTest, PurgeThatCannotOpenTheQueueKeepsTheBrowseHandle)
{
    fake.browseCursors = true;
    connect();
    addNamedMessages(kBrowseQueue, 3);
    Message m;
    ASSERT_TRUE(session.get(kBrowseQueue, m, modeOptions(GetMode::BrowseFirst)).ok());
    fake.openRcByName[kBrowseQueue] = MQRC_OBJECT_IN_USE;
    MQLONG purged = 0;
    EXPECT_FALSE(session.purge(kBrowseQueue, purged).ok());
    EXPECT_EQ(1, session.browsePosition()) << "nothing was removed, so the browse is undisturbed";
    EXPECT_EQ(1u, fake.liveHandles.size());
}
