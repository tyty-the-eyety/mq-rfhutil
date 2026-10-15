// MqSessionTests.cpp — integration tests for rfh::MqSession against a real queue manager.
//
// Bindings mode (always attempted):
//   MQ_BINDINGS_QMGR  local queue manager (default TEST_QM). The fixture creates the
//                     queues RFHUTIL.TEST.Q and RFHUTIL.TEST.EMPTY with runmqsc and
//                     deletes them afterwards, so the user running the tests must be
//                     in the mqm group. Tests skip when the queue manager is unavailable.
// Client mode (only when MQ_CHANNEL is set; matches tests/IntegrationTests):
//   MQ_HOST, MQ_PORT, MQ_CHANNEL, MQ_QMGR, MQ_USER, MQ_PASSWORD, MQ_CLIENT_QUEUE
//   (defaults: localhost, 1414, -, QM1, app, passw0rd, DEV.QUEUE.1), e.g. the
//   queue manager from tests/docker/compose.yaml.
//
// Tests skip when MQ is not available. Set RFHUTIL_REQUIRE_MQ=1 to make those
// skips failures (for CI machines that must have MQ).

#include <gtest/gtest.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <regex>
#include <string>
#include <thread>

#include "rfh_mq_session.h"

using namespace rfh;

namespace
{
    std::string env(const char * name, const char * def)
    {
        const char * v = getenv(name);
        return (v != nullptr && v[0] != 0) ? v : def;
    }

    bool requireMq() { return env("RFHUTIL_REQUIRE_MQ", "") == "1"; }

    std::string bindingsQm() { return env("MQ_BINDINGS_QMGR", "TEST_QM"); }

    const char * kQueue = "RFHUTIL.TEST.Q";
    const char * kEmptyQueue = "RFHUTIL.TEST.EMPTY";

    // Runs MQSC commands against the bindings queue manager; returns true on success.
    // The commands go to runmqsc's standard input; only validated names reach the shell.
    bool runMqsc(const std::string & commands)
    {
        const std::string qm = bindingsQm();
        const std::string bin = env("MQ_INSTALLATION_PATH", "/opt/mqm") + "/bin/runmqsc";
        if (!std::regex_match(qm, std::regex("[A-Za-z0-9._%/]{1,48}")) ||
            !std::regex_match(bin, std::regex("[A-Za-z0-9._/+-]+")))
        {
            return false;   // never pass an unexpected name to the shell
        }

        FILE * pipe = popen((bin + " " + qm + " >/dev/null 2>&1").c_str(), "w");
        if (pipe == nullptr)
        {
            return false;
        }

        const bool written = fputs((commands + "\n").c_str(), pipe) >= 0;
        return pclose(pipe) == 0 && written;
    }

    Message textMessage(const std::string & text)
    {
        Message m;
        memcpy(m.mqmd.Format, MQFMT_STRING, MQ_FORMAT_LENGTH);
        m.body.assign(text.begin(), text.end());
        return m;
    }

    std::string bodyText(const Message & m)
    {
        return std::string(m.body.begin(), m.body.end());
    }
}

class MqBindingsTest : public ::testing::Test
{
protected:
    static bool queuesReady;

    static void SetUpTestSuite()
    {
        signal(SIGPIPE, SIG_IGN);   // a runmqsc that exits early must not kill the tests
        queuesReady = runMqsc(std::string("DEFINE QLOCAL(") + kQueue + ") DEFSOPT(SHARED) SHARE REPLACE\n"
                              "DEFINE QLOCAL(" + kEmptyQueue + ") REPLACE");
    }

    static void TearDownTestSuite()
    {
        runMqsc(std::string("DELETE QLOCAL(") + kQueue + ") PURGE\nDELETE QLOCAL(" + kEmptyQueue + ") PURGE");
    }

    void SetUp() override
    {
        if (!queuesReady)
        {
            const std::string why = "queue manager " + bindingsQm() + " is not available for bindings tests";
            if (requireMq())
            {
                FAIL() << why;
            }
            GTEST_SKIP() << why;
        }

        ConnectOptions options;
        options.mode = Mode::Bindings;
        options.qmName = bindingsQm();
        Result r = session.connect(options);
        ASSERT_TRUE(r.ok()) << r.text;

        MQLONG purged = 0;
        ASSERT_TRUE(session.purge(kQueue, purged).ok());
        ASSERT_TRUE(session.purge(kEmptyQueue, purged).ok());
    }

