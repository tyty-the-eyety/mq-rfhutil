// ListQueuesTests.cpp - MqSession::listQueues and the PCF reply parser, driven by the fake MQI.
//
// Every PCF message below is built byte by byte from the IBM layouts (MQCFH 36 bytes: Type, StrucLength,
// Version, Command, MsgSeqNumber, Control, CompCode, Reason, ParameterCount; MQCFIN 16 bytes: Type,
// StrucLength, Parameter, Value; MQCFST 20 bytes plus the string padded to 4: Type, StrucLength, Parameter,
// CodedCharSetId, StringLength; MQCFIL 16 bytes plus 4 per value: Type, StrucLength, Parameter, Count,
// Values). The tests do not use the implementation's builders, so an error shared by the request
// builder and the reader cannot hide.
//
// The fake plays the command server: a hook on SYSTEM.ADMIN.COMMAND.QUEUE reads which queue type a
// request asks for and enqueues the scripted reply messages on the request's ReplyToQ.

#include <gtest/gtest.h>

#include <cmqc.h>
#include <cmqcfc.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "rfh_mq_session.h"
#include "rfh_pcf_queues.h"

using namespace rfh;

#include "fake_mq.h"

namespace
{
    using rfh_test::fakeApi;
    using Bytes = std::vector<unsigned char>;

    rfh_test::FakeMq & fake = rfh_test::fakeMq();

    const char * kAdminQueue = "SYSTEM.ADMIN.COMMAND.QUEUE";

    // Writes 32-bit integers in the byte order of a message.
    class Wire
    {
    public:
        explicit Wire(bool bigEndian = false) : big(bigEndian) {}

        Wire & i32(long value)
        {
            const unsigned long u = static_cast<unsigned long>(value) & 0xFFFFFFFFul;
            for (int k = 0; k < 4; k++)
            {
                const int shift = big ? (24 - 8 * k) : (8 * k);
                data.push_back(static_cast<unsigned char>((u >> shift) & 0xFF));
            }
            return *this;
        }

        Wire & text(const std::string & s)
        {
            data.insert(data.end(), s.begin(), s.end());
            return *this;
        }

        Wire & padTo4()
        {
            while (data.size() % 4 != 0)
            {
                data.push_back(0);
            }
            return *this;
        }

        Bytes data;

    private:
        bool big;
    };

    void header(Wire & w, long type, long control, long compCode, long reason, long count)
    {
        w.i32(type).i32(36).i32(3).i32(13).i32(1).i32(control).i32(compCode).i32(reason).i32(count);
    }

    void integer(Wire & w, long parameter, long value)
    {
        w.i32(3).i32(16).i32(parameter).i32(value);
    }

    // An MQCFST whose StrucLength and StringLength are given, so a test can make them lie.
    void stringParameter(Wire & w, long parameter, const std::string & text, long ccsid, long structLength, long stringLength)
    {
        w.i32(4).i32(structLength).i32(parameter).i32(ccsid).i32(stringLength).text(text).padTo4();
    }

    void stringParameter(Wire & w, long parameter, const std::string & text, long ccsid = 819)
    {
        const long padded = static_cast<long>((text.size() + 3) / 4 * 4);
        stringParameter(w, parameter, text, ccsid, 20 + padded, static_cast<long>(text.size()));
    }

    std::string blankPadded(const std::string & name, size_t width = 48)
    {
        return name + std::string(width - name.size(), ' ');
    }

    // One reply as the command server sends it for an Inquire Queue: the name, then the type.
    Bytes queueReply(const std::string & name, long queueType, long control, long messageType = MQCFT_XR_ITEM, bool bigEndian = false)
    {
        Wire w(bigEndian);
        header(w, messageType, control, 0, 0, 2);
        stringParameter(w, 2016, blankPadded(name));
        integer(w, 20, queueType);
        return w.data;
    }

    Bytes bareReply(long messageType, long control, long compCode, long reason)
    {
        Wire w;
        header(w, messageType, control, compCode, reason, 0);
        return w.data;
    }

    std::string ebcdic(const std::string & ascii)
    {
        std::string out;
        for (char c : ascii)
        {
            if (c >= 'A' && c <= 'I') out += static_cast<char>(0xC1 + (c - 'A'));
            else if (c >= 'J' && c <= 'R') out += static_cast<char>(0xD1 + (c - 'J'));
            else if (c >= 'S' && c <= 'Z') out += static_cast<char>(0xE2 + (c - 'S'));
            else if (c >= '0' && c <= '9') out += static_cast<char>(0xF0 + (c - '0'));
            else if (c == '.') out += static_cast<char>(0x4B);
            else if (c == ' ') out += static_cast<char>(0x40);
            else out += '?';
        }
        return out;
    }

    // The bytes of a message in a buffer of exactly its size, so the guard allocator catches an over-read.
    struct ExactBuffer
    {
        explicit ExactBuffer(const Bytes & bytes) : size(bytes.size()), data(new unsigned char[bytes.size() == 0 ? 1 : bytes.size()])
        {
            if (!bytes.empty())
            {
                memcpy(data.get(), bytes.data(), bytes.size());
            }
        }

        size_t size;
        std::unique_ptr<unsigned char[]> data;
    };

    pcf::QueueReply parse(const Bytes & bytes, MQLONG encoding = MQENC_NATIVE, MQLONG ccsid = 819)
    {
        ExactBuffer exact(bytes);
        return pcf::parseQueueReply(exact.data.get(), exact.size, encoding, ccsid);
    }

    struct Scripted
    {
        std::vector<Bytes> messages;
        MQLONG encoding = MQENC_NATIVE;
        MQLONG ccsid = 819;
    };

    class ListQueues : public ::testing::Test
    {
    protected:
        ListQueues() : session(fakeApi()) {}

        void SetUp() override
        {
            fake.reset();
            options.replyWaitMs = 50;
            fake.onPut[kAdminQueue] = [this](const rfh_test::FakeMessage & request) { serve(request); };
        }

        void connect()
        {
            ConnectOptions connectOptions;
            connectOptions.mode = Mode::Bindings;
            Result r = session.connect(connectOptions);
            ASSERT_TRUE(r.ok()) << r.text;
        }

