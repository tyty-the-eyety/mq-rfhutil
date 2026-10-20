// MqmdFieldsTests.cpp — tests for rfh::mqmd_fields, the port of
// MQMDPAGE::buildMQMD / processMQMD. Expected MQMD bytes come from mq_layout.h,
// which lays the structure out at IBM's documented offsets.

#include <gtest/gtest.h>

#include <stddef.h>
#include <limits>
#include <string>
#include <vector>
#include <string.h>

#include <cmqc.h>

#include "rfh_header_chain.h"
#include "rfh_mqmd_fields.h"
#include "mq_layout.h"

using rfh::MqmdFields;
namespace L = mq_layout;
namespace O = mq_layout::mqmd;

namespace
{
    std::vector<unsigned char> bytesOf(const MQMD2 & md)
    {
        const unsigned char * p = reinterpret_cast<const unsigned char *>(&md);
        return std::vector<unsigned char>(p, p + sizeof(MQMD2));
    }

    MQMD2 built(const MqmdFields & f)
    {
        MQMD2 md = { MQMD2_DEFAULT };
        md.Version = MQMD_VERSION_2;
        rfh::mqmd_fields::toMqmd(f, md);
        return md;
    }
}

TEST(MqmdFields, StructureMatchesDocumentedLayout)
{
    ASSERT_EQ(L::mqmd::Length2, sizeof(MQMD2));
    ASSERT_EQ(L::mqmd::Length1, sizeof(MQMD1));

    EXPECT_EQ(O::Report, offsetof(MQMD2, Report));
    EXPECT_EQ(O::MsgType, offsetof(MQMD2, MsgType));
    EXPECT_EQ(O::Expiry, offsetof(MQMD2, Expiry));
    EXPECT_EQ(O::Feedback, offsetof(MQMD2, Feedback));
    EXPECT_EQ(O::Encoding, offsetof(MQMD2, Encoding));
    EXPECT_EQ(O::CodedCharSetId, offsetof(MQMD2, CodedCharSetId));
    EXPECT_EQ(O::Format, offsetof(MQMD2, Format));
    EXPECT_EQ(O::Priority, offsetof(MQMD2, Priority));
    EXPECT_EQ(O::Persistence, offsetof(MQMD2, Persistence));
    EXPECT_EQ(O::MsgId, offsetof(MQMD2, MsgId));
    EXPECT_EQ(O::CorrelId, offsetof(MQMD2, CorrelId));
    EXPECT_EQ(O::BackoutCount, offsetof(MQMD2, BackoutCount));
    EXPECT_EQ(O::ReplyToQ, offsetof(MQMD2, ReplyToQ));
    EXPECT_EQ(O::ReplyToQMgr, offsetof(MQMD2, ReplyToQMgr));
    EXPECT_EQ(O::UserIdentifier, offsetof(MQMD2, UserIdentifier));
    EXPECT_EQ(O::AccountingToken, offsetof(MQMD2, AccountingToken));
    EXPECT_EQ(O::ApplIdentityData, offsetof(MQMD2, ApplIdentityData));
    EXPECT_EQ(O::PutApplType, offsetof(MQMD2, PutApplType));
    EXPECT_EQ(O::PutApplName, offsetof(MQMD2, PutApplName));
    EXPECT_EQ(O::PutDate, offsetof(MQMD2, PutDate));
    EXPECT_EQ(O::PutTime, offsetof(MQMD2, PutTime));
    EXPECT_EQ(O::ApplOriginData, offsetof(MQMD2, ApplOriginData));
    EXPECT_EQ(O::GroupId, offsetof(MQMD2, GroupId));
    EXPECT_EQ(O::MsgSeqNumber, offsetof(MQMD2, MsgSeqNumber));
    EXPECT_EQ(O::Offset, offsetof(MQMD2, Offset));
    EXPECT_EQ(O::MsgFlags, offsetof(MQMD2, MsgFlags));
    EXPECT_EQ(O::OriginalLength, offsetof(MQMD2, OriginalLength));
}

TEST(MqmdFields, DefaultLayoutMatchesMqmd2DefaultNumbers)
{
    MQMD2 md = { MQMD2_DEFAULT };
    std::vector<unsigned char> raw = bytesOf(md);
    L::Bytes d = L::defaultMqmd2();
    const size_t numeric[] = { 4, O::Report, O::MsgType, O::Expiry, O::Feedback, O::Encoding,
        O::CodedCharSetId, O::Priority, O::Persistence, O::BackoutCount, O::PutApplType,
        O::MsgSeqNumber, O::Offset, O::MsgFlags, O::OriginalLength };
    for (size_t offset : numeric)
    {
        EXPECT_EQ(0, memcmp(&raw[offset], &d.data[offset], 4)) << "offset " << offset;
    }

    const struct { size_t offset, length; } bytes[] = { { 0, 4 }, { O::Format, 8 }, { O::MsgId, 24 },
        { O::CorrelId, 24 }, { O::AccountingToken, 32 }, { O::PutDate, 8 }, { O::PutTime, 8 },
        { O::GroupId, 24 } };
    for (const auto & range : bytes)
    {
        EXPECT_EQ(0, memcmp(&raw[range.offset], &d.data[range.offset], range.length)) << "offset " << range.offset;
    }
}

TEST(MqmdFields, ClearMatchesClearMQMD)
{
    MqmdFields f;
    f.format = "x"; f.ccsid = "x"; f.msgType = "x"; f.expiry = "x"; f.feedback = "x";
    f.priority = "x"; f.backoutCount = "x"; f.replyToQ = "x"; f.replyToQMgr = "x";
    f.userId = "x"; f.accountingToken = "x"; f.applIdentity = "x"; f.applOrigin = "x";
    f.putApplName = "x"; f.putApplType = "x"; f.dateTime = "x"; f.seqNo = "x";
    f.offset = "x"; f.originalLength = "x";
    f.persistence = MQPERSIST_YES;
    f.encoding = NUMERIC_HOST;
    f.pdEncoding = NUMERIC_HOST;
    f.floatEncoding = FLOAT_390;
    f.reportException = f.reportExpiration = f.reportCoa = f.reportCod = REPORT_FULL;
    f.reportPan = f.reportNan = f.reportActivity = f.reportDiscard = true;
    f.reportPassMsgId = f.reportPassCorrelId = f.reportPassDiscardAndExpiry = true;
    f.groupYes = f.groupLast = f.segmentYes = f.segmentLast = f.segmentAllowed = true;
    memset(f.msgId, 1, sizeof(f.msgId));
    memset(f.correlId, 2, sizeof(f.correlId));
    memset(f.groupId, 3, sizeof(f.groupId));

    rfh::mqmd_fields::clear(f);

    // the literal values clearMQMD sets
    EXPECT_EQ("0", f.backoutCount);
    EXPECT_EQ("1", f.seqNo);
    EXPECT_EQ("-1", f.originalLength);
    for (const std::string * text : { &f.format, &f.ccsid, &f.msgType, &f.expiry, &f.feedback,
        &f.priority, &f.replyToQ, &f.replyToQMgr, &f.userId, &f.accountingToken, &f.applIdentity,
        &f.applOrigin, &f.putApplName, &f.putApplType, &f.dateTime, &f.offset })
    {
        EXPECT_EQ("", *text);
    }
    EXPECT_EQ(0, f.persistence);
    EXPECT_EQ(0, f.encoding);
    EXPECT_EQ(0, f.pdEncoding);
    EXPECT_EQ(-1, f.reportException);
    EXPECT_EQ(-1, f.reportExpiration);
    EXPECT_EQ(-1, f.reportCoa);
    EXPECT_EQ(-1, f.reportCod);
    EXPECT_FALSE(f.reportPan);
    EXPECT_FALSE(f.reportNan);
    EXPECT_FALSE(f.reportActivity);
    EXPECT_FALSE(f.reportDiscard);
    EXPECT_FALSE(f.reportPassMsgId);
    EXPECT_FALSE(f.reportPassCorrelId);
    EXPECT_FALSE(f.reportPassDiscardAndExpiry);
    EXPECT_FALSE(f.groupYes);
    EXPECT_FALSE(f.groupLast);
    EXPECT_FALSE(f.segmentYes);
    EXPECT_FALSE(f.segmentLast);
    EXPECT_FALSE(f.segmentAllowed);
    const unsigned char zeros[24] = { 0 };
    EXPECT_EQ(0, memcmp(zeros, f.msgId, 24));
    EXPECT_EQ(0, memcmp(zeros, f.correlId, 24));
    EXPECT_EQ(0, memcmp(zeros, f.groupId, 24));
    EXPECT_EQ(FLOAT_390, f.floatEncoding);
}

TEST(MqmdFields, DefaultFieldsGiveDocumentedDefaultsExceptTabValues)
{
    // An untouched MQMD tab sets persistence "No", PC encodings, backout 0,
    // sequence 1, original length -1, and blank text fields.
    L::Bytes expected = L::defaultMqmd2();
    expected.putLong(O::Persistence, 0);            // MQPER_NOT_PERSISTENT
    expected.putLong(O::Encoding, 546);

    EXPECT_EQ(expected.data, bytesOf(built(MqmdFields())));
}

TEST(MqmdFields, EveryFieldLandsAtItsOffset)
{
    MqmdFields f;
    f.format = "MQSTR";
    f.ccsid = "1208";
    f.msgType = "1 Request";
    f.expiry = "6000";
    f.feedback = "0";
    f.priority = "4";
    f.persistence = MQPERSIST_YES;
    f.backoutCount = "2";
    f.replyToQ = "REPLY.Q";
    f.replyToQMgr = "QM2";
    f.userId = "tyron";
    f.accountingToken = "0102";
    f.applIdentity = "ident";
    f.applOrigin = "ori";
    f.putApplName = "rfhutil";
    f.putApplType = "6 Unix";
    f.dateTime = "2026/10/16 13:45:07.89";
    f.seqNo = "3";
    f.offset = "100";
    f.originalLength = "500";
    f.encoding = NUMERIC_HOST;
    f.pdEncoding = NUMERIC_HOST;
    f.floatEncoding = FLOAT_390;
    f.reportCoa = REPORT_FULL;
    f.reportPan = true;
    f.groupYes = true;
    f.segmentLast = true;
    memcpy(f.msgId, "MSGID-123456789012345678", 24);
    memcpy(f.correlId, "CORRELID-123456789012345", 24);
    memcpy(f.groupId, "GROUPID-1234567890123456", 24);

    L::Bytes e = L::defaultMqmd2();
    e.putLong(O::Report, MQRO_COA_WITH_FULL_DATA | MQRO_PAN);
    e.putLong(O::MsgType, 1);
    e.putLong(O::Expiry, 6000);
    e.putLong(O::Feedback, 0);
    e.putLong(O::Encoding, 785);
    e.putLong(O::CodedCharSetId, 1208);
    e.putChars(O::Format, 8, "MQSTR");
    e.putLong(O::Priority, 4);
    e.putLong(O::Persistence, 1);
    e.putBytes(O::MsgId, "MSGID-123456789012345678", 24);
    e.putBytes(O::CorrelId, "CORRELID-123456789012345", 24);
    e.putLong(O::BackoutCount, 2);
    e.putChars(O::ReplyToQ, 48, "REPLY.Q");
    e.putChars(O::ReplyToQMgr, 48, "QM2");
    e.putChars(O::UserIdentifier, 12, "tyron");
    const unsigned char token[2] = { 0x01, 0x02 };
    e.putBytes(O::AccountingToken, token, 2);
    e.putChars(O::ApplIdentityData, 32, "ident");
    e.putLong(O::PutApplType, 6);
    e.putChars(O::PutApplName, 28, "rfhutil");
    e.putChars(O::PutDate, 8, "20261016");
    e.putChars(O::PutTime, 8, "13450789");
    e.putChars(O::ApplOriginData, 4, "ori");
    e.putBytes(O::GroupId, "GROUPID-1234567890123456", 24);
    e.putLong(O::MsgSeqNumber, 3);
    e.putLong(O::Offset, 100);
    e.putLong(O::MsgFlags, MQMF_MSG_IN_GROUP | MQMF_LAST_SEGMENT);
    e.putLong(O::OriginalLength, 500);

    EXPECT_EQ(e.data, bytesOf(built(f)));
}

