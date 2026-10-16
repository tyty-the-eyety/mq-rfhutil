// HeaderChainTests.cpp — tests for rfh::walkHeaders, detectLeadingHeader and
// looksLikeEbcdic (ports of DataArea::parseMsgHeaders, checkDataForHeader and
// checkForEBCDIC). Headers are built with mq_layout.h at IBM's documented offsets.

#include <gtest/gtest.h>

#include <sys/mman.h>
#include <unistd.h>

#include <cmqc.h>

#include "rfhconst.h"
#include "rfh_header_chain.h"
#include "mq_layout.h"

using namespace rfh;
using mq_layout::Order;
namespace L = mq_layout;

namespace
{
    // Copies data to the end of a mapping that is followed by a PROT_NONE page, so a
    // read past the data faults instead of passing silently.
    class GuardedBuffer
    {
    public:
        explicit GuardedBuffer(const std::vector<unsigned char> & data)
        {
            size_t page = (size_t)sysconf(_SC_PAGESIZE);
            size_t pages = (data.size() + page - 1) / page + 1;
            mapSize = (pages + 1) * page;
            base = (unsigned char *)mmap(NULL, mapSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (MAP_FAILED == (void *)base)
            {
                base = NULL;
                return;
            }
            mprotect(base + pages * page, page, PROT_NONE);
            size = data.size();
            start = base + pages * page - size;
            if (size > 0)
            {
                memcpy(start, data.data(), size);
            }
        }
        ~GuardedBuffer() { if (base) munmap(base, mapSize); }
        GuardedBuffer(const GuardedBuffer &) = delete;
        GuardedBuffer & operator=(const GuardedBuffer &) = delete;

        const unsigned char * data() const { return start; }
        size_t length() const { return size; }

    private:
        unsigned char * base = NULL;
        unsigned char * start = NULL;
        size_t mapSize = 0;
        size_t size = 0;
    };

    HeaderChain walk(const std::vector<unsigned char> & data, const char * format, int ccsid = 1208, int encoding = NUMERIC_PC)
    {
        GuardedBuffer guarded(data);
        return walkHeaders(guarded.data(), guarded.length(), format, ccsid, CHAR_ASCII, encoding);
    }

    LeadingHeader detect(const std::vector<unsigned char> & data)
    {
        GuardedBuffer guarded(data);
        return detectLeadingHeader(guarded.data(), guarded.length());
    }

