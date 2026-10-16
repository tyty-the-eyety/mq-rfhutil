// MqSessionFakeTests.cpp - MqSession paths that need MQ to misbehave, driven by a
// fake MQI so they run without a queue manager.

#include <gtest/gtest.h>

#include "rfh_mq_session.h"

using namespace rfh;

namespace
{
    // What the fake MQI should answer, and what it saw.
    struct Fake
    {
        MQLONG openRc = MQRC_NONE;
        MQLONG closeRc = MQRC_NONE;
        MQLONG discRc = MQRC_NONE;
        MQLONG putRc = MQRC_NONE;
        int connxCalls = 0;
        int openCalls = 0;
        int discCalls = 0;
    };

    Fake fake;

    void report(MQLONG rc, PMQLONG cc, PMQLONG reason)
    {
        *reason = rc;
        *cc = (rc == MQRC_NONE) ? MQCC_OK : MQCC_FAILED;
    }

    void fakeConnx(PMQCHAR, PMQCNO, PMQHCONN hConn, PMQLONG cc, PMQLONG rc)
    {
        fake.connxCalls++;
        *hConn = 1;
        report(MQRC_NONE, cc, rc);
    }

    void fakeDisc(PMQHCONN, PMQLONG cc, PMQLONG rc)
    {
        fake.discCalls++;
        report(fake.discRc, cc, rc);
    }

    void fakeOpen(MQHCONN, PMQVOID, MQLONG, PMQHOBJ hObj, PMQLONG cc, PMQLONG rc)
    {
        fake.openCalls++;
        *hObj = fake.openCalls;
        report(fake.openRc, cc, rc);
    }

    void fakeClose(MQHCONN, PMQHOBJ hObj, MQLONG, PMQLONG cc, PMQLONG rc)
    {
        if (fake.closeRc == MQRC_NONE)
        {
            *hObj = MQHO_UNUSABLE_HOBJ;   // as real MQ does on success
        }
        report(fake.closeRc, cc, rc);
    }

    void fakePut(MQHCONN, MQHOBJ, PMQVOID, PMQVOID, MQLONG, PMQVOID, PMQLONG cc, PMQLONG rc)
    {
        report(fake.putRc, cc, rc);
    }

    void fakeGet(MQHCONN, MQHOBJ, PMQVOID, PMQVOID, MQLONG, PMQVOID, PMQLONG length, PMQLONG cc, PMQLONG rc)
    {
        *length = 0;
        report(MQRC_NONE, cc, rc);
    }

    void fakeInq(MQHCONN, MQHOBJ, MQLONG, PMQLONG, MQLONG, PMQLONG intAttrs, MQLONG, PMQCHAR, PMQLONG cc, PMQLONG rc)
    {
        intAttrs[0] = 7;   // a fixed queue depth
        report(MQRC_NONE, cc, rc);
    }

    MqApi fakeApi()
    {
        MqApi api;
        api.connx = fakeConnx;
        api.disc = fakeDisc;
        api.open = fakeOpen;
        api.close = fakeClose;
        api.put = fakePut;
        api.get = fakeGet;
        api.inq = fakeInq;
        api.serverLibrary = true;
        api.bindingsAvailable = true;
        api.libraryPath = "fake";
        return api;
    }

    class MqSessionFakeTest : public ::testing::Test
    {
    protected:
        void SetUp() override { fake = Fake(); }

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
    fake = Fake();
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