TEST(MqmdFields, EmptyTextFieldsKeepTheGivenMqmdValues)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.Expiry = 77;
    md.Priority = 9;
    md.CodedCharSetId = 437;
    MqmdFields f;
    f.backoutCount.clear();
    rfh::mqmd_fields::toMqmd(f, md);
    EXPECT_EQ(77, md.Expiry);
    EXPECT_EQ(9, md.Priority);
    EXPECT_EQ(437, md.CodedCharSetId);
}

TEST(MqmdFields, UnknownMsgTypeIsIgnoredApplicationTypeIsKept)
{
    MqmdFields f;
    f.msgType = "5";
    EXPECT_EQ(MQMT_DATAGRAM, built(f).MsgType);
    f.msgType = "65536";
    EXPECT_EQ(65536, built(f).MsgType);
}

TEST(MqmdFields, FullLengthApplicationFieldsAreDroppedHistoric)
{
    // Windows tests "length < field size", so a value that fills the field is dropped.
    MqmdFields f;
    f.applIdentity = std::string(32, 'I');
    f.applOrigin = "ABCD";
    f.putApplName = std::string(27, 'N');
    MQMD2 md = built(f);
    EXPECT_EQ(0, memcmp(md.ApplIdentityData, std::string(32, ' ').data(), 32));
    EXPECT_EQ(0, memcmp(md.ApplOriginData, "    ", 4));
    EXPECT_EQ(0, memcmp(md.PutApplName, std::string(27, 'N').data(), 27));
}

TEST(MqmdFields, ShortDateTimeLeavesZeroBytesHistoric)
{
    MqmdFields f;
    f.dateTime = "2026/10";
    MQMD2 md = built(f);
    EXPECT_EQ(0, memcmp(md.PutDate, "202610\0\0", 8));
    EXPECT_EQ(0, memcmp(md.PutTime, "\0\0\0\0\0\0\0\0", 8));
}

TEST(MqmdFields, LongFieldsAreTruncated)
{
    MqmdFields f;
    f.format = "TOOLONGFORMAT";
    f.userId = "averyveryverylonguser";
    MQMD2 md = built(f);
    EXPECT_EQ(0, memcmp(md.Format, "TOOLONGF", 8));
    EXPECT_EQ(0, memcmp(md.UserIdentifier, "averyveryver", 12));
    EXPECT_EQ(-1, md.Priority) << "the format must not overrun into Priority";
}

TEST(MqmdFields, ShortFormatOverwritesOnlyItsBytes)
{
    MQMD2 md = { MQMD2_DEFAULT };
    memcpy(md.Format, "MQHRF2  ", 8);
    MqmdFields f;
    f.format = "MQ";
    rfh::mqmd_fields::toMqmd(f, md);
    EXPECT_EQ(0, memcmp(md.Format, "MQHRF2  ", 8)) << "Windows copies without blank filling";
}

TEST(MqmdFields, FromMqmdReadsEveryField)
{
    L::Bytes e = L::defaultMqmd2();
    e.putLong(O::Report, MQRO_EXCEPTION_WITH_DATA | MQRO_COD | MQRO_PASS_MSG_ID | MQRO_NAN);
    e.putLong(O::MsgType, 2);
    e.putLong(O::Expiry, 100);
    e.putLong(O::Encoding, 273);
    e.putLong(O::CodedCharSetId, 819);
    e.putChars(O::Format, 8, "MQSTR");
    e.putLong(O::Priority, 7);
    e.putLong(O::Persistence, 1);
    e.putBytes(O::MsgId, "abcdefghijklmnopqrstuvwx", 24);
    e.putChars(O::ReplyToQ, 48, "R.Q");
    e.putChars(O::UserIdentifier, 12, "me");
    const unsigned char token[3] = { 0xAB, 0x00, 0x7F };
    e.putBytes(O::AccountingToken, token, 3);
    e.putLong(O::PutApplType, 28);
    e.putChars(O::PutApplName, 28, "app");
    e.putChars(O::PutDate, 8, "20261016");
    e.putChars(O::PutTime, 8, "01020304");
    e.putLong(O::MsgSeqNumber, 5);
    e.putLong(O::MsgFlags, MQMF_SEGMENTATION_ALLOWED);
    e.putLong(O::OriginalLength, 42);

    MQMD2 md;
    memcpy(&md, e.data.data(), sizeof(md));
    MqmdFields f;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));

    EXPECT_EQ(REPORT_DATA, f.reportException);
    EXPECT_EQ(REPORT_YES, f.reportCod);
    EXPECT_EQ(REPORT_NONE, f.reportCoa);
    EXPECT_TRUE(f.reportPassMsgId);
    EXPECT_TRUE(f.reportNan);
    EXPECT_FALSE(f.reportPan);
    EXPECT_EQ("2 Reply", f.msgType);
    EXPECT_EQ("100", f.expiry);
    EXPECT_EQ(NUMERIC_HOST, f.encoding);
    EXPECT_EQ(NUMERIC_HOST, f.pdEncoding);
    EXPECT_EQ("819", f.ccsid);
    EXPECT_EQ("MQSTR", f.format);
    EXPECT_EQ("7", f.priority);
    EXPECT_EQ(MQPERSIST_YES, f.persistence);
    EXPECT_EQ(0, memcmp(f.msgId, "abcdefghijklmnopqrstuvwx", 24));
    EXPECT_EQ("R.Q", f.replyToQ);
    EXPECT_EQ("me", f.userId);
    EXPECT_EQ("AB007F" + std::string(58, '0'), f.accountingToken);
    EXPECT_EQ("28 Java", f.putApplType);
    EXPECT_EQ("app", f.putApplName);
    EXPECT_EQ("2026/10/16 01:02:03.04", f.dateTime);
    EXPECT_EQ("5", f.seqNo);
    EXPECT_TRUE(f.segmentAllowed);
    EXPECT_FALSE(f.groupYes);
    EXPECT_EQ("42", f.originalLength);
}

TEST(MqmdFields, FromMqmdKeepsFloatEncodingAsOnWindows)
{
    MQMD2 md = { MQMD2_DEFAULT };
    MqmdFields f;
    f.floatEncoding = FLOAT_TNS;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
    EXPECT_EQ(FLOAT_TNS, f.floatEncoding);
}

TEST(MqmdFields, FromMqmdVersion1LeavesVersion2FieldsCleared)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = MQMD_VERSION_1;
    md.MsgSeqNumber = 9;
    md.MsgFlags = MQMF_MSG_IN_GROUP;
    MqmdFields f;
    f.groupYes = true;
    f.seqNo = "4";
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
    EXPECT_EQ("1", f.seqNo);
    EXPECT_FALSE(f.groupYes);
    EXPECT_EQ("-1", f.originalLength);
}

TEST(MqmdFields, FromMqmdRejectsOtherVersions)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = 3;
    MqmdFields f;
    f.ccsid = "unchanged";
    EXPECT_FALSE(rfh::mqmd_fields::fromMqmd(md, f));
    EXPECT_EQ("unchanged", f.ccsid);
}

TEST(MqmdFields, FormatStopsAtFirstControlOrBlankHistoric)
{
    MQMD2 md = { MQMD2_DEFAULT };
    memcpy(md.Format, "MQ STR  ", 8);
    MqmdFields f;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
    EXPECT_EQ("MQ", f.format);
}

TEST(MqmdFields, ApplicationDefinedMsgTypeIsNotShownHistoric)
{
    EXPECT_EQ("", rfh::mqmd_fields::msgTypeText(65536));
    EXPECT_EQ("8 Datagram", rfh::mqmd_fields::msgTypeText(8));
}

TEST(MqmdFields, RoundTripThroughFieldsIsByteIdentical)
{
    L::Bytes e = L::defaultMqmd2();
    e.putLong(O::Report, MQRO_EXPIRATION_WITH_FULL_DATA | MQRO_ACTIVITY);
    e.putLong(O::MsgType, 4);
    e.putLong(O::Expiry, 12);
    e.putLong(O::Feedback, 3);
    e.putLong(O::CodedCharSetId, 1208);
    e.putChars(O::Format, 8, "MQHRF2");
    e.putLong(O::Priority, 0);
    e.putLong(O::Persistence, 0);
    e.putBytes(O::CorrelId, "123456789012345678901234", 24);
    e.putChars(O::ReplyToQMgr, 48, "QMX");
    e.putChars(O::ApplIdentityData, 32, "id");
    e.putLong(O::PutApplType, 11);
    e.putChars(O::PutApplName, 28, "prog");
    e.putChars(O::PutDate, 8, "20251231");
    e.putChars(O::PutTime, 8, "23595999");
    e.putChars(O::ApplOriginData, 4, "or");
    e.putLong(O::MsgSeqNumber, 1);
    e.putLong(O::OriginalLength, -1);

    MQMD2 in;
    memcpy(&in, e.data.data(), sizeof(in));
    MqmdFields f;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(in, f));
    EXPECT_EQ(e.data, bytesOf(built(f)));
}

TEST(MqmdFields, FromMqmdWithZeroDateAndTimeGivesJustTheSeparator)
{
    MQMD2 md = { MQMD2_DEFAULT };
    MqmdFields f;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
    EXPECT_EQ(" ", f.dateTime);
}

TEST(MqmdFields, BlankDateTimeTextWritesOnlyOneBlankHistoric)
{
    MqmdFields f;
    f.dateTime = " ";
    MQMD2 md = built(f);
    EXPECT_EQ(0, memcmp(md.PutDate, " \0\0\0\0\0\0\0", 8));
    EXPECT_EQ(0, memcmp(md.PutTime, "\0\0\0\0\0\0\0\0", 8));
}

TEST(MqmdFields, NulInPutDateCutsOnlyTheDatePartHistoric)
{
    MQMD2 md = { MQMD2_DEFAULT };
    memcpy(md.PutDate, "2026\0\0\0\0", 8);
    memcpy(md.PutTime, "12345678", 8);
    MqmdFields f;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
    EXPECT_EQ("2026/ 12:34:56.78", f.dateTime);
}

TEST(MqmdFields, FormatBytesAbove7FEndTheTextHistoric)
{
    MQMD2 md = { MQMD2_DEFAULT };
    memcpy(md.Format, "MQ\xC1STR  ", 8);
    MqmdFields f;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
    EXPECT_EQ("MQ", f.format);
}

TEST(MqmdFields, UnknownPutApplTypeRoundTrip)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.PutApplType = 65536;
    MqmdFields f;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
    EXPECT_EQ("", f.putApplType);
    MQMD2 out = { MQMD2_DEFAULT };
    out.PutApplType = 77;
    rfh::mqmd_fields::toMqmd(f, out);
    EXPECT_EQ(77, out.PutApplType);
}

TEST(MqmdFields, ReportChoicesRoundTripWithPrecedence)
{
    struct Group { MQLONG base, data, full; int MqmdFields::*member; } groups[] = {
        { MQRO_EXCEPTION, MQRO_EXCEPTION_WITH_DATA, MQRO_EXCEPTION_WITH_FULL_DATA, &MqmdFields::reportException },
        { MQRO_EXPIRATION, MQRO_EXPIRATION_WITH_DATA, MQRO_EXPIRATION_WITH_FULL_DATA, &MqmdFields::reportExpiration },
        { MQRO_COA, MQRO_COA_WITH_DATA, MQRO_COA_WITH_FULL_DATA, &MqmdFields::reportCoa },
        { MQRO_COD, MQRO_COD_WITH_DATA, MQRO_COD_WITH_FULL_DATA, &MqmdFields::reportCod } };
    for (const Group & g : groups)
    {
        const MQLONG reports[] = { g.base, g.data, g.full };
        const int choices[] = { REPORT_YES, REPORT_DATA, REPORT_FULL };
        for (int i = 0; i < 3; i++)
        {
            MQMD2 md = { MQMD2_DEFAULT };
            md.Report = reports[i];
            MqmdFields f;
            ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
            EXPECT_EQ(choices[i], f.*(g.member));
            MqmdFields back;
            back.*(g.member) = choices[i];
            EXPECT_EQ(reports[i], built(back).Report);
        }
        MqmdFields none;
        none.*(g.member) = -1;
        EXPECT_EQ(0, built(none).Report);
    }
}

