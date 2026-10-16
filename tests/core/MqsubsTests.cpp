// MqsubsTests.cpp — tests for the MQ helper functions in RFHUtil/mqsubs.cpp,
// which became portable in Plan 2A. Linux CMake only: the Windows UnitTests
// project has no MQ headers.

#include <gtest/gtest.h>

#include <string.h>

#include <cmqc.h>

#include "rfh_platform.h"
#include "rfhconst.h"
#include "comsubs.h"
#include "mqsubs.h"

TEST(Mqsubs_CcsidType, EbcdicCodePagesAreEbcdic)
{
    EXPECT_EQ(CHAR_EBCDIC, getCcsidType(37));
    EXPECT_EQ(CHAR_EBCDIC, getCcsidType(500));
    EXPECT_EQ(CHAR_EBCDIC, getCcsidType(1047));
    EXPECT_EQ(CHAR_EBCDIC, getCcsidType(20273));
}

TEST(Mqsubs_CcsidType, AsianCodePagesHaveTheirOwnTypes)
{
    EXPECT_EQ(CHAR_TRAD_CHIN, getCcsidType(950));
}

TEST(Mqsubs_CcsidType, AsciiAndUnicodeAreAscii)
{
    EXPECT_EQ(CHAR_ASCII, getCcsidType(819));
    EXPECT_EQ(CHAR_ASCII, getCcsidType(1252));
    EXPECT_EQ(CHAR_ASCII, getCcsidType(1208));
}

TEST(Mqsubs_Encoding, PcEncodingIs546)
{
    // integer reversed (2), decimal reversed (0x20), float IEEE reversed (0x200)
    EXPECT_EQ(546, getEncodingValue(NUMERIC_PC, NUMERIC_PC, FLOAT_PC));
}

TEST(Mqsubs_Encoding, HostEncodingIs785)
{
    // integer normal (1), decimal normal (0x10), float S/390 (0x300)
    EXPECT_EQ(785, getEncodingValue(NUMERIC_HOST, NUMERIC_HOST, FLOAT_390));
}

TEST(Mqsubs_Encoding, UnixEncodingIs273)
{
    // integer normal (1), decimal normal (0x10), float IEEE normal (0x100)
    EXPECT_EQ(273, getEncodingValue(NUMERIC_HOST, NUMERIC_HOST, FLOAT_UNIX));
}

TEST(Mqsubs_Encoding, IntegerAndDecimalDecodeBack)
{
    EXPECT_EQ(NUMERIC_PC, getIntEncode(546));
    EXPECT_EQ(NUMERIC_PC, getPDEncode(546));
    EXPECT_EQ(NUMERIC_HOST, getIntEncode(785));
    EXPECT_EQ(NUMERIC_HOST, getPDEncode(785));
}

TEST(Mqsubs_Encoding, FloatDecodeIsInvertedHistoric)
{
    // Upstream bug, kept for compatibility; reported upstream: getFloatEncode tests
    // single bits instead of comparing (encoding & MQENC_FLOAT_MASK). IEEE_NORMAL
    // decodes as FLOAT_PC and IEEE_REVERSED as FLOAT_UNIX, the opposite of
    // getEncodingValue, and S/390 (0x300, both bits) also decodes as FLOAT_PC.
    EXPECT_EQ(FLOAT_UNIX, getFloatEncode(getEncodingValue(NUMERIC_PC, NUMERIC_PC, FLOAT_PC)));
    EXPECT_EQ(FLOAT_PC, getFloatEncode(getEncodingValue(NUMERIC_HOST, NUMERIC_HOST, FLOAT_UNIX)));
    EXPECT_EQ(FLOAT_PC, getFloatEncode(785));
    EXPECT_EQ(FLOAT_TNS, getFloatEncode(0x411));
}

TEST(Mqsubs_ApplType, KnownTypesHaveNumberAndName)
{
    EXPECT_STREQ("6 Unix", getApplTypeDisplay(MQAT_UNIX));
    EXPECT_STREQ("28 Java", getApplTypeDisplay(MQAT_JAVA));
    EXPECT_STREQ("11 32-bit Windows", getApplTypeDisplay(MQAT_WINDOWS_NT));
}

TEST(Mqsubs_ApplType, UnknownTypeIsEmpty)
{
    EXPECT_STREQ("", getApplTypeDisplay(65536));
}

TEST(Mqsubs_TranslateMqmd, NativeMqmdIsAcceptedUnchanged)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = MQMD_VERSION_2;
    md.CodedCharSetId = 1208;
    memcpy(md.MsgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24);

    MQMD2 copy = md;
    EXPECT_TRUE(translateMQMD(&copy));
    EXPECT_EQ(0, memcmp(&md, &copy, sizeof(md)));
}

TEST(Mqsubs_TranslateMqmd, BigEndianMqmdIsNormalisedAndIdsKept)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = reverseBytes4(MQMD_VERSION_2);
    md.CodedCharSetId = reverseBytes4(500);
    md.Encoding = reverseBytes4(785);
    md.MsgSeqNumber = reverseBytes4(7);
    memcpy(md.MsgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24);

    EXPECT_TRUE(translateMQMD(&md));
    EXPECT_EQ(MQMD_VERSION_2, md.Version);
    EXPECT_EQ(500, md.CodedCharSetId);
    EXPECT_EQ(785, md.Encoding);
    EXPECT_EQ(7, md.MsgSeqNumber);
    EXPECT_EQ(0, memcmp(md.MsgId, "ABCDEFGHIJKLMNOPQRSTUVWX", 24)) << "byte-string ids are never reversed";
}

TEST(Mqsubs_TranslateMqmd, EbcdicMqmdTextFieldsBecomeAscii)
{
    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = reverseBytes4(MQMD_VERSION_2);
    AsciiToEbcdic((unsigned char *)MQMD_STRUC_ID, 4, (unsigned char *)md.StrucId);
    AsciiToEbcdic((unsigned char *)"MQSTR   ", 8, (unsigned char *)md.Format);

    EXPECT_TRUE(translateMQMD(&md));
    EXPECT_EQ(0, memcmp(md.Format, "MQSTR   ", 8));
}

TEST(Mqsubs_TranslateMqmd, NonMqmdIsRejected)
{
    MQMD2 md = { MQMD2_DEFAULT };
    memcpy(md.StrucId, "XXXX", 4);
    EXPECT_FALSE(translateMQMD(&md));

    MQMD2 badVersion = { MQMD2_DEFAULT };
    badVersion.Version = 3;
    EXPECT_FALSE(translateMQMD(&badVersion));
}