        // The command server: answers a request with the messages scripted for its queue type.
        void serve(const rfh_test::FakeMessage & request)
        {
            ASSERT_GE(request.data.size(), 76u);
            // Type 1 starts at offset 36 + 24, the MQCFIN of MQIA_Q_TYPE; its Value is the last of four integers
            const unsigned char * v = &request.data[36 + 24 + 12];
            const long type = static_cast<long>(v[0] | (v[1] << 8) | (v[2] << 16) | (v[3] << 24));
            asked.push_back(type);

            std::string replyTo(request.md.ReplyToQ, strnlen(request.md.ReplyToQ, sizeof(request.md.ReplyToQ)));
            while (!replyTo.empty() && replyTo.back() == ' ')
            {
                replyTo.pop_back();
            }

            Scripted none;     // a queue manager with no queue of the type asked for answers "none found"
            none.messages = {bareReply(MQCFT_XR_ITEM, 1, MQCC_FAILED, 3200)};
            const Scripted & scripted = (script.count(type) != 0) ? script[type] : none;
            if (silent)
            {
                return;
            }
            for (const Bytes & message : scripted.messages)
            {
                rfh_test::FakeMessage m;
                m.md.Encoding = scripted.encoding;
                m.md.CodedCharSetId = scripted.ccsid;
                m.data = message;
                fake.queue(replyTo).messages.push_back(m);
            }
        }

        Result list(std::vector<std::string> & names)
        {
            return session.listQueues(options, names);
        }

        bool anyWarningContains(const Result & r, const std::string & text)
        {
            for (const std::string & w : r.warnings)
            {
                if (w.find(text) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        // Nothing may stay open or exist after a listing, whatever its outcome.
        void expectNoLeaks()
        {
            EXPECT_TRUE(fake.liveHandles.empty()) << fake.liveHandles.size() << " handle(s) left open";
            for (std::map<std::string, rfh_test::FakeQueue>::const_iterator it = fake.queues.begin(); it != fake.queues.end(); ++it)
            {
                EXPECT_FALSE(it->second.dynamic) << "dynamic queue " << it->first << " was not deleted";
            }
        }

        MqSession session;
        QueueListOptions options;
        std::map<long, Scripted> script;
        std::vector<long> asked;
        bool silent = false;    // the command server does not answer
    };

    const long kLocal = MQQT_LOCAL;
    const long kRemote = MQQT_REMOTE;
    const long kAlias = MQQT_ALIAS;
}

// ---------------------------------------------------------------------------------------------------
// The request (DataArea::buildPCFRequestQueues)
// ---------------------------------------------------------------------------------------------------

TEST_F(ListQueues, TheRequestHasTheWindowsLayoutForAnExtendedQueueManager)
{
    connect();
    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());

    ASSERT_EQ(3u, fake.putLog.size());
    EXPECT_EQ(kAdminQueue, fake.putLog[0].queue);

    Wire expected;
    header(expected, 16 /* MQCFT_COMMAND_XR */, 1 /* LAST */, 0, 0, 3);
    stringParameter(expected, 2016, "*", 0 /* MQCCSI_DEFAULT */);   // StrucLength 24, StringLength 1, padded to 4
    integer(expected, 20, 1);                                       // MQIA_Q_TYPE = MQQT_LOCAL
    expected.i32(5).i32(24).i32(1002).i32(2).i32(2016).i32(20);     // MQCFIL MQIACF_Q_ATTRS = { MQCA_Q_NAME, MQIA_Q_TYPE }
    ASSERT_EQ(100u, expected.data.size());
    // the header's Version is 3 for the extended form
    EXPECT_EQ(expected.data, fake.putLog[0].message.data);

    // the second and third requests differ only in the queue type
    Bytes remote = expected.data;
    remote[36 + 24 + 12] = 6;       // MQQT_REMOTE
    Bytes alias = expected.data;
    alias[36 + 24 + 12] = 3;        // MQQT_ALIAS
    EXPECT_EQ(remote, fake.putLog[1].message.data);
    EXPECT_EQ(alias, fake.putLog[2].message.data);
}

TEST_F(ListQueues, TheRequestUsesAPlainCommandHeaderBelowCommandLevel600)
{
    fake.commandLevel = 530;
    connect();
    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());

    ASSERT_FALSE(fake.putLog.empty());
    const Bytes & request = fake.putLog[0].message.data;
    ASSERT_EQ(100u, request.size());
    EXPECT_EQ(1, request[0]) << "MQCFT_COMMAND";
    EXPECT_EQ(1, request[8]) << "MQCFH_VERSION_1";
}

TEST_F(ListQueues, TheClusterOptionAddsTheClusterInfoFilter)
{
    options.cluster = true;
    options.remote = false;
    options.alias = false;
    connect();
    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());

    ASSERT_EQ(1u, fake.putLog.size());
    Wire expected;
    header(expected, 16, 1, 0, 0, 4);
    stringParameter(expected, 2016, "*", 0);
    integer(expected, 20, 1);
    integer(expected, 1083, 0);                                     // MQIACF_CLUSTER_INFO
    expected.i32(5).i32(24).i32(1002).i32(2).i32(2016).i32(20);
    ASSERT_EQ(116u, expected.data.size());
    EXPECT_EQ(expected.data, fake.putLog[0].message.data);
}

TEST_F(ListQueues, TheRequestDescriptorIsANonPersistentRequestWithTheReplyQueue)
{
    connect();
    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());

    ASSERT_FALSE(fake.putLog.empty());
    const MQMD & md = fake.putLog[0].message.md;
    EXPECT_EQ(0, memcmp(md.Format, MQFMT_ADMIN, MQ_FORMAT_LENGTH));
    EXPECT_EQ(MQMT_REQUEST, md.MsgType);
    EXPECT_EQ(MQPER_NOT_PERSISTENT, md.Persistence);
    EXPECT_EQ(100, md.Expiry) << "ten times the 50 ms wait is below the 10 second floor, in tenths of a second";
    EXPECT_NE(0, fake.putLog[0].pmoOptions & MQPMO_NO_SYNCPOINT);

    ASSERT_EQ(2u, fake.openLog.size()) << "the reply queue and the admin queue; connect opened the queue manager object";
    std::string replyName;
    for (const rfh_test::OpenRecord & open : fake.openLog)
    {
        if (open.name == "SYSTEM.DEFAULT.MODEL.QUEUE")
        {
            replyName = open.openedAs;
        }
    }
    ASSERT_FALSE(replyName.empty());
    EXPECT_EQ(replyName, std::string(md.ReplyToQ, replyName.size()));
    EXPECT_EQ(' ', md.ReplyToQ[replyName.size()]) << "blank padded";
}

