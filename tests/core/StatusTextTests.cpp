// StatusTextTests.cpp - rfh::statusText against the texts of DataArea::setErrorMsg.
//
// The expected strings were copied from RFHUtil/DataArea.cpp, not produced by the code under test.
// Windows lines: the case labels are at 9332-9865, the default branch at 9866-9876.

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "rfh_status_text.h"

using rfh::Result;
using rfh::StatusContext;
using rfh::statusText;

namespace
{
    Result failed(MQLONG rc)
    {
        Result r;
        r.cc = MQCC_FAILED;
        r.rc = rc;
        return r;
    }

    std::string textFor(MQLONG rc, const char * operation = "Get")
    {
        StatusContext context;
        context.operation = operation;
        return statusText(failed(rc), context);
    }

    struct FixedCase
    {
        MQLONG rc;
        int line;           // line of the case label in RFHUtil/DataArea.cpp
        const char * text;  // the strcpy/sprintf literal at that case
    };

    // Every case of the Windows switch whose text has no substitution (94 of 102), in source order.
    const FixedCase fixedCases[] =
    {
        { MQRC_ALIAS_BASE_Q_TYPE_ERROR, 9332, "2001 Invalid base queue type for alias queue" },
        { MQRC_BUFFER_ERROR, 9337, "2004 Buffer error" },
        { MQRC_BUFFER_LENGTH_ERROR, 9342, "2005 Buffer length error" },
        { MQRC_CONNECTION_BROKEN, 9347, "2009 Connection to QM has been lost - Q closed" },
        { MQRC_EXPIRY_ERROR, 9364, "2013 Invalid expiry value" },
        { MQRC_FEEDBACK_ERROR, 9369, "2014 Invalid feedback value" },
        { MQRC_GET_INHIBITED, 9374, "2016 Queue get inhibited" },
        { MQRC_HCONN_ERROR, 9379, "2018 Connection handle error" },
        { MQRC_HOBJ_ERROR, 9384, "2019 Queue handle error" },
        { MQRC_SYNCPOINT_LIMIT_REACHED, 9389, "2024 Maximum uncommitted messages in UOW exceeded for queue manager" },
        { MQRC_MAX_CONNS_LIMIT_REACHED, 9394, "2025 Maximum connections exceeded for queue manager" },
        { MQRC_MD_ERROR, 9399, "2026 Error in MQMD" },
        { MQRC_MISSING_REPLY_TO_Q, 9404, "2027 Reply to queue name missing - request message or report options specified" },
        { MQRC_MSG_TYPE_ERROR, 9409, "2029 Invalid message type" },
        { MQRC_NO_MSG_AVAILABLE, 9424, "2033 No messages in queue" },
        { MQRC_NOT_OPEN_FOR_INQUIRE, 9435, "2038 Not open for inquire" },
        { MQRC_NOT_OPEN_FOR_OUTPUT, 9440, "2039 Not open for output" },
        { MQRC_OBJECT_CHANGED, 9445, "2041 Object definition changed" },
        { MQRC_OBJECT_IN_USE, 9450, "2042 Object in use" },
        { MQRC_OPTION_NOT_VALID_FOR_TYPE, 9455, "2045 Cannot read remote queue" },
        { MQRC_PERSISTENCE_ERROR, 9465, "2047 Persistence error" },
        { MQRC_PERSISTENT_NOT_ALLOWED, 9470, "2048 Persistence not supported on this queue" },
        { MQRC_PRIORITY_EXCEEDS_MAXIMUM, 9475, "2049 Priority exceeds maximum" },
        { MQRC_PRIORITY_ERROR, 9480, "2050 Invalid priority value" },
        { MQRC_PUT_INHIBITED, 9485, "2051 Put inhibited" },
        { MQRC_Q_DELETED, 9490, "2052 Queue has been deleted" },
        { MQRC_Q_FULL, 9495, "2053 Queue full" },
        { MQRC_Q_SPACE_NOT_AVAILABLE, 9500, "2056 Out of space on queue disk" },
        { MQRC_REPORT_OPTIONS_ERROR, 9524, "2061 Invalid report options" },
        { MQRC_SECURITY_ERROR, 9529, "2063 Security exit rejected connection" },
        { MQRC_STORAGE_NOT_AVAILABLE, 9534, "2071 Main storage not available" },
        { MQRC_TRUNCATED_MSG_ACCEPTED, 9539, "2079 Truncated message accepted" },
        { MQRC_TRUNCATED_MSG_FAILED, 9544, "2080 Message too long for buffer" },
        { MQRC_UNKNOWN_OBJECT_NAME, 9549, "2085 No such queue" },
        { MQRC_UNKNOWN_REMOTE_Q_MGR, 9554, "2087 Invalid remote queue manager name" },
        { MQRC_NOT_OPEN_FOR_PASS_ALL, 9559, "2093 Queue not open for pass all" },
        { MQRC_NOT_OPEN_FOR_PASS_IDENT, 9564, "2094 Queue not open for pass identity" },
        { MQRC_NOT_OPEN_FOR_SET_ALL, 9569, "2095 Queue not open for set all" },
        { MQRC_NOT_OPEN_FOR_SET_IDENT, 9574, "2096 Queue not open for set identity" },
        { MQRC_OBJECT_DAMAGED, 9579, "2101 Object has been corrupted" },
        { MQRC_RESOURCE_PROBLEM, 9584, "2101 Resource shortage - see FFST record" },
        { MQRC_FORMAT_ERROR, 9589, "2110 Convert requested but format does not allow conversion" },
        { MQRC_SOURCE_CCSID_ERROR, 9594, "2111 Invalid or unsupported code page" },
        { MQRC_HEADER_ERROR, 9599, "2142 Invalid MQ header" },
        { MQRC_Q_MGR_QUIESCING, 9604, "2161 Queue manager is quiescing" },
        { MQRC_Q_MGR_STOPPING, 9609, "2162 Queue manager is stopping" },
        { MQRC_STORAGE_MEDIUM_FULL, 9614, "2192 No more space on disk" },
        { MQRC_PAGESET_ERROR, 9619, "2193 Page set error" },
        { MQRC_UNEXPECTED_ERROR, 9624, "2195 Internal error processing request" },
        { MQRC_CONNECTION_QUIESCING, 9629, "2202 Connection is quiescing" },
        { MQRC_CONNECTION_STOPPING, 9638, "2203 Connection is stopping" },
        { MQRC_CD_ERROR, 9643, "2277 Invalid channel name" },
        { MQRC_SELECTOR_NOT_SUPPORTED, 9648, "2318 QMgr does not support selectors" },
        { MQRC_RFH_ERROR, 9653, "2334 Error in RFH" },
        { MQRC_RFH_STRING_ERROR, 9658, "2335 RFH string error" },
        { MQRC_RFH_COMMAND_ERROR, 9663, "2336 RFH command error" },
        { MQRC_RFH_PARM_ERROR, 9668, "2337 RFH parameter error" },
        { MQRC_RFH_DUPLICATE_PARM, 9673, "2338 Duplicate parameter in RFH" },
        { MQRC_RFH_PARM_MISSING, 9678, "2339 Missing parameter in RFH" },
        { MQRC_KEY_REPOSITORY_ERROR, 9683, "2381 Key repository error" },
        { MQRC_SSL_CONFIG_ERROR, 9688, "2392 SSL configuration error" },
        { MQRC_SSL_INITIALIZATION_ERROR, 9693, "2393 SSL unable to initialize - check SSL parms" },
        { MQRC_SSL_NOT_ALLOWED, 9698, "2396 SSL connection not allowed" },
        { MQRC_SSL_PEER_NAME_MISMATCH, 9703, "2398 Peer name mismatch" },
        { MQRC_SSL_PEER_NAME_ERROR, 9708, "2399 Peer name error" },
        { MQRC_SSL_CERTIFICATE_REVOKED, 9713, "2401 Certificate revoked" },
        { MQRC_SSL_CERT_STORE_ERROR, 9718, "2402 Certificate store error" },
        { MQRC_SSL_KEY_RESET_ERROR, 9723, "2409 SSL key reset error" },
        { MQRC_MSG_NOT_ALLOWED_IN_GROUP, 9728, "2417 Published message cannot be part of group" },
        { MQRC_SD_ERROR, 9733, "2424 Error in subscription descriptor" },
        { MQRC_TOPIC_STRING_ERROR, 9738, "2425 Topic string error" },
        { MQRC_NO_SUBSCRIPTION, 9743, "2428 No subscription" },
        { MQRC_SUBSCRIPTION_IN_USE, 9748, "2429 Subscription in use" },
        { MQRC_SUB_USER_DATA_ERROR, 9753, "2431 Subscription user data error" },
        { MQRC_SUB_ALREADY_EXISTS, 9758, "2432 Subscription already exists" },
        { MQRC_SUB_NAME_ERROR, 9763, "2440 Subscription name error" },
        { MQRC_PROPERTY_NAME_ERROR, 9768, "2442 Property name error" },
        { MQRC_SELECTOR_SYNTAX_ERROR, 9773, "2459 Selector syntax error" },
        { MQRC_PROP_TYPE_NOT_SUPPORTED, 9778, "2467 Property type not supported" },
        { MQRC_PROPERTY_TYPE_ERROR, 9783, "2473 Invalid property type" },
        { MQRC_PUBLICATION_FAILURE, 9788, "2502 Publication failed" },
        { MQRC_SUB_INHIBITED, 9793, "2503 Topic is subscribe inhibited" },
        { MQRC_SELECTOR_ALWAYS_FALSE, 9798, "2504 Selector is always false" },
        { MQRC_PROPERTY_NAME_LENGTH_ERR, 9803, "2513 Property name length error" },
        { MQRC_SELECTION_STRING_ERROR, 9808, "2519 Selection string error" },
        { MQRC_SELECTOR_NOT_ALTERABLE, 9813, "2524 Selector cannot be altered" },
        { MQRC_CONNECTION_STOPPED, 9818, "2528 Connection stopped" },
        { MQRC_PUBSUB_INHIBITED, 9827, "2531 Pub/sub inhibited" },
        { MQRC_CHANNEL_NOT_AVAILABLE, 9832, "2537 Channel not available or all connections in use" },
        { MQRC_HOST_NOT_AVAILABLE, 9837, "2538 Host not available" },
        { MQRC_CHANNEL_CONFIG_ERROR, 9842, "2539 channel configuration error" },
        { MQRC_UNKNOWN_CHANNEL_NAME, 9847, "2540 Invalid MQ server connection channel name" },
        { MQRC_STANDBY_Q_MGR, 9852, "2543 Queue manager is in standby mode" },
        { MQRC_ATTRIBUTE_LOCKED, 9857, "6104 Resouce locked - queue has been closed" },
    };

