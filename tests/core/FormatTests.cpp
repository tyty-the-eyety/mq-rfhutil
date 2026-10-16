// FormatTests.cpp - format::render, the Data tab views, and their default settings.
//
// The expected text is the Windows Data tab's: offsets of 8 digits, CR LF line
// ends, 32 characters a line in the character view and 16 bytes a line in the hex
// and both views. ctest -L memcheck runs these under the guard allocator.

#include <gtest/gtest.h>

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "comsubs.h"
#include "mq_layout.h"
#include "rfh_format.h"

using namespace rfh;
using namespace rfh::format;
namespace L = mq_layout;
using L::Order;

namespace
{
    Message message(const std::string & body, int ccsid = 819, int encoding = 546)
    {
        Message m;
        m.mqmd.CodedCharSetId = ccsid;
        m.mqmd.Encoding = encoding;
        memcpy(m.mqmd.Format, MQFMT_STRING, MQ_FORMAT_LENGTH);
        m.body.assign(body.begin(), body.end());
        return m;
    }

    RenderOptions options(int ccsid = 819, int charFormat = CHAR_ASCII)
    {
        RenderOptions o;
        o.ccsid = ccsid;
        o.charFormat = charFormat;
        o.display = { 1252, 437 };
        return o;
    }

    std::string render(const Message & m, View view, const RenderOptions & o)
    {
        std::string out;
        Result r = format::render(m, view, o, out);
        EXPECT_TRUE(r.ok()) << r.text;
        return out;
    }

    std::string ebcdic(const std::string & ascii)
    {
        std::string out(ascii.size(), 0);
        AsciiToEbcdic((unsigned char *)ascii.data(), (int)ascii.size(), (unsigned char *)&out[0]);
        return out;
    }
}

// --- hex view -----------------------------------------------------------------

TEST(Format_Hex, SixteenBytesALineInGroupsOfFour)
{
    EXPECT_EQ("00000000 30313233 34353637 38394142 43444546\r\n"
              "00000016 4748494A",
              render(message("0123456789ABCDEFGHIJ"), View::Hex, options()));
}

TEST(Format_Hex, EmptyBodyIsEmpty)
{
    EXPECT_EQ("", render(message(""), View::Hex, options()));
}

// --- character view -----------------------------------------------------------

TEST(Format_Character, ThirtyTwoCharactersALine)
{
    EXPECT_EQ("00000000 abcdefghijklmnopqrstuvwxyz012345\r\n"
              "00000032 6789ABCD",
              render(message("abcdefghijklmnopqrstuvwxyz0123456789ABCD"), View::Character, options()));
}

TEST(Format_Character, ControlCharactersAreShownAsPeriods)
{
    EXPECT_EQ("00000000 a.b.c\xe9",
              render(message(std::string("a\x01" "b\0c\xe9", 6)), View::Character, options()));
}

TEST(Format_Character, CrLfStartsALineWhenHonoured)
{
    RenderOptions o = options();
    o.crlf = true;
    EXPECT_EQ("00000000 line one\r\n"
              "00000010 line two",
              render(message("line one\r\nline two"), View::Character, o));
}

TEST(Format_Character, CrLfIsAPeriodPairOtherwise)
{
    EXPECT_EQ("00000000 line one..line two",
              render(message("line one\r\nline two"), View::Character, options()));
}

TEST(Format_Character, EdiSegmentsEachHaveALine)
{
    RenderOptions o = options();
    o.edi = true;
    EXPECT_EQ("00000000 UNA:+.? '\r\n"
              "00000009 UNB+UNOC:3+X'\r\n"
              "00000022 UNH+1'",
              render(message("UNA:+.? 'UNB+UNOC:3+X'UNH+1'"), View::Character, o));
}

TEST(Format_Character, EbcdicIsTranslated)
{
    EXPECT_EQ("00000000 Hello, world!",
              render(message(ebcdic("Hello, world!"), 500, 785), View::Character, options(500, CHAR_EBCDIC)));
}