TEST(MqmdFields, AccountingTokenHexHandling)
{
    MqmdFields f;
    f.accountingToken = "abc";
    MQMD2 md = built(f);
    const unsigned char odd[2] = { 0xAB, 0x00 };
    EXPECT_EQ(0, memcmp(md.AccountingToken, odd, 2));

    f.accountingToken = std::string(70, '1');
    md = built(f);
    for (int i = 0; i < 32; i++)
    {
        EXPECT_EQ(0x11, md.AccountingToken[i]) << i;
    }

    f.accountingToken = "abcdef";
    md = built(f);
    const unsigned char lower[3] = { 0xAB, 0xCD, 0xEF };
    EXPECT_EQ(0, memcmp(md.AccountingToken, lower, 3));
}

TEST(MqmdFields, FromMqmdVersion1StillCopiesGroupId)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = MQMD_VERSION_1;
    memcpy(md.GroupId, "GROUPID-1234567890123456", 24);
    MqmdFields f;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, f));
    EXPECT_EQ(0, memcmp(f.groupId, "GROUPID-1234567890123456", 24));
}

TEST(MqmdFields, ApplyMdExtensionSetsVersion2Fields)
{
    rfh::MdExtensionFields e = {};
    memcpy(e.groupId, "GROUPID-1234567890123456", 24);
    e.msgSeqNumber = 7;
    e.offset = 100;
    e.originalLength = 250;
    e.msgFlags = MQMF_MSG_IN_GROUP | MQMF_LAST_SEGMENT | MQMF_SEGMENTATION_ALLOWED;

    MqmdFields f;
    f.format = "MQSTR";
    f.msgType = "8 Datagram";
    f.segmentYes = true;
    f.groupLast = true;
    rfh::mqmd_fields::applyMdExtension(e, f);

    EXPECT_EQ(0, memcmp(f.groupId, "GROUPID-1234567890123456", 24));
    EXPECT_EQ("7", f.seqNo);
    EXPECT_EQ("100", f.offset);
    EXPECT_EQ("250", f.originalLength);
    EXPECT_TRUE(f.groupYes);
    EXPECT_FALSE(f.groupLast);
    EXPECT_FALSE(f.segmentYes);
    EXPECT_TRUE(f.segmentLast);
    EXPECT_TRUE(f.segmentAllowed);
    // nothing outside the version 2 fields changes
    EXPECT_EQ("MQSTR", f.format);
    EXPECT_EQ("8 Datagram", f.msgType);
}

// ---------------------------------------------------------------------------
// toPutMqmd and applyAfterPut: MQMDPAGE::setMessageMQMD (MQMDPAGE.cpp:2408-2692) and
// MQMDPAGE::updateMQMDafterPut (MQMDPAGE.cpp:2694-2735). Expected bytes are built by
// hand from the Windows source with mq_layout.h.

namespace
{
    using rfh::PutContext;

    struct FieldSpan
    {
        const char * name;
        size_t offset;
        size_t length;
    };

    // every byte of an MQMD2, so a comparison covers the whole structure
    const FieldSpan mqmdSpans[] = {
        { "StrucId", 0, 4 }, { "Version", 4, 4 }, { "Report", O::Report, 4 },
        { "MsgType", O::MsgType, 4 }, { "Expiry", O::Expiry, 4 }, { "Feedback", O::Feedback, 4 },
        { "Encoding", O::Encoding, 4 }, { "CodedCharSetId", O::CodedCharSetId, 4 },
        { "Format", O::Format, 8 }, { "Priority", O::Priority, 4 },
        { "Persistence", O::Persistence, 4 }, { "MsgId", O::MsgId, 24 },
        { "CorrelId", O::CorrelId, 24 }, { "BackoutCount", O::BackoutCount, 4 },
        { "ReplyToQ", O::ReplyToQ, 48 }, { "ReplyToQMgr", O::ReplyToQMgr, 48 },
        { "UserIdentifier", O::UserIdentifier, 12 }, { "AccountingToken", O::AccountingToken, 32 },
        { "ApplIdentityData", O::ApplIdentityData, 32 }, { "PutApplType", O::PutApplType, 4 },
        { "PutApplName", O::PutApplName, 28 }, { "PutDate", O::PutDate, 8 },
        { "PutTime", O::PutTime, 8 }, { "ApplOriginData", O::ApplOriginData, 4 },
        { "GroupId", O::GroupId, 24 }, { "MsgSeqNumber", O::MsgSeqNumber, 4 },
        { "Offset", O::Offset, 4 }, { "MsgFlags", O::MsgFlags, 4 },
        { "OriginalLength", O::OriginalLength, 4 } };

    // Compares every field and names each one that differs.
    void expectMqmd(const L::Bytes & expected, const MQMD2 & actual)
    {
        const unsigned char * got = reinterpret_cast<const unsigned char *>(&actual);
        ASSERT_EQ(sizeof(MQMD2), expected.data.size());
        for (const FieldSpan & span : mqmdSpans)
        {
            if (memcmp(&expected.data[span.offset], got + span.offset, span.length) != 0)
            {
                ADD_FAILURE() << "MQMD field " << span.name << " (offset " << span.offset << ") differs";
            }
        }
    }

    // The bytes of MQMD2_DEFAULT: defaultMqmd2 with the text fields that the C macro
    // leaves as zero bytes (Format is blank).
    L::Bytes windowsDefault()
    {
        L::Bytes b = L::defaultMqmd2();
        const size_t zeroed[][2] = { { O::ReplyToQ, 48 }, { O::ReplyToQMgr, 48 }, { O::UserIdentifier, 12 },
            { O::ApplIdentityData, 32 }, { O::PutApplName, 28 }, { O::ApplOriginData, 4 } };
        for (const auto & z : zeroed)
        {
            memset(&b.data[z[0]], 0, z[1]);
        }
        return b;
    }

    // What the put leaves for a page in its cleared state: Format is always written
    // (blank filled), Encoding and Persistence too.
    L::Bytes clearedPutExpectation()
    {
        L::Bytes b = windowsDefault();
        b.putChars(O::Format, 8, "");
        b.putLong(O::Persistence, 0, L::Order::Little);     // MQPER_NOT_PERSISTENT
        return b;
    }

    MQMD2 putBuilt(const MqmdFields & f, const PutContext & c)
    {
        MQMD2 md = { MQMD2_DEFAULT };
        rfh::mqmd_fields::toPutMqmd(f, c, md);
        return md;
    }

    MqmdFields cleared()
    {
        MqmdFields f;
        rfh::mqmd_fields::clear(f);
        return f;
    }

    PutContext none() { return PutContext(); }
    PutContext setAll() { PutContext c; c.setAll = true; return c; }
    PutContext setIdentity() { PutContext c; c.setIdentity = true; return c; }
}

TEST(ToPutMqmd, DefaultsMatchTheHandBuiltBaseline)
{
    // the baseline the other tests start from is the real MQMD2_DEFAULT
    MQMD2 md = { MQMD2_DEFAULT };
    expectMqmd(windowsDefault(), md);
}

TEST(ToPutMqmd, ClearedPageWritesOnlyFormatEncodingAndPersistence)
{
    // MQMDPAGE.cpp:2448 Format, :2456 Encoding, :2465 Persistence are unconditional.
    // The cleared page has seq no 1, backout count 0, original length -1, all of which
    // equal the default; backout count is never written.
    expectMqmd(clearedPutExpectation(), putBuilt(cleared(), none()));
}

TEST(ToPutMqmd, ZeroIdsAreNotCopiedNonZeroOnesAre)
{
    // MQMDPAGE.cpp:2496, 2502, 2521: an id is copied only when it differs from MQMI_NONE,
    // MQCI_NONE, MQGI_NONE. A zero id leaves the default, so start from a changed default.
    MqmdFields f = cleared();
    MQMD2 md = { MQMD2_DEFAULT };
    memset(md.MsgId, 0x55, sizeof(md.MsgId));
    memset(md.CorrelId, 0x66, sizeof(md.CorrelId));
    memset(md.GroupId, 0x77, sizeof(md.GroupId));
    rfh::mqmd_fields::toPutMqmd(f, none(), md);

    L::Bytes expected = clearedPutExpectation();
    memset(&expected.data[O::MsgId], 0x55, 24);
    memset(&expected.data[O::CorrelId], 0x66, 24);
    memset(&expected.data[O::GroupId], 0x77, 24);
    expectMqmd(expected, md);

    // non-zero ids replace them; the group id alone sets MQMF_LAST_MSG_IN_GROUP (2527)
    memcpy(f.msgId, "MSGID---MSGID---MSGID---", 24);
    memcpy(f.correlId, "CORREL--CORREL--CORREL--", 24);
    memcpy(f.groupId, "GROUP---GROUP---GROUP---", 24);
    md = { MQMD2_DEFAULT };
    memset(md.MsgId, 0x55, sizeof(md.MsgId));
    rfh::mqmd_fields::toPutMqmd(f, none(), md);

    expected = clearedPutExpectation();
    expected.putBytes(O::MsgId, "MSGID---MSGID---MSGID---", 24);
    expected.putBytes(O::CorrelId, "CORREL--CORREL--CORREL--", 24);
    expected.putBytes(O::GroupId, "GROUP---GROUP---GROUP---", 24);
    expected.putLong(O::MsgFlags, 0x10);    // MQMF_LAST_MSG_IN_GROUP
    expectMqmd(expected, md);
}

TEST(ToPutMqmd, GroupIdKeepsTheCheckedFlagsInsteadOfLastMsgInGroup)
{
    // MQMDPAGE.cpp:2525: MsgFlags becomes MQMF_LAST_MSG_IN_GROUP only when no flag is set
    MqmdFields f = cleared();
    memcpy(f.groupId, "GROUP---GROUP---GROUP---", 24);
    f.groupYes = true;
    L::Bytes expected = clearedPutExpectation();
    expected.putBytes(O::GroupId, "GROUP---GROUP---GROUP---", 24);
    expected.putLong(O::MsgFlags, 0x08);    // MQMF_MSG_IN_GROUP only
    expectMqmd(expected, putBuilt(f, none()));
}

TEST(ToPutMqmd, NumericTextFieldsAreWrittenOnlyWhenNotEmpty)
{
    // MQMDPAGE.cpp:2459 ccsid, :2467 priority, :2474 expiry, :2479 feedback,
    // :2485 original length, :2490 sequence number
    MqmdFields f = cleared();
    f.ccsid = "1208";
    f.priority = "5";
    f.expiry = "600";
    f.feedback = "258";
    f.originalLength = "99";
    f.seqNo = "7";
    f.backoutCount = "3";   // never written
    L::Bytes expected = clearedPutExpectation();
    expected.putLong(O::CodedCharSetId, 1208);
    expected.putLong(O::Priority, 5);
    expected.putLong(O::Expiry, 600);
    expected.putLong(O::Feedback, 258);
    expected.putLong(O::OriginalLength, 99);
    expected.putLong(O::MsgSeqNumber, 7);
    expectMqmd(expected, putBuilt(f, none()));

    // empty texts keep the defaults
    f.ccsid = f.priority = f.expiry = f.feedback = f.originalLength = f.seqNo = "";
    expectMqmd(clearedPutExpectation(), putBuilt(f, none()));
}

TEST(ToPutMqmd, MessageTypeOnlyForTheAcceptedTypes)
{
    // MQMDPAGE.cpp:2531-2546: 1, 2, 4, 8, 112, 113 and the application range 65536..999999999
    MqmdFields f = cleared();
    const int accepted[] = { 1, 2, 4, 8, 112, 113, 65536, 999999999 };
    for (int type : accepted)
    {
        f.msgType = std::to_string(type) + " text";
        L::Bytes expected = clearedPutExpectation();
        expected.putLong(O::MsgType, type);
        expectMqmd(expected, putBuilt(f, none()));
    }
    const int rejected[] = { 0, 3, 5, 111, 114, 65535, 1000000000 };
    for (int type : rejected)
    {
        f.msgType = std::to_string(type);
        expectMqmd(clearedPutExpectation(), putBuilt(f, none()));
    }
}