    const size_t fixedCaseCount = sizeof(fixedCases) / sizeof(fixedCases[0]);
}

// DataArea.cpp:9332-9865: all 94 fixed texts, byte for byte, whatever the operation word is.
TEST(StatusText, EveryFixedCaseGivesItsWindowsText)
{
    EXPECT_EQ(94u, fixedCaseCount);

    for (size_t i = 0; i < fixedCaseCount; i++)
    {
        SCOPED_TRACE(std::string("rc ") + std::to_string(fixedCases[i].rc) + " at DataArea.cpp:" + std::to_string(fixedCases[i].line));
        EXPECT_EQ(std::string(fixedCases[i].text), textFor(fixedCases[i].rc, "Put"));
        EXPECT_EQ(std::string(fixedCases[i].text), textFor(fixedCases[i].rc, "Open"));
    }
}

TEST(StatusText, FixedCasesAreDistinctReasonCodes)
{
    std::set<MQLONG> seen;

    for (size_t i = 0; i < fixedCaseCount; i++)
    {
        EXPECT_TRUE(seen.insert(fixedCases[i].rc).second) << fixedCases[i].rc;
    }
}

// DataArea.cpp:9424-9429
TEST(StatusText, NoMessagesInQueue)
{
    EXPECT_EQ("2033 No messages in queue", textFor(MQRC_NO_MSG_AVAILABLE, "Get"));
}