TEST_F(ListQueues, TheRequestExpiryIsTenTimesTheReplyWaitInTenthsOfASecond)
{
    struct Case
    {
        MQLONG waitMs;
        MQLONG expiry;
    };
    // wait x 10, in tenths of a second (wait / 100 x 10): 3000 ms is 30 tenths, so 300 tenths = 30 s;
    // at least 100 (10 s) so that a very short wait does not make a live server's answer impossible;
    // at most 36000 (1 hour); a wait that never ends (negative) gets the cap, never MQEI_UNLIMITED
    const Case cases[] = { { 3000, 300 }, { 60000, 6000 }, { 0, 100 }, { 50, 100 }, { 2000000, 36000 },
                           { MQWI_UNLIMITED, 36000 }, { 2147483647, 36000 } };
    for (const Case & c : cases)
    {
        fake.reset();
        fake.onPut[kAdminQueue] = [this](const rfh_test::FakeMessage & request) { serve(request); };
        MqSession local(fakeApi());
        ConnectOptions connectOptions;
        connectOptions.mode = Mode::Bindings;
        ASSERT_TRUE(local.connect(connectOptions).ok());

        options.replyWaitMs = c.waitMs;
        std::vector<std::string> names;
        ASSERT_TRUE(local.listQueues(options, names).ok()) << c.waitMs;
        ASSERT_EQ(3u, fake.putLog.size());
        for (const auto & put : fake.putLog)
        {
            EXPECT_EQ(c.expiry, put.message.md.Expiry) << "wait " << c.waitMs << " ms";
            EXPECT_NE(MQEI_UNLIMITED, put.message.md.Expiry);
        }
    }
}

TEST_F(ListQueues, OnlyTheRequestedTypesAreAskedFor)
{
    options.local = false;
    options.alias = false;
    connect();
    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());
    EXPECT_EQ((std::vector<long>{kRemote}), asked);

    asked.clear();
    options = QueueListOptions();
    options.replyWaitMs = 50;
    options.remote = false;
    ASSERT_TRUE(list(names).ok());
    EXPECT_EQ((std::vector<long>{kLocal, kAlias}), asked);
}

// ---------------------------------------------------------------------------------------------------
// The names (DataArea::processQueuesReply)
// ---------------------------------------------------------------------------------------------------

TEST_F(ListQueues, ReturnsLocalRemoteAndAliasNames)
{
    script[kLocal].messages = {queueReply("APP.IN", kLocal, 0), queueReply("SYSTEM.ADMIN.COMMAND.QUEUE", kLocal, 1)};
    script[kRemote].messages = {queueReply("TO.OTHER.QM", kRemote, 1)};
    script[kAlias].messages = {queueReply("APP.ALIAS", kAlias, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());
    EXPECT_EQ((std::vector<std::string>{"APP.IN", "SYSTEM.ADMIN.COMMAND.QUEUE", "TO.OTHER.QM", "APP.ALIAS"}), names)
        << "local, remote, alias in that order; SYSTEM names are kept for the caller to hide";
    EXPECT_EQ((std::vector<long>{kLocal, kRemote, kAlias}), asked);
    expectNoLeaks();
}

TEST_F(ListQueues, ModelQueuesTheReplyQueueAndDuplicatesAreLeftOut)
{
    fake.dynamicQueueName = "RFHUTIL.REPLY.QUEUE.ABC";
    script[kLocal].messages = {queueReply("APP.IN", kLocal, 0), queueReply("SYSTEM.DEFAULT.MODEL.QUEUE", MQQT_MODEL, 0),
                               queueReply("RFHUTIL.REPLY.QUEUE.ABC", kLocal, 0), queueReply("APP.IN", kLocal, 1)};
    script[kAlias].messages = {queueReply("APP.IN", kAlias, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"APP.IN"}), names) << "the port keeps one entry per name (Windows: per type letter and name); the reply queue and models are skipped";
}

TEST_F(ListQueues, ClusterAndUnrecognisedQueueTypesAreListed)
{
    options.cluster = true;
    script[kLocal].messages = {queueReply("CLUS.Q", MQQT_CLUSTER, 0), queueReply("ODD.Q", 77, 1)};
    connect();

    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());
    EXPECT_EQ((std::vector<std::string>{"CLUS.Q", "ODD.Q"}), names) << "Windows prefixes cluster queues with C and others with a blank";
}

TEST_F(ListQueues, MoreThanOneReplyMessageEndsWithTheLastFlag)
{
    script[kLocal].messages = {queueReply("Q1", kLocal, 0), queueReply("Q2", kLocal, 0), queueReply("Q3", kLocal, 1)};
    script[kRemote].messages = {queueReply("R1", kRemote, 0), queueReply("R2", kRemote, 1)};
    script[kAlias].messages = {queueReply("A1", kAlias, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"Q1", "Q2", "Q3", "R1", "R2", "A1"}), names);
    EXPECT_EQ(6, fake.getCalls) << "one get per reply message; nothing is read after the last";
    EXPECT_EQ(0, fake.lastWaitedMs);
    expectNoLeaks();
}

TEST_F(ListQueues, ALastMessageWithNoQueueAndOtherMessageTypesAreIgnored)
{
    script[kLocal].messages = {queueReply("Q1", kLocal, 0), bareReply(MQCFT_XR_SUMMARY, 0, 0, 0), bareReply(MQCFT_XR_MSG, 0, 0, 0),
                               bareReply(MQCFT_XR_ITEM, 1, 0, 0)};
    script[kRemote].messages = {queueReply("R1", kRemote, 1, MQCFT_RESPONSE)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());
    EXPECT_EQ((std::vector<std::string>{"Q1", "R1"}), names);
}

TEST_F(ListQueues, AnEmptyResultIsSuccess)
{
    // A queue manager with no such queue answers with a failed header whose reason is MQRCCF_NONE_FOUND
    for (long type : {kLocal, kRemote, kAlias})
    {
        script[type].messages = {bareReply(MQCFT_XR_ITEM, 1, MQCC_FAILED, 3200)};
    }
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());
    EXPECT_TRUE(names.empty());
    expectNoLeaks();
}

TEST_F(ListQueues, QueueNamesAreReadAsBlankTerminatedAndAtMost48Characters)
{
    const std::string exactly48 = std::string("A") + std::string(46, 'B') + "C";
    ASSERT_EQ(48u, exactly48.size());
    script[kLocal].messages = {queueReply(exactly48, kLocal, 1)};
    connect();

    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());
    EXPECT_EQ((std::vector<std::string>{exactly48}), names);
}