TEST(ToPutMqmd, ReportOptionsAreSetOnlyWhenNotZero)
{
    // MQMDPAGE.cpp:2549-2554 with buildReportOptions (2204-2356)
    MqmdFields f = cleared();
    f.reportException = REPORT_FULL;
    f.reportCoa = REPORT_YES;
    f.reportPan = true;
    f.reportPassMsgId = true;
    f.reportDiscard = true;
    L::Bytes expected = clearedPutExpectation();
    // EXCEPTION_WITH_FULL_DATA 0x07000000, COA 0x00000100, PAN 0x01, PASS_MSG_ID 0x00000080,
    // DISCARD_MSG 0x08000000
    expected.putLong(O::Report, 0x07000000 | 0x00000100 | 0x00000001 | 0x00000080 | 0x08000000);
    expectMqmd(expected, putBuilt(f, none()));

    // no report option leaves the Report of the starting MQMD alone
    MQMD2 md = { MQMD2_DEFAULT };
    md.Report = 0x00000040;
    rfh::mqmd_fields::toPutMqmd(cleared(), none(), md);
    expected = clearedPutExpectation();
    expected.putLong(O::Report, 0x40);
    expectMqmd(expected, md);
}

TEST(ToPutMqmd, FlagsAndOffset)
{
    // MQMDPAGE.cpp:2507-2518: the flags and the offset are written together, and the
    // offset only when at least one flag box is checked
    MqmdFields f = cleared();
    f.offset = "512";
    expectMqmd(clearedPutExpectation(), putBuilt(f, none()));

    f.groupYes = true;
    f.groupLast = true;
    f.segmentAllowed = true;
    L::Bytes expected = clearedPutExpectation();
    expected.putLong(O::MsgFlags, 0x08 | 0x10 | 0x01);  // group, last in group, segmentation allowed
    expected.putLong(O::Offset, 512);
    expectMqmd(expected, putBuilt(f, none()));
}

TEST(ToPutMqmd, SegmentSetsFlagsAndOffsetWithNonNumericOffsetAsZero)
{
    // MQMDPAGE.cpp:2557-2564 repeats the offset for a segment; atoi of text with no digits is 0
    MqmdFields f = cleared();
    f.segmentYes = true;
    f.segmentLast = true;
    f.offset = "x9";
    L::Bytes expected = clearedPutExpectation();
    expected.putLong(O::MsgFlags, 0x02 | 0x04);   // MQMF_SEGMENT | MQMF_LAST_SEGMENT
    expected.putLong(O::Offset, 0);
    MQMD2 md = { MQMD2_DEFAULT };
    md.Offset = 77;
    rfh::mqmd_fields::toPutMqmd(f, none(), md);
    expectMqmd(expected, md);
}

TEST(ToPutMqmd, FormatIsBlankPaddedAndCutToEight)
{
    // MQMDPAGE.cpp:2442-2448
    MqmdFields f = cleared();
    f.format = "MQSTR";
    L::Bytes expected = clearedPutExpectation();
    expected.putChars(O::Format, 8, "MQSTR");
    expectMqmd(expected, putBuilt(f, none()));

    f.format = "MQHRF2XYZ";
    expected.putChars(O::Format, 8, "MQHRF2XY");
    expectMqmd(expected, putBuilt(f, none()));
}

TEST(ToPutMqmd, ReplyFieldsAreCopiedWithoutPaddingAndNeverPastTheField)
{
    // MQMDPAGE.cpp:2451-2452 copies GetLength() bytes over the MQMD2_DEFAULT contents.
    // The default holds zero bytes there, so a short text is followed by zeros, not
    // blanks; a longer text than the field would overrun on Windows and is cut here.
    MqmdFields f = cleared();
    f.replyToQ = "REPLY.Q";
    f.replyToQMgr = "QM1";
    L::Bytes expected = clearedPutExpectation();
    memcpy(&expected.data[O::ReplyToQ], "REPLY.Q", 7);
    memcpy(&expected.data[O::ReplyToQMgr], "QM1", 3);
    expectMqmd(expected, putBuilt(f, none()));

    // content already in the MQMD stays behind a shorter text
    MQMD2 md = { MQMD2_DEFAULT };
    memset(md.ReplyToQ, '.', sizeof(md.ReplyToQ));
    rfh::mqmd_fields::toPutMqmd(f, none(), md);
    expected = clearedPutExpectation();
    memset(&expected.data[O::ReplyToQ], '.', 48);
    memcpy(&expected.data[O::ReplyToQ], "REPLY.Q", 7);
    memcpy(&expected.data[O::ReplyToQMgr], "QM1", 3);
    expectMqmd(expected, md);

    // 60 characters: only 48 are stored, and the field after it is untouched
    f.replyToQ = std::string(60, 'Q');
    f.replyToQMgr = "";
    expected = clearedPutExpectation();
    memset(&expected.data[O::ReplyToQ], 'Q', 48);
    expectMqmd(expected, putBuilt(f, none()));
}

TEST(ToPutMqmd, EncodingFollowsTheThreeRadioGroups)
{
    // MQMDPAGE.cpp:2455 getEncodingValue
    MqmdFields f = cleared();
    f.encoding = NUMERIC_HOST;
    f.pdEncoding = NUMERIC_HOST;
    f.floatEncoding = FLOAT_390;
    L::Bytes expected = clearedPutExpectation();
    // integers normal (0x1), packed decimal normal (0x10), float S/390 (0x300)
    expected.putLong(O::Encoding, 0x311);
    expectMqmd(expected, putBuilt(f, none()));
}

TEST(ToPutMqmd, PersistenceFollowsTheRadioGroup)
{
    // MQMDPAGE.cpp:2465 and getPersistence (2859)
    MqmdFields f = cleared();
    L::Bytes expected = clearedPutExpectation();
    f.persistence = MQPERSIST_YES;
    expected.putLong(O::Persistence, 1);
    expectMqmd(expected, putBuilt(f, none()));
    f.persistence = MQPERSIST_AS_Q;
    expected.putLong(O::Persistence, 2);
    expectMqmd(expected, putBuilt(f, none()));

    // getPersistence starts from MQPER_PERSISTENCE_AS_Q_DEF and the switch has no default:
    // any other radio value (a stale or unset control) means "as queue default"
    const int others[] = { 3, 99, -1, 0x7FFFFFFF };
    for (int other : others)
    {
        SCOPED_TRACE(other);
        f.persistence = other;
        expectMqmd(expected, putBuilt(f, none()));
    }
}

TEST(ToPutMqmd, IdentityAndApplFieldsAreIgnoredWithoutSetIdentityOrSetAll)
{
    // MQMDPAGE.cpp:2567: everything below depends on m_setUserID || m_set_all
    MqmdFields f = cleared();
    f.userId = "USER";
    f.accountingToken = "0102";
    f.applIdentity = "IDENT";
    f.applOrigin = "ORG";
    f.putApplName = "APPL";
    f.putApplType = "6 Unix";
    f.dateTime = "2026/10/17 12:34:56.78";
    expectMqmd(clearedPutExpectation(), putBuilt(f, none()));
}

TEST(ToPutMqmd, UserIdAndAccountingTokenWithSetIdentity)
{
    // MQMDPAGE.cpp:2569-2572 setUserId (4028-4050): zero padded, 12 at most;
    // 2606-2628 the token is hex converted: at most 48 bytes, 32 stored
    MqmdFields f = cleared();
    f.userId = "USER1234567890";
    f.accountingToken = "01FFa0";
    f.applIdentity = "IDENT";
    f.putApplType = "6 Unix";
    f.dateTime = "2026/10/17 12:34:56.78";
    L::Bytes expected = clearedPutExpectation();
    expected.putBytes(O::UserIdentifier, "USER12345678", 12);
    const unsigned char token[3] = { 0x01, 0xFF, 0xA0 };
    expected.putBytes(O::AccountingToken, token, 3);
    // Set Iden does not reach the Appl fields or the date
    expectMqmd(expected, putBuilt(f, setIdentity()));

    f.userId = "AB";
    memset(&expected.data[O::UserIdentifier], 0, 12);
    memcpy(&expected.data[O::UserIdentifier], "AB", 2);
    expectMqmd(expected, putBuilt(f, setIdentity()));
}

TEST(ToPutMqmd, AccountingTokenOfFortyEightBytesStoresThirtyTwo)
{
    // MQMDPAGE.cpp:2617: at most sizeof(tempToken) = 48 bytes converted; 32 copied
    MqmdFields f = cleared();
    std::string hex;
    for (int i = 0; i < 50; i++)
    {
        hex += "A1";
    }
    f.accountingToken = hex;    // 50 bytes of text, only 48 converted
    L::Bytes expected = clearedPutExpectation();
    memset(&expected.data[O::AccountingToken], 0xA1, 32);
    expectMqmd(expected, putBuilt(f, setIdentity()));
}

TEST(ToPutMqmd, BlankUserIdBecomesZerosNotTheOsUser)
{
    // MQMDPAGE.cpp:2573-2604: the OS user is looked up into the local userId and then
    // setUserId(mqmd) is called, which reads m_mqmd_userid (empty) and so zeroes
    // UserIdentifier. The looked-up name never reaches the MQMD. If the lookup fails,
    // nothing is written. Pinned as Windows behaviour (the brief expected the OS user).
    MqmdFields f = cleared();
    PutContext c = setIdentity();
    c.osUser = "someone";
    MQMD2 md = { MQMD2_DEFAULT };
    memset(md.UserIdentifier, 'x', sizeof(md.UserIdentifier));
    rfh::mqmd_fields::toPutMqmd(f, c, md);
    expectMqmd(clearedPutExpectation(), md);    // baseline has zeros there

    // without Set Iden or Set All nothing is touched
    md = { MQMD2_DEFAULT };
    memset(md.UserIdentifier, 'x', sizeof(md.UserIdentifier));
    c.setIdentity = false;
    rfh::mqmd_fields::toPutMqmd(f, c, md);
    L::Bytes expected = clearedPutExpectation();
    memset(&expected.data[O::UserIdentifier], 'x', 12);
    expectMqmd(expected, md);

    // lookup failed (empty name): the field stays as it was
    c = setAll();
    md = { MQMD2_DEFAULT };
    memset(md.UserIdentifier, 'x', sizeof(md.UserIdentifier));
    rfh::mqmd_fields::toPutMqmd(f, c, md);
    expectMqmd(expected, md);
}

TEST(ToPutMqmd, ApplFieldsAndPutDateTimeOnlyWithSetAll)
{
    // MQMDPAGE.cpp:2630-2689
    MqmdFields f = cleared();
    f.applIdentity = "IDENT";
    f.applOrigin = "ORG";
    f.putApplName = "APPLICATION";
    f.putApplType = "6 Unix";
    f.dateTime = "2026/10/17 12:34:56.78";
    L::Bytes expected = clearedPutExpectation();
    memcpy(&expected.data[O::ApplIdentityData], "IDENT", 5);
    memcpy(&expected.data[O::ApplOriginData], "ORG", 3);
    memcpy(&expected.data[O::PutApplName], "APPLICATION", 11);
    expected.putLong(O::PutApplType, 6);
    expected.putBytes(O::PutDate, "20261017", 8);
    expected.putBytes(O::PutTime, "12345678", 8);
    expectMqmd(expected, putBuilt(f, setAll()));
    // Set Iden alone leaves them
    expectMqmd(clearedPutExpectation(), putBuilt(f, setIdentity()));
}

namespace
{
    // An MQMD whose identity and application fields hold non-zero bytes, so that a field the
    // put leaves alone can be told from one it zero fills (the default is all zeros).
    MQMD2 presetIdentityMqmd()
    {
        MQMD2 md = { MQMD2_DEFAULT };
        memset(md.UserIdentifier, 'u', sizeof(md.UserIdentifier));
        memset(md.AccountingToken, 'a', sizeof(md.AccountingToken));
        memset(md.ApplIdentityData, 'i', sizeof(md.ApplIdentityData));
        memset(md.ApplOriginData, 'o', sizeof(md.ApplOriginData));
        memset(md.PutApplName, 'p', sizeof(md.PutApplName));
        return md;
    }