// DataArea.cpp:9347-9358 (the cleanup that Windows does there is not part of the text)
TEST(StatusText, ConnectionLost)
{
    EXPECT_EQ("2009 Connection to QM has been lost - Q closed", textFor(MQRC_CONNECTION_BROKEN, "Get"));
}

// DataArea.cpp:9857-9865: the misspelling "Resouce" is Windows's
TEST(StatusText, AttributeLockedKeepsTheWindowsSpelling)
{
    EXPECT_EQ("6104 Resouce locked - queue has been closed", textFor(MQRC_ATTRIBUTE_LOCKED, "Close"));
}

// DataArea.cpp:9579-9588: two texts start with 2101
TEST(StatusText, ObjectDamagedAndResourceProblemBothStartWith2101)
{
    EXPECT_EQ("2101 Object has been corrupted", textFor(MQRC_OBJECT_DAMAGED));
    EXPECT_EQ("2101 Resource shortage - see FFST record", textFor(MQRC_RESOURCE_PROBLEM));
}

// DataArea.cpp:9430-9434
TEST(StatusText, NotAuthorizedNamesTheOperation)
{
    EXPECT_EQ("2035 Not authorized (Open)", textFor(MQRC_NOT_AUTHORIZED, "Open"));
    EXPECT_EQ("2035 Not authorized (MQSETMP)", textFor(MQRC_NOT_AUTHORIZED, "MQSETMP"));
}

