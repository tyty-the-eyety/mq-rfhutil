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
