// GetOptionsTests.cpp - the Get options added by Task 13 (group, logical order, complete message,
// all available, selector, alternate user, shared input) and the queue manager facts that
// connect() learns, driven by the fake MQI. Windows: DataArea::setMQoptions, getMessage, openQ,
// connect2QM.

#include <gtest/gtest.h>

#include <string.h>

#include <string>
#include <vector>

#include "rfh_mq_session.h"

using namespace rfh;

#include "fake_mq.h"

namespace
{
    using rfh_test::fakeApi;

    rfh_test::FakeMq & fake = rfh_test::fakeMq();

    const char * kQueue = "RFHUTIL.TEST.GETOPT";

    class GetOptionsFake : public ::testing::Test
    {
    protected:
        GetOptionsFake() : session(fakeApi()) {}

        void SetUp() override { fake.reset(); }

        Result connect()
        {
            ConnectOptions options;
            options.mode = Mode::Bindings;
            return session.connect(options);
        }

        Result getWith(const GetOptions & options)
        {
            Message m;
            return session.get(kQueue, m, options);
        }

        // opens of queues; the queue manager object that connect() inquires on is logged apart
        static const std::vector<rfh_test::OpenRecord> & queueOpens() { return fake.openLog; }

        static bool anyWarningContains(const Result & r, const std::string & text)
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

        MqSession session;
    };

    const MQLONG kAllAvailable = MQGMO_ALL_MSGS_AVAILABLE | MQGMO_ALL_SEGMENTS_AVAILABLE;
    const MQLONG kGroupBits = MQGMO_LOGICAL_ORDER | MQGMO_COMPLETE_MSG | kAllAvailable;
}

// ---------------------------------------------------------------------------------------------------
// Existing behaviour stays as it is
// ---------------------------------------------------------------------------------------------------

TEST_F(GetOptionsFake, ExistingDefaultsAreUnchanged)
{
    ASSERT_TRUE(connect().ok());
    Result r = getWith(GetOptions());
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());

    ASSERT_EQ(1u, fake.getLog.size());
    EXPECT_EQ(MQGMO_NO_SYNCPOINT | MQGMO_FAIL_IF_QUIESCING | MQGMO_NO_WAIT, fake.getLog[0].options);
    EXPECT_EQ(MQMO_NONE, fake.getLog[0].matchOptions);
    EXPECT_EQ(MQGMO_VERSION_2, fake.getLog[0].gmoVersion);

    ASSERT_EQ(1u, queueOpens().size());
    const rfh_test::OpenRecord & open = queueOpens()[0];
    EXPECT_EQ(MQOO_INPUT_SHARED | MQOO_BROWSE | MQOO_FAIL_IF_QUIESCING, open.options);
    EXPECT_EQ(MQOD_VERSION_1, open.odVersion);
    EXPECT_EQ("", open.alternateUser);
}

// ---------------------------------------------------------------------------------------------------
// Group id (getMessage 8055-8059)
// ---------------------------------------------------------------------------------------------------

TEST_F(GetOptionsFake, MatchGroupIdSetsTheMatchOption)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.matchGroupId = true;
    for (int i = 0; i < 24; i++)
    {
        options.groupId[i] = (MQBYTE)(0x41 + i);
    }
    ASSERT_TRUE(getWith(options).ok());

    ASSERT_EQ(1u, fake.getLog.size());
    EXPECT_EQ(MQMO_MATCH_GROUP_ID, fake.getLog[0].matchOptions);
    EXPECT_EQ(0, memcmp(options.groupId, fake.getLog[0].groupId, 24));
    EXPECT_EQ(0, memcmp(MQMI_NONE, fake.getLog[0].msgId, 24)) << "no message id is matched";
}

TEST_F(GetOptionsFake, MatchGroupIdCombinesWithTheOtherMatchOptions)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.matchGroupId = true;
    options.matchCorrelId = true;
    memset(options.groupId, 0x22, 24);
    memset(options.correlId, 0x33, 24);
    ASSERT_TRUE(getWith(options).ok());

    EXPECT_EQ(MQMO_MATCH_GROUP_ID | MQMO_MATCH_CORREL_ID, fake.getLog[0].matchOptions);
    EXPECT_EQ(0x22, fake.getLog[0].groupId[0]);
    EXPECT_EQ(0x33, fake.getLog[0].correlId[0]);
}

TEST_F(GetOptionsFake, AnAllZeroGroupIdIsMatchedWhenAsked)
{
    // Windows skips the match when the field is MQGI_NONE (getMessage 8055); the core takes the
    // caller's flag as it is, as it does for the message and correlation ids. The caller applies
    // the Windows rule.
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.matchGroupId = true;
    ASSERT_TRUE(getWith(options).ok());
    EXPECT_EQ(MQMO_MATCH_GROUP_ID, fake.getLog[0].matchOptions);
}

// ---------------------------------------------------------------------------------------------------
// Logical order, complete message, all available (setMQoptions 17816-17855)
// ---------------------------------------------------------------------------------------------------

