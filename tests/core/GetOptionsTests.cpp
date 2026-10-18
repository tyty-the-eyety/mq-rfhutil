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
    EXPECT_EQ(MQOO_INPUT_AS_Q_DEF | MQOO_BROWSE | MQOO_FAIL_IF_QUIESCING, open.options);
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
    EXPECT_EQ(MQOO_INPUT_AS_Q_DEF | MQOO_BROWSE | MQOO_FAIL_IF_QUIESCING, open.options) << "the selector adds no open option";
}

TEST_F(GetOptionsFake, ASelectorIsNotCutToAnythingShorter)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.selector = std::string(MQ_SELECTOR_LENGTH - 1, 'a');
    ASSERT_TRUE(getWith(options).ok());
    EXPECT_EQ(options.selector, queueOpens()[0].selector);
}

TEST_F(GetOptionsFake, ASelectorLongerThanWindowsKeepsIsRefusedNotCut)
{
    // Windows copies at most MQ_SELECTOR_LENGTH - 1 characters and selects with the rest dropped
    // (openQ 12087-12091). A cut selector selects something else, so the core refuses it.
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.selector = std::string(MQ_SELECTOR_LENGTH, 'a');
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
    EXPECT_EQ(MQOO_INPUT_AS_Q_DEF | MQOO_BROWSE | MQOO_FAIL_IF_QUIESCING | MQOO_ALTERNATE_USER_AUTHORITY, queueOpens()[0].options);
    EXPECT_EQ("APPUSER", queueOpens()[0].alternateUser);
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(GetOptionsFake, AnAlternateUserOverTwelveCharactersIsCutAsWindowsCutsItAndReported)
{
    ASSERT_TRUE(connect().ok());
    GetOptions options;
    options.alternateUser = "ABCDEFGHIJKLMNOP";
    Result r = getWith(options);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ("ABCDEFGHIJKL", queueOpens()[0].alternateUser);
    EXPECT_TRUE(anyWarningContains(r, "12 characters"));
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
