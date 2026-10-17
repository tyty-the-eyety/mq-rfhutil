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

// --- byte order mark and double-byte data -------------------------------------

TEST(Format_Bom, CharacterViewSkipsTheMarkWhenHonoured)
{
    RenderOptions o = options();
    o.bom = true;
    EXPECT_EQ("00000000 \xff" "ab", render(message("\xfe\xff" "ab"), View::Character, o));
    EXPECT_EQ("00000000 \xfe\xff" "ab", render(message("\xfe\xff" "ab"), View::Character, options()));
}

TEST(Format_Bom, BothViewSkipsTheMarkWhenHonoured)
{
    RenderOptions o = options();
    o.bom = true;
    EXPECT_EQ("00000000 FF6162                              \xff" "ab",
              render(message("\xfe\xff" "ab"), View::Both, o));
}

TEST(Format_Bom, HexViewIsUnchanged)
{
    EXPECT_EQ("00000000 FEFF6162", render(message("\xfe\xff" "ab"), View::Hex, options()));
}

TEST(Format_Character, ADoubleByteCharacterAtColumnThirtyOneMakesALineOfThirtyThree)
{
    std::string body(31, 'a');
    body += "\x82\xa0";
    body += "bc";
    RenderOptions o = options(932, CHAR_JAPANESE);
    EXPECT_EQ("00000000 " + std::string(31, 'a') + "\x82\xa0\r\n00000033 bc",
              render(message(body, 932), View::Character, o));
}

TEST(Format_Options, ApplyReplacesOnlyTheFormats)
{
    RenderOptions o = options();
    o.crlf = true;
    o.copybookPath = "book.cpy";
    DataFormats f;
    f.ccsid = 37;
    f.intFormat = NUMERIC_HOST;
    f.charFormat = CHAR_EBCDIC;
    f.numericFormat = NUMERIC_HOST;
    f.pdNumericFormat = NUMERIC_HOST;
    o.apply(f);
    EXPECT_EQ(37, o.ccsid);
    EXPECT_EQ(CHAR_EBCDIC, o.charFormat);
    EXPECT_EQ(NUMERIC_HOST, o.pdNumericFormat);
    EXPECT_TRUE(o.crlf);
    EXPECT_EQ("book.cpy", o.copybookPath);
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
    DataFormats o = defaultsFor(m, warnings);
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
    DataFormats o = defaultsFor(message("x", reverseBytes4(500)), warnings);
    EXPECT_EQ(500, o.ccsid);
    EXPECT_EQ(CHAR_EBCDIC, o.charFormat) << "the type comes from the repaired code page";
    EXPECT_TRUE(warnings.empty());
}

TEST(Format_Defaults, AfterAGetAReversedJapaneseCodePageIsJapanese)
{
    std::vector<std::string> warnings;
    DataFormats o = defaultsFor(message("x", reverseBytes4(932)), warnings);
    EXPECT_EQ(932, o.ccsid);
    EXPECT_EQ(CHAR_JAPANESE, o.charFormat);
}

TEST(Format_Defaults, AfterAGetWithoutHeadersTheMqmdDecides)
{
    std::vector<std::string> warnings;
    EXPECT_EQ(CHAR_EBCDIC, defaultsFor(message("x", 500), warnings).charFormat);
    EXPECT_EQ(CHAR_JAPANESE, defaultsFor(message("x", 932), warnings).charFormat);
    DataFormats o = defaultsFor(message("x", 819, 785), warnings);
    EXPECT_EQ(NUMERIC_HOST, o.intFormat);
    EXPECT_EQ(NUMERIC_HOST, o.numericFormat);
}

TEST(Format_Defaults, AfterAGetAnInvalidDlqEndsTheChainWithAsciiAndNoEncoding)
{
    Message m = message("", 819, 546);
    memcpy(m.mqmd.Format, MQFMT_RF_HEADER_2, MQ_FORMAT_LENGTH);
    m.headers = L::rfh2(Order::Little, 546, 819, "MQDEAD").data;

    std::vector<std::string> warnings;
    DataFormats o = defaultsFor(m, warnings);
    EXPECT_EQ(CHAR_ASCII, o.charFormat);
    EXPECT_EQ(-1, o.numericFormat);
    EXPECT_EQ(NUMERIC_HOST, o.pdNumericFormat);
}