TEST_F(GetOptionsFake, LogicalOrderCompleteMsgAndAllAvailableSetTheirFlags)
{
    ASSERT_TRUE(connect().ok());   // the fake is a UNIX queue manager at level 940

    GetOptions logical;
    logical.logicalOrder = true;
    ASSERT_TRUE(getWith(logical).ok());
    EXPECT_EQ(MQGMO_LOGICAL_ORDER, fake.getLog.back().options & kGroupBits);

    GetOptions complete;
    complete.completeMsg = true;
    ASSERT_TRUE(getWith(complete).ok());
    EXPECT_EQ(MQGMO_COMPLETE_MSG, fake.getLog.back().options & kGroupBits);

    GetOptions available;
    available.allAvailable = true;
    ASSERT_TRUE(getWith(available).ok());
    EXPECT_EQ(kAllAvailable, fake.getLog.back().options & kGroupBits);

    GetOptions all;
    all.logicalOrder = all.completeMsg = all.allAvailable = true;
    Result r = getWith(all);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(kGroupBits, fake.getLog.back().options & kGroupBits);
    EXPECT_TRUE(r.warnings.empty());
    EXPECT_NE(0, fake.getLog.back().options & MQGMO_NO_SYNCPOINT) << "the other options stay";
}

TEST_F(GetOptionsFake, TheGroupOptionsApplyToABrowseToo)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.mode = GetMode::BrowseFirst;
    options.logicalOrder = true;
    options.allAvailable = true;
    ASSERT_TRUE(getWith(options).ok());
    EXPECT_EQ(MQGMO_BROWSE_FIRST | MQGMO_LOGICAL_ORDER | kAllAvailable | MQGMO_FAIL_IF_QUIESCING | MQGMO_NO_WAIT, fake.getLog.back().options);
}

TEST_F(GetOptionsFake, AllAvailableOnZosBeforeV6SetsNeither)
{
    fake.platform = MQPL_MVS;
    fake.commandLevel = 530;
    ASSERT_TRUE(connect().ok());

    GetOptions options;
    options.allAvailable = true;
    Result r = getWith(options);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(0, fake.getLog.back().options & kAllAvailable);
    EXPECT_TRUE(anyWarningContains(r, "all available")) << "the dropped option is reported";
}

TEST_F(GetOptionsFake, AllAvailableOnZosV6SetsOnlyAllMsgs)
{
    fake.platform = MQPL_MVS;
    fake.commandLevel = 600;
    ASSERT_TRUE(connect().ok());

    GetOptions options;
    options.allAvailable = true;
    Result r = getWith(options);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(MQGMO_ALL_MSGS_AVAILABLE, fake.getLog.back().options & kAllAvailable);
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(GetOptionsFake, LogicalOrderAndCompleteMsgOnZosBeforeV6AreDroppedAndReported)
{
    fake.platform = MQPL_MVS;
    fake.commandLevel = 530;
    ASSERT_TRUE(connect().ok());

    GetOptions options;
    options.logicalOrder = true;
    options.completeMsg = true;
    Result r = getWith(options);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(0, fake.getLog.back().options & kGroupBits);
    EXPECT_TRUE(anyWarningContains(r, "logical order"));
    EXPECT_TRUE(anyWarningContains(r, "complete message"));

    // at V6 on z/OS both are allowed
    session.disconnect();
    fake.commandLevel = 600;
    ASSERT_TRUE(connect().ok());
    r = getWith(options);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(MQGMO_LOGICAL_ORDER | MQGMO_COMPLETE_MSG, fake.getLog.back().options & kGroupBits);
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(GetOptionsFake, ZosWithAnUnknownLevelDropsTheGroupOptionsAsWindowsDoes)
{
    // Windows keeps level 0 when the inquiry fails and tests "level >= 600" (setMQoptions 17820),
    // so a z/OS queue manager whose level is unknown loses the options.
    fake.platform = MQPL_MVS;
    fake.inqRcBySelector[MQIA_COMMAND_LEVEL] = MQRC_SELECTOR_ERROR;
    ASSERT_TRUE(connect().ok());

    GetOptions options;
    options.logicalOrder = true;
    options.allAvailable = true;
    Result r = getWith(options);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(0, fake.getLog.back().options & kGroupBits);
    EXPECT_FALSE(r.warnings.empty());
}

TEST_F(GetOptionsFake, AnUnknownPlatformAndLevelAreTreatedAsModern)
{
    fake.inqRcBySelector[MQIA_PLATFORM] = MQRC_SELECTOR_ERROR;
    fake.inqRcBySelector[MQIA_COMMAND_LEVEL] = MQRC_SELECTOR_ERROR;
    ASSERT_TRUE(connect().ok());

    GetOptions options;
    options.logicalOrder = options.completeMsg = options.allAvailable = true;
    Result r = getWith(options);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(kGroupBits, fake.getLog.back().options & kGroupBits);
}

// ---------------------------------------------------------------------------------------------------
// Selector (openQ 12079-12121)
// ---------------------------------------------------------------------------------------------------

TEST_F(GetOptionsFake, SelectorGoesIntoTheOpenDescriptor)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.selector = "color = 'red'";
    Result r = getWith(options);
    ASSERT_TRUE(r.ok()) << r.text;

    ASSERT_EQ(1u, queueOpens().size());
    const rfh_test::OpenRecord & open = queueOpens()[0];
    EXPECT_EQ(MQOD_VERSION_4, open.odVersion);
    EXPECT_EQ("color = 'red'", open.selector);
    EXPECT_GT(open.resObjectStringBufSize, 0) << "V7 answers 2520 when the resolved object string has no buffer (openQ 12102)";
    EXPECT_EQ(MQOO_INPUT_SHARED | MQOO_BROWSE | MQOO_FAIL_IF_QUIESCING, open.options) << "the selector adds no open option";
}

TEST_F(GetOptionsFake, ASelectorIsNotCutToAnythingShorter)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.selector = std::string(MQ_SELECTOR_LENGTH - 1, 'a');
    ASSERT_TRUE(getWith(options).ok());
    EXPECT_EQ(options.selector, queueOpens()[0].selector);
}

