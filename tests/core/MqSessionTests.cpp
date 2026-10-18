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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "rfh_mq_session.h"
#include "mq_layout.h"

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
    const char * kExclQueue = "RFHUTIL.TEST.EXCL";    // exclusive input: a second open by the same session fails with 2042

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
                              "DEFINE QLOCAL(" + kEmptyQueue + ") REPLACE\n"
                              "DEFINE QLOCAL(" + kExclQueue + ") DEFSOPT(EXCL) NOSHARE REPLACE");
    }

    static void TearDownTestSuite()
    {
        runMqsc(std::string("DELETE QLOCAL(") + kQueue + ") PURGE\nDELETE QLOCAL(" + kEmptyQueue + ") PURGE\n"
                 "DELETE QLOCAL(" + kExclQueue + ") PURGE");
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
        ASSERT_TRUE(session.purge(kExclQueue, purged).ok());
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

namespace
{
    // An RFH2 with name/value data, so StrucLength is larger than the fixed 36 bytes.
    mq_layout::Bytes rfh2WithFolder(const std::string & format = "MQSTR")
    {
        const std::string folder = "<mcd><Msd>jms_text</Msd></mcd>  ";    // 32 bytes
        return mq_layout::rfh2(mq_layout::Order::Little, 546, 1208, format, std::string("\x20\0\0\0", 4) + folder);    // folder length, little endian like the header
    }

    Message rfh2Message(const mq_layout::Bytes & rfh, const char * format, const std::string & text)
    {
        Message m = textMessage(text);
        memcpy(m.mqmd.Format, format, MQ_FORMAT_LENGTH);
        m.headers = rfh.data;
        return m;
    }
}

TEST_F(MqBindingsTest, Rfh2HeaderIsSeparatedFromTheBody)
{
    auto rfh = rfh2WithFolder();
    ASSERT_GT(rfh.data.size(), 36u);
    Message out = rfh2Message(rfh, MQFMT_RF_HEADER_2, "after the header");
    Result p = session.put(kQueue, out, PutOptions());
    ASSERT_TRUE(p.ok()) << p.text;
    EXPECT_TRUE(p.warnings.empty());

    Message in;
    Result r = session.get(kQueue, in, GetOptions());
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(rfh.data, in.headers);
    EXPECT_EQ("after the header", bodyText(in));
    EXPECT_EQ(0, memcmp(in.mqmd.Format, MQFMT_RF_HEADER_2, MQ_FORMAT_LENGTH));
}

TEST_F(MqBindingsTest, PutWarnsWhenFormatDoesNotNameTheHeaders)
{
    auto rfh = rfh2WithFolder();
    Message out = rfh2Message(rfh, MQFMT_STRING, "body");
    Result p = session.put(kQueue, out, PutOptions());
    ASSERT_TRUE(p.ok()) << p.text;
    ASSERT_EQ(1u, p.warnings.size());
    EXPECT_EQ("headers do not match the MQMD format chain; a get splits them differently", p.warnings[0]);

    Message in;
    ASSERT_TRUE(session.get(kQueue, in, GetOptions()).ok());
    EXPECT_TRUE(in.headers.empty());
    std::vector<unsigned char> expected = rfh.data;
    expected.insert(expected.end(), out.body.begin(), out.body.end());
    EXPECT_EQ(expected, in.body);
}

TEST_F(MqBindingsTest, PutWarnsWhenBodyContinuesTheHeaderChain)
{
    auto dlh = mq_layout::dlh(mq_layout::Order::Little, 546, 1208, "MQHRF2");
    auto rfh = rfh2WithFolder();
    Message out = textMessage("");
    memcpy(out.mqmd.Format, MQFMT_DEAD_LETTER_HEADER, MQ_FORMAT_LENGTH);
    out.headers = dlh.data;
    out.body = rfh.data;    // the body itself starts with a valid RFH2
    Result p = session.put(kQueue, out, PutOptions());
    ASSERT_TRUE(p.ok()) << p.text;
    ASSERT_EQ(1u, p.warnings.size());
    EXPECT_EQ("headers do not match the MQMD format chain; a get splits them differently", p.warnings[0]);

    Message in;
    ASSERT_TRUE(session.get(kQueue, in, GetOptions()).ok());
    EXPECT_EQ(dlh.data.size() + rfh.data.size(), in.headers.size());
    EXPECT_GT(in.headers.size(), out.headers.size());
}

TEST_F(MqBindingsTest, GetReturnsHeaderWarnings)
{
    auto cih = mq_layout::cih(mq_layout::Order::Little, 2, MQCLT_TRANSACTION, 546, 999999, "MQSTR");
    Message out = textMessage("cics body");
    memcpy(out.mqmd.Format, MQFMT_CICS, MQ_FORMAT_LENGTH);
    out.headers = cih.data;
    ASSERT_TRUE(session.put(kQueue, out, PutOptions()).ok());

    Message in;
    Result r = session.get(kQueue, in, GetOptions());
    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_EQ("*** Errors parsing CICS header\r\n", r.warnings[0]);
    EXPECT_EQ(180u, in.headers.size());
    EXPECT_EQ("cics body", bodyText(in));
}

TEST_F(MqBindingsTest, BrowseKeepsHeadersPerMessage)
{
    auto rfh = rfh2WithFolder();
    Message withHeader = rfh2Message(rfh, MQFMT_RF_HEADER_2, "first");
    Message plain = textMessage("second");
    ASSERT_TRUE(session.put(kQueue, withHeader, PutOptions()).ok());
    ASSERT_TRUE(session.put(kQueue, plain, PutOptions()).ok());

    GetOptions browse;
    Message m;
    browse.mode = GetMode::BrowseFirst;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ(rfh.data, m.headers);
    EXPECT_EQ("first", bodyText(m));

    browse.mode = GetMode::BrowseNext;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_TRUE(m.headers.empty()) << "headers of the previous message must not linger";
    EXPECT_EQ("second", bodyText(m));

    browse.mode = GetMode::BrowsePrevious;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ(rfh.data, m.headers);
    EXPECT_EQ("first", bodyText(m));
}

TEST_F(MqBindingsTest, DeadLetterThenRfh2ChainIsSeparated)
{
    auto dlh = mq_layout::dlh(mq_layout::Order::Little, 546, 1208, "MQHRF2");
    auto rfh = rfh2WithFolder();
    Message out = textMessage("dead body");
    memcpy(out.mqmd.Format, MQFMT_DEAD_LETTER_HEADER, MQ_FORMAT_LENGTH);
    out.headers = dlh.data;
    out.headers.insert(out.headers.end(), rfh.data.begin(), rfh.data.end());
    ASSERT_TRUE(session.put(kQueue, out, PutOptions()).ok());

    GetOptions get;
    get.matchMsgId = true;
    memcpy(get.msgId, out.mqmd.MsgId, sizeof(get.msgId));
    Message in;
    Result r = session.get(kQueue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(172u + rfh.data.size(), in.headers.size());
    EXPECT_EQ(out.headers, in.headers);
    EXPECT_EQ("dead body", bodyText(in));
}

TEST_F(MqBindingsTest, SmallBufferStillSeparatesHeaders)
{
    auto rfh = rfh2WithFolder();
    Message out = rfh2Message(rfh, MQFMT_RF_HEADER_2, "after the header");
    ASSERT_TRUE(session.put(kQueue, out, PutOptions()).ok());

    GetOptions get;
    get.initialBufferSize = 16;
    Message in;
    Result r = session.get(kQueue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(rfh.data, in.headers);
    EXPECT_EQ("after the header", bodyText(in));
}

TEST_F(MqBindingsTest, ConvertedHeadersAreSeparatedByTheirConvertedLength)
{
    auto rfh = rfh2WithFolder();
    Message out = rfh2Message(rfh, MQFMT_RF_HEADER_2, "convert me");
    out.mqmd.CodedCharSetId = 1208;
    ASSERT_TRUE(session.put(kQueue, out, PutOptions()).ok());

    GetOptions get;
    get.convert = true;
    get.convertCcsid = 500;
    get.convertEncoding = MQENC_S390;
    Message in;
    Result r = session.get(kQueue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(500, in.mqmd.CodedCharSetId);

    // Observed on IBM MQ 9.4: the converted RFH2 keeps its length (72 bytes here), is
    // big endian with an EBCDIC StrucId, and the body follows as EBCDIC text.
    ASSERT_EQ(rfh.data.size(), in.headers.size());
    EXPECT_EQ(0xD9, in.headers[0]);
    EXPECT_EQ(0xC6, in.headers[1]);
    EXPECT_EQ(0xC8, in.headers[2]);
    EXPECT_EQ(0x40, in.headers[3]);
    EXPECT_EQ(rfh.data.size(), (size_t)(in.headers[8] << 24 | in.headers[9] << 16 | in.headers[10] << 8 | in.headers[11]));
    ASSERT_EQ(10u, in.body.size());
    EXPECT_EQ(0x83, in.body[0]);    // EBCDIC 'c'
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

TEST_F(MqBindingsTest, QueueInfoOnARealQueueAfterAPut)
{
    MQLONG before = 0;
    ASSERT_TRUE(session.depth(kQueue, before).ok());

    Message m = textMessage("queue info");
    ASSERT_TRUE(session.put(kQueue, m, PutOptions()).ok());

    QueueInfo info;
    Result r = session.queueInfo(kQueue, info);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());
    EXPECT_EQ(MQQT_LOCAL, info.type);
    EXPECT_EQ(before + 1, info.depth);
    EXPECT_GT(info.maxMsgLength, 0);
    EXPECT_EQ("", info.resolvedName);

    Result missing = session.queueInfo("RFHUTIL.TEST.NO.SUCH.QUEUE", info);
    EXPECT_EQ(MQRC_UNKNOWN_OBJECT_NAME, missing.rc);
}

namespace
{
    // Removes the queues a listing test defines, whatever the outcome of the test.
    struct ListQueuesCleanup
    {
        ~ListQueuesCleanup()
        {
            runMqsc("DELETE QLOCAL(RFHUTIL.TEST.LIST) PURGE\nDELETE QALIAS(RFHUTIL.TEST.LIST.ALIAS)\nDELETE QREMOTE(RFHUTIL.TEST.LIST.REMOTE)");
        }
    };

    bool contains(const std::vector<std::string> & names, const std::string & name)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }
}

TEST_F(MqBindingsTest, ListQueuesFindsATestQueue)
{
    ListQueuesCleanup cleanup;
    ASSERT_TRUE(runMqsc("DEFINE QLOCAL(RFHUTIL.TEST.LIST) REPLACE\n"
                        "DEFINE QALIAS(RFHUTIL.TEST.LIST.ALIAS) TARGET(RFHUTIL.TEST.LIST) REPLACE\n"
                        "DEFINE QREMOTE(RFHUTIL.TEST.LIST.REMOTE) RNAME(SOME.QUEUE) RQMNAME(OTHER.QM) REPLACE"));

    std::vector<std::string> names;
    Result r = session.listQueues(QueueListOptions(), names);
    if (r.rc == MQRC_NO_MSG_AVAILABLE)
    {
        GTEST_SKIP() << "the PCF command server of " << bindingsQm() << " does not answer (strmqcsv starts it); listQueues is covered by the fake tests: " << r.text;
    }
    ASSERT_TRUE(r.ok()) << r.text;
    for (const std::string & w : r.warnings)
    {
        ADD_FAILURE() << "warning: " << w;
    }

    EXPECT_TRUE(contains(names, "RFHUTIL.TEST.LIST")) << "local";
    EXPECT_TRUE(contains(names, "RFHUTIL.TEST.LIST.REMOTE")) << "remote";
    EXPECT_TRUE(contains(names, "RFHUTIL.TEST.LIST.ALIAS")) << "alias";
    EXPECT_TRUE(contains(names, "SYSTEM.ADMIN.COMMAND.QUEUE")) << "system queues are listed; hiding them is the caller's choice";
    EXPECT_FALSE(contains(names, "SYSTEM.DEFAULT.MODEL.QUEUE")) << "model queues are left out";
    for (const std::string & name : names)
    {
        EXPECT_NE(0u, name.find("RFHUTIL.REPLY.QUEUE")) << "the temporary reply queue is not listed: " << name;
    }

    // a second listing works on the same connection and the first one's reply queue is gone
    std::vector<std::string> again;
    ASSERT_TRUE(session.listQueues(QueueListOptions(), again).ok());
    EXPECT_EQ(names.size(), again.size());

    // only the alias queues
    QueueListOptions aliasOnly;
    aliasOnly.local = false;
    aliasOnly.remote = false;
    std::vector<std::string> aliases;
    ASSERT_TRUE(session.listQueues(aliasOnly, aliases).ok());
    EXPECT_TRUE(contains(aliases, "RFHUTIL.TEST.LIST.ALIAS"));
    EXPECT_FALSE(contains(aliases, "RFHUTIL.TEST.LIST"));
    EXPECT_FALSE(contains(aliases, "RFHUTIL.TEST.LIST.REMOTE"));
}

TEST_F(MqBindingsTest, BrowsePositionFollowsTheRealCursor)
{
    for (const char * text : {"p1", "p2", "p3"})
    {
        Message out = textMessage(text);
        ASSERT_TRUE(session.put(kQueue, out, PutOptions()).ok());
    }
    EXPECT_EQ(0, session.browsePosition()) << "a put starts no browse";

    GetOptions first;
    first.mode = GetMode::BrowseFirst;
    GetOptions next;
    next.mode = GetMode::BrowseNext;
    GetOptions prev;
    prev.mode = GetMode::BrowsePrevious;
    Message m;

    ASSERT_TRUE(session.get(kQueue, m, first).ok());
    EXPECT_EQ(1, session.browsePosition());
    ASSERT_TRUE(session.get(kQueue, m, next).ok());
    ASSERT_TRUE(session.get(kQueue, m, next).ok());
    EXPECT_EQ(3, session.browsePosition());
    EXPECT_EQ("p3", bodyText(m));

    Result end = session.get(kQueue, m, next);
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, end.rc);
    EXPECT_EQ(3, session.browsePosition()) << "the cursor stays on the last message";

    ASSERT_TRUE(session.get(kQueue, m, prev).ok());
    EXPECT_EQ(2, session.browsePosition());
    EXPECT_EQ("p2", bodyText(m));
    ASSERT_TRUE(session.get(kQueue, m, next).ok());
    EXPECT_EQ(3, session.browsePosition());
    EXPECT_EQ("p3", bodyText(m));

    // a destructive get on the browse handle: the position is unknown
    ASSERT_TRUE(session.get(kQueue, m, GetOptions()).ok());
    EXPECT_EQ("p1", bodyText(m));
    EXPECT_EQ(0, session.browsePosition());

    // another application empties the queue; a restarted browse finds nothing
    {
        MqSession other;
        ConnectOptions options;
        options.mode = Mode::Bindings;
        options.qmName = bindingsQm();
        ASSERT_TRUE(other.connect(options).ok());
        MQLONG purged = 0;
        ASSERT_TRUE(other.purge(kQueue, purged).ok());
        EXPECT_EQ(2, purged);
    }
    ASSERT_FALSE(session.get(kQueue, m, first).ok());
    EXPECT_EQ(0, session.browsePosition());

    // a browse next after the failed browse first returns the first new message
    Message again = textMessage("n1");
    ASSERT_TRUE(session.put(kQueue, again, PutOptions()).ok());
    ASSERT_TRUE(session.get(kQueue, m, next).ok());
    EXPECT_EQ("n1", bodyText(m));
    EXPECT_EQ(1, session.browsePosition());

    // an empty queue gives no position
    ASSERT_FALSE(session.get(kEmptyQueue, m, first).ok());
    EXPECT_EQ(0, session.browsePosition());

    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_EQ(0, session.browsePosition());
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

TEST_F(MqBindingsTest, UnlimitedWaitKeepsWaitingUntilCancelled)
{
    std::atomic<bool> cancel(false);
    GetOptions get;
    get.waitMs = MQWI_UNLIMITED;
    get.cancel = &cancel;

    std::thread canceller([&cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        cancel = true;
    });

    auto start = std::chrono::steady_clock::now();
    Message in;
    Result r = session.get(kEmptyQueue, in, get);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    canceller.join();

    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_NE(std::string::npos, r.text.find("wait cancelled"));
    EXPECT_GE(elapsed.count(), 1400);
    EXPECT_LT(elapsed.count(), 4000);
}

TEST_F(MqBindingsTest, NegativeWaitIsRejectedWithoutWaiting)
{
    GetOptions get;
    get.waitMs = -5;

    auto start = std::chrono::steady_clock::now();
    Message in;
    Result r = session.get(kEmptyQueue, in, get);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    EXPECT_FALSE(r.ok());
    EXPECT_EQ("Invalid wait interval", r.text);
    EXPECT_LT(elapsed.count(), 500);
}

TEST_F(MqBindingsTest, InvalidWaitKeepsBrowseHandleOfAnotherQueue)
{
    for (const char * text : { "one", "two" })
    {
        Message m = textMessage(text);
        ASSERT_TRUE(session.put(kQueue, m, PutOptions()).ok());
    }

    GetOptions browse;
    browse.mode = GetMode::BrowseFirst;
    Message m;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("one", bodyText(m));

    GetOptions invalid;
    invalid.waitMs = -5;
    EXPECT_EQ("Invalid wait interval", session.get(kEmptyQueue, m, invalid).text);

    browse.mode = GetMode::BrowseNext;
    Result r = session.get(kQueue, m, browse);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ("two", bodyText(m));
}

TEST_F(MqBindingsTest, CancelledBrowsePreviousLeavesMessageAlone)
{
    for (const char * text : { "one", "two" })
    {
        Message m = textMessage(text);
        ASSERT_TRUE(session.put(kQueue, m, PutOptions()).ok());
    }

    GetOptions browse;
    browse.mode = GetMode::BrowseFirst;
    Message m;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    browse.mode = GetMode::BrowseNext;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("two", bodyText(m));

    std::atomic<bool> cancel(true);
    browse.mode = GetMode::BrowsePrevious;
    browse.cancel = &cancel;
    Result r = session.get(kQueue, m, browse);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("Browse previous cancelled before reaching position 1", r.text);
    EXPECT_EQ("two", bodyText(m));
}

TEST_F(MqBindingsTest, BrowseRetryWithUniqueMsgIdsAdvances)
{
    const std::string bodies[] = { "x", std::string(2000, 'b'), "z" };
    for (const std::string & body : bodies)
    {
        Message m = textMessage(body);
        m.mqmd.CodedCharSetId = 1208;
        ASSERT_TRUE(session.put(kQueue, m, PutOptions()).ok());
    }

    GetOptions browse;
    browse.mode = GetMode::BrowseFirst;
    browse.convert = true;
    browse.convertCcsid = 1200;
    browse.initialBufferSize = 2000;

    Message m;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ(2u, m.body.size());

    browse.mode = GetMode::BrowseNext;
    Result second = session.get(kQueue, m, browse);
    ASSERT_TRUE(second.ok()) << second.text;
    EXPECT_EQ(4000u, m.body.size());

    Result third = session.get(kQueue, m, browse);
    ASSERT_TRUE(third.ok()) << third.text;
    EXPECT_EQ(2u, m.body.size()) << "the browse must reach z";

    Result fourth = session.get(kQueue, m, browse);
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, fourth.rc) << fourth.text;
}

TEST_F(MqBindingsTest, TwoSessionsAreIndependent)
{
    ConnectOptions options;
    options.mode = Mode::Bindings;
    options.qmName = bindingsQm();

    MqSession a;
    MqSession b;
    ASSERT_TRUE(a.connect(options).ok());
    Result cb = b.connect(options);
    EXPECT_EQ(MQCC_OK, cb.cc) << cb.text;
    EXPECT_NE(MQRC_ALREADY_CONNECTED, cb.rc);
    ASSERT_TRUE(b.disconnect().ok());

    Message out = textMessage("still connected");
    Result put = a.put(kQueue, out, PutOptions());
    ASSERT_TRUE(put.ok()) << put.text;
    GetOptions get;
    Message in;
    Result r = a.get(kQueue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ("still connected", bodyText(in));
}

TEST_F(MqBindingsTest, CloseFailureKeepsHandle)
{
    Message out = textMessage("exclusive");
    ASSERT_TRUE(session.put(kExclQueue, out, PutOptions()).ok());

    GetOptions browse;
    browse.mode = GetMode::BrowseFirst;
    Message in;
    ASSERT_TRUE(session.get(kExclQueue, in, browse).ok());

    Result bad = session.closeQueue(CloseOption::Delete);
    EXPECT_EQ(MQRC_OPTION_NOT_VALID_FOR_TYPE, bad.rc) << bad.text;

    Result good = session.closeQueue();
    EXPECT_TRUE(good.ok()) << good.text;

    Result r = session.get(kExclQueue, in, GetOptions());
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ("exclusive", bodyText(in));
}

TEST_F(MqBindingsTest, ConvertTargetDoesNotDrift)
{
    Message first;
    memcpy(first.mqmd.Format, MQFMT_NONE, MQ_FORMAT_LENGTH);
    first.mqmd.CodedCharSetId = 500;
    first.body.push_back(0xC1);
    ASSERT_TRUE(session.put(kQueue, first, PutOptions()).ok());

    Message second = textMessage("hello");
    second.mqmd.CodedCharSetId = 1208;
    ASSERT_TRUE(session.put(kQueue, second, PutOptions()).ok());

    GetOptions get;
    get.convert = true;
    get.convertCcsid = 819;

    Message in;
    Result r1 = session.get(kQueue, in, get);
    EXPECT_TRUE(r1.ok()) << r1.text;
    EXPECT_EQ(MQRC_FORMAT_ERROR, r1.rc) << r1.text;

    Result r2 = session.get(kQueue, in, get);
    ASSERT_TRUE(r2.ok()) << r2.text;
    EXPECT_EQ("hello", bodyText(in));
    EXPECT_EQ(819, in.mqmd.CodedCharSetId);
}

TEST_F(MqBindingsTest, ConvertExpansionGrowsBufferOnBrowse)
{
    Message out = textMessage(std::string(2000, 'a'));
    out.mqmd.CodedCharSetId = 1208;
    ASSERT_TRUE(session.put(kQueue, out, PutOptions()).ok());

    GetOptions browse;
    browse.mode = GetMode::BrowseFirst;
    browse.convert = true;
    browse.convertCcsid = 1200;
    browse.initialBufferSize = 2500;

    Message in;
    Result r = session.get(kQueue, in, browse);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(MQRC_NONE, r.rc) << r.text;
    EXPECT_EQ(4000u, in.body.size());
    EXPECT_EQ(1200, in.mqmd.CodedCharSetId);
}

TEST_F(MqBindingsTest, OwnHandleConflictGivesClearText)
{
    Message out = textMessage("held");
    ASSERT_TRUE(session.put(kExclQueue, out, PutOptions()).ok());

    GetOptions browse;
    browse.mode = GetMode::BrowseFirst;
    Message in;
    ASSERT_TRUE(session.get(kExclQueue, in, browse).ok());

    MQLONG purged = 0;
    Result r = session.purge(kExclQueue, purged);
    EXPECT_EQ(MQRC_OBJECT_IN_USE, r.rc) << r.text;
    EXPECT_NE(std::string::npos, r.text.find(" - this session already has the queue open; close it first")) << r.text;

    MQLONG depth = 0;
    Result d = session.depth(kExclQueue, depth);
    EXPECT_TRUE(d.ok()) << d.text;   // inquire opens do not conflict with exclusive input
}

TEST_F(MqBindingsTest, BrowsePositionUnknownAfterDestructiveGet)
{
    for (const char * text : { "A", "B", "C" })
    {
        Message m = textMessage(text);
        ASSERT_TRUE(session.put(kQueue, m, PutOptions()).ok());
    }

    GetOptions browse;
    browse.mode = GetMode::BrowseFirst;
    Message a;
    ASSERT_TRUE(session.get(kQueue, a, browse).ok());
    EXPECT_EQ("A", bodyText(a));

    GetOptions remove;
    remove.matchMsgId = true;
    memcpy(remove.msgId, a.mqmd.MsgId, sizeof(remove.msgId));
    Message removed;
    ASSERT_TRUE(session.get(kQueue, removed, remove).ok());

    browse.mode = GetMode::BrowseNext;
    Message next;
    ASSERT_TRUE(session.get(kQueue, next, browse).ok());
    EXPECT_TRUE(bodyText(next) == "B" || bodyText(next) == "C");

    browse.mode = GetMode::BrowsePrevious;
    Result r = session.get(kQueue, next, browse);
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_NE(std::string::npos, r.text.find(" - browse position unknown after a destructive get; browse first")) << r.text;
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
