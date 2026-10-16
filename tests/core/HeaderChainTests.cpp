// HeaderChainTests.cpp — tests for rfh::walkHeaders, detectLeadingHeader and
// looksLikeEbcdic (ports of DataArea::parseMsgHeaders, checkDataForHeader and
// checkForEBCDIC). Headers are built with mq_layout.h at IBM's documented offsets.

#include <gtest/gtest.h>

#include <cmqc.h>

#include "rfhconst.h"
#include "rfh_header_chain.h"
#include "mq_layout.h"

using namespace rfh;
using mq_layout::Order;
namespace L = mq_layout;

namespace
{
	HeaderChain walk(const std::vector<unsigned char> & data, const char * format, int ccsid = 1208, int encoding = NUMERIC_PC)
	{
		return walkHeaders(data.data(), data.size(), format, ccsid, CHAR_ASCII, encoding);
	}
}

TEST(HeaderChain, NoHeadersLeavesTheBodyAlone)
{
	auto data = L::text("hello");
	HeaderChain c = walk(data, "MQSTR   ", 819);
	EXPECT_TRUE(c.headers.empty());
	EXPECT_EQ(0u, c.bodyOffset);
	EXPECT_EQ("MQSTR", c.bodyFormat);
	EXPECT_EQ(819, c.bodyCcsid);
	EXPECT_EQ(NUMERIC_PC, c.encoding);
}

TEST(HeaderChain, Rfh2BeforeTextIsFound)
{
	std::string folder = "<mcd><Msd>jms_text</Msd></mcd>  ";		// 32 bytes, a multiple of 4
	auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR", std::string("\0\0\0\x20", 4) + folder);
	auto data = L::join({ rfh.data, L::text("body") });

	HeaderChain c = walk(data, MQFMT_RF_HEADER_2, 819);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(HeaderType::Rfh2, c.headers[0].type);
	EXPECT_EQ(0u, c.headers[0].offset);
	EXPECT_EQ(rfh.data.size(), c.headers[0].length);
	EXPECT_EQ(rfh.data.size(), c.bodyOffset);
	EXPECT_EQ("MQSTR", c.bodyFormat);
	EXPECT_EQ(1208, c.bodyCcsid);
	EXPECT_EQ(NUMERIC_PC, c.encoding);
	EXPECT_TRUE(c.warnings.empty());
}

TEST(HeaderChain, BigEndianRfh2IsReadWithHostEncoding)
{
	auto rfh = L::rfh2(Order::Big, 785, 500, "MQSTR");
	auto data = L::join({ rfh.data, L::text("x") });

	HeaderChain c = walk(data, MQFMT_RF_HEADER_2, 819, NUMERIC_HOST);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(36u, c.bodyOffset);
	EXPECT_EQ(500, c.bodyCcsid);
	EXPECT_EQ(CHAR_EBCDIC, c.charFormat);
	EXPECT_EQ(NUMERIC_HOST, c.encoding);
}

TEST(HeaderChain, EbcdicRfh2NamesAreTranslated)
{
	auto rfh = L::rfh2(Order::Big, 785, 500, "MQSTR", std::string(), true);
	HeaderChain c = walk(rfh.data, MQFMT_RF_HEADER_2, 500, NUMERIC_HOST);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ("MQSTR", c.bodyFormat);
}

TEST(HeaderChain, DeadLetterThenRfh2ThenBody)
{
	auto dlh = L::dlh(Order::Little, 546, 1208, "MQHRF2");
	auto rfh = L::rfh2(Order::Little, 546, 819, "MQSTR");
	auto data = L::join({ dlh.data, rfh.data, L::text("payload") });

	HeaderChain c = walk(data, MQFMT_DEAD_LETTER_HEADER);
	ASSERT_EQ(2u, c.headers.size());
	EXPECT_EQ(HeaderType::DeadLetter, c.headers[0].type);
	EXPECT_EQ(172u, c.headers[0].length);
	EXPECT_EQ(HeaderType::Rfh2, c.headers[1].type);
	EXPECT_EQ(172u, c.headers[1].offset);
	EXPECT_EQ(208u, c.bodyOffset);
	EXPECT_EQ("MQSTR", c.bodyFormat);
	EXPECT_EQ(819, c.bodyCcsid);
}