TEST(Format_Defaults, AfterAGetAnInvalidCodePageIsZeroWithAWarning)
{
    std::vector<std::string> warnings;
    DataFormats o = defaultsFor(message("x", 100000), warnings);
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
    DataFormats o = defaultsFor(read);
    EXPECT_EQ(37, o.ccsid);
    EXPECT_EQ(NUMERIC_HOST, o.intFormat);
    EXPECT_EQ(CHAR_EBCDIC, o.charFormat);
    EXPECT_EQ(NUMERIC_HOST, o.numericFormat);
    EXPECT_EQ(NUMERIC_PC, o.pdNumericFormat);
}

// --- XML view -----------------------------------------------------------------

TEST(Format_Xml, ElementsAreIndentedOneSpaceALevel)
{
    EXPECT_EQ("<a>\r\n"
              " <b x=\"1\">text</b>\r\n"
              " <c/>\r\n"
              "</a>",
              render(message("<a><b x=\"1\">text</b><c/></a>"), View::Xml, options()));
}

TEST(Format_Xml, EbcdicIsTranslated)
{
    EXPECT_EQ("<a>\r\n"
              " <b>cafe</b>\r\n"
              "</a>",
              render(message(ebcdic("<a><b>cafe</b></a>"), 500, 785), View::Xml, options(500, CHAR_EBCDIC)));
}

TEST(Format_Xml, DataThatIsNotXmlGetsTheWindowsMessage)
{
    EXPECT_EQ("*****RFHUtil format problem - Data does not appear to be XML\r\n"
              "*****No beginning bracket found\r\n",
              render(message("plain text"), View::Xml, options()));
}

// --- parsed view --------------------------------------------------------------

TEST(Format_Parsed, ElementsAreDottedPathsWithValues)
{
    EXPECT_EQ("a\r\n"
              "a.b='text'\r\n"
              "a.(XML.attr)x='1'\r\n"
              "a.c=''",
              render(message("<a><b x=\"1\">text</b><c/></a>"), View::Parsed, options()));
}

TEST(Format_Parsed, AnUnterminatedAttributeQuoteEndsAtTheEndOfTheTag)
{
    // xmlsubs.cpp's extractAttribute ran past the end of the text before Plan 2B
    std::string out = render(message("<a x=\"open>t</a>"), View::Parsed, options());
    EXPECT_NE(std::string::npos, out.find("a.(XML.attr)x='open"));
}

// --- JSON view ----------------------------------------------------------------

TEST(Format_Json, MembersBecomeDottedNameValueLines)
{
    EXPECT_EQ("a=1\r\n"
              "b.c=\"x\"",
              render(message("{\"a\":1,\"b\":{\"c\":\"x\"}}"), View::Json, options()));
}

TEST(Format_Json, ArraysAreShownAsIbmsParserLeavesThem)
{
    // CJsonParse keeps only the last array element and the text up to the next
    // name; this is IBM's behaviour (reported upstream), pinned here
    EXPECT_EQ("a=1\r\n"
              "b=\"x\"\r\n"
              ",\"c\".d=null",
              render(message("{\"a\":1,\"b\":[true,\"x\"],\"c\":{\"d\":null}}"), View::Json, options()));
}

TEST(Format_Json, DataThatIsNotJsonIsAWarningAndNoText)
{
    std::string out;
    Result r = format::render(message("not json"), View::Json, options(), out);
    EXPECT_TRUE(r.ok());
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_EQ("Error parsing user data", r.warnings[0]);
    EXPECT_EQ("", out);
}

TEST(Format_Json, MalformedJsonIsAWarningNotAHang)
{
    std::string out;
    Result r = format::render(message("{]"), View::Json, options(), out);
    ASSERT_EQ(1u, r.warnings.size());
    EXPECT_EQ("Error parsing user data", r.warnings[0]);
}