    bool ebcdicLooking(const std::vector<unsigned char> & data)
    {
        GuardedBuffer guarded(data);
        return looksLikeEbcdic(guarded.data(), guarded.length());
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
    std::string folder = "<mcd><Msd>jms_text</Msd></mcd>  ";        // 32 bytes, a multiple of 4
    auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR", std::string("\x20\0\0\0", 4) + folder);
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
    EXPECT_EQ(188u, c.bodyOffset);      // 180 + the 8-byte program name
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

// ---- bounds: a header whose length runs past the data ends the chain ----

TEST(HeaderChain, TruncatedCicsVersion2EndsTheChainWithAWarning)
{
    // 170 bytes is a whole version 1 header but short of the 180 version 2 needs.
    auto cih = L::cih(Order::Little, 2, MQCLT_TRANSACTION, 546, 1208, "MQSTR");
    cih.data.resize(170);
    HeaderChain c = walk(cih.data, MQFMT_CICS);
    EXPECT_TRUE(c.headers.empty());
    EXPECT_EQ(0u, c.bodyOffset);
    EXPECT_EQ("", c.bodyFormat);
    ASSERT_EQ(1u, c.warnings.size());
    EXPECT_EQ("header length runs past the end of the data", c.warnings[0]);
}

TEST(HeaderChain, CicsVersion2WithProgramNameCutShortOfTheHeaderEndsTheChain)
{
    // 176 bytes passes the program name test (172 or more) but is short of the 180
    // the header itself needs.
    auto cih = L::cih(Order::Little, 2, MQCLT_PROGRAM, 546, 1208, "MQSTR");
    cih.data.resize(176);
    HeaderChain c = walk(cih.data, MQFMT_CICS);
    EXPECT_TRUE(c.headers.empty());
    EXPECT_EQ(0u, c.bodyOffset);
    EXPECT_EQ("", c.bodyFormat);
    ASSERT_EQ(1u, c.warnings.size());
    EXPECT_EQ("header length runs past the end of the data", c.warnings[0]);
}

TEST(HeaderChain, CicsVersion2WithProgramNameCutOffKeepsTheHeader)
{
    // Exactly 180 bytes: the header fits but the program name does not. Windows
    // recognises the header and reads the name past the data; this keeps the header
    // at its base length.
    auto cih = L::cih(Order::Little, 2, MQCLT_PROGRAM, 546, 1208, "MQSTR");
    ASSERT_EQ(180u, cih.data.size());
    HeaderChain c = walk(cih.data, MQFMT_CICS);
    ASSERT_EQ(1u, c.headers.size());
    EXPECT_EQ(180u, c.headers[0].length);
    EXPECT_EQ(180u, c.bodyOffset);
    EXPECT_EQ("MQSTR", c.bodyFormat);
    EXPECT_TRUE(c.warnings.empty());
}

TEST(HeaderChain, TruncatedCicsNamingRfh2NextDoesNotReadPastTheData)
{
    // Before the bound the next parser started 180 bytes into 170 bytes of data.
    auto cih = L::cih(Order::Little, 2, MQCLT_TRANSACTION, 546, 1208, "MQHRF2");
    cih.data.resize(170);
    HeaderChain c = walk(cih.data, MQFMT_CICS);
    EXPECT_TRUE(c.headers.empty());
    EXPECT_EQ(0u, c.bodyOffset);
    EXPECT_EQ("", c.bodyFormat);
}

// ---- deliberate differences from Windows ----

TEST(HeaderChain, SecondHeaderOfSameTypeFailingEndsTheChain)
{
    // Windows reuses the earlier DLQ parse's state here and can loop or overrun;
    // this port uses the cleared state.
    auto dlh = L::dlh(Order::Little, 546, 1208, "MQHRF2");
    auto rfh = L::rfh2(Order::Little, 546, 1208, "MQDEAD");
    auto bad = L::dlh(Order::Little, 546, 1208, "MQSTR");
    bad.putChars(0, 4, "XXXX");
    HeaderChain c = walk(L::join({ dlh.data, rfh.data, bad.data }), MQFMT_DEAD_LETTER_HEADER);
    ASSERT_EQ(2u, c.headers.size());
    EXPECT_EQ(208u, c.bodyOffset);
    EXPECT_EQ("", c.bodyFormat);
}

TEST(HeaderChain, EncodingAfterMdExtensionIsTheDecodedOne)
{
    // Windows keeps the raw MQENC value (546), so later DLQ/CICS/IMS headers always
    // swap; here the encoding is getIntEncode(546), i.e. little-endian data stays so.
    auto mde = L::mde(Order::Little, 546, 1208, "MQDEAD", 1, 0, 0, -1, "GROUP-ID-123456789012345");
    auto dlh = L::dlh(Order::Little, 546, 1208, "MQSTR");
    HeaderChain c = walk(L::join({ mde.data, dlh.data, L::text("x") }), MQFMT_MD_EXTENSION);
    ASSERT_EQ(2u, c.headers.size());
    EXPECT_EQ(72u + 172u, c.bodyOffset);
    EXPECT_EQ("MQSTR", c.bodyFormat);
}

// ---- warnings ----

TEST(HeaderChain, CicsCcsidOutOfRangeWarns)
{
    auto cih = L::cih(Order::Little, 1, MQCLT_TRANSACTION, 546, 200000, "MQSTR");
    HeaderChain c = walk(L::join({ cih.data, L::text("x") }), MQFMT_CICS);
    ASSERT_EQ(1u, c.headers.size());
    EXPECT_EQ(0, c.bodyCcsid);
    ASSERT_EQ(1u, c.warnings.size());
    EXPECT_EQ("*** Errors parsing CICS header\r\n", c.warnings[0]);
}

TEST(HeaderChain, CicsVersion2CursorAndErrorOffsetOutOfRangeWarn)
{
    // Windows checks CursorPosition (164) and ErrorOffset (168) only for version 2
    // (it zeroes them for version 1) and reports one error for any bad field.
    auto cursor = L::cih(Order::Little, 2, MQCLT_TRANSACTION, 546, 1208, "MQSTR");
    cursor.putLong(164, 100000);
    EXPECT_EQ(1u, walk(cursor.data, MQFMT_CICS).warnings.size());

    auto errorOfs = L::cih(Order::Little, 2, MQCLT_TRANSACTION, 546, 1208, "MQSTR");
    errorOfs.putLong(168, -10000);
    EXPECT_EQ(1u, walk(errorOfs.data, MQFMT_CICS).warnings.size());

    auto fine = L::cih(Order::Little, 2, MQCLT_TRANSACTION, 546, 1208, "MQSTR");
    fine.putLong(164, 99999);
    fine.putLong(168, -9999);
    EXPECT_TRUE(walk(fine.data, MQFMT_CICS).warnings.empty());
}

TEST(HeaderChain, TruncatedRfh2WarnsButTruncatedRfh1IsSilent)
{
    auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
    rfh.data.resize(20);
    HeaderChain c = walk(rfh.data, MQFMT_RF_HEADER_2);
    EXPECT_TRUE(c.headers.empty());
    ASSERT_EQ(1u, c.warnings.size());
    EXPECT_EQ("***Invalid RFH Length field*** ", c.warnings[0]);

    auto rfh1 = L::rfh1(Order::Little, 546, 1208, "MQSTR", "a=b ");
    rfh1.data.resize(20);
    HeaderChain c1 = walk(rfh1.data, MQFMT_RF_HEADER);
    EXPECT_TRUE(c1.headers.empty());
    EXPECT_TRUE(c1.warnings.empty());
}

// ---- edge cases ----

TEST(HeaderChain, HostEncodedDeadLetterHeaderIsSwapped)
{
    auto dlh = L::dlh(Order::Big, 785, 500, "MQSTR");
    HeaderChain c = walk(L::join({ dlh.data, L::text("x") }), MQFMT_DEAD_LETTER_HEADER, 819, NUMERIC_HOST);
    ASSERT_EQ(1u, c.headers.size());
    EXPECT_EQ(172u, c.bodyOffset);
    EXPECT_EQ(500, c.bodyCcsid);
    EXPECT_EQ(NUMERIC_HOST, c.encoding);
}

TEST(HeaderChain, HostEncodedCicsVersion1IsSwapped)
{
    auto cih = L::cih(Order::Big, 1, MQCLT_TRANSACTION, 785, 500, "MQSTR");
    HeaderChain c = walk(L::join({ cih.data, L::text("x") }), MQFMT_CICS, 819, NUMERIC_HOST);
    ASSERT_EQ(1u, c.headers.size());
    EXPECT_EQ(164u, c.bodyOffset);
    EXPECT_EQ(500, c.bodyCcsid);
    EXPECT_EQ("MQSTR", c.bodyFormat);
}

TEST(HeaderChain, HostEncodedImsHeaderIsSwapped)
{
    auto iih = L::iih(Order::Big, 785, 500, "MQSTR");
    HeaderChain c = walk(L::join({ iih.data, L::text("x") }), MQFMT_IMS, 819, NUMERIC_HOST);
    ASSERT_EQ(1u, c.headers.size());
    EXPECT_EQ(84u, c.bodyOffset);
    EXPECT_EQ(500, c.bodyCcsid);
}

TEST(HeaderChain, HostEncodedRfh1IsSwapped)
{
    auto rfh = L::rfh1(Order::Big, 785, 500, "MQSTR", "a=b ");
    HeaderChain c = walk(L::join({ rfh.data, L::text("x") }), MQFMT_RF_HEADER, 819, NUMERIC_HOST);
    ASSERT_EQ(1u, c.headers.size());
    EXPECT_EQ(36u, c.bodyOffset);
    EXPECT_EQ(500, c.bodyCcsid);
    EXPECT_EQ(NUMERIC_HOST, c.encoding);
}

TEST(HeaderChain, EbcdicDeadLetterHeaderIsTranslated)
{
    // StrucId and Format are EBCDIC because the incoming data is CCSID 500.
    auto dlh = L::dlh(Order::Big, 785, 500, "");
    dlh.putBytes(0, L::ebcdic("DLH ").data(), 4);
    dlh.putBytes(116, L::ebcdic("MQSTR   ").data(), 8);
    HeaderChain c = walk(L::join({ dlh.data, L::text("x") }), MQFMT_DEAD_LETTER_HEADER, 500, NUMERIC_HOST);
    ASSERT_EQ(1u, c.headers.size());
    EXPECT_EQ("MQSTR", c.bodyFormat);
    EXPECT_EQ(500, c.bodyCcsid);
}

TEST(HeaderChain, Rfh2OfExactlyTheFixedLengthIsAccepted)
{
    auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
    ASSERT_EQ(36u, rfh.data.size());
    HeaderChain c = walk(rfh.data, MQFMT_RF_HEADER_2);
    ASSERT_EQ(1u, c.headers.size());
    EXPECT_EQ(36u, c.bodyOffset);
    EXPECT_TRUE(c.warnings.empty());
}

TEST(HeaderChain, HostEncodedMdExtensionIsSwapped)
{
    auto mde = L::mde(Order::Big, 785, 500, "MQSTR", 3, 100, MQMF_SEGMENT, 400, "GROUP-ID-123456789012345");
    HeaderChain c = walk(mde.data, MQFMT_MD_EXTENSION, 819, NUMERIC_HOST);
    ASSERT_EQ(1u, c.headers.size());
    ASSERT_TRUE(c.hasMdExtension);
    EXPECT_EQ(3, c.mdExtension.msgSeqNumber);
    EXPECT_EQ(100, c.mdExtension.offset);
    EXPECT_EQ(MQMF_SEGMENT, c.mdExtension.msgFlags);
    EXPECT_EQ(400, c.mdExtension.originalLength);
    EXPECT_EQ(500, c.bodyCcsid);
}

TEST(HeaderChain, MdExtensionShorterThan72BytesEndsTheChain)
{
    auto mde = L::mde(Order::Little, 546, 500, "MQSTR", 3, 100, 0, 400, "GROUP-ID-123456789012345");
    mde.data.resize(60);
    HeaderChain c = walk(mde.data, MQFMT_MD_EXTENSION, 819);
    EXPECT_TRUE(c.headers.empty());
    EXPECT_FALSE(c.hasMdExtension);
    EXPECT_EQ("", c.bodyFormat);
    EXPECT_EQ(819, c.bodyCcsid) << "ccsid is left as it was";
    EXPECT_EQ(NUMERIC_PC, c.encoding);
}

TEST(HeaderChain, LooseStopRuleParsesTwoRfh2HeadersInARow)
{
    // A header followed by one of its own kind stops the pass, not the chain: the
    // next pass reads the second RFH2.
    auto dlh = L::dlh(Order::Little, 546, 1208, "MQHRF2");
    auto first = L::rfh2(Order::Little, 546, 1208, "MQHRF2");
    auto second = L::rfh2(Order::Little, 546, 1208, "MQSTR");
    HeaderChain c = walk(L::join({ dlh.data, first.data, second.data, L::text("x") }), MQFMT_DEAD_LETTER_HEADER);
    ASSERT_EQ(3u, c.headers.size());
    EXPECT_EQ(244u, c.bodyOffset);
    EXPECT_EQ("MQSTR", c.bodyFormat);
}

TEST(LeadingHeader, LittleEndianCicsVersion2IsClassedAsHost)
{
    // Windows compares the version with MQCIH_VERSION_1 only, so a PC version 2
    // header counts as host data.
    auto cih = L::cih(Order::Little, 2, MQCLT_TRANSACTION, 546, 1208, "MQSTR");
    LeadingHeader h = detect(cih.data);
    ASSERT_TRUE(h.found);
    EXPECT_EQ(std::string(MQFMT_CICS), h.format);
    EXPECT_EQ(NUMERIC_HOST, h.encoding);
    EXPECT_EQ(FLOAT_390, h.floatEncoding);
}

TEST(LeadingHeader, ImsHeaderOf84BytesIsNotDetected)
{
    // Windows asks for 8 bytes more than the header's own length.
    auto iih = L::iih(Order::Little, 546, 1208, "MQSTR");
    EXPECT_FALSE(detect(iih.data).found);
}

TEST(LeadingHeader, Rfh1IsDetected)
{
    auto rfh = L::rfh1(Order::Little, 546, 1208, "MQSTR", "a=b ");
    LeadingHeader h = detect(rfh.data);
    ASSERT_TRUE(h.found);
    EXPECT_EQ(std::string(MQFMT_RF_HEADER), h.format);
    EXPECT_EQ(NUMERIC_PC, h.encoding);
}

TEST(LeadingHeader, AsciiPcRfh2)
{
    auto rfh = L::rfh2(Order::Little, 546, 1208, "MQSTR");
    LeadingHeader h = detect(rfh.data);
    ASSERT_TRUE(h.found);
    EXPECT_EQ(std::string(MQFMT_RF_HEADER_2), h.format);
    EXPECT_EQ(NUMERIC_PC, h.encoding);
    EXPECT_EQ(CHAR_ASCII, h.charFormat);
    EXPECT_EQ(1208, h.ccsid);
}

TEST(LeadingHeader, EbcdicHostRfh2DefaultsToCcsid500)
{
    auto rfh = L::rfh2(Order::Big, 785, 1208, "MQSTR", std::string(), true);
    LeadingHeader h = detect(rfh.data);
    ASSERT_TRUE(h.found);
    EXPECT_EQ(NUMERIC_HOST, h.encoding);
    EXPECT_EQ(FLOAT_390, h.floatEncoding);
    EXPECT_EQ(CHAR_EBCDIC, h.charFormat);
    EXPECT_EQ(500, h.ccsid) << "1208 is not an EBCDIC code page, so the default is used";
}

TEST(LeadingHeader, UnixEncodingSelectsUnixFloats)
{
    auto rfh = L::rfh2(Order::Big, 273, 819, "MQSTR");
    LeadingHeader h = detect(rfh.data);
    ASSERT_TRUE(h.found);
    EXPECT_EQ(FLOAT_UNIX, h.floatEncoding);
    EXPECT_EQ(819, h.ccsid);
}

TEST(LeadingHeader, PlainTextHasNoHeader)
{
    auto data = L::text("just some text that is long enough");
    EXPECT_FALSE(detect(data).found);
}

TEST(LooksLikeEbcdic, EbcdicTextIsRecognised)
{
    std::vector<unsigned char> data;
    for (int i = 0; i < 20; i++)
    {
        auto word = L::ebcdic("HELLO WORLD ");
        data.insert(data.end(), word.begin(), word.end());
    }
    EXPECT_TRUE(ebcdicLooking(data));
}

TEST(LooksLikeEbcdic, AsciiTextAndShortDataAreNot)
{
    std::string ascii;
    for (int i = 0; i < 20; i++) ascii += "hello world ";
    EXPECT_FALSE(ebcdicLooking(L::text(ascii)));

    auto shortData = L::ebcdic("HELLO");
    EXPECT_FALSE(ebcdicLooking(shortData));
}

TEST(HeaderChain, EveryTruncationOfAFullChainStaysInBounds)
{
    // Every prefix of a chain using each header type is walked in a guarded buffer, so
    // any read past the cut faults.
    auto dlh = L::dlh(Order::Little, 546, 1208, "MQCICS");
    auto cih = L::cih(Order::Little, 2, MQCLT_TRANSACTION, 546, 1208, "MQIMS");
    auto iih = L::iih(Order::Little, 546, 1208, "MQHRF");
    auto rfh1 = L::rfh1(Order::Little, 546, 1208, "MQHRF2", "a=b ");
    auto rfh2 = L::rfh2(Order::Little, 546, 1208, "MQHMDE");
    auto mde = L::mde(Order::Little, 546, 1208, "MQSTR", 1, 0, 0, -1, "GROUP-ID-123456789012345");
    auto full = L::join({ dlh.data, cih.data, iih.data, rfh1.data, rfh2.data, mde.data, L::text("body") });

    for (size_t cut = 0; cut <= full.size(); cut++)
    {
        std::vector<unsigned char> prefix(full.begin(), full.begin() + cut);
        HeaderChain c = walk(prefix, MQFMT_DEAD_LETTER_HEADER);
        EXPECT_LE(c.bodyOffset, cut) << "cut " << cut;
    }

    HeaderChain whole = walk(full, MQFMT_DEAD_LETTER_HEADER);
    EXPECT_EQ(6u, whole.headers.size());
    EXPECT_EQ("MQSTR", whole.bodyFormat);
}

TEST(HeaderChain, WalkingFromAnMqmdStartsWithItsFormatCcsidAndEncoding)
{
    auto rfh = L::rfh2(Order::Big, 785, 500, "MQSTR");
    auto data = L::join({ rfh.data, L::text("body") });

    MQMD2 md = { MQMD2_DEFAULT };
    md.Version = MQMD_VERSION_2;
    memcpy(md.Format, MQFMT_RF_HEADER_2, MQ_FORMAT_LENGTH);
    md.CodedCharSetId = 819;
    md.Encoding = 785;

    GuardedBuffer guarded(data);
    HeaderChain viaMqmd = walkHeaders(guarded.data(), guarded.length(), md);
    HeaderChain explicitly = walkHeaders(guarded.data(), guarded.length(), std::string(md.Format, MQ_FORMAT_LENGTH),
                                         819, CHAR_ASCII, NUMERIC_HOST);

    ASSERT_EQ(1u, viaMqmd.headers.size());
    EXPECT_EQ(explicitly.headers.size(), viaMqmd.headers.size());
    EXPECT_EQ(explicitly.bodyOffset, viaMqmd.bodyOffset);
    EXPECT_EQ(explicitly.bodyFormat, viaMqmd.bodyFormat);
    EXPECT_EQ(explicitly.bodyCcsid, viaMqmd.bodyCcsid);
    EXPECT_EQ(explicitly.charFormat, viaMqmd.charFormat);
    EXPECT_EQ(explicitly.encoding, viaMqmd.encoding);
    EXPECT_EQ(NUMERIC_HOST, viaMqmd.encoding);
}