TEST(HeaderChain, CicsVersion2WithProgramNameIncludesIt)
{
	auto cih = L::cih(Order::Little, 2, MQCLT_PROGRAM, 546, 500, "MQSTR");
	auto data = L::join({ cih.data, L::text("PROGNAME"), L::text("commarea") });

	HeaderChain c = walk(data, MQFMT_CICS);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(188u, c.bodyOffset);		// 180 + the 8-byte program name
	EXPECT_EQ("MQSTR", c.bodyFormat);
	EXPECT_EQ(500, c.bodyCcsid);
	EXPECT_EQ(CHAR_ASCII, c.charFormat) << "CICS leaves the character format alone";
}

TEST(HeaderChain, CicsTransactionLinkHasNoProgramName)
{
	auto cih = L::cih(Order::Little, 1, MQCLT_TRANSACTION, 546, 1208, "MQSTR");
	auto data = L::join({ cih.data, L::text("data") });
	HeaderChain c = walk(data, MQFMT_CICS);
	EXPECT_EQ(164u, c.bodyOffset);
}

TEST(HeaderChain, ImsHeaderIsAlways84Bytes)
{
	auto iih = L::iih(Order::Little, 546, 37, "MQIMSVS");
	auto data = L::join({ iih.data, L::text("segment") });
	HeaderChain c = walk(data, MQFMT_IMS);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(84u, c.bodyOffset);
	EXPECT_EQ("MQIMSVS", c.bodyFormat);
	EXPECT_EQ(37, c.bodyCcsid);
	EXPECT_EQ(NUMERIC_PC, c.encoding) << "IMS leaves the encoding alone";
}

TEST(HeaderChain, InvalidRfh2LengthEndsTheChain)
{
	auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
	rfh.putLong(8, 9999);
	auto data = L::join({ rfh.data, L::text("x") });

	HeaderChain c = walk(data, MQFMT_RF_HEADER_2, 819);
	EXPECT_TRUE(c.headers.empty());
	EXPECT_EQ(0u, c.bodyOffset);
	EXPECT_EQ("", c.bodyFormat);
	ASSERT_EQ(1u, c.warnings.size());
	EXPECT_EQ("***Invalid RFH Length field*** ", c.warnings[0]);
}

TEST(HeaderChain, Rfh1NeedsNameValueDataHistoric)
{
	// Windows rejects an RFH1 whose StrucLength is just the 32-byte fixed part.
	auto empty = L::rfh1(Order::Little, 546, 1208, "MQSTR", std::string());
	EXPECT_TRUE(walk(empty.data, MQFMT_RF_HEADER).headers.empty());

	auto withData = L::rfh1(Order::Little, 546, 1208, "MQSTR", "a=b ");
	HeaderChain c = walk(L::join({ withData.data, L::text("x") }), MQFMT_RF_HEADER);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(36u, c.bodyOffset);
}

TEST(HeaderChain, SameHeaderTypeTwiceStopsAfterTheFirstHistoric)
{
	auto first = L::dlh(Order::Little, 546, 1208, "MQDEAD");
	auto second = L::dlh(Order::Little, 546, 1208, "MQSTR");
	HeaderChain c = walk(L::join({ first.data, second.data }), MQFMT_DEAD_LETTER_HEADER);
	ASSERT_EQ(1u, c.headers.size());
	EXPECT_EQ(172u, c.bodyOffset);
	EXPECT_EQ("MQDEAD", c.bodyFormat);
}