TEST_F(ListQueues, AReplyQueueNameOf48CharactersStillGetsItsReplies)
{
    fake.dynamicQueueName = std::string("RFHUTIL.REPLY.QUEUE.") + std::string(28, 'X');
    ASSERT_EQ(48u, fake.dynamicQueueName.size());
    script[kLocal].messages = {queueReply("Q1", kLocal, 1)};
    connect();

    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());
    EXPECT_EQ((std::vector<std::string>{"Q1"}), names);
    ASSERT_FALSE(fake.putLog.empty());
    EXPECT_EQ(fake.dynamicQueueName, std::string(fake.putLog[0].message.md.ReplyToQ, 48))
        << "Windows leaves ReplyToQ empty for a 48 character name (strlen < 48), so no reply could ever come";
}

TEST_F(ListQueues, NamesFromAForeignByteOrderAndEbcdicAreConverted)
{
    script[kLocal].messages = {queueReply("BIG.ENDIAN", kLocal, 1, MQCFT_XR_ITEM, true)};
    script[kLocal].encoding = 0x111;    // MQENC_INTEGER_NORMAL | decimal and float normal: the order the other way round
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 2);
    stringParameter(w, 2016, ebcdic("EBCDIC.QUEUE") + std::string(36, '\x40'));
    integer(w, 20, kRemote);
    script[kRemote].messages = {w.data};
    script[kRemote].ccsid = 37;
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"BIG.ENDIAN", "EBCDIC.QUEUE"}), names);
}

// ---------------------------------------------------------------------------------------------------
// The flow, handles and cleanup (OpenTempQ, openAdminQ, loadNames)
// ---------------------------------------------------------------------------------------------------

TEST_F(ListQueues, ReplyQueueIsClosedAndDeleted)
{
    fake.dynamicQueueName = "RFHUTIL.REPLY.QUEUE.ONE";
    script[kLocal].messages = {queueReply("Q1", kLocal, 1)};
    connect();

    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());

    EXPECT_EQ((std::vector<std::string>{"RFHUTIL.REPLY.QUEUE.ONE"}), fake.deletedQueues);
    expectNoLeaks();

    // the model open: template RFHUTIL.REPLY.QUEUE*, input shared
    bool sawModel = false;
    bool sawAdmin = false;
    for (const rfh_test::OpenRecord & open : fake.openLog)
    {
        if (open.name == "SYSTEM.DEFAULT.MODEL.QUEUE")
        {
            sawModel = true;
            EXPECT_EQ("RFHUTIL.REPLY.QUEUE*", open.dynamicName);
            EXPECT_NE(0, open.options & MQOO_INPUT_SHARED);
        }
        if (open.name == kAdminQueue)
        {
            sawAdmin = true;
            EXPECT_NE(0, open.options & MQOO_OUTPUT);
            EXPECT_NE(0, open.options & MQOO_FAIL_IF_QUIESCING);
        }
    }
    EXPECT_TRUE(sawModel);
    EXPECT_TRUE(sawAdmin);

    // the reply queue is closed with MQCO_DELETE_PURGE, the admin queue without a delete option
    for (const rfh_test::CloseRecord & close : fake.closeLog)
    {
        if (close.queue == "RFHUTIL.REPLY.QUEUE.ONE")
        {
            EXPECT_EQ(MQCO_DELETE_PURGE, close.options);
        }
        if (close.queue == kAdminQueue)
        {
            EXPECT_EQ(MQCO_NONE, close.options);
        }
    }
    EXPECT_TRUE(session.isConnected());
}

TEST_F(ListQueues, EachListingCreatesAndDeletesItsOwnReplyQueue)
{
    script[kLocal].messages = {queueReply("Q1", kLocal, 1)};
    connect();

    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());
    ASSERT_TRUE(list(names).ok());
    ASSERT_TRUE(list(names).ok());
    EXPECT_EQ(3u, fake.deletedQueues.size());
    EXPECT_NE(fake.deletedQueues[0], fake.deletedQueues[1]);
    expectNoLeaks();
    EXPECT_EQ((std::vector<std::string>{"Q1"}), names) << "names are replaced, not appended";
}

TEST_F(ListQueues, AFailureToOpenTheAdminQueueIsReported)
{
    for (MQLONG rc : {MQRC_NOT_AUTHORIZED, MQRC_UNKNOWN_OBJECT_NAME})
    {
        fake.reset();
        fake.onPut[kAdminQueue] = [this](const rfh_test::FakeMessage & request) { serve(request); };
        fake.openRcByName[kAdminQueue] = rc;
        MqSession local(fakeApi());
        ConnectOptions connectOptions;
        ASSERT_TRUE(local.connect(connectOptions).ok());

        std::vector<std::string> names = {"stale"};
        Result r = local.listQueues(options, names);
        EXPECT_FALSE(r.ok());
        EXPECT_EQ(MQCC_FAILED, r.cc);
        EXPECT_EQ(rc, r.rc);
        EXPECT_EQ("Open for admin queue failed rc=" + std::to_string(rc), r.text);
        EXPECT_TRUE(names.empty());
        EXPECT_EQ(0, fake.putCalls);
        EXPECT_EQ(1u, fake.deletedQueues.size()) << "the reply queue opened first is deleted";
        EXPECT_TRUE(fake.liveHandles.empty());
        EXPECT_TRUE(local.isConnected());
    }
}

TEST_F(ListQueues, AFailureToOpenTheReplyQueueIsReported)
{
    fake.openRcByName["SYSTEM.DEFAULT.MODEL.QUEUE"] = MQRC_NOT_AUTHORIZED;
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(MQRC_NOT_AUTHORIZED, r.rc);
    EXPECT_EQ("Open for temporary reply queue failed rc=2035", r.text);
    EXPECT_EQ(0, fake.putCalls);
    expectNoLeaks();
}

TEST_F(ListQueues, AZosQueueManagerBelowV6HasNoPcf)
{
    fake.platform = MQPL_MVS;
    fake.commandLevel = 530;
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ("MQ on 390 does not support PCF prior to V6", r.text);
    EXPECT_EQ(0, fake.putCalls);
    EXPECT_EQ(0, fake.dynamicQueueCount) << "no reply queue is created";
    expectNoLeaks();

    // at V6 it is allowed; the level is learned when the session connects
    ASSERT_TRUE(session.disconnect().ok());
    fake.commandLevel = 600;
    connect();
    script[kLocal].messages = {queueReply("Q1", kLocal, 1)};
    EXPECT_TRUE(list(names).ok());
}