    MqSession session;
};

bool MqBindingsTest::queuesReady = false;

TEST(MqSession, OperationsFailWhenNotConnected)
{
    MqSession session;
    Message m = textMessage("x");
    Result r = session.put(kQueue, m, PutOptions());
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("Not connected to a queue manager", r.text);
}

TEST(MqSession, UnknownQueueManagerFails)
{
    MqSession session;
    ConnectOptions options;
    options.mode = Mode::Bindings;
    options.qmName = "RFHUTIL_NO_SUCH_QM";

    MqApi api;
    Result loaded = MqLoader::load(api);
    if (!loaded.ok())
    {
        if (requireMq())
        {
            FAIL() << loaded.text;
        }
        GTEST_SKIP() << loaded.text;   // no MQ library on this machine
    }

    Result r = session.connect(options);
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_TRUE(r.rc == MQRC_Q_MGR_NAME_ERROR || r.rc == MQRC_Q_MGR_NOT_AVAILABLE) << r.text;
    EXPECT_FALSE(session.isConnected());
}

TEST_F(MqBindingsTest, PutThenGetReturnsSameMessage)
{
    Message out = textMessage("hello rfhutil");
    Result put = session.put(kQueue, out, PutOptions());
    ASSERT_TRUE(put.ok()) << put.text;

    MQBYTE24 none = { 0 };
    EXPECT_NE(0, memcmp(out.mqmd.MsgId, none, sizeof(none))) << "MQ should assign a MsgId";

    GetOptions get;
    get.matchMsgId = true;
    memcpy(get.msgId, out.mqmd.MsgId, sizeof(get.msgId));

    Message in;
    Result r = session.get(kQueue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ("hello rfhutil", bodyText(in));
    EXPECT_EQ(0, memcmp(in.mqmd.Format, MQFMT_STRING, MQ_FORMAT_LENGTH));
    EXPECT_EQ(0, memcmp(in.mqmd.MsgId, out.mqmd.MsgId, sizeof(MQBYTE24)));
}

TEST_F(MqBindingsTest, GetFromEmptyQueueReportsNoMessage)
{
    Message in;
    Result r = session.get(kEmptyQueue, in, GetOptions());
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_EQ("MQGET cc=2 rc=2033 MQRC_NO_MSG_AVAILABLE (No message available)", r.text);
}

TEST_F(MqBindingsTest, OpenUnknownQueueReportsUnknownObject)
{
    Message in;
    Result r = session.get("RFHUTIL.TEST.NO.SUCH.QUEUE", in, GetOptions());
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(MQRC_UNKNOWN_OBJECT_NAME, r.rc);
}

TEST_F(MqBindingsTest, BrowsesForwardAndBackward)
{
    for (const char * text : { "one", "two", "three" })
    {
        Message m = textMessage(text);
        ASSERT_TRUE(session.put(kQueue, m, PutOptions()).ok());
    }

    GetOptions browse;
    Message m;

    browse.mode = GetMode::BrowseFirst;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("one", bodyText(m));

    browse.mode = GetMode::BrowseNext;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("two", bodyText(m));
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("three", bodyText(m));

    browse.mode = GetMode::BrowsePrevious;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("two", bodyText(m));
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("one", bodyText(m));

    Result atStart = session.get(kQueue, m, browse);
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, atStart.rc);
    EXPECT_NE(std::string::npos, atStart.text.find("already at the first message"));

    browse.mode = GetMode::BrowseNext;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("two", bodyText(m));

    MQLONG depth = 0;
    ASSERT_TRUE(session.depth(kQueue, depth).ok());
    EXPECT_EQ(3, depth) << "browsing must not remove messages";
}