// DataArea.cpp:9460-9464
TEST(StatusText, OptionsErrorNamesTheOperation)
{
    EXPECT_EQ("2046 Invalid option for operation Get (Browse)", textFor(MQRC_OPTIONS_ERROR, "Get (Browse)"));
}

// DataArea.cpp:9505-9509
TEST(StatusText, QueueTypeErrorNamesTheOperation)
{
    EXPECT_EQ("2057 Invalid queue type for operation (Put)", textFor(MQRC_Q_TYPE_ERROR, "Put"));
}

// DataArea.cpp:9510-9514
TEST(StatusText, QueueManagerNameNotFoundNamesTheOperation)
{
    EXPECT_EQ("2058 Queue manager name not found (Connect)", textFor(MQRC_Q_MGR_NAME_ERROR, "Connect"));
}

// DataArea.cpp:9515-9523
TEST(StatusText, QueueManagerNotAvailable)
{
    EXPECT_EQ("2059 Queue manager not available (Connect) - may not be started", textFor(MQRC_Q_MGR_NOT_AVAILABLE, "Connect"));
}

// DataArea.cpp:9359-9363
TEST(StatusText, MaxMessageLengthExceededShowsBothMaxima)
{
    StatusContext context;
    context.operation = "Put";
    context.maxMsgLenQm = 4194304;
    context.maxMsgLenQueue = 1048576;
    EXPECT_EQ("2010 Max msg length exceeded - check Q/QM/Channel defs - QMgr max=4194304 Queue max=1048576",
        statusText(failed(MQRC_DATA_LENGTH_ERROR), context));
}

// DataArea.cpp:9414-9418
TEST(StatusText, MessageTooLargeForQueue)
{
    StatusContext context;
    context.operation = "Put";
    context.maxMsgLenQm = 4194304;
    context.maxMsgLenQueue = 1000;
    EXPECT_EQ("2030 Message too large for queue (check q and qm settings) QMgr max=4194304 Queue max=1000",
        statusText(failed(MQRC_MSG_TOO_BIG_FOR_Q), context));
}