TEST_F(ListQueues, AFailedQueueManagerInquiryIsAWarningAndTheListingGoesOn)
{
    fake.inqRcBySelector[MQIA_COMMAND_LEVEL] = MQRC_SELECTOR_ERROR;
    script[kLocal].messages = {queueReply("Q1", kLocal, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"Q1"}), names);
    EXPECT_TRUE(anyWarningContains(r, "command level")) << "the unknown level is reported";
    ASSERT_FALSE(fake.putLog.empty());
    EXPECT_EQ(16, fake.putLog[0].message.data[0]) << "an unknown level is treated as modern";
}

TEST_F(ListQueues, TheAlternateUserIsPassedToBothOpensAndCutToTwelveCharacters)
{
    options.alternateUser = "ABCDEFGHIJKLMNOP";
    script[kLocal].messages = {queueReply("Q1", kLocal, 1)};
    connect();

    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());
    int seen = 0;
    for (const rfh_test::OpenRecord & open : fake.openLog)
    {
        if (open.objectType == MQOT_Q_MGR)
        {
            continue;
        }
        seen++;
        EXPECT_NE(0, open.options & MQOO_ALTERNATE_USER_AUTHORITY) << open.name;
        EXPECT_EQ("ABCDEFGHIJKL", open.alternateUser) << open.name;
    }
    EXPECT_EQ(2, seen);
}

TEST_F(ListQueues, TheQueueManagerFactsComeFromTheConnectNotAnotherInquiry)
{
    script[kLocal].messages = {queueReply("Q1", kLocal, 1)};
    connect();
    ASSERT_EQ(1, fake.qmgrOpenCalls) << "connect opens the queue manager object once";
    ASSERT_EQ(3, fake.qmgrInqCalls);

    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());
    EXPECT_EQ(1, fake.qmgrOpenCalls) << "listQueues does not open the queue manager object again";
    EXPECT_EQ(3, fake.qmgrInqCalls) << "and does not inquire on it";
}

TEST_F(ListQueues, TheSessionsOwnQueueIsNotDisturbed)
{
    connect();
    Message m;
    m.mqmd = MQMD2{MQMD2_DEFAULT};
    memcpy(m.mqmd.Format, MQFMT_STRING, MQ_FORMAT_LENGTH);
    m.body = {'x'};
    ASSERT_TRUE(session.put("APP.OUT", m, PutOptions()).ok());
    ASSERT_EQ(1u, fake.liveHandles.size());
    const int opensBefore = fake.openCalls;

    script[kLocal].messages = {queueReply("Q1", kLocal, 1)};
    std::vector<std::string> names;
    ASSERT_TRUE(list(names).ok());
    EXPECT_EQ(1u, fake.liveHandles.size()) << "only the session's own queue stays open";
    EXPECT_GT(fake.openCalls, opensBefore);

    const int opensAfterList = fake.openCalls;
    ASSERT_TRUE(session.put("APP.OUT", m, PutOptions()).ok());
    EXPECT_EQ(opensAfterList, fake.openCalls) << "the put reuses its handle";
}

TEST_F(ListQueues, NotConnectedFails)
{
    std::vector<std::string> names = {"stale"};
    Result r = list(names);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("Not connected to a queue manager", r.text);
    EXPECT_TRUE(names.empty());
    EXPECT_EQ(0, fake.openCalls);
}

// ---------------------------------------------------------------------------------------------------
// Failures of the command server, timeouts and lost connections (DataArea::getListOfNames)
// ---------------------------------------------------------------------------------------------------

TEST_F(ListQueues, ACommandServerRejectionIsReportedWithItsReason)
{
    Wire rejection;
    header(rejection, MQCFT_XR_ITEM, 1, MQCC_FAILED, MQRCCF_CFH_COMMAND_ERROR, 1);
    integer(rejection, MQIACF_PARAMETER_ID, 2016);
    script[kLocal].messages = {rejection.data};
    script[kRemote].messages = {queueReply("R1", kRemote, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(3007, r.rc);
    EXPECT_NE(std::string::npos, r.text.find("3007")) << r.text;
    EXPECT_NE(std::string::npos, r.text.find("2016")) << "the parameter the server objected to: " << r.text;
    EXPECT_EQ((std::vector<std::string>{"R1"}), names) << "the other types are still listed, as Windows does";
    expectNoLeaks();
}

TEST_F(ListQueues, ANotAuthorizedReplyKeepsTheMqReasonCode)
{
    script[kLocal].messages = {bareReply(MQCFT_RESPONSE, 1, MQCC_FAILED, MQRC_NOT_AUTHORIZED)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(MQRC_NOT_AUTHORIZED, r.rc);
}

TEST_F(ListQueues, ALaterTypeThatFailsIsOnlyAWarning)
{
    script[kLocal].messages = {queueReply("Q1", kLocal, 1)};
    script[kRemote].messages = {bareReply(MQCFT_XR_ITEM, 1, MQCC_FAILED, MQRCCF_CFH_COMMAND_ERROR)};
    script[kAlias].messages = {queueReply("A1", kAlias, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"Q1", "A1"}), names);
    EXPECT_TRUE(anyWarningContains(r, "3007")) << "Windows drops this failure without a trace";
}

TEST_F(ListQueues, NoReplyAtAllIsReportedAsATimeout)
{
    silent = true;
    connect();
    std::vector<std::string> names;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    Result r = list(names);
    const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_NE(std::string::npos, r.text.find("command server")) << r.text;
    EXPECT_TRUE(names.empty());
    EXPECT_EQ(1, fake.putCalls) << "with no reply to the first request the other types are not tried (Windows waits 3 seconds for each)";
    EXPECT_EQ(1, fake.getCalls);
    EXPECT_LT(elapsed, 2000);
    expectNoLeaks();
}

TEST_F(ListQueues, AReplyThatStopsMidWayKeepsTheNamesAlreadyRead)
{
    script[kLocal].messages = {queueReply("Q1", kLocal, 0), queueReply("Q2", kLocal, 0)};   // never marked last
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_EQ((std::vector<std::string>{"Q1", "Q2"}), names);
    EXPECT_EQ(3, fake.getCalls);
    EXPECT_EQ(1, fake.putCalls);
    expectNoLeaks();
}

TEST_F(ListQueues, AConnectionLostMidReplyIsReportedAndLeavesNothingBehind)
{
    script[kLocal].messages = {queueReply("Q1", kLocal, 0), queueReply("Q2", kLocal, 1)};
    fake.failGetsAfter = 1;
    fake.failGetsRc = MQRC_CONNECTION_BROKEN;
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_EQ((std::vector<std::string>{"Q1"}), names);
    EXPECT_FALSE(session.isConnected()) << "the session notices the loss, as after any other call";
    EXPECT_EQ(1, fake.discCalls);
    EXPECT_EQ(1, fake.putCalls) << "the other types are not tried";
    EXPECT_TRUE(fake.liveHandles.empty()) << "MQDISC released the handles";

    fake.failGetsAfter = -1;
    connect();
    EXPECT_TRUE(session.isConnected());
}

TEST_F(ListQueues, AConnectionLostWhileSendingTheRequestIsReported)
{
    fake.putRc = MQRC_CONNECTION_BROKEN;
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_NE(std::string::npos, r.text.find("Admin queue")) << r.text;
    EXPECT_FALSE(session.isConnected());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(ListQueues, APutThatFailsForAnotherReasonStillCleansUp)
{
    fake.putRc = MQRC_PUT_INHIBITED;
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_EQ(MQRC_PUT_INHIBITED, r.rc);
    EXPECT_EQ("***** Error writing to Admin queue cc=2, reason=2051", r.text);
    EXPECT_TRUE(session.isConnected());
    expectNoLeaks();
}

TEST_F(ListQueues, AFailedCloseOfTheReplyQueueIsAWarning)
{
    script[kLocal].messages = {queueReply("Q1", kLocal, 1)};
    connect();
    fake.closeRc = MQRC_UNEXPECTED_ERROR;

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"Q1"}), names);
    EXPECT_TRUE(anyWarningContains(r, "MQCLOSE"));
}