    // What the cleared page leaves of presetIdentityMqmd under Set All: those five fields as
    // preset, and the fields Set All always writes (Put Appl Type 0, blank date/time zeros).
    L::Bytes presetIdentityExpectation()
    {
        L::Bytes b = clearedPutExpectation();
        memset(&b.data[O::UserIdentifier], 'u', 12);
        memset(&b.data[O::AccountingToken], 'a', 32);
        memset(&b.data[O::ApplIdentityData], 'i', 32);
        memset(&b.data[O::ApplOriginData], 'o', 4);
        memset(&b.data[O::PutApplName], 'p', 28);
        b.putLong(O::PutApplType, 0);
        memset(&b.data[O::PutDate], 0, 16);
        return b;
    }
}

TEST(ToPutMqmd, ApplFieldLengthLimitsAndZeroPadding)
{
    // :2632 identity has no length limit but only 32 bytes are stored; :2639 origin and
    // :2646 name are ignored when longer than their fields (4, 28). The MQMD starts with
    // non-zero bytes, so "ignored" (bytes kept) differs from "zero filled" and "written".
    MqmdFields f = cleared();
    f.applIdentity = std::string(40, 'I');
    f.applOrigin = "ABCDE";
    f.putApplName = std::string(29, 'N');
    L::Bytes expected = presetIdentityExpectation();
    memset(&expected.data[O::ApplIdentityData], 'I', 32);
    MQMD2 md = presetIdentityMqmd();
    rfh::mqmd_fields::toPutMqmd(f, setAll(), md);
    expectMqmd(expected, md);

    // exactly full fields are stored, unlike toMqmd which drops them
    f.applOrigin = "ABCD";
    f.putApplName = std::string(28, 'N');
    memcpy(&expected.data[O::ApplOriginData], "ABCD", 4);
    memset(&expected.data[O::PutApplName], 'N', 28);
    md = presetIdentityMqmd();
    rfh::mqmd_fields::toPutMqmd(f, setAll(), md);
    expectMqmd(expected, md);

    // a short text is zero padded: the old bytes do not show behind it
    f.applIdentity = "ID";
    f.applOrigin = "AB";
    f.putApplName = "NM";
    memset(&expected.data[O::ApplIdentityData], 0, 32);
    memcpy(&expected.data[O::ApplIdentityData], "ID", 2);
    memset(&expected.data[O::ApplOriginData], 0, 4);
    memcpy(&expected.data[O::ApplOriginData], "AB", 2);
    memset(&expected.data[O::PutApplName], 0, 28);
    memcpy(&expected.data[O::PutApplName], "NM", 2);
    md = presetIdentityMqmd();
    rfh::mqmd_fields::toPutMqmd(f, setAll(), md);
    expectMqmd(expected, md);
}

TEST(ToPutMqmd, EmptyIdentityAndApplFieldsLeaveTheMqmdBytesUnderSetAll)
{
    // :2569-2603 an empty User Id with no OS user looked up writes nothing; :2606 an empty
    // token, :2630 an empty Appl Identity, Origin and Name are skipped. Only Put Appl Type
    // and the date/time are written unconditionally.
    MqmdFields f = cleared();
    MQMD2 md = presetIdentityMqmd();
    rfh::mqmd_fields::toPutMqmd(f, setAll(), md);
    expectMqmd(presetIdentityExpectation(), md);

    // the same under Set Iden alone: the Set All fields are also not reached
    md = presetIdentityMqmd();
    rfh::mqmd_fields::toPutMqmd(f, setIdentity(), md);
    L::Bytes expected = presetIdentityExpectation();
    expected.putLong(O::PutApplType, 0);
    memcpy(&expected.data[O::PutDate], windowsDefault().data.data() + O::PutDate, 16);
    expectMqmd(expected, md);
}

TEST(ToPutMqmd, EmptyApplTypeIsZeroWithSetAll)
{
    // :2653 PutApplType = atoi(text), unconditional under Set All
    MqmdFields f = cleared();
    MQMD2 md = { MQMD2_DEFAULT };
    md.PutApplType = 9;
    rfh::mqmd_fields::toPutMqmd(f, setAll(), md);
    L::Bytes expected = clearedPutExpectation();
    expected.putLong(O::PutApplType, 0);
    expectMqmd(expected, md);
}

TEST(ToPutMqmd, BlankPutDateTimeIsZerosEvenWhenAClockIsGiven)
{
    // :2668-2674 formats the current time into todaysDate but :2678-2689 read the zeroed
    // tempToken, so a blank Put Date/Time is written as binary zeros. Pinned.
    MqmdFields f = cleared();
    PutContext c = setAll();
    struct tm now = {};
    now.tm_year = 126;
    now.tm_mon = 9;
    now.tm_mday = 17;
    now.tm_hour = 12;
    c.now = &now;
    MQMD2 md = { MQMD2_DEFAULT };
    memset(md.PutDate, 'D', sizeof(md.PutDate));
    memset(md.PutTime, 'T', sizeof(md.PutTime));
    rfh::mqmd_fields::toPutMqmd(f, c, md);
    L::Bytes expected = clearedPutExpectation();
    expected.putLong(O::PutApplType, 0);
    memset(&expected.data[O::PutDate], 0, 16);
    expectMqmd(expected, md);
}

TEST(ToPutMqmd, ShortPutDateTimeLeavesZeroBytes)
{
    // :2660-2667 copies at most 47 characters into a zeroed buffer
    MqmdFields f = cleared();
    f.dateTime = "2026/10/17";
    L::Bytes expected = clearedPutExpectation();
    expected.putBytes(O::PutDate, "20261017", 8);
    memset(&expected.data[O::PutTime], 0, 8);
    expectMqmd(expected, putBuilt(f, setAll()));
}

TEST(ToPutMqmd, UserIdAndAccountingTokenWithSetAllAlone)
{
    // :2567 the test is m_setUserID || m_set_all, so Set All alone reaches :2569-2628 too
    MqmdFields f = cleared();
    f.userId = "USER";
    f.accountingToken = "01FFa0";
    MQMD2 md = presetIdentityMqmd();
    rfh::mqmd_fields::toPutMqmd(f, setAll(), md);
    L::Bytes expected = presetIdentityExpectation();
    memset(&expected.data[O::UserIdentifier], 0, 12);
    memcpy(&expected.data[O::UserIdentifier], "USER", 4);
    memset(&expected.data[O::AccountingToken], 0, 32);
    const unsigned char token[3] = { 0x01, 0xFF, 0xA0 };
    expected.putBytes(O::AccountingToken, token, 3);
    expectMqmd(expected, md);
}

TEST(ToPutMqmd, AccountingTokenTextThatIsOddOrNotHex)
{
    // :2606-2628 len = text length / 2 bytes are converted by HexToAscii (comsubs.cpp:170)
    // and all 32 bytes of the zeroed work area are then copied, so an old token never
    // survives a non-empty text. getHexCharValue (comsubs.cpp:114) gives 0 for any
    // character that is not 0-9, A-F or a-f. The odd last character is not read.
    struct Case
    {
        const char * text;
        std::vector<unsigned char> bytes;
    };
    const Case cases[] =
    {
        { "ABC", { 0xAB } },                // the odd last character is dropped
        { "A", { } },                       // zero bytes converted: the token is zeroed
        { "ZZ12", { 0x00, 0x12 } },         // non-hex characters count as 0
        { "G1", { 0x01 } },
        { "1Z", { 0x10 } },
        { "-f", { 0x0F } },
        { " 1", { 0x01 } },
        { "ab", { 0xAB } },
        { "0g9", { 0x00 } },
    };

    for (const Case & c : cases)
    {
        SCOPED_TRACE(c.text);
        MqmdFields f = cleared();
        f.accountingToken = c.text;
        MQMD2 md = presetIdentityMqmd();
        rfh::mqmd_fields::toPutMqmd(f, setIdentity(), md);
        L::Bytes expected = presetIdentityExpectation();
        expected.putLong(O::PutApplType, 0);
        memcpy(&expected.data[O::PutDate], windowsDefault().data.data() + O::PutDate, 16);
        memset(&expected.data[O::AccountingToken], 0, 32);
        if (!c.bytes.empty())
        {
            expected.putBytes(O::AccountingToken, c.bytes.data(), c.bytes.size());
        }
        expectMqmd(expected, md);
    }
}

TEST(ToPutMqmd, PutDateTimeTextLongerThanFortySevenCharactersIsCut)
{
    // :2660-2667 at most sizeof(tempToken) - 1 = 47 characters are copied; only the first
    // 22 are read, so the result is that of the 22 character text and nothing else changes
    MqmdFields f = cleared();
    f.dateTime = "2026/10/17 12:34:56.78" + std::string(60, 'Z');
    ASSERT_GT(f.dateTime.size(), 47u);
    MQMD2 md = presetIdentityMqmd();
    rfh::mqmd_fields::toPutMqmd(f, setAll(), md);
    L::Bytes expected = presetIdentityExpectation();
    expected.putBytes(O::PutDate, "20261017", 8);
    expected.putBytes(O::PutTime, "12345678", 8);
    expectMqmd(expected, md);
}

TEST(ToPutMqmd, MessageTypeTextIsReadByAtoi)
{
    // atoi skips leading white space and takes a sign; only the accepted types are written
    // (the default MQMD2 type is 8, so the accepted cases use other types)
    struct Case
    {
        const char * text;
        MQLONG type;
    };
    const Case cases[] =
    {
        { "   2 Reply", MQMT_REPLY },
        { " +2", MQMT_REPLY },
        { "\t 4abc", MQMT_REPORT },
        { "+1", MQMT_REQUEST },
        { "-1", MQMT_DATAGRAM },        // negative: not accepted, the default stays
        { "- 2", MQMT_DATAGRAM },       // a sign followed by a blank is no number: 0
        { "+ 2", MQMT_DATAGRAM },
        { "x2", MQMT_DATAGRAM },
        { " -8", MQMT_DATAGRAM },
    };

    for (const Case & c : cases)
    {
        SCOPED_TRACE(c.text);
        MqmdFields f = cleared();
        f.msgType = c.text;
        L::Bytes expected = clearedPutExpectation();
        expected.putLong(O::MsgType, c.type);
        expectMqmd(expected, putBuilt(f, none()));
    }
}

TEST(ToPutMqmd, StrucIdVersionAndBackoutCountAreNotWritten)
{
    MqmdFields f = cleared();
    f.backoutCount = "9";
    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = MQMD_VERSION_1;
    md.BackoutCount = 4;
    rfh::mqmd_fields::toPutMqmd(f, setAll(), md);
    EXPECT_EQ(MQMD_VERSION_1, md.Version) << "MQMD field Version was written";
    EXPECT_EQ(4, md.BackoutCount) << "MQMD field BackoutCount was written";
    EXPECT_EQ(0, memcmp(md.StrucId, MQMD_STRUC_ID, 4)) << "MQMD field StrucId was written";
}

TEST(ToPutMqmd, ToMqmdIsUnchanged)
{
    // the file-write path keeps its rules: it copies the ids as they are, even when zero,
    // blank fills the user id and clears the Appl fields without Set All
    MqmdFields f = cleared();
    f.userId = "USER";
    f.applIdentity = "IDENT";
    memcpy(f.msgId, "MSGID---MSGID---MSGID---", 24);
    MQMD2 md = built(f);
    L::Bytes expected = L::defaultMqmd2();
    expected.putBytes(O::MsgId, "MSGID---MSGID---MSGID---", 24);
    expected.putChars(O::UserIdentifier, 12, "USER");
    expected.putChars(O::ApplIdentityData, 32, "IDENT");
    expected.putChars(O::Format, 8, "");
    expected.putLong(O::Persistence, 0);
    expected.putChars(O::PutApplName, 28, "");
    expected.putChars(O::ApplOriginData, 4, "");
    memset(&expected.data[O::ReplyToQ], ' ', 96);
    expected.putLong(4, 2);
    expectMqmd(expected, md);
}

