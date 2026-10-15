// ResultTests.cpp — tests for rfh::Result, the outcome type of every core operation.

#include <gtest/gtest.h>

#include "rfh_result.h"

// cmqstrc.h defines non-inline functions with external linkage. This translation
// unit includes it on purpose: if rfhcore exported those symbols, linking this test
// would fail with "multiple definition".
#include <cmqstrc.h>

using rfh::Result;

TEST(Result, SuccessIsOkWithoutText)
{
    Result r = Result::success();
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(MQCC_OK, r.cc);
    EXPECT_EQ(MQRC_NONE, r.rc);
    EXPECT_TRUE(r.text.empty());
}

TEST(Result, FailureCarriesTextWithoutReasonCode)
{
    Result r = Result::failure("IBM MQ library not found");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(MQRC_NONE, r.rc);
    EXPECT_EQ("IBM MQ library not found", r.text);
}

TEST(Result, MqSuccessHasNoText)
{
    Result r = Result::fromMq("MQPUT", MQCC_OK, MQRC_NONE);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.text.empty());
}

TEST(Result, MqFailureNamesOperationCodesAndReason)
{
    Result r = Result::fromMq("MQGET", MQCC_FAILED, MQRC_NO_MSG_AVAILABLE);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("MQGET cc=2 rc=2033 MQRC_NO_MSG_AVAILABLE (No message available)", r.text);
}

TEST(Result, MqWarningIsOkAndDescribed)
{
    Result r = Result::fromMq("MQGET", MQCC_WARNING, MQRC_TRUNCATED_MSG_ACCEPTED);
    EXPECT_TRUE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("rc=2079 MQRC_TRUNCATED_MSG_ACCEPTED"));
}

TEST(Result, UnknownReasonCodeIsNamedUnknown)
{
    EXPECT_STREQ("MQRC_UNKNOWN", rfh::reasonName(987654));
    Result r = Result::fromMq("MQOPEN", MQCC_FAILED, 987654);
    EXPECT_EQ("MQOPEN cc=2 rc=987654 MQRC_UNKNOWN", r.text);
}

TEST(Result, ReasonNameDoesNotExportCmqstrcSymbols)
{
    // The link of this executable is the proof; see the include of <cmqstrc.h> above.
    EXPECT_STREQ(MQRC_STR(MQRC_NO_MSG_AVAILABLE), rfh::reasonName(MQRC_NO_MSG_AVAILABLE));
}