// DataArea.cpp:9419-9423
TEST(StatusText, MessageTooLargeForQueueManager)
{
    StatusContext context;
    context.operation = "Put";
    context.maxMsgLenQm = 100;
    context.maxMsgLenQueue = 0;
    EXPECT_EQ("2031 Message is too large for queue manager QMgr max=100 Queue max=0",
        statusText(failed(MQRC_MSG_TOO_BIG_FOR_Q_MGR), context));
}

// DataArea.cpp:9866-9876, with a name: 2002 is not a case of the switch, mqReasonString knows it
TEST(StatusText, UnknownReasonUsesTheReasonText)
{
    Result r;
    r.cc = MQCC_FAILED;
    r.rc = MQRC_ALREADY_CONNECTED;
    StatusContext context;
    context.operation = "Connect";
    EXPECT_EQ("*Error cc=2 rc=2002 (Already connected) Cannot Connect", statusText(r, context));
}

// DataArea.cpp:9873-9876, without a name
TEST(StatusText, UnknownReasonWithoutAName)
{
    Result r;
    r.cc = MQCC_FAILED;
    r.rc = 99999;
    StatusContext context;
    context.operation = "Put";
    EXPECT_EQ("*Error cc=2 rc=99999 Cannot Put", statusText(r, context));
}

// DataArea.cpp:9871: cc is printed as given, a warning is not changed to a failure
TEST(StatusText, DefaultTextShowsTheCompletionCode)
{
    Result r;
    r.cc = MQCC_WARNING;
    r.rc = 99998;
    StatusContext context;
    context.operation = "Get";
    EXPECT_EQ("*Error cc=1 rc=99998 Cannot Get", statusText(r, context));
}

TEST(StatusText, SuccessIsEmpty)
{
    EXPECT_TRUE(statusText(Result::success(), StatusContext()).empty());
}

TEST(StatusText, NonMqFailureKeepsItsText)
{
    EXPECT_EQ("IBM MQ library not found", statusText(Result::failure("IBM MQ library not found"), StatusContext()));
}

// Every operation word the Windows callers pass (see the header); only the operation texts use them
TEST(StatusText, EveryOperationWordIsAccepted)
{
    const char * words[] =
    {
        "Open", "OpenBase", "Close", "Get", "Get (Browse)", "Get (QBrowse)", "GetNext", "BrowsePrevGet",
        "BrowsePrevGetMsg", "Get under cursor", "Getting message", "Put", "Connect", "Disc", "begin",
        "commit", "rollback", "MQCRTMH", "MQDLTMH", "MQSETMP", "MQINQMP", "MQGETCAPT", "MQSub", "MQSubRQ",
        "OpenSubQ", "CloseSub", "CloseSubQ", "PubOpen", "PubXMQSetMp", "MQCLOSE", "MQCMIT"
    };

    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
    {
        std::string word = words[i];
        SCOPED_TRACE(word);
        EXPECT_EQ("2035 Not authorized (" + word + ")", textFor(MQRC_NOT_AUTHORIZED, words[i]));
        EXPECT_EQ("2046 Invalid option for operation " + word, textFor(MQRC_OPTIONS_ERROR, words[i]));
        EXPECT_EQ("2057 Invalid queue type for operation (" + word + ")", textFor(MQRC_Q_TYPE_ERROR, words[i]));
        EXPECT_EQ("2058 Queue manager name not found (" + word + ")", textFor(MQRC_Q_MGR_NAME_ERROR, words[i]));
        EXPECT_EQ("2059 Queue manager not available (" + word + ") - may not be started", textFor(MQRC_Q_MGR_NOT_AVAILABLE, words[i]));
        EXPECT_EQ("*Error cc=2 rc=99999 Cannot " + word, textFor(99999, words[i]));
    }
}

TEST(StatusText, NullOperationIsTreatedAsEmpty)
{
    StatusContext context;
    context.operation = NULL;
    EXPECT_EQ("2035 Not authorized ()", statusText(failed(MQRC_NOT_AUTHORIZED), context));
}