TEST(HeaderChain, MdExtensionIsReadAtItsRealOffset)
{
	auto dlh = L::dlh(Order::Little, 546, 1208, "MQHMDE");
	auto mde = L::mde(Order::Little, 546, 819, "MQSTR", 3, 100, MQMF_SEGMENT, 400, "GROUP-ID-123456789012345");
	HeaderChain c = walk(L::join({ dlh.data, mde.data, L::text("x") }), MQFMT_DEAD_LETTER_HEADER);

	ASSERT_EQ(2u, c.headers.size());
	EXPECT_EQ(HeaderType::MdExtension, c.headers[1].type);
	EXPECT_EQ(244u, c.bodyOffset);
	EXPECT_EQ("MQSTR", c.bodyFormat);
	EXPECT_EQ(819, c.bodyCcsid);
	ASSERT_TRUE(c.hasMdExtension);
	EXPECT_EQ(3, c.mdExtension.msgSeqNumber);
	EXPECT_EQ(100, c.mdExtension.offset);
	EXPECT_EQ(MQMF_SEGMENT, c.mdExtension.msgFlags);
	EXPECT_EQ(400, c.mdExtension.originalLength);
	EXPECT_EQ(0, memcmp(c.mdExtension.groupId, "GROUP-ID-123456789012345", 24));
}

TEST(HeaderChain, TruncatedHeaderEndsTheChain)
{
	auto dlh = L::dlh(Order::Little, 546, 1208, "MQSTR");
	dlh.data.resize(100);
	HeaderChain c = walk(dlh.data, MQFMT_DEAD_LETTER_HEADER);
	EXPECT_TRUE(c.headers.empty());
	EXPECT_EQ("", c.bodyFormat);
	EXPECT_EQ(-1, c.encoding) << "the cleared DLQ tab has encoding -1";
}

TEST(LeadingHeader, AsciiPcRfh2)
{
	auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
	LeadingHeader h = detectLeadingHeader(rfh.data.data(), rfh.data.size());
	ASSERT_TRUE(h.found);
	EXPECT_EQ(std::string(MQFMT_RF_HEADER_2), h.format);
	EXPECT_EQ(NUMERIC_PC, h.encoding);
	EXPECT_EQ(CHAR_ASCII, h.charFormat);
	EXPECT_EQ(1208, h.ccsid);
}

TEST(LeadingHeader, EbcdicHostRfh2DefaultsToCcsid500)
{
	auto rfh = L::rfh2(Order::Big, 785, 1208, "MQSTR", std::string(), true);
	LeadingHeader h = detectLeadingHeader(rfh.data.data(), rfh.data.size());
	ASSERT_TRUE(h.found);
	EXPECT_EQ(NUMERIC_HOST, h.encoding);
	EXPECT_EQ(FLOAT_390, h.floatEncoding);
	EXPECT_EQ(CHAR_EBCDIC, h.charFormat);
	EXPECT_EQ(500, h.ccsid) << "1208 is not an EBCDIC code page, so the default is used";
}

TEST(LeadingHeader, UnixEncodingSelectsUnixFloats)
{
	auto rfh = L::rfh2(Order::Big, 273, 819, "MQSTR");
	LeadingHeader h = detectLeadingHeader(rfh.data.data(), rfh.data.size());
	ASSERT_TRUE(h.found);
	EXPECT_EQ(FLOAT_UNIX, h.floatEncoding);
	EXPECT_EQ(819, h.ccsid);
}

TEST(LeadingHeader, PlainTextHasNoHeader)
{
	auto data = L::text("just some text that is long enough");
	EXPECT_FALSE(detectLeadingHeader(data.data(), data.size()).found);
}

TEST(LooksLikeEbcdic, EbcdicTextIsRecognised)
{
	std::vector<unsigned char> data;
	for (int i = 0; i < 20; i++)
	{
		auto word = L::ebcdic("HELLO WORLD ");
		data.insert(data.end(), word.begin(), word.end());
	}
	EXPECT_TRUE(looksLikeEbcdic(data.data(), data.size()));
}

TEST(LooksLikeEbcdic, AsciiTextAndShortDataAreNot)
{
	std::string ascii;
	for (int i = 0; i < 20; i++) ascii += "hello world ";
	EXPECT_FALSE(looksLikeEbcdic((const unsigned char *)ascii.data(), ascii.size()));

	auto shortData = L::ebcdic("HELLO");
	EXPECT_FALSE(looksLikeEbcdic(shortData.data(), shortData.size()));
}