// ---------------------------------------------------------------------------------------------------
// Replies that are truncated, malformed or huge
// ---------------------------------------------------------------------------------------------------

TEST_F(ListQueues, AReplyShorterThanTheHeaderStopsThatRequestWithAWarning)
{
    script[kLocal].messages = {queueReply("Q1", kLocal, 0), Bytes(20, 0)};
    script[kRemote].messages = {queueReply("R1", kRemote, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"Q1", "R1"}), names);
    EXPECT_TRUE(anyWarningContains(r, "shorter")) << "the damaged reply is reported";
    expectNoLeaks();
}

TEST_F(ListQueues, AParameterThatRunsPastTheEndOfTheReplyIsReported)
{
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 2);
    stringParameter(w, 2016, "TRUNCATED.QUEUE", 819, 4000, 15);     // claims 4000 bytes, has 36
    integer(w, 20, kLocal);
    script[kLocal].messages = {w.data};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(names.empty());
    EXPECT_TRUE(anyWarningContains(r, "runs past the end"));
}

TEST_F(ListQueues, ANameLengthThatPointsPastItsParameterIsIgnored)
{
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 2);
    stringParameter(w, 2016, "SHORT", 819, 28, 500);               // StringLength 500 in a 28 byte parameter
    integer(w, 20, kLocal);
    script[kLocal].messages = {w.data};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(names.empty());
    EXPECT_TRUE(anyWarningContains(r, "does not fit"));
}

TEST_F(ListQueues, AZeroLengthParameterEndsTheWalkInsteadOfLoopingOnIt)
{
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 3);
    w.i32(3).i32(0).i32(20).i32(1);                                 // an integer parameter whose StrucLength is 0
    stringParameter(w, 2016, "NEVER.SEEN");
    integer(w, 20, kLocal);
    script[kLocal].messages = {w.data};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(names.empty());
    EXPECT_TRUE(anyWarningContains(r, "has length 0"));
}

TEST_F(ListQueues, AnEmptyNameIsNotAnEntry)
{
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 2);
    stringParameter(w, 2016, "", 819, 20, 0);
    integer(w, 20, kLocal);
    script[kLocal].messages = {w.data};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(names.empty()) << "Windows would insert a bare type letter";
    EXPECT_TRUE(anyWarningContains(r, "empty"));
}

TEST_F(ListQueues, ANameOverFortyEightCharactersIsDroppedWithAWarning)
{
    script[kLocal].messages = {queueReply("OK.Q", kLocal, 0)};
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 2);
    stringParameter(w, 2016, std::string(49, 'L'));                 // Windows copies this into a 48 byte field
    integer(w, 20, kLocal);
    script[kLocal].messages.push_back(w.data);
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"OK.Q"}), names);
    EXPECT_TRUE(anyWarningContains(r, "49 characters"));
}

TEST_F(ListQueues, AHugeParameterCountOnAShortReplyIsSafe)
{
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 0x7FFFFFFF);
    integer(w, 99, 1);
    script[kLocal].messages = {w.data};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(names.empty());
    EXPECT_TRUE(anyWarningContains(r, "ends inside parameter"));
}

TEST_F(ListQueues, AReplyBiggerThanTheBufferIsReadAsFarAsItGoesAndReported)
{
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 3);
    stringParameter(w, 2016, blankPadded("BIG.REPLY.Q"));
    integer(w, 20, kLocal);
    // a third parameter that makes the message about 1 MiB: an integer list of 250000 values
    const long count = 250000;
    w.i32(5).i32(16 + 4 * count).i32(1002).i32(count);
    for (long i = 0; i < count; i++)
    {
        w.i32(i);
    }
    ASSERT_GT(w.data.size(), 1000000u);
    script[kLocal].messages = {w.data};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"BIG.REPLY.Q"}), names) << "the name came before the cut";
    EXPECT_TRUE(anyWarningContains(r, "longer than the 65536 byte buffer")) << "truncation is reported, not silent";
    expectNoLeaks();
}

TEST_F(ListQueues, MoreNamesThanTheLimitStopTheListingAndSayWhy)
{
    options.maxNames = 3;
    for (int i = 0; i < 10; i++)
    {
        script[kLocal].messages.push_back(queueReply("Q" + std::to_string(i), kLocal, i == 9 ? 1 : 0));
    }
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"Q0", "Q1", "Q2"}), names);
    EXPECT_TRUE(anyWarningContains(r, "limited to 3 names"));
    EXPECT_EQ(3, fake.getCalls) << "reading stops at the limit";
    EXPECT_EQ(1, fake.putCalls) << "the other types are not asked";
    expectNoLeaks();
}