TEST(Format_Character, UnicodeIsShownAsItsBytes)
{
    // IBM disabled the UCS-2 branch of the character view (getCharacterData has
    // "if (0)" there), so Windows shows UTF-16 data byte by byte too
    EXPECT_EQ("00000000 H.i.",
              render(message(std::string("H\0i\0", 4), 1200), View::Character, options(1200)));
}

// --- both view ----------------------------------------------------------------

TEST(Format_Both, HexThenTextSixteenBytesALine)
{
    EXPECT_EQ("00000000 30313233 34353637 38394142 43444546 01234567 89ABCDEF \r\n"
              "00000016 4748494A                            GHIJ",
              render(message("0123456789ABCDEFGHIJ"), View::Both, options()));
}

TEST(Format_Both, EbcdicTextIsTranslatedButTheHexIsNot)
{
    EXPECT_EQ("00000000 C8859393 96                         Hello",
              render(message(ebcdic("Hello"), 500, 785), View::Both, options(500, CHAR_EBCDIC)));
}

TEST(Format_Both, LongMessageFitsItsBuffer)
{
    // Windows allocates 4 bytes per data byte plus 65 for 65 bytes per line of 16,
    // so it writes past the end above 1040 bytes (memcheck catches a regression)
    std::string body(5000, 'x');
    std::string out = render(message(body), View::Both, options());
    std::string lastLine = out.substr(out.rfind("\r\n") + 2);
    // a blank follows every 8 characters of text, so an 8-character last line ends in one
    EXPECT_EQ("00004992 78787878 78787878                   xxxxxxxx ", lastLine);
    EXPECT_EQ(312u * 65u + lastLine.size(), out.size());
}

// --- default settings ---------------------------------------------------------

TEST(Format_Defaults, AfterAGetTheyFollowTheLastHeader)
{
    Message m = message("data", 819, 546);
    memcpy(m.mqmd.Format, MQFMT_RF_HEADER_2, MQ_FORMAT_LENGTH);
    m.headers = L::rfh2(Order::Little, 785, 500, "MQSTR").data;

    std::vector<std::string> warnings;
    RenderOptions o = defaultsFor(m, warnings);
    EXPECT_EQ(819, o.ccsid) << "the code page is the MQMD's, as DataArea::fileCcsid";
    EXPECT_EQ(NUMERIC_PC, o.intFormat);
    EXPECT_EQ(CHAR_EBCDIC, o.charFormat);
    EXPECT_EQ(NUMERIC_HOST, o.numericFormat);
    EXPECT_EQ(NUMERIC_HOST, o.pdNumericFormat);
    EXPECT_TRUE(warnings.empty());
}

TEST(Format_Defaults, AfterAGetAReversedCodePageIsRepaired)
{
    std::vector<std::string> warnings;
    RenderOptions o = defaultsFor(message("x", reverseBytes4(500)), warnings);
    EXPECT_EQ(500, o.ccsid);
    EXPECT_TRUE(warnings.empty());
}

TEST(Format_Defaults, AfterAGetAnInvalidCodePageIsZeroWithAWarning)
{
    std::vector<std::string> warnings;
    RenderOptions o = defaultsFor(message("x", 100000), warnings);
    EXPECT_EQ(0, o.ccsid);
    ASSERT_EQ(1u, warnings.size());
    EXPECT_EQ("Invalid code page in MQMD - 100000\r\n", warnings[0]);
}

TEST(Format_Defaults, AfterAReadTheyAreTheReadResults)
{
    fileio::ReadResult read;
    read.fileCcsid = 37;
    read.fileIntFormat = NUMERIC_HOST;
    read.charFormat = CHAR_EBCDIC;
    read.numericFormat = NUMERIC_HOST;
    read.pdFormat = NUMERIC_PC;
    RenderOptions o = defaultsFor(read);
    EXPECT_EQ(37, o.ccsid);
    EXPECT_EQ(NUMERIC_HOST, o.intFormat);
    EXPECT_EQ(CHAR_EBCDIC, o.charFormat);
    EXPECT_EQ(NUMERIC_HOST, o.numericFormat);
    EXPECT_EQ(NUMERIC_PC, o.pdNumericFormat);
}