TEST(ApplyAfterPut, ChangesExactlyFourFields)
{
    // MQMDPAGE.cpp:2708-2734
    MqmdFields f;
    f.format = "FMT";
    f.ccsid = "1";
    f.msgType = "8 Datagram";
    f.expiry = "e";
    f.feedback = "f";
    f.priority = "p";
    f.backoutCount = "b";
    f.replyToQ = "rq";
    f.replyToQMgr = "rqm";
    f.userId = "u";
    f.accountingToken = "ab";
    f.applIdentity = "ai";
    f.applOrigin = "ao";
    f.putApplName = "pn";
    f.putApplType = "6 Unix";
    f.dateTime = "dt";
    f.originalLength = "ol";
    f.persistence = MQPERSIST_YES;
    f.encoding = NUMERIC_HOST;
    f.pdEncoding = NUMERIC_HOST;
    f.floatEncoding = FLOAT_UNIX;
    f.reportException = REPORT_DATA;
    f.reportPan = true;
    f.segmentAllowed = true;
    memset(f.correlId, 0xCC, sizeof(f.correlId));
    const MqmdFields before = f;

    MQMD2 returned = { MQMD2_DEFAULT };
    memset(returned.MsgId, 0xAA, sizeof(returned.MsgId));
    returned.MsgSeqNumber = 12;
    returned.Offset = 100;
    f.groupYes = true;
    memset(returned.GroupId, 0xBB, sizeof(returned.GroupId));
    MqmdFields expected = before;
    expected.groupYes = true;
    expected.seqNo = "12";
    expected.offset = "0";
    memset(expected.msgId, 0xAA, sizeof(expected.msgId));
    memset(expected.groupId, 0xBB, sizeof(expected.groupId));
    rfh::mqmd_fields::applyAfterPut(returned, 50, f);

    EXPECT_EQ(expected.seqNo, f.seqNo) << "field seqNo";
    EXPECT_EQ(expected.offset, f.offset) << "field offset";
    EXPECT_EQ(0, memcmp(expected.msgId, f.msgId, 24)) << "field msgId";
    EXPECT_EQ(0, memcmp(expected.groupId, f.groupId, 24)) << "field groupId";
    EXPECT_EQ(0, memcmp(expected.correlId, f.correlId, 24)) << "field correlId";
    EXPECT_EQ(expected.format, f.format) << "field format";
    EXPECT_EQ(expected.ccsid, f.ccsid) << "field ccsid";
    EXPECT_EQ(expected.msgType, f.msgType) << "field msgType";
    EXPECT_EQ(expected.expiry, f.expiry) << "field expiry";
    EXPECT_EQ(expected.feedback, f.feedback) << "field feedback";
    EXPECT_EQ(expected.priority, f.priority) << "field priority";
    EXPECT_EQ(expected.backoutCount, f.backoutCount) << "field backoutCount";
    EXPECT_EQ(expected.replyToQ, f.replyToQ) << "field replyToQ";
    EXPECT_EQ(expected.replyToQMgr, f.replyToQMgr) << "field replyToQMgr";
    EXPECT_EQ(expected.userId, f.userId) << "field userId";
    EXPECT_EQ(expected.accountingToken, f.accountingToken) << "field accountingToken";
    EXPECT_EQ(expected.applIdentity, f.applIdentity) << "field applIdentity";
    EXPECT_EQ(expected.applOrigin, f.applOrigin) << "field applOrigin";
    EXPECT_EQ(expected.putApplName, f.putApplName) << "field putApplName";
    EXPECT_EQ(expected.putApplType, f.putApplType) << "field putApplType";
    EXPECT_EQ(expected.dateTime, f.dateTime) << "field dateTime";
    EXPECT_EQ(expected.originalLength, f.originalLength) << "field originalLength";
    EXPECT_EQ(expected.persistence, f.persistence) << "field persistence";
    EXPECT_EQ(expected.encoding, f.encoding) << "field encoding";
    EXPECT_EQ(expected.pdEncoding, f.pdEncoding) << "field pdEncoding";
    EXPECT_EQ(expected.floatEncoding, f.floatEncoding) << "field floatEncoding";
    EXPECT_EQ(expected.reportException, f.reportException) << "field reportException";
    EXPECT_EQ(expected.reportPan, f.reportPan) << "field reportPan";
    EXPECT_EQ(expected.segmentAllowed, f.segmentAllowed) << "field segmentAllowed";
    EXPECT_EQ(expected.groupYes, f.groupYes) << "field groupYes";
    EXPECT_EQ(expected.groupLast, f.groupLast) << "field groupLast";
    EXPECT_EQ(expected.segmentYes, f.segmentYes) << "field segmentYes";
    EXPECT_EQ(expected.segmentLast, f.segmentLast) << "field segmentLast";
}

TEST(ApplyAfterPut, OffsetAdvancesOnlyWhileASegmentSequenceContinues)
{
    // :2712-2721
    MQMD2 returned = { MQMD2_DEFAULT };
    returned.Offset = 100;

    MqmdFields f;
    f.segmentYes = true;
    rfh::mqmd_fields::applyAfterPut(returned, 50, f);
    EXPECT_EQ("150", f.offset) << "segment in progress";

    f.segmentLast = true;
    rfh::mqmd_fields::applyAfterPut(returned, 50, f);
    EXPECT_EQ("0", f.offset) << "last segment";

    f.segmentYes = false;
    f.segmentLast = false;
    f.offset = "77";
    rfh::mqmd_fields::applyAfterPut(returned, 50, f);
    EXPECT_EQ("0", f.offset) << "no segmentation";
}

TEST(ApplyAfterPut, NegativeAndOverflowingOffsetsFollowTwoComplementArithmetic)
{
    // :2715 formats mqmd->Offset + msgLen as %d. Both are 32 bit and the sum is signed
    // overflow (undefined in C, a wrap in two's complement as MSVC compiles it). The port
    // does the sum unsigned and converts back, so it wraps and never traps; a negative
    // returned Offset is subtracted from.
    struct Case
    {
        MQLONG offset;
        MQLONG msgLen;
        const char * text;
    };
    const Case cases[] =
    {
        { -10, 50, "40" },
        { -100, 50, "-50" },
        { -50, 50, "0" },
        { 2147483647, 0, "2147483647" },
        { 2147483647, 1, "-2147483648" },
        { 2147483647, 2147483647, "-2" },
        { -2147483647 - 1, -1, "2147483647" },
        { 100, -150, "-50" },
    };

    for (const Case & c : cases)
    {
        SCOPED_TRACE(std::to_string(c.offset) + " + " + std::to_string(c.msgLen));
        MQMD2 returned = { MQMD2_DEFAULT };
        returned.Offset = c.offset;
        MqmdFields f;
        f.segmentYes = true;
        rfh::mqmd_fields::applyAfterPut(returned, c.msgLen, f);
        EXPECT_EQ(c.text, f.offset);
    }
}

TEST(ApplyAfterPut, GroupIdIsKeptForGroupsAndSegmentsAndZeroedOtherwise)
{
    // :2727-2733
    MQMD2 returned = { MQMD2_DEFAULT };
    memset(returned.GroupId, 0x42, sizeof(returned.GroupId));
    const int boxes = 4;
    for (int which = -1; which < boxes; which++)
    {
        MqmdFields f;
        memset(f.groupId, 0x99, sizeof(f.groupId));
        f.groupYes = (which == 0);
        f.groupLast = (which == 1);
        f.segmentYes = (which == 2);
        f.segmentLast = (which == 3);
        rfh::mqmd_fields::applyAfterPut(returned, 0, f);
        unsigned char want = (which < 0) ? 0 : 0x42;
        for (size_t i = 0; i < sizeof(f.groupId); i++)
        {
            ASSERT_EQ(want, f.groupId[i]) << "field groupId byte " << i << " with box " << which;
        }
    }
}

// ---------------------------------------------------------------------------
// fromGetMqmd: MQMDPAGE::processMessageMQMD (MQMDPAGE.cpp:1530-1686), the MQMD
// tab after a get or browse. Expectations are written from that source and from
// the helpers it calls (setPutApplType :1046, formatDateTime comsubs.cpp:636).
// ---------------------------------------------------------------------------

namespace
{
    const size_t kVersion = 4;

    MQMD2 receivedFrom(const L::Bytes & b)
    {
        MQMD2 md;
        EXPECT_EQ(sizeof(md), b.data.size());
        memcpy(&md, b.data.data(), sizeof(md));
        return md;
    }

    MqmdFields gotFrom(const L::Bytes & b)
    {
        MqmdFields f;
        rfh::mqmd_fields::fromGetMqmd(receivedFrom(b), f);
        return f;
    }

    // Compares every member of MqmdFields and names each one that differs.
    void expectFields(const MqmdFields & want, const MqmdFields & got)
    {
#define SAME(member) EXPECT_EQ(want.member, got.member) << "field " #member
        SAME(format);
        SAME(ccsid);
        SAME(msgType);
        SAME(expiry);
        SAME(feedback);
        SAME(priority);
        SAME(backoutCount);
        SAME(replyToQ);
        SAME(replyToQMgr);
        SAME(userId);
        SAME(accountingToken);
        SAME(applIdentity);
        SAME(applOrigin);
        SAME(putApplName);
        SAME(putApplType);
        SAME(dateTime);
        SAME(seqNo);
        SAME(offset);
        SAME(originalLength);
        SAME(persistence);
        SAME(encoding);
        SAME(pdEncoding);
        SAME(floatEncoding);
        SAME(reportException);
        SAME(reportExpiration);
        SAME(reportCoa);
        SAME(reportCod);
        SAME(reportPan);
        SAME(reportNan);
        SAME(reportActivity);
        SAME(reportDiscard);
        SAME(reportPassMsgId);
        SAME(reportPassCorrelId);
        SAME(reportPassDiscardAndExpiry);
        SAME(groupYes);
        SAME(groupLast);
        SAME(segmentYes);
        SAME(segmentLast);
        SAME(segmentAllowed);
#undef SAME
        EXPECT_EQ(0, memcmp(want.msgId, got.msgId, 24)) << "field msgId";
        EXPECT_EQ(0, memcmp(want.correlId, got.correlId, 24)) << "field correlId";
        EXPECT_EQ(0, memcmp(want.groupId, got.groupId, 24)) << "field groupId";
    }

    // A version 2 MQMD with every text field filled to its size with characters that
    // are neither blank nor NUL, and each field a different letter so that a read past
    // the end of one field shows in the text.
    L::Bytes unterminatedMqmd()
    {
        L::Bytes b = L::defaultMqmd2();
        b.putChars(O::Format, 8, "FFFFFFFF");
        b.putChars(O::ReplyToQ, 48, std::string(48, 'Q'));
        b.putChars(O::ReplyToQMgr, 48, std::string(48, 'M'));
        b.putChars(O::UserIdentifier, 12, std::string(12, 'U'));
        b.putChars(O::ApplIdentityData, 32, std::string(32, 'I'));
        b.putChars(O::PutApplName, 28, std::string(28, 'N'));
        b.putChars(O::ApplOriginData, 4, "OOOO");
        b.putChars(O::PutDate, 8, "20261016");
        b.putChars(O::PutTime, 8, "01020304");
        return b;
    }

    // MQMD with the fields that fromMqmd and fromGetMqmd read alike
    L::Bytes populatedMqmd()
    {
        L::Bytes b = unterminatedMqmd();
        b.putLong(O::Report, MQRO_EXCEPTION_WITH_FULL_DATA | MQRO_COA_WITH_DATA | MQRO_COD | MQRO_PAN | MQRO_PASS_CORREL_ID);
        b.putLong(O::MsgType, 1);
        b.putLong(O::Expiry, 600);
        b.putLong(O::Feedback, 3);
        b.putLong(O::Encoding, 273);
        b.putLong(O::CodedCharSetId, 1208);
        b.putLong(O::Priority, 5);
        b.putLong(O::Persistence, 1);
        b.putBytes(O::MsgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24);
        b.putBytes(O::CorrelId, "abcdefghijklmnopqrstuvwx", 24);
        b.putLong(O::BackoutCount, 2);
        const unsigned char token[3] = { 0xDE, 0xAD, 0xBE };
        b.putBytes(O::AccountingToken, token, 3);
        b.putLong(O::PutApplType, 11);
        b.putBytes(O::GroupId, "GRPGRPGRPGRPGRPGRPGRPGRP", 24);
        b.putLong(O::MsgSeqNumber, 4);
        b.putLong(O::Offset, 100);
        b.putLong(O::MsgFlags, MQMF_MSG_IN_GROUP | MQMF_SEGMENT);
        b.putLong(O::OriginalLength, 900);
        return b;
    }
}