TEST_F(GetOptionsFake, ASelectorLongerThanMqAllowsIsRefusedNotCut)
{
    // Windows copies at most MQ_SELECTOR_LENGTH - 1 characters and selects with the rest dropped
    // (openQ 12087-12091). A cut selector selects something else, so the core refuses what is longer than
    // MQ_SELECTOR_LENGTH bytes (the length is explicit in an MQCHARV, so exactly that many are legal).
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.selector = std::string(MQ_SELECTOR_LENGTH + 1, 'a');
    Result r = getWith(options);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("selector"));
    EXPECT_TRUE(queueOpens().empty());
    EXPECT_EQ(0, fake.getCalls);
}

TEST_F(GetOptionsFake, SelectorFailsClearlyBelowV7)
{
    fake.commandLevel = 600;
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.selector = "color = 'red'";
    Result r = getWith(options);

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_NE(std::string::npos, r.text.find("700")) << r.text;
    EXPECT_NE(std::string::npos, r.text.find("600")) << "the level found is named: " << r.text;
    EXPECT_TRUE(queueOpens().empty()) << "no MQOPEN is made";
    EXPECT_EQ(0, fake.getCalls);
    EXPECT_TRUE(session.isConnected());
}

TEST_F(GetOptionsFake, SelectorIsAllowedAtV7AndWhenTheLevelIsUnknown)
{
    fake.commandLevel = 700;
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.selector = "a = 1";
    EXPECT_TRUE(getWith(options).ok());
    ASSERT_TRUE(session.disconnect().ok());

    // an unknown level (inquiry failed) leaves the decision to the queue manager
    fake.reset();
    fake.inqRcBySelector[MQIA_COMMAND_LEVEL] = MQRC_SELECTOR_ERROR;
    ASSERT_TRUE(connect().ok());
    EXPECT_TRUE(getWith(options).ok());
    EXPECT_EQ(1u, queueOpens().size());
}

TEST_F(GetOptionsFake, ASelectorTheQueueManagerRefusesIsReportedAndLeavesNoHandle)
{
    ASSERT_TRUE(connect().ok());
    fake.openRcByName[kQueue] = MQRC_SELECTOR_SYNTAX_ERROR;
    GetOptions options;
    options.selector = "color =";
    Result r = getWith(options);

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_SELECTOR_SYNTAX_ERROR, r.rc);
    EXPECT_TRUE(session.isConnected());
    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(GetOptionsFake, ChangingTheSelectorReopensTheQueue)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.selector = "a = 1";
    ASSERT_TRUE(getWith(options).ok());
    ASSERT_TRUE(getWith(options).ok());
    EXPECT_EQ(1u, queueOpens().size()) << "the same selector reuses the handle";

    options.selector = "a = 2";
    ASSERT_TRUE(getWith(options).ok());
    ASSERT_EQ(2u, queueOpens().size());
    EXPECT_EQ("a = 2", queueOpens()[1].selector);
    EXPECT_EQ(1u, fake.liveHandles.size()) << "the first handle was closed";

    options.selector.clear();
    ASSERT_TRUE(getWith(options).ok());
    ASSERT_EQ(3u, queueOpens().size());
    EXPECT_EQ(MQOD_VERSION_1, queueOpens()[2].odVersion);
    EXPECT_EQ("", queueOpens()[2].selector);

    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(GetOptionsFake, BrowsePreviousAfterTheSelectorChangedDoesNotWalkAnotherSelection)
{
    fake.browseCursors = true;
    for (int i = 0; i < 3; i++)
    {
        fake.queue(kQueue).messages.emplace_back();
    }
    ASSERT_TRUE(connect().ok());

    GetOptions first;
    first.mode = GetMode::BrowseFirst;
    first.selector = "a = 1";
    ASSERT_TRUE(getWith(first).ok());
    first.mode = GetMode::BrowseNext;
    ASSERT_TRUE(getWith(first).ok());
    ASSERT_EQ(2, session.browsePosition());

    GetOptions previous;
    previous.mode = GetMode::BrowsePrevious;
    previous.selector = "a = 2";
    const int getsBefore = fake.getCalls;
    Result r = getWith(previous);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(getsBefore, fake.getCalls) << "the position belongs to the other selection";
    EXPECT_EQ(1u, queueOpens().size()) << "nothing was reopened";
}