TEST_F(MqBindingsTest, GetsByCorrelId)
{
    PutOptions keepCorrelId;
    for (const char * text : { "first", "second" })
    {
        Message m = textMessage(text);
        memset(m.mqmd.CorrelId, 0, sizeof(m.mqmd.CorrelId));
        memcpy(m.mqmd.CorrelId, text, strlen(text));
        ASSERT_TRUE(session.put(kQueue, m, keepCorrelId).ok());
    }

    GetOptions get;
    get.matchCorrelId = true;
    memcpy(get.correlId, "second", strlen("second"));

    Message in;
    Result r = session.get(kQueue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ("second", bodyText(in));
}

TEST_F(MqBindingsTest, ReportsDepthAndPurges)
{
    for (int i = 0; i < 3; i++)
    {
        Message m = textMessage("purge me");
        ASSERT_TRUE(session.put(kQueue, m, PutOptions()).ok());
    }

    MQLONG depth = 0;
    ASSERT_TRUE(session.depth(kQueue, depth).ok());
    EXPECT_EQ(3, depth);

    MQLONG purged = 0;
    Result r = session.purge(kQueue, purged);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(3, purged);

    ASSERT_TRUE(session.depth(kQueue, depth).ok());
    EXPECT_EQ(0, depth);
}

TEST_F(MqBindingsTest, GrowsBufferForLargeMessage)
{
    std::string big(100 * 1024, 'x');
    big[0] = 'A';
    big[big.size() - 1] = 'Z';

    Message out = textMessage(big);
    ASSERT_TRUE(session.put(kQueue, out, PutOptions()).ok());

    GetOptions browse;
    browse.mode = GetMode::BrowseFirst;
    browse.initialBufferSize = 16;
    Message browsed;
    Result b = session.get(kQueue, browsed, browse);
    ASSERT_TRUE(b.ok()) << b.text;
    EXPECT_EQ(big, bodyText(browsed));

    GetOptions get;
    get.initialBufferSize = 16;
    Message in;
    Result r = session.get(kQueue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(big, bodyText(in));

    MQLONG depth = 0;
    ASSERT_TRUE(session.depth(kQueue, depth).ok());
    EXPECT_EQ(0, depth);
}

TEST_F(MqBindingsTest, WaitTimesOutAfterInterval)
{
    GetOptions get;
    get.waitMs = 1500;

    auto start = std::chrono::steady_clock::now();
    Message in;
    Result r = session.get(kEmptyQueue, in, get);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_GE(elapsed.count(), 1400);
    EXPECT_LT(elapsed.count(), 5000);
}

TEST_F(MqBindingsTest, CancelStopsWaitingGet)
{
    std::atomic<bool> cancel(false);
    GetOptions get;
    get.waitMs = 20000;
    get.cancel = &cancel;

    std::thread canceller([&cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        cancel = true;
    });

    auto start = std::chrono::steady_clock::now();
    Message in;
    Result r = session.get(kEmptyQueue, in, get);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    canceller.join();

    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_NE(std::string::npos, r.text.find("wait cancelled"));
    EXPECT_LT(elapsed.count(), 2500);
}

TEST_F(MqBindingsTest, DisconnectEndsSession)
{
    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_FALSE(session.isConnected());

    Message in;
    EXPECT_EQ("Not connected to a queue manager", session.get(kQueue, in, GetOptions()).text);
}

TEST(MqClient, PutThenGetOverClientChannel)
{
    if (env("MQ_CHANNEL", "").empty())
    {
        GTEST_SKIP() << "set MQ_CHANNEL (and MQ_HOST, MQ_PORT, MQ_QMGR, MQ_USER, MQ_PASSWORD) to test client mode";
    }

    ConnectOptions options;
    options.mode = Mode::Client;
    options.qmName = env("MQ_QMGR", "QM1");
    options.channel = env("MQ_CHANNEL", "");
    options.connName = env("MQ_HOST", "localhost") + "(" + env("MQ_PORT", "1414") + ")";
    options.userId = env("MQ_USER", "app");
    options.password = env("MQ_PASSWORD", "passw0rd");

    MqSession session;
    Result c = session.connect(options);
    ASSERT_TRUE(c.ok()) << c.text;

    const std::string queue = env("MQ_CLIENT_QUEUE", "DEV.QUEUE.1");
    Message out = textMessage("over the channel");
    ASSERT_TRUE(session.put(queue, out, PutOptions()).ok());

    GetOptions get;
    get.matchMsgId = true;
    memcpy(get.msgId, out.mqmd.MsgId, sizeof(get.msgId));
    Message in;
    Result r = session.get(queue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ("over the channel", bodyText(in));
}