// --- fixed (FIX protocol) view ------------------------------------------------

TEST(Format_Fixed, EachSohEndsALine)
{
    EXPECT_EQ("8=FIX.4.2\r\n"
              "35=D\r\n"
              "10=123\r\n",
              render(message("8=FIX.4.2\x01" "35=D\x01" "10=123\x01"), View::Fixed, options()));
}

TEST(Format_Fixed, EbcdicAccentedLettersAreShown)
{
    // 0x51 is e-acute in code page 500; DataArea indexed its display table with a
    // signed char, which for this byte reads before the table
    std::string body = ebcdic("58=caf") + "\x51" + "\x01";
    EXPECT_EQ("58=caf\xe9\r\n", render(message(body, 500, 785), View::Fixed, options(500, CHAR_EBCDIC)));
}

// --- COBOL view ---------------------------------------------------------------

namespace
{
    // a copy book with display, character, packed decimal and binary fields
    std::string writeCopybook()
    {
        std::string path = testing::TempDir() + "rfhutil_customer.cpy";
        FILE * f = fopen(path.c_str(), "w");
        EXPECT_NE(nullptr, f);
        fputs("       01  CUSTOMER-RECORD.\n"
              "           05  CUST-ID          PIC 9(5).\n"
              "           05  CUST-NAME        PIC X(10).\n"
              "           05  BALANCE          PIC S9(5)V99 COMP-3.\n"
              "           05  TXN-COUNT        PIC S9(4) COMP.\n", f);
        fclose(f);
        return path;
    }

    RenderOptions cobolOptions(int numericFormat)
    {
        RenderOptions o = options();
        o.numericFormat = numericFormat;
        o.pdNumericFormat = NUMERIC_HOST;
        o.copybookPath = writeCopybook();
        return o;
    }

    const std::string kCustomer = std::string("00042SMITH     ") + std::string("\x01\x23\x45\x6C", 4);
}

TEST(Format_Cobol, FieldsAreListedWithTheirOffsetsAndValues)
{
    EXPECT_EQ("Level  Ofs  Len Type  Occ Variable Name                  Value\r\n"
              "01     0    21           CUSTOMER-RECORD\r\n"
              "05     0     5 NUMB      CUST-ID                        00042\r\n"
              "05     5    10 CHAR      CUST-NAME                      SMITH     \r\n"
              "05    15     4 PD        BALANCE                        0123456\r\n"
              "05    19     2 INT       TXN-COUNT                      0007\r\n"
              "Copy book size 21, Data area size 21",
              render(message(kCustomer + std::string("\x00\x07", 2)), View::Cobol, cobolOptions(NUMERIC_HOST)));
}

TEST(Format_Cobol, BinaryFieldsFollowTheIntegerEncoding)
{
    std::string out = render(message(kCustomer + std::string("\x07\x00", 2)), View::Cobol, cobolOptions(NUMERIC_PC));
    EXPECT_NE(std::string::npos, out.find("TXN-COUNT                      0007\r\n"));
}

TEST(Format_Cobol, IndentShiftsTheLowerLevels)
{
    RenderOptions o = cobolOptions(NUMERIC_HOST);
    o.indent = true;
    std::string out = render(message(kCustomer + std::string("\x00\x07", 2)), View::Cobol, o);
    EXPECT_NE(std::string::npos, out.find("\r\n01     0    21           CUSTOMER-RECORD\r\n 05     0     5 NUMB"));
}

TEST(Format_Cobol, WithoutACopybookTheViewFails)
{
    std::string out;
    Result r = format::render(message(kCustomer), View::Cobol, options(), out);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("the COBOL view needs a copy book file", r.text);
}

TEST(Format_Cobol, AnUnreadableCopybookFails)
{
    RenderOptions o = options();
    o.copybookPath = testing::TempDir() + "rfhutil_no_such_copybook.cpy";
    std::string out;
    Result r = format::render(message(kCustomer), View::Cobol, o, out);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(0u, r.text.find("copy book file " + o.copybookPath + " could not be read"));
}
