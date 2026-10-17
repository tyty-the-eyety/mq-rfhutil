// MqSessionFakeTests.cpp - MqSession paths that need MQ to misbehave, driven by a
// fake MQI so they run without a queue manager.

#include <gtest/gtest.h>

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