TEST(FromGetMqmd, EveryFieldMatchesFromMqmdForAVersionTwoMqmd)
{
    L::Bytes e = populatedMqmd();
    MQMD2 md = receivedFrom(e);

    MqmdFields want;
    want.format = "FFFFFFFF";
    want.ccsid = "1208";
    want.msgType = "1 Request";
    want.expiry = "600";
    want.feedback = "3";
    want.priority = "5";
    want.backoutCount = "2";
    want.replyToQ = std::string(48, 'Q');
    want.replyToQMgr = std::string(48, 'M');
    want.userId = std::string(12, 'U');
    want.accountingToken = "DEADBE" + std::string(58, '0');
    want.applIdentity = std::string(32, 'I');
    want.applOrigin = "OOOO";
    want.putApplName = std::string(28, 'N');
    want.putApplType = "11 32-bit Windows";
    want.dateTime = "2026/10/16 01:02:03.04";
    want.seqNo = "4";
    want.offset = "100";
    want.originalLength = "900";
    want.persistence = MQPERSIST_YES;
    want.encoding = NUMERIC_HOST;
    want.pdEncoding = NUMERIC_HOST;
    want.reportException = REPORT_FULL;
    want.reportExpiration = REPORT_NONE;
    want.reportCoa = REPORT_DATA;
    want.reportCod = REPORT_YES;
    want.reportPan = true;
    want.reportPassCorrelId = true;
    want.groupYes = true;
    want.segmentYes = true;
    memcpy(want.msgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24);
    memcpy(want.correlId, "abcdefghijklmnopqrstuvwx", 24);
    memcpy(want.groupId, "GRPGRPGRPGRPGRPGRPGRPGRP", 24);

    MqmdFields got;
    rfh::mqmd_fields::fromGetMqmd(md, got);
    expectFields(want, got);

    // where the header comment lists no difference, the file path gives the same result
    MqmdFields fromFile;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, fromFile));
    expectFields(fromFile, got);
}

TEST(FromGetMqmd, AGetPathWithNothingReceivedLeavesTheClearedPage)
{
    // clearMQMD runs first: nothing of an earlier message survives except the float
    // encoding, which neither function touches
    MqmdFields got;
    got.userId = "old";
    got.replyToQ = "OLD.Q";
    got.groupYes = true;
    got.segmentAllowed = true;
    got.reportCoa = REPORT_FULL;
    got.floatEncoding = FLOAT_TNS;
    memcpy(got.msgId, "OLDOLDOLDOLDOLDOLDOLDOLD", 24);
    memcpy(got.groupId, "OLDOLDOLDOLDOLDOLDOLDOLD", 24);

    L::Bytes e = L::defaultMqmd2();
    e.putLong(kVersion, 1);
    rfh::mqmd_fields::fromGetMqmd(receivedFrom(e), got);

    MqmdFields want;
    want.floatEncoding = FLOAT_TNS;
    want.msgType = "8 Datagram";
    want.expiry = "-1";
    want.feedback = "0";
    want.priority = "-1";
    want.ccsid = "0";
    want.persistence = MQPERSIST_AS_Q;
    want.encoding = NUMERIC_PC;
    want.pdEncoding = NUMERIC_PC;
    want.putApplType = "0 None";
    want.accountingToken = std::string(64, '0');
    want.dateTime = "";
    want.format = std::string(8, ' ');          // blank filled, and kept
    want.replyToQ = std::string(48, ' ');
    want.replyToQMgr = std::string(48, ' ');
    want.putApplName = std::string(28, ' ');
    want.reportException = REPORT_NONE;         // processReportOptions sets none, not -1
    want.reportExpiration = REPORT_NONE;
    want.reportCoa = REPORT_NONE;
    want.reportCod = REPORT_NONE;
    expectFields(want, got);
}

TEST(FromGetMqmd, VersionOneGroupIdIsHandledAsWindowsDoes)
{
    // processMessageMQMD copies GroupId only for version 2 (:1655-1658); the group id
    // stays at what clearMQMD left, zeros, and so do the version 2 fields
    L::Bytes e = populatedMqmd();
    e.putLong(kVersion, 1);
    MqmdFields got = gotFrom(e);

    MqmdFields want;
    want.floatEncoding = got.floatEncoding;
    EXPECT_EQ(0, memcmp(want.groupId, got.groupId, 24)) << "field groupId";
    EXPECT_EQ("1", got.seqNo) << "field seqNo";
    EXPECT_EQ("", got.offset) << "field offset";
    EXPECT_EQ("-1", got.originalLength) << "field originalLength";
    EXPECT_FALSE(got.groupYes) << "field groupYes";
    EXPECT_FALSE(got.segmentYes) << "field segmentYes";

    // fromMqmd copies the bytes after a version 1 MQMD as the Group Id: this is the
    // difference
    MqmdFields file;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(receivedFrom(e), file));
    EXPECT_EQ(0, memcmp(file.groupId, "GRPGRPGRPGRPGRPGRPGRPGRP", 24));
}

TEST(FromGetMqmd, EveryVersionOfTwoOrMoreIsReadAsVersionTwoAndOthersAsVersionOne)
{
    // Version >= MQMD_VERSION_2 (:1655); fromMqmd would return false for all but 1 and 2
    const struct { int version; bool v2; } cases[] = { { 3, true }, { 100, true }, { 2, true },
        { 1, false }, { 0, false }, { -1, false } };
    for (const auto & c : cases)
    {
        L::Bytes e = populatedMqmd();
        e.putLong(kVersion, c.version);
        MqmdFields got = gotFrom(e);
        EXPECT_EQ(c.v2 ? "4" : "1", got.seqNo) << "field seqNo, version " << c.version;
        EXPECT_EQ(c.v2 ? "900" : "-1", got.originalLength) << "field originalLength, version " << c.version;
        EXPECT_EQ(c.v2, got.groupYes) << "field groupYes, version " << c.version;
        EXPECT_EQ(c.v2 ? 'G' : 0, got.groupId[0]) << "field groupId, version " << c.version;
    }
}

TEST(FromGetMqmd, IdsAreKeptAsReceived)
{
    // NUL, blank and high bytes are all kept; nothing is reversed or translated
    unsigned char msgId[24], correlId[24], groupId[24];
    for (int i = 0; i < 24; i++)
    {
        msgId[i] = (unsigned char)(i * 11);          // starts with a NUL
        correlId[i] = (unsigned char)(0xFF - i * 7); // high bytes
        groupId[i] = (i % 2) ? ' ' : (unsigned char)(0x80 + i);
    }
    L::Bytes e = L::defaultMqmd2();
    e.putBytes(O::MsgId, msgId, 24);
    e.putBytes(O::CorrelId, correlId, 24);
    e.putBytes(O::GroupId, groupId, 24);
    MqmdFields got = gotFrom(e);
    for (int i = 0; i < 24; i++)
    {
        EXPECT_EQ(msgId[i], got.msgId[i]) << "field msgId byte " << i;
        EXPECT_EQ(correlId[i], got.correlId[i]) << "field correlId byte " << i;
        EXPECT_EQ(groupId[i], got.groupId[i]) << "field groupId byte " << i;
    }
}

TEST(FromGetMqmd, OverlongOrUnterminatedTextFieldsAreTruncatedNotOverrun)
{
    // every text field is full, with no blank or NUL; each result is exactly the field
    // and the bytes of the next field (a different letter) never show
    MqmdFields got = gotFrom(unterminatedMqmd());
    EXPECT_EQ("FFFFFFFF", got.format) << "field format";
    EXPECT_EQ(std::string(48, 'Q'), got.replyToQ) << "field replyToQ";
    EXPECT_EQ(std::string(48, 'M'), got.replyToQMgr) << "field replyToQMgr";
    EXPECT_EQ(std::string(12, 'U'), got.userId) << "field userId";
    EXPECT_EQ(std::string(32, 'I'), got.applIdentity) << "field applIdentity";
    EXPECT_EQ(std::string(28, 'N'), got.putApplName) << "field putApplName";
    EXPECT_EQ("OOOO", got.applOrigin) << "field applOrigin";
    EXPECT_EQ("2026/10/16 01:02:03.04", got.dateTime) << "field dateTime";
}

TEST(FromGetMqmd, FieldsOfHighBytesAndNoNulAreAlsoBounded)
{
    // as above, with bytes above 0x7F, which MSVC's signed char sees as negative
    L::Bytes e = unterminatedMqmd();
    e.putChars(O::Format, 8, std::string(8, '\xC1'));
    e.putChars(O::ReplyToQ, 48, std::string(48, '\xC2'));
    e.putChars(O::PutApplName, 28, std::string(28, '\xC3'));
    MqmdFields got = gotFrom(e);
    EXPECT_EQ(std::string(8, '\xC1'), got.format) << "field format";
    EXPECT_EQ(std::string(48, '\xC2'), got.replyToQ) << "field replyToQ";
    EXPECT_EQ(std::string(28, '\xC3'), got.putApplName) << "field putApplName";
}

TEST(FromGetMqmd, FormatKeepsItsBlanksAndEndsAtTheFirstNul)
{
    // :1641-1643 copies the 8 bytes into a zeroed buffer; nothing trims it
    L::Bytes e = L::defaultMqmd2();
    e.putChars(O::Format, 8, "MQSTR");
    EXPECT_EQ("MQSTR   ", gotFrom(e).format) << "field format";

    e.putChars(O::Format, 8, "");
    EXPECT_EQ("        ", gotFrom(e).format) << "field format of a blank format";

    e.putBytes(O::Format, "AB\0CD   ", 8);
    EXPECT_EQ("AB", gotFrom(e).format) << "field format with a NUL";

    e.putBytes(O::Format, "\0\0\0\0\0\0\0\0", 8);
    EXPECT_EQ("", gotFrom(e).format) << "field format of zero bytes";

    // a control byte does not end it, unlike in fromMqmd
    e.putBytes(O::Format, "A\tB     ", 8);
    EXPECT_EQ(std::string("A\tB     "), gotFrom(e).format) << "field format with a tab";
    MqmdFields file;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(receivedFrom(e), file));
    EXPECT_EQ("A", file.format);
}

TEST(FromGetMqmd, ReplyToNamesAndPutApplNameKeepTheirBlanksAndEndAtTheFirstNul)
{
    // :1636-1652; User Id, Appl Identity and Appl Origin are trimmed (Rtrim), these are not
    L::Bytes e = L::defaultMqmd2();
    e.putChars(O::ReplyToQ, 48, "REPLY.Q");
    e.putChars(O::ReplyToQMgr, 48, "QM1");
    e.putChars(O::PutApplName, 28, "app.exe");
    e.putChars(O::UserIdentifier, 12, "me");
    e.putChars(O::ApplIdentityData, 32, "ident");
    e.putChars(O::ApplOriginData, 4, "o");
    MqmdFields got = gotFrom(e);
    EXPECT_EQ("REPLY.Q" + std::string(41, ' '), got.replyToQ) << "field replyToQ";
    EXPECT_EQ("QM1" + std::string(45, ' '), got.replyToQMgr) << "field replyToQMgr";
    EXPECT_EQ("app.exe" + std::string(21, ' '), got.putApplName) << "field putApplName";
    EXPECT_EQ("me", got.userId) << "field userId";
    EXPECT_EQ("ident", got.applIdentity) << "field applIdentity";
    EXPECT_EQ("o", got.applOrigin) << "field applOrigin";

    e.putBytes(O::ReplyToQ, "Q1\0GARBAGE", 10);
    e.putBytes(O::ReplyToQMgr, "M1\0GARBAGE", 10);
    e.putBytes(O::PutApplName, "N1\0GARBAGE", 10);
    e.putBytes(O::UserIdentifier, "U1\0GARBAGE", 10);
    got = gotFrom(e);
    EXPECT_EQ("Q1", got.replyToQ) << "field replyToQ with a NUL";
    EXPECT_EQ("M1", got.replyToQMgr) << "field replyToQMgr with a NUL";
    EXPECT_EQ("N1", got.putApplName) << "field putApplName with a NUL";
    EXPECT_EQ("U1", got.userId) << "field userId with a NUL";

    // fromMqmd trims all of them
    MqmdFields file;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(receivedFrom(L::defaultMqmd2()), file));
    EXPECT_EQ("", file.replyToQ);
}