// ---------------------------------------------------------------------------------------------------
// Alternate user (openQ 12126-12144)
// ---------------------------------------------------------------------------------------------------

TEST_F(GetOptionsFake, AlternateUserSetsTheOpenOptionAndName)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.alternateUser = "APPUSER";
    Result r = getWith(options);
    ASSERT_TRUE(r.ok()) << r.text;

    ASSERT_EQ(1u, queueOpens().size());
    EXPECT_EQ(MQOO_INPUT_SHARED | MQOO_BROWSE | MQOO_FAIL_IF_QUIESCING | MQOO_ALTERNATE_USER_AUTHORITY, queueOpens()[0].options);
    EXPECT_EQ("APPUSER", queueOpens()[0].alternateUser);
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(GetOptionsFake, AnAlternateUserOverTwelveCharactersFails)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.alternateUser = "ABCDEFGHIJKLMNOP";
    Result r = getWith(options);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("12 characters")) << r.text;
    EXPECT_TRUE(queueOpens().empty()) << "MQOPEN was not called";
}

TEST_F(GetOptionsFake, ChangingTheAlternateUserReopensTheQueue)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    ASSERT_TRUE(getWith(options).ok());
    EXPECT_EQ(0, queueOpens()[0].options & MQOO_ALTERNATE_USER_AUTHORITY);

    options.alternateUser = "ONE";
    ASSERT_TRUE(getWith(options).ok());
    ASSERT_EQ(2u, queueOpens().size()) << "switching the alternate user on reopens (Windows keeps the old handle)";

    ASSERT_TRUE(getWith(options).ok());
    EXPECT_EQ(2u, queueOpens().size());

    options.alternateUser = "TWO";
    ASSERT_TRUE(getWith(options).ok());
    ASSERT_EQ(3u, queueOpens().size());
    EXPECT_EQ("TWO", queueOpens()[2].alternateUser);
    EXPECT_EQ(1u, fake.liveHandles.size());
}

TEST_F(GetOptionsFake, ADeniedAlternateUserIsReportedAndLeavesNoHandle)
{
    ASSERT_TRUE(connect().ok());
    fake.openRcByName[kQueue] = MQRC_NOT_AUTHORIZED;
    GetOptions options;
    options.alternateUser = "NOBODY";
    Result r = getWith(options);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_NOT_AUTHORIZED, r.rc);
    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_TRUE(fake.liveHandles.empty());
}

// ---------------------------------------------------------------------------------------------------
// Shared input
// ---------------------------------------------------------------------------------------------------

TEST_F(GetOptionsFake, InputSharedChangesTheOpenOption)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.inputShared = true;
    ASSERT_TRUE(getWith(options).ok());

    ASSERT_EQ(1u, queueOpens().size());
    EXPECT_EQ(MQOO_INPUT_SHARED | MQOO_BROWSE | MQOO_FAIL_IF_QUIESCING, queueOpens()[0].options);

    options.inputShared = false;
    ASSERT_TRUE(getWith(options).ok());
    ASSERT_EQ(2u, queueOpens().size()) << "the change reopens";
    EXPECT_EQ(MQOO_INPUT_AS_Q_DEF | MQOO_BROWSE | MQOO_FAIL_IF_QUIESCING, queueOpens()[1].options);
    EXPECT_EQ(1u, fake.liveHandles.size());
}

// ---------------------------------------------------------------------------------------------------
// Connection lost
// ---------------------------------------------------------------------------------------------------

TEST_F(GetOptionsFake, ALostConnectionDuringTheOpenWithOptionsLeavesNothingBehind)
{
    ASSERT_TRUE(connect().ok());
    fake.openRcByName[kQueue] = MQRC_CONNECTION_BROKEN;
    GetOptions options;
    options.selector = "a = 1";
    options.alternateUser = "APPUSER";
    options.inputShared = true;
    Result r = getWith(options);

    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_TRUE(fake.liveHandles.empty());

    fake.openRcByName.clear();
    ASSERT_TRUE(connect().ok());
    ASSERT_TRUE(getWith(options).ok());
    EXPECT_EQ(2u, queueOpens().size()) << "the stale handle is not reused";
}

TEST_F(GetOptionsFake, ALostConnectionDuringTheGetWithGroupOptionsIsReported)
{
    ASSERT_TRUE(connect().ok());
    fake.getRc = MQRC_CONNECTION_BROKEN;
    GetOptions options;
    options.logicalOrder = options.completeMsg = options.allAvailable = true;
    options.matchGroupId = true;
    Result r = getWith(options);
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_TRUE(fake.liveHandles.empty());
}

