// MqmdFieldsTests.cpp — tests for rfh::mqmd_fields, the port of
// MQMDPAGE::buildMQMD / processMQMD. Expected MQMD bytes come from mq_layout.h,
// which lays the structure out at IBM's documented offsets.

#include <gtest/gtest.h>

#include <string.h>

#include <cmqc.h>

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
}

TEST(MqmdFields, DefaultFieldsGiveDocumentedDefaultsExceptTabValues)
{
	// An untouched MQMD tab sets persistence "No", PC encodings, backout 0,
	// sequence 1, original length -1, and blank text fields.
	L::Bytes expected = L::defaultMqmd2();
	expected.putLong(O::Persistence, 0);			// MQPER_NOT_PERSISTENT
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