TEST_F(ListQueues, ADamagedReplyDoesNotLeaveTheRestOfTheRequestForTheNextOne)
{
    // Without reading on, L2 would be taken as the answer to the remote request, and R1 left behind.
    script[kLocal].messages = {queueReply("L1", kLocal, 0), Bytes(20, 0), queueReply("L2", kLocal, 1)};
    script[kRemote].messages = {queueReply("R1", kRemote, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"L1", "L2", "R1"}), names);
    EXPECT_TRUE(anyWarningContains(r, "shorter"));
    EXPECT_TRUE(anyWarningContains(r, "may be incomplete"));
    EXPECT_EQ(5, fake.getCalls) << "L1, junk, L2, R1 and the alias request's own none-found reply";
    expectNoLeaks();
}

TEST_F(ListQueues, AHeaderLengthThatIsNotValidIsDamageToo)
{
    Bytes bad = queueReply("BAD", kLocal, 0);
    bad[4] = 8;                                                     // StrucLength 8: below the 36 byte header
    script[kLocal].messages = {queueReply("L1", kLocal, 0), bad, queueReply("L2", kLocal, 1)};
    script[kRemote].messages = {queueReply("R1", kRemote, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"L1", "L2", "R1"}), names);
}

TEST_F(ListQueues, ADamagedReplyThenSilenceIsAWarningNotAFailure)
{
    script[kLocal].messages = {queueReply("L1", kLocal, 0), Bytes(20, 0)};      // the real last message was the damaged one
    script[kRemote].messages = {queueReply("R1", kRemote, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"L1", "R1"}), names);
    EXPECT_TRUE(anyWarningContains(r, "may be incomplete"));
    expectNoLeaks();
}

TEST_F(ListQueues, ReplyMessagesPerRequestAreCappedAndTheListingStops)
{
    options.maxReplies = 5;
    for (int i = 0; i < 20; i++)
    {
        script[kLocal].messages.push_back(queueReply("Q" + std::to_string(i), kLocal, 0));
    }
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"Q0", "Q1", "Q2", "Q3", "Q4"}), names);
    EXPECT_TRUE(anyWarningContains(r, "more than 5 reply messages"));
    EXPECT_EQ(5, fake.getCalls);
    EXPECT_EQ(1, fake.putCalls) << "leftover replies would be taken for the next request's: stop";
    expectNoLeaks();
}

TEST_F(ListQueues, AnEndlessRunOfDamagedRepliesIsCappedToo)
{
    options.maxReplies = 4;
    script[kLocal].messages.assign(50, Bytes(20, 0));
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(names.empty());
    EXPECT_TRUE(anyWarningContains(r, "more than 4 reply messages"));
    EXPECT_EQ(4, fake.getCalls);
    EXPECT_EQ(1, fake.putCalls);
}

TEST_F(ListQueues, AListingThatEndsExactlyAtTheReplyCapIsNotTruncated)
{
    options.maxReplies = 3;
    script[kLocal].messages = {queueReply("Q1", kLocal, 0), queueReply("Q2", kLocal, 0), queueReply("Q3", kLocal, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"Q1", "Q2", "Q3"}), names);
    EXPECT_FALSE(anyWarningContains(r, "more than"));
    EXPECT_EQ(3, fake.putCalls) << "the other types are still asked";
}

TEST_F(ListQueues, AQueueWithoutAPositiveTypeIsNotListed)
{
    // Windows requires QType > 0 before it records a queue (DataArea.cpp:21958)
    script[kLocal].messages = {queueReply("ZERO.TYPE", 0, 0), queueReply("NEGATIVE.TYPE", -1, 0), queueReply("GOOD.Q", kLocal, 1)};
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ((std::vector<std::string>{"GOOD.Q"}), names);
}

TEST_F(ListQueues, ThousandsOfBadRepliesGiveABoundedNumberOfWarnings)
{
    for (int i = 0; i < 200; i++)
    {
        Wire w;
        header(w, MQCFT_XR_ITEM, i == 199 ? 1 : 0, 0, 0, 2);
        stringParameter(w, 2016, std::string(60, 'N'));
        integer(w, 20, kLocal);
        script[kLocal].messages.push_back(w.data);
    }
    connect();

    std::vector<std::string> names;
    Result r = list(names);
    EXPECT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(names.empty());
    EXPECT_LE(r.warnings.size(), 51u);
    EXPECT_TRUE(anyWarningContains(r, "further warnings"));
}

// ---------------------------------------------------------------------------------------------------
// The reply parser on its own, with buffers of exactly the message size
// ---------------------------------------------------------------------------------------------------

TEST(PcfQueueReply, ReadsTheNameAndTheTypeInEitherOrder)
{
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 2);
    integer(w, 20, kAlias);
    stringParameter(w, 2016, blankPadded("ALIAS.Q"));
    pcf::QueueReply r = parse(w.data);
    EXPECT_TRUE(r.headerValid);
    EXPECT_TRUE(r.hasQueue);
    EXPECT_EQ("ALIAS.Q", r.name);
    EXPECT_EQ(kAlias, r.queueType);
    EXPECT_TRUE(r.last);
    EXPECT_TRUE(r.warnings.empty());
}

TEST(PcfQueueReply, StepsOverParametersOfOtherAndUnknownTypes)
{
    Wire w;
    header(w, MQCFT_XR_ITEM, 0, 0, 0, 5);
    w.i32(9).i32(24).i32(7).i32(4).text("ABCD").i32(0);             // a byte string: Type 9, StrucLength 24 (20 + 4)
    w.i32(77).i32(12).i32(5);                                       // a type nobody knows, 12 bytes long
    w.i32(5).i32(24).i32(1002).i32(2).i32(2016).i32(20);            // an integer list
    stringParameter(w, 2016, "AFTER.SKIPS");
    integer(w, 20, kLocal);
    pcf::QueueReply r = parse(w.data);
    EXPECT_TRUE(r.hasQueue);
    EXPECT_EQ("AFTER.SKIPS", r.name);
    EXPECT_FALSE(r.last) << "Control 0 is MQCFC_NOT_LAST";
}

TEST(PcfQueueReply, StopsAtTheNameAndTypeSoLaterDamageIsNeverRead)
{
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 3);
    stringParameter(w, 2016, "FOUND");
    integer(w, 20, kLocal);
    w.i32(3).i32(0).i32(5).i32(5);                                  // damaged third parameter
    pcf::QueueReply r = parse(w.data);
    EXPECT_TRUE(r.hasQueue);
    EXPECT_TRUE(r.warnings.empty()) << "Windows stops here too";
}