// ---------------------------------------------------------------------------------------------------
// The queue manager's platform, level and maximum length (connect2QM 11242-11395)
// ---------------------------------------------------------------------------------------------------

TEST_F(GetOptionsFake, ConnectRecordsPlatformLevelAndMaximum)
{
    fake.platform = MQPL_WINDOWS_NT;
    fake.commandLevel = 912;
    fake.qmgrMaxMsgLength = 1234567;

    EXPECT_EQ(0, session.queueManagerInfo().platform) << "nothing is known before connect";
    Result r = connect();
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(r.warnings.empty());

    EXPECT_EQ(MQPL_WINDOWS_NT, session.queueManagerInfo().platform);
    EXPECT_EQ(912, session.queueManagerInfo().commandLevel);
    EXPECT_EQ(1234567, session.queueManagerInfo().maxMsgLength);

    // one open of the queue manager object, three inquiries, one close: nothing is left open
    ASSERT_EQ(1u, fake.qmgrOpenLog.size());
    EXPECT_EQ(MQOT_Q_MGR, fake.qmgrOpenLog[0].objectType);
    EXPECT_EQ(MQOO_INQUIRE | MQOO_FAIL_IF_QUIESCING, fake.qmgrOpenLog[0].options);
    EXPECT_EQ(3, fake.qmgrInqCalls);
    EXPECT_EQ(1, fake.qmgrCloseCalls);
    EXPECT_EQ(0, fake.openCalls + fake.inqCalls + fake.closeCalls) << "no queue is touched";
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(GetOptionsFake, TheFactsAreForgottenOnTheNextConnect)
{
    fake.platform = MQPL_MVS;
    fake.commandLevel = 710;
    ASSERT_TRUE(connect().ok());
    ASSERT_TRUE(session.disconnect().ok());

    fake.platform = MQPL_UNIX;
    fake.commandLevel = 940;
    fake.qmgrMaxMsgLength = 100;
    ASSERT_TRUE(connect().ok());
    EXPECT_EQ(MQPL_UNIX, session.queueManagerInfo().platform);
    EXPECT_EQ(940, session.queueManagerInfo().commandLevel);
    EXPECT_EQ(100, session.queueManagerInfo().maxMsgLength);

    // a connect that cannot learn them does not keep the old ones
    ASSERT_TRUE(session.disconnect().ok());
    fake.openRcByName[""] = MQRC_NOT_AUTHORIZED;
    ASSERT_TRUE(connect().ok());
    EXPECT_EQ(0, session.queueManagerInfo().platform);
    EXPECT_EQ(0, session.queueManagerInfo().commandLevel);
    EXPECT_EQ(0, session.queueManagerInfo().maxMsgLength);
}

TEST_F(GetOptionsFake, AQueueManagerThatRefusesTheInquiryStillConnectsWithAWarning)
{
    // Windows ignores the failure and keeps 0 (connect2QM 11264, 11283, 11299)
    fake.openRcByName[""] = MQRC_NOT_AUTHORIZED;
    Result r = connect();

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_TRUE(session.isConnected());
    EXPECT_EQ(0, session.queueManagerInfo().platform);
    EXPECT_EQ(0, session.queueManagerInfo().commandLevel);
    EXPECT_EQ(0, session.queueManagerInfo().maxMsgLength);
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_NE(std::string::npos, r.warnings[0].find("MQOPEN")) << r.warnings[0];
    EXPECT_NE(std::string::npos, r.warnings[0].find("2035")) << r.warnings[0];
    EXPECT_EQ(0, fake.qmgrInqCalls);
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(GetOptionsFake, AFailedInquiryLeavesOnlyThatFactUnknown)
{
    fake.platform = MQPL_UNIX;
    fake.commandLevel = 800;
    fake.inqRcBySelector[MQIA_PLATFORM] = MQRC_SELECTOR_ERROR;
    Result r = connect();

    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(0, session.queueManagerInfo().platform);
    EXPECT_EQ(800, session.queueManagerInfo().commandLevel);
    EXPECT_EQ(4194304, session.queueManagerInfo().maxMsgLength);
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_NE(std::string::npos, r.warnings[0].find("MQINQ")) << r.warnings[0];
    EXPECT_EQ(3, fake.qmgrInqCalls) << "the other inquiries are still made";
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(GetOptionsFake, AFailedCloseOfTheInquiryHandleIsAWarning)
{
    fake.closeRc = MQRC_OPTION_NOT_VALID_FOR_TYPE;
    Result r = connect();
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(940, session.queueManagerInfo().commandLevel) << "the facts were learned before the close";
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_NE(std::string::npos, r.warnings[0].find("MQCLOSE")) << r.warnings[0];
}

TEST_F(GetOptionsFake, AConnectionLostDuringTheInquiryFailsTheConnect)
{
    fake.openRcByName[""] = MQRC_CONNECTION_BROKEN;
    Result r = connect();

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_TRUE(fake.liveHandles.empty());

    fake.openRcByName.clear();
    ASSERT_TRUE(connect().ok()) << "the session can connect again";
    EXPECT_EQ(940, session.queueManagerInfo().commandLevel);
}

TEST_F(GetOptionsFake, AConnectionLostBetweenTheInquiriesStopsThem)
{
    fake.inqRcBySelector[MQIA_PLATFORM] = MQRC_CONNECTION_BROKEN;
    Result r = connect();

    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_EQ(2, fake.qmgrInqCalls) << "no inquiry after the connection is gone";
    EXPECT_EQ(0, session.queueManagerInfo().commandLevel);
}

TEST_F(GetOptionsFake, AFailedConnectLearnsNothing)
{
    // the fake cannot fail MQCONNX, so use the guard that stops a second connect
    ASSERT_TRUE(connect().ok());
    fake.commandLevel = 111;
    Result again = connect();
    EXPECT_FALSE(again.ok());
    EXPECT_EQ(940, session.queueManagerInfo().commandLevel) << "the facts of the live connection stay";
}

// ---------------------------------------------------------------------------------------------------
// Message properties mode (Task 17). Windows: processMQGet 7690-7760, browsePrev 9054-9107.
// ---------------------------------------------------------------------------------------------------

namespace
{
    const MQLONG kPropertyBits = MQGMO_PROPERTIES_FORCE_MQRFH2 | MQGMO_NO_PROPERTIES | MQGMO_PROPERTIES_IN_HANDLE | MQGMO_PROPERTIES_COMPATIBILITY;

    struct ModeCase
    {
        PropertiesMode mode;
        MQLONG flag;
    };

    const ModeCase kModeCases[] = {
        { PropertiesMode::None, MQGMO_NO_PROPERTIES },
        { PropertiesMode::Rfh2, MQGMO_PROPERTIES_FORCE_MQRFH2 },
        { PropertiesMode::Compat, MQGMO_PROPERTIES_COMPATIBILITY },
    };

    void addTextMessages(const char * queue, int count)
    {
        for (int i = 1; i <= count; i++)
        {
            rfh_test::FakeMessage m;
            const std::string text = "m" + std::to_string(i);
            m.data.assign(text.begin(), text.end());
            fake.queue(queue).messages.push_back(m);
        }
    }
}

TEST_F(GetOptionsFake, PropertiesModesSetTheirFlags)
{
    ASSERT_TRUE(connect().ok());

    for (const ModeCase & c : kModeCases)
    {
        for (GetMode mode : { GetMode::Destructive, GetMode::BrowseFirst, GetMode::BrowseNext })
        {
            GetOptions options;
            options.mode = mode;
            options.properties = c.mode;
            Result r = getWith(options);
            ASSERT_TRUE(r.ok()) << r.text;
            EXPECT_TRUE(r.warnings.empty());

            const rfh_test::GetRecord & sent = fake.getLog.back();
            EXPECT_EQ(c.flag, sent.options & kPropertyBits) << "exactly this one property option";
            EXPECT_EQ(MQGMO_VERSION_4, sent.gmoVersion) << "Windows raises the GMO to version 4 for the property options";
            EXPECT_NE(0, sent.options & MQGMO_FAIL_IF_QUIESCING) << "the other options stay";
        }
    }
}

TEST_F(GetOptionsFake, AsQueueSetsNoPropertiesFlag)
{
    ASSERT_TRUE(connect().ok());

    GetOptions options;
    options.properties = PropertiesMode::AsQueue;
    ASSERT_TRUE(getWith(options).ok());
    EXPECT_EQ(0, fake.getLog.back().options & kPropertyBits);
    EXPECT_EQ(MQGMO_VERSION_2, fake.getLog.back().gmoVersion) << "the version stays what it was";
    EXPECT_EQ(PropertiesMode::AsQueue, GetOptions().properties) << "the default";
}

TEST_F(GetOptionsFake, ABrowsePreviousKeepsTheMode)
{
    fake.browseCursors = true;
    addTextMessages(kQueue, 3);
    ASSERT_TRUE(connect().ok());

    for (const ModeCase & c : kModeCases)
    {
        GetOptions options;
        options.properties = c.mode;
        options.mode = GetMode::BrowseFirst;
        ASSERT_TRUE(getWith(options).ok());
        options.mode = GetMode::BrowseNext;
        ASSERT_TRUE(getWith(options).ok());
        ASSERT_TRUE(getWith(options).ok());
        ASSERT_EQ(3, session.browsePosition());

        fake.getLog.clear();
        options.mode = GetMode::BrowsePrevious;
        Result r = getWith(options);
        ASSERT_TRUE(r.ok()) << r.text;
        EXPECT_EQ(2, session.browsePosition());
        ASSERT_EQ(2u, fake.getLog.size()) << "the walk to position 2 is a browse first and a browse next";
        for (const rfh_test::GetRecord & sent : fake.getLog)
        {
            EXPECT_EQ(c.flag, sent.options & kPropertyBits) << "every step of the walk carries the mode";
            EXPECT_EQ(MQGMO_VERSION_4, sent.gmoVersion);
        }
    }
}

TEST_F(GetOptionsFake, ModesAreIgnoredBelowV7AndSaySo)
{
    fake.commandLevel = 600;
    ASSERT_TRUE(connect().ok());

    for (const ModeCase & c : kModeCases)
    {
        GetOptions options;
        options.properties = c.mode;
        Result r = getWith(options);
        ASSERT_TRUE(r.ok()) << r.text;
        EXPECT_EQ(0, fake.getLog.back().options & kPropertyBits);
        EXPECT_EQ(MQGMO_VERSION_2, fake.getLog.back().gmoVersion);
        EXPECT_TRUE(anyWarningContains(r, "message properties")) << "the dropped option is reported";
        EXPECT_TRUE(anyWarningContains(r, "600"));
    }

    // AsQueue sends nothing, so there is nothing to report
    GetOptions asQueue;
    Result r = getWith(asQueue);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(GetOptionsFake, ModesApplyFromV7AndWhenTheLevelIsUnknown)
{
    // Windows tests "0 == level || level >= MQCMDL_LEVEL_700" (processMQGet 7690)
    fake.commandLevel = MQCMDL_LEVEL_700;
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.properties = PropertiesMode::None;
    Result r = getWith(options);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(MQGMO_NO_PROPERTIES, fake.getLog.back().options & kPropertyBits);
    EXPECT_TRUE(r.warnings.empty());

    session.disconnect();
    fake.inqRcBySelector[MQIA_COMMAND_LEVEL] = MQRC_SELECTOR_ERROR;
    ASSERT_TRUE(connect().ok());
    ASSERT_EQ(0, session.queueManagerInfo().commandLevel);
    r = getWith(options);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(MQGMO_NO_PROPERTIES, fake.getLog.back().options & kPropertyBits);
    EXPECT_FALSE(anyWarningContains(r, "message properties"));
}

TEST_F(GetOptionsFake, TheModeCombinesWithGroupMatchAndConvertOptions)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.properties = PropertiesMode::Rfh2;
    options.logicalOrder = true;
    options.matchCorrelId = true;
    options.convert = true;
    ASSERT_TRUE(getWith(options).ok());

    const rfh_test::GetRecord & sent = fake.getLog.back();
    EXPECT_EQ(MQGMO_PROPERTIES_FORCE_MQRFH2, sent.options & kPropertyBits);
    EXPECT_NE(0, sent.options & MQGMO_LOGICAL_ORDER);
    EXPECT_NE(0, sent.options & MQGMO_CONVERT);
    EXPECT_EQ(MQMO_MATCH_CORREL_ID, sent.matchOptions);
}

TEST_F(GetOptionsFake, TheModeSurvivesTheBufferGrowingRetry)
{
    rfh_test::FakeMessage m;
    m.data.assign(200, 'x');
    fake.queue(kQueue).messages.push_back(m);
    ASSERT_TRUE(connect().ok());

    GetOptions options;
    options.mode = GetMode::BrowseFirst;
    options.properties = PropertiesMode::Compat;
    options.initialBufferSize = 16;
    Message out;
    Result r = session.get(kQueue, out, options);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(200u, out.body.size());
    ASSERT_GE(fake.getLog.size(), 2u) << "the first get was too small";
    for (const rfh_test::GetRecord & sent : fake.getLog)
    {
        EXPECT_EQ(MQGMO_PROPERTIES_COMPATIBILITY, sent.options & kPropertyBits);
    }
}

TEST_F(GetOptionsFake, ARefusedModeIsAFailureWithTheReasonAndNothingLeaks)
{
    ASSERT_TRUE(connect().ok());
    fake.getRc = MQRC_OPTIONS_ERROR;

    GetOptions options;
    options.properties = PropertiesMode::None;
    Result r = getWith(options);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_OPTIONS_ERROR, r.rc);
    EXPECT_TRUE(session.isConnected());

    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(GetOptionsFake, ConnectionLostDuringAGetWithAModeReleasesEverything)
{
    ASSERT_TRUE(connect().ok());
    fake.getRc = MQRC_CONNECTION_BROKEN;

    GetOptions options;
    options.properties = PropertiesMode::Rfh2;
    Result r = getWith(options);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQRC_CONNECTION_BROKEN, r.rc);
    EXPECT_FALSE(session.isConnected());
    EXPECT_TRUE(fake.liveHandles.empty());
    EXPECT_EQ(0, session.browsePosition());
}

// ---- the converted-too-big browse retry keeps every other option (review follow-up I5) ----
//
// A browse whose converted data does not fit gets the unconverted message with 2120 or 2190 and the browse
// cursor has moved onto it. The retry reads that message again, with a larger buffer, and nothing else may
// change: the group options, the property options, the conversion, NO_SYNCPOINT and the structure version
// all have to be sent again. Only the way of finding the message and the wait differ.

namespace
{
    const MQLONG kLocatingBits = MQGMO_BROWSE_FIRST | MQGMO_BROWSE_NEXT | MQGMO_BROWSE_MSG_UNDER_CURSOR;
    const MQLONG kWaitBits = MQGMO_WAIT | MQGMO_NO_WAIT;
}

TEST_F(GetOptionsFake, TheConvertedTooBigRetryOfABrowseKeepsEveryOtherOption)
{
    for (GetMode mode : { GetMode::BrowseFirst, GetMode::BrowseNext })
    {
        SCOPED_TRACE((int)mode);
        fake.reset();
        MqSession local(fakeApi());
        ConnectOptions connectOptions;
        connectOptions.mode = Mode::Bindings;
        ASSERT_TRUE(local.connect(connectOptions).ok());
        fake.browseCursors = true;
        for (int i = 0; i < 2; i++)
        {
            rfh_test::FakeMessage m;
            m.data.assign(10, (unsigned char)('a' + i));
            fake.queue(kQueue).messages.push_back(m);
        }

        GetOptions options;
        options.mode = GetMode::BrowseFirst;
        options.convert = true;
        options.convertCcsid = 1200;
        options.completeMsg = true;
        options.logicalOrder = true;
        options.allAvailable = true;
        options.properties = PropertiesMode::Rfh2;
        options.waitMs = 0;
        Message out;
        if (mode == GetMode::BrowseNext)
        {
            ASSERT_TRUE(local.get(kQueue, out, options).ok());
            options.mode = GetMode::BrowseNext;
        }
        const size_t firstGet = fake.getLog.size();
        fake.warnGetRc[fake.getCalls + 1] = MQRC_CONVERTED_MSG_TOO_BIG;

        Result r = local.get(kQueue, out, options);
        ASSERT_TRUE(r.ok()) << r.text;
        ASSERT_EQ(firstGet + 2, fake.getLog.size()) << "one get that was too small and one retry";
        const rfh_test::GetRecord & original = fake.getLog[firstGet];
        const rfh_test::GetRecord & retry = fake.getLog[firstGet + 1];

        EXPECT_EQ((original.options & ~(kLocatingBits | kWaitBits)), (retry.options & ~(kLocatingBits | kWaitBits)))
            << "everything but how the message is found and waited for is sent again";
        EXPECT_NE(0, retry.options & MQGMO_COMPLETE_MSG);
        EXPECT_NE(0, retry.options & MQGMO_LOGICAL_ORDER);
        EXPECT_NE(0, retry.options & MQGMO_ALL_MSGS_AVAILABLE);
        EXPECT_NE(0, retry.options & MQGMO_ALL_SEGMENTS_AVAILABLE);
        EXPECT_NE(0, retry.options & MQGMO_CONVERT);
        EXPECT_EQ(original.options & (MQGMO_NO_SYNCPOINT | MQGMO_SYNCPOINT | MQGMO_SYNCPOINT_IF_PERSISTENT),
                  retry.options & (MQGMO_NO_SYNCPOINT | MQGMO_SYNCPOINT | MQGMO_SYNCPOINT_IF_PERSISTENT)) << "a browse sets none of them, and the retry must not add one";
        EXPECT_NE(0, retry.options & MQGMO_FAIL_IF_QUIESCING);
        EXPECT_EQ(MQGMO_PROPERTIES_FORCE_MQRFH2, retry.options & kPropertyBits);
        EXPECT_EQ(0, retry.options & MQGMO_WAIT) << "MQGMO_NO_WAIT is zero";
        EXPECT_EQ(original.gmoVersion, retry.gmoVersion);

        // the message the cursor is on is read again where it is; it is not looked for by MsgId, which
        // MQ answers 2033 to for a message that is not first in its group when LOGICAL_ORDER is set
        EXPECT_EQ(MQGMO_BROWSE_MSG_UNDER_CURSOR, retry.options & kLocatingBits);
        EXPECT_EQ(MQMO_NONE, retry.matchOptions);
        EXPECT_EQ((size_t)(mode == GetMode::BrowseNext ? 2 : 1), (size_t)local.browsePosition());
    }
}

TEST_F(GetOptionsFake, TheConvertedTooBigRetryKeepsTheCallersMatchOptionsOffTheCursorRead)
{
    // the message under the cursor already matched; asking MQ to match again could only lose it
    rfh_test::FakeMessage m;
    m.data.assign(10, 'a');
    fake.queue(kQueue).messages.push_back(m);
    fake.browseCursors = true;
    ASSERT_TRUE(connect().ok());
    fake.warnGetRc[fake.getCalls + 1] = MQRC_CONVERTED_STRING_TOO_BIG;

    GetOptions options;
    options.mode = GetMode::BrowseFirst;
    options.convert = true;
    options.matchCorrelId = true;
    Message out;
    ASSERT_TRUE(session.get(kQueue, out, options).ok());
    ASSERT_EQ(2u, fake.getLog.size());
    EXPECT_EQ(MQMO_MATCH_CORREL_ID, fake.getLog[0].matchOptions);
    EXPECT_EQ(MQMO_NONE, fake.getLog[1].matchOptions);
}