TEST(FromGetMqmd, PutApplTypeShowsAnUnnamedTypeAsWindowsSetPutApplTypeDoes)
{
    const struct { int type; const char * text; } cases[] = {
        { 0, "0 None" }, { 6, "6 Unix" }, { 28, "28 Java" },
        { 99, "99" },                          // unnamed, positive: its number
        { 1000, "1000" }, { 2147483647, "2147483647" },
        { -1, "-1 Other/Unknown" }, { -7, "-1 Other/Unknown" } };
    for (const auto & c : cases)
    {
        L::Bytes e = L::defaultMqmd2();
        e.putLong(O::PutApplType, c.type);
        EXPECT_EQ(c.text, gotFrom(e).putApplType) << "field putApplType for type " << c.type;
    }

    // fromMqmd shows an unnamed type as an empty text
    L::Bytes e = L::defaultMqmd2();
    e.putLong(O::PutApplType, 99);
    MqmdFields file;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(receivedFrom(e), file));
    EXPECT_EQ("", file.putApplType);
}

TEST(FromGetMqmd, ADateAndTimeAreShownAsWindowsFormatsThem)
{
    L::Bytes e = L::defaultMqmd2();
    e.putChars(O::PutDate, 8, "20261016");
    e.putChars(O::PutTime, 8, "23595999");
    EXPECT_EQ("2026/10/16 23:59:59.99", gotFrom(e).dateTime) << "field dateTime";

    // blanks are not NULs: all 22 characters
    e.putChars(O::PutDate, 8, "");
    e.putChars(O::PutTime, 8, "");
    EXPECT_EQ("    /  /     :  :  .  ", gotFrom(e).dateTime) << "field dateTime of blanks";
}

TEST(FromGetMqmd, AZeroDateAndTimeGiveAnEmptyText)
{
    // formatDateTime's first byte is the NUL of PutDate, which ends the text; fromMqmd
    // gives a single blank here
    L::Bytes e = L::defaultMqmd2();
    e.putBytes(O::PutDate, "\0\0\0\0\0\0\0\0", 8);
    e.putBytes(O::PutTime, "\0\0\0\0\0\0\0\0", 8);
    MQMD2 md = receivedFrom(e);
    MqmdFields got;
    rfh::mqmd_fields::fromGetMqmd(md, got);
    EXPECT_EQ("", got.dateTime) << "field dateTime";
    MqmdFields file;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, file));
    EXPECT_EQ(" ", file.dateTime);
}

TEST(FromGetMqmd, ANulInTheDateEndsTheWholeDateAndTime)
{
    // the single string ends at the first NUL, wherever it is
    L::Bytes e = L::defaultMqmd2();
    e.putBytes(O::PutDate, "20\0\0\0\0\0\0", 8);
    e.putChars(O::PutTime, 8, "01020304");
    MQMD2 md = receivedFrom(e);
    MqmdFields got;
    rfh::mqmd_fields::fromGetMqmd(md, got);
    EXPECT_EQ("20", got.dateTime) << "field dateTime";

    MqmdFields file;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(md, file));
    EXPECT_EQ("20 01:02:03.04", file.dateTime);

    // a NUL in the time keeps the date and the text before it, and the next separator
    e.putChars(O::PutDate, 8, "20261016");
    e.putBytes(O::PutTime, "01\0\0\0\0\0\0", 8);
    EXPECT_EQ("2026/10/16 01:", gotFrom(e).dateTime) << "field dateTime with a NUL in the time";
    e.putBytes(O::PutTime, "010203\0\0", 8);
    EXPECT_EQ("2026/10/16 01:02:03.", gotFrom(e).dateTime) << "field dateTime with a NUL in the hundredths";
}

TEST(FromGetMqmd, MessageTypeNamesAndUnnamedTypes)
{
    const struct { int type; const char * text; } cases[] = { { 1, "1 Request" }, { 2, "2 Reply" },
        { 4, "4 Report" }, { 8, "8 Datagram" }, { 112, "112 FR_MQE" }, { 113, "113 MQE" },
        { 3, "" }, { 0, "" }, { 65536, "" }, { -1, "" } };
    for (const auto & c : cases)
    {
        L::Bytes e = L::defaultMqmd2();
        e.putLong(O::MsgType, c.type);
        EXPECT_EQ(c.text, gotFrom(e).msgType) << "field msgType for type " << c.type;
    }
}

TEST(FromGetMqmd, ReportOptionsAreReadAsProcessReportOptionsDoes)
{
    // :1389-1512: each pair of options has none/yes/data/full; the rest are check boxes
    L::Bytes e = L::defaultMqmd2();
    e.putLong(O::Report, MQRO_EXCEPTION | MQRO_EXPIRATION_WITH_DATA | MQRO_COA_WITH_FULL_DATA
        | MQRO_NAN | MQRO_ACTIVITY | MQRO_DISCARD_MSG | MQRO_PASS_MSG_ID | MQRO_PASS_DISCARD_AND_EXPIRY);
    MqmdFields got = gotFrom(e);
    EXPECT_EQ(REPORT_YES, got.reportException) << "field reportException";
    EXPECT_EQ(REPORT_DATA, got.reportExpiration) << "field reportExpiration";
    EXPECT_EQ(REPORT_FULL, got.reportCoa) << "field reportCoa";
    EXPECT_EQ(REPORT_NONE, got.reportCod) << "field reportCod";
    EXPECT_FALSE(got.reportPan) << "field reportPan";
    EXPECT_TRUE(got.reportNan) << "field reportNan";
    EXPECT_TRUE(got.reportActivity) << "field reportActivity";
    EXPECT_TRUE(got.reportDiscard) << "field reportDiscard";
    EXPECT_TRUE(got.reportPassMsgId) << "field reportPassMsgId";
    EXPECT_FALSE(got.reportPassCorrelId) << "field reportPassCorrelId";
    EXPECT_TRUE(got.reportPassDiscardAndExpiry) << "field reportPassDiscardAndExpiry";
}

TEST(FromGetMqmd, EncodingsAreReadFromTheirOwnBits)
{
    // getIntEncode tests MQENC_INTEGER_REVERSED (2), getPDEncode MQENC_DECIMAL_REVERSED (0x20)
    const struct { int encoding; int integer; int decimal; } cases[] = {
        { 0x222, NUMERIC_PC, NUMERIC_PC }, { 0x111, NUMERIC_HOST, NUMERIC_HOST },
        { 0x012, NUMERIC_PC, NUMERIC_HOST }, { 0x101, NUMERIC_HOST, NUMERIC_HOST },
        { 0x021, NUMERIC_HOST, NUMERIC_PC }, { 0, NUMERIC_HOST, NUMERIC_HOST } };
    for (const auto & c : cases)
    {
        L::Bytes e = L::defaultMqmd2();
        e.putLong(O::Encoding, c.encoding);
        MqmdFields got = gotFrom(e);
        EXPECT_EQ(c.integer, got.encoding) << "field encoding for " << c.encoding;
        EXPECT_EQ(c.decimal, got.pdEncoding) << "field pdEncoding for " << c.encoding;
        // floatEncoding is left unchanged by fromGetMqmd (as on Windows)
    }
}

TEST(FromGetMqmd, TheAccountingTokenIsAllThirtyTwoBytesInUpperCaseHex)
{
    L::Bytes e = L::defaultMqmd2();
    unsigned char token[32];
    for (int i = 0; i < 32; i++)
    {
        token[i] = (unsigned char)(0xF0 - i * 3);
    }
    e.putBytes(O::AccountingToken, token, 32);
    EXPECT_EQ("F0EDEAE7E4E1DEDBD8D5D2CFCCC9C6C3C0BDBAB7B4B1AEABA8A5A29F9C999693", gotFrom(e).accountingToken)
        << "field accountingToken";
}

TEST(FromGetMqmd, VersionTwoFlagsAreReadBitByBit)
{
    const struct { int flags; bool group; bool last; bool segment; bool segLast; bool allowed; } cases[] = {
        { MQMF_MSG_IN_GROUP, true, false, false, false, false },
        { MQMF_LAST_MSG_IN_GROUP, false, true, false, false, false },
        { MQMF_SEGMENT, false, false, true, false, false },
        { MQMF_LAST_SEGMENT, false, false, false, true, false },
        { MQMF_SEGMENTATION_ALLOWED, false, false, false, false, true },
        { 0, false, false, false, false, false } };
    for (const auto & c : cases)
    {
        L::Bytes e = L::defaultMqmd2();
        e.putLong(O::MsgFlags, c.flags);
        MqmdFields got = gotFrom(e);
        EXPECT_EQ(c.group, got.groupYes) << "field groupYes for flags " << c.flags;
        EXPECT_EQ(c.last, got.groupLast) << "field groupLast for flags " << c.flags;
        EXPECT_EQ(c.segment, got.segmentYes) << "field segmentYes for flags " << c.flags;
        EXPECT_EQ(c.segLast, got.segmentLast) << "field segmentLast for flags " << c.flags;
        EXPECT_EQ(c.allowed, got.segmentAllowed) << "field segmentAllowed for flags " << c.flags;
    }
}

TEST(FromGetMqmd, TrailingBytesAbove7FAreTrimmedWhereCharIsSigned)
{
    // trimmedField ends in Rtrim (comsubs.cpp:409), whose test is str[len - 1] <= ' ' on a
    // plain char. Where char is signed (MSVC, x86 Linux) a byte of 0x80 or more is negative
    // and is trimmed like a blank; where it is unsigned (ARM Linux) it would be kept. Pinned
    // for the signed case, which is the one the Windows results come from. Only trailing
    // bytes are trimmed; one before a visible character stays.
    L::Bytes e = L::defaultMqmd2();
    e.putChars(O::UserIdentifier, 12, std::string("USER") + std::string(2, '\xC1') + std::string(6, ' '));
    e.putChars(O::ApplOriginData, 4, std::string("A") + std::string("\xC1") + "B" + std::string("\xE2"));
    e.putChars(O::ApplIdentityData, 32, std::string("ID") + std::string(30, '\xFF'));
    MqmdFields got = gotFrom(e);
    if (std::numeric_limits<char>::is_signed)
    {
        EXPECT_EQ("USER", got.userId);
        EXPECT_EQ(std::string("A\xC1") + "B", got.applOrigin);
        EXPECT_EQ("ID", got.applIdentity);
    }
    else
    {
        EXPECT_EQ(std::string("USER") + std::string(2, '\xC1'), got.userId);
        EXPECT_EQ(std::string("A\xC1") + "B" + "\xE2", got.applOrigin);
        EXPECT_EQ("ID" + std::string(30, '\xFF'), got.applIdentity);
    }
}

TEST(FromGetMqmd, FromMqmdIsUnchanged)
{
    // regression: the file path keeps its own rules (trimmed Format and names, an empty
    // text for an unnamed application type, the single blank for a zero date, and the
    // Group Id of a version 1 MQMD)
    L::Bytes e = L::defaultMqmd2();
    e.putLong(kVersion, 1);
    e.putChars(O::Format, 8, "MQSTR");
    e.putChars(O::ReplyToQ, 48, "R.Q");
    e.putChars(O::PutApplName, 28, "app");
    e.putLong(O::PutApplType, 99);
    e.putBytes(O::GroupId, "GRPGRPGRPGRPGRPGRPGRPGRP", 24);
    MqmdFields f;
    ASSERT_TRUE(rfh::mqmd_fields::fromMqmd(receivedFrom(e), f));
    EXPECT_EQ("MQSTR", f.format) << "field format";
    EXPECT_EQ("R.Q", f.replyToQ) << "field replyToQ";
    EXPECT_EQ("app", f.putApplName) << "field putApplName";
    EXPECT_EQ("", f.putApplType) << "field putApplType";
    EXPECT_EQ(" ", f.dateTime) << "field dateTime";
    EXPECT_EQ(0, memcmp(f.groupId, "GRPGRPGRPGRPGRPGRPGRPGRP", 24)) << "field groupId";

    e.putLong(kVersion, 3);
    EXPECT_FALSE(rfh::mqmd_fields::fromMqmd(receivedFrom(e), f));
}