TEST(PcfQueueReply, AMissingTypeOrNameIsNotAQueue)
{
    Wire onlyName;
    header(onlyName, MQCFT_XR_ITEM, 1, 0, 0, 1);
    stringParameter(onlyName, 2016, "NO.TYPE");
    EXPECT_FALSE(parse(onlyName.data).hasQueue);

    Wire onlyType;
    header(onlyType, MQCFT_XR_ITEM, 1, 0, 0, 1);
    integer(onlyType, 20, kLocal);
    EXPECT_FALSE(parse(onlyType.data).hasQueue);
}

TEST(PcfQueueReply, EveryTruncationOfAValidReplyIsHandled)
{
    const Bytes whole = queueReply("TRUNCATE.ME", kLocal, 1);
    for (size_t n = 0; n <= whole.size(); n++)
    {
        const Bytes cut(whole.begin(), whole.begin() + static_cast<long>(n));
        pcf::QueueReply r = parse(cut);
        if (n < whole.size())
        {
            EXPECT_FALSE(r.hasQueue) << "cut to " << n << " bytes";
        }
        else
        {
            EXPECT_TRUE(r.hasQueue);
        }
    }
}

TEST(PcfQueueReply, EveryByteOfAValidReplyCorruptedIsHandled)
{
    const Bytes whole = queueReply("CORRUPT.ME", kLocal, 1);
    for (size_t i = 0; i < whole.size(); i++)
    {
        for (unsigned char value : {0x00, 0x01, 0x7F, 0x80, 0xFF})
        {
            Bytes damaged = whole;
            damaged[i] = value;
            pcf::QueueReply r = parse(damaged);       // must not read outside the exact-size buffer
            (void)r;
        }
    }
}

TEST(PcfQueueReply, AFailedReplyCarriesItsReasonAndParameter)
{
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, MQCC_FAILED, 3007, 2);
    integer(w, 7, 5);
    integer(w, MQIACF_PARAMETER_ID, 2016);
    pcf::QueueReply r = parse(w.data);
    EXPECT_TRUE(r.headerValid);
    EXPECT_EQ(MQCC_FAILED, r.compCode);
    EXPECT_EQ(3007, r.reason);
    EXPECT_EQ(2016, r.parameterId);
    EXPECT_FALSE(r.hasQueue);
}

TEST(PcfQueueReply, ABigEndianReplyIsReadWithTheOtherByteOrder)
{
    const Bytes big = queueReply("SWAPPED", kRemote, 1, MQCFT_XR_ITEM, true);
    pcf::QueueReply wrongOrder = parse(big, MQENC_NATIVE);
    EXPECT_FALSE(wrongOrder.headerValid) << "read in the wrong order the header length is nonsense";
    pcf::QueueReply r = parse(big, 0x111);
    EXPECT_TRUE(r.hasQueue);
    EXPECT_EQ("SWAPPED", r.name);
    EXPECT_EQ(kRemote, r.queueType);
}

TEST(PcfQueueReply, AnUndefinedEncodingIsTakenAsTheHosts)
{
    pcf::QueueReply r = parse(queueReply("NATIVE", kLocal, 1), MQENC_INTEGER_UNDEFINED);
    EXPECT_TRUE(r.hasQueue);
}

TEST(PcfQueueReply, ANameLengthBetweenTheSpaceAndTheParameterLengthIsRejected)
{
    // The name is the last bytes of a message whose size is a multiple of 16, so a read of one byte too
    // many leaves the buffer (a guard allocator faults). StringLength 9 is within StrucLength 28 but not
    // within the 8 bytes after the 20 fixed ones: a check against the whole parameter would accept it.
    Wire w;
    header(w, MQCFT_XR_ITEM, 1, 0, 0, 2);
    integer(w, 20, kLocal);
    stringParameter(w, 2016, "ABCDEFGH", 819, 28, 9);
    ASSERT_EQ(0u, w.data.size() % 16);
    pcf::QueueReply r = parse(w.data);
    EXPECT_FALSE(r.hasQueue);
    EXPECT_TRUE(r.name.empty());
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_NE(std::string::npos, r.warnings[0].find("does not fit"));

    // and the longest name that does fit is read
    Wire fits;
    header(fits, MQCFT_XR_ITEM, 1, 0, 0, 2);
    integer(fits, 20, kLocal);
    stringParameter(fits, 2016, "ABCDEFGH", 819, 28, 8);
    pcf::QueueReply ok = parse(fits.data);
    EXPECT_TRUE(ok.hasQueue);
    EXPECT_EQ("ABCDEFGH", ok.name);
}

TEST(PcfQueueReply, TheParametersStartWhereTheHeaderSaysItsLengthIs)
{
    // a later header version with 4 more bytes: StrucLength 40
    Wire w;
    w.i32(MQCFT_XR_ITEM).i32(40).i32(4).i32(13).i32(1).i32(1).i32(0).i32(0).i32(2).i32(0xABCDEF);
    stringParameter(w, 2016, blankPadded("LATER.VERSION"));
    integer(w, 20, kAlias);
    pcf::QueueReply r = parse(w.data);
    EXPECT_TRUE(r.headerValid);
    EXPECT_TRUE(r.hasQueue);
    EXPECT_EQ("LATER.VERSION", r.name);
    EXPECT_EQ(kAlias, r.queueType);
    EXPECT_TRUE(r.last);

    // a failed reply is searched for its parameter from the same place
    Wire failed;
    failed.i32(MQCFT_XR_ITEM).i32(40).i32(4).i32(13).i32(1).i32(1).i32(MQCC_FAILED).i32(3007).i32(1).i32(0x1234);
    integer(failed, MQIACF_PARAMETER_ID, 2016);
    EXPECT_EQ(2016, parse(failed.data).parameterId);
}

TEST(PcfQueueReply, AHeaderLengthBelow36OrPastTheMessageIsInvalid)
{
    for (long structLength : {0l, 8l, 32l, 35l, -36l, 121l, 1000l})
    {
        Wire w;
        w.i32(MQCFT_XR_ITEM).i32(structLength).i32(3).i32(13).i32(1).i32(1).i32(0).i32(0).i32(2);
        stringParameter(w, 2016, blankPadded("X"));
        integer(w, 20, kLocal);                                     // 36 + 68 + 16 = 120 bytes
        ASSERT_EQ(120u, w.data.size());
        pcf::QueueReply r = parse(w.data);
        EXPECT_FALSE(r.headerValid) << structLength;
        EXPECT_FALSE(r.warnings.empty()) << structLength;
    }
}
