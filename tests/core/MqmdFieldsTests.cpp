// MqmdFieldsTests.cpp — tests for rfh::mqmd_fields, the port of
// MQMDPAGE::buildMQMD / processMQMD. Expected MQMD bytes come from mq_layout.h,
// which lays the structure out at IBM's documented offsets.

#include <gtest/gtest.h>

#include <stddef.h>
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

TEST(ToPutMqmd, ApplFieldLengthLimitsAndZeroPadding)
{
    // :2632 identity has no length limit but only 32 bytes are stored; :2639 origin and
    // :2646 name are ignored when longer than their fields (4, 28)
    MqmdFields f = cleared();
    f.applIdentity = std::string(40, 'I');
    f.applOrigin = "ABCDE";
    f.putApplName = std::string(29, 'N');
    L::Bytes expected = clearedPutExpectation();
    memset(&expected.data[O::ApplIdentityData], 'I', 32);
    expectMqmd(expected, putBuilt(f, setAll()));

    // exactly full fields are stored, unlike toMqmd which drops them
    f.applOrigin = "ABCD";
    f.putApplName = std::string(28, 'N');
    memcpy(&expected.data[O::ApplOriginData], "ABCD", 4);
    memset(&expected.data[O::PutApplName], 'N', 28);
    expectMqmd(expected, putBuilt(f, setAll()));
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
