// FormatTests.cpp - format::render, the Data tab views, and their default settings.
//
// The expected text is the Windows Data tab's: offsets of 8 digits, CR LF line
// ends, 32 characters a line in the character view and 16 bytes a line in the hex
// and both views. ctest -L memcheck runs these under the guard allocator.

#include <gtest/gtest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include <chrono>
#include <memory>
#include <string_view>

#include <string>
#include <vector>

#include "comsubs.h"
#include "copybook_file.h"
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

TEST(Format_Xml, NumericCharacterReferencesAreShownAsTheyAre)
{
    // IBM's behaviour, reported upstream: setEscChar's '#' branch tests the byte
    // it is given, which is always the '&', so &#65; and &#x41; are never decoded,
    // while the named entities are
    EXPECT_EQ("<a>&#65;&#x41;<</a>",
              render(message("<a>&#65;&#x41;&lt;</a>"), View::Xml, options()));
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

TEST(Format_Parsed, AnEmptyTagNameIsNotReadBeforeItsStart)
{
    // "< />" has an empty name and ends with "/>": DataArea's getNameValueData
    // looked for a trailing '/' and '\\' at tempVarName[strlen(tempVarName) - 1],
    // the byte before the name, and could clear it
    EXPECT_EQ("a\r\n"
              "a.=''",
              render(message("<a>< /></a>"), View::Parsed, options()));
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
    Result renderCobol(const std::string & copybook, const std::string & body, RenderOptions o, std::string & out)
    {
        CopybookFile file(copybook);
        o.copybookPath = file.path();
        return format::render(message(body), View::Cobol, o, out);
    }

    // the view, which must render
    std::string cobol(const std::string & copybook, const std::string & body, const RenderOptions & o = options())
    {
        std::string out;
        Result r = renderCobol(copybook, body, o, out);
        EXPECT_TRUE(r.ok()) << r.text;
        return out;
    }

    RenderOptions with(RenderOptions o, bool indent, bool checkData)
    {
        o.indent = indent;
        o.checkData = checkData;
        return o;
    }

    const std::string kHeading = "Level  Ofs  Len Type  Occ Variable Name                  Value\r\n";
    const std::string kMismatch = "\r\n***** RFHUtil error - Data length does not match copybook";

    // a copy book with display, character, packed decimal and binary fields
    const std::string kCustomerCopybook =
        "       01  CUSTOMER-RECORD.\n"
        "           05  CUST-ID          PIC 9(5).\n"
        "           05  CUST-NAME        PIC X(10).\n"
        "           05  BALANCE          PIC S9(5)V99 COMP-3.\n"
        "           05  TXN-COUNT        PIC S9(4) COMP.\n";

    RenderOptions cobolOptions(int numericFormat)
    {
        RenderOptions o = options();
        o.numericFormat = numericFormat;
        o.pdNumericFormat = NUMERIC_HOST;
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
              cobol(kCustomerCopybook, kCustomer + std::string("\x00\x07", 2), cobolOptions(NUMERIC_HOST)));
}

TEST(Format_Cobol, BinaryFieldsFollowTheIntegerEncoding)
{
    std::string out = cobol(kCustomerCopybook, kCustomer + std::string("\x07\x00", 2), cobolOptions(NUMERIC_PC));
    EXPECT_NE(std::string::npos, out.find("TXN-COUNT                      0007\r\n"));
}

TEST(Format_Cobol, IndentShiftsTheLowerLevels)
{
    RenderOptions o = cobolOptions(NUMERIC_HOST);
    o.indent = true;
    std::string out = cobol(kCustomerCopybook, kCustomer + std::string("\x00\x07", 2), o);
    EXPECT_NE(std::string::npos, out.find("\r\n01     0    21           CUSTOMER-RECORD\r\n 05     0     5 NUMB"));
}

TEST(Format_Cobol, WithoutACopybookTheViewFails)
{
    std::string out;
    Result r = format::render(message(kCustomer), View::Cobol, options(), out);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("the COBOL view needs a copy book file", r.text);
}

TEST(Format_Cobol, ACopybookThatCannotBeOpenedFails)
{
    RenderOptions o = options();
    o.copybookPath = testing::TempDir() + "rfhutil_no_such_copybook.cpy";
    std::string out;
    Result r = format::render(message(kCustomer), View::Cobol, o, out);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("copy book file " + o.copybookPath + " could not be opened", r.text);
}

// Windows ignores parseCopyBook's errors and shows the copy book as far as it was
// parsed, without its offsets.
TEST(Format_Cobol, ARedefinitionOfAnUndefinedFieldFails)
{
    std::string out;
    CopybookFile file("       01 R.\n"
                      "         05 A PIC X(4).\n"
                      "         05 B REDEFINES NOPE PIC 9(4).\n");
    RenderOptions o = options();
    o.copybookPath = file.path();
    Result r = format::render(message("1234"), View::Cobol, o, out);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("copy book file " + file.path() + " redefines a field that it does not define (99)", r.text);
    EXPECT_EQ("", out);
}

TEST(Format_Cobol, ALevelOutsideItsRecordFails)
{
    std::string out;
    CopybookFile file("       05 A.\n"
                      "         10 B PIC X.\n"
                      "       03 C PIC X.\n");
    RenderOptions o = options();
    o.copybookPath = file.path();
    Result r = format::render(message("12"), View::Cobol, o, out);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("copy book file " + file.path() + " has a level that is outside its record's hierarchy (97)", r.text);
}

TEST(Format_Cobol, ACopybookThatNeedsMoreThanTheLimitFails)
{
    // 2000 * 2000 * 1000 lines overflowed the int buffer size; getMaxLines saturates
    // at MAX_COBOL_LINES, (2147475328 - 65536) / 128
    std::string out;
    CopybookFile file("       01 R.\n"
                      "         05 A OCCURS 2000 TIMES.\n"
                      "           10 B OCCURS 2000 TIMES.\n"
                      "             15 C OCCURS 1000 TIMES.\n"
                      "               20 V PIC X.\n");
    RenderOptions o = options();
    o.copybookPath = file.path();
    Result r = format::render(message("12"), View::Cobol, o, out);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("copy book file " + file.path() + " needs a buffer of 2147475328 bytes,"
              " more than the COBOL view's limit of 268435456", r.text);
    EXPECT_EQ("", out);
}

TEST(Format_Cobol, ACopybookWhoseLineCountWrapsFails)
{
    // 99999 * 99999 wrapped to a small count, and the view looped for its occurrences
    std::string out;
    Result r = renderCobol("       01 R.\n"
                           "         05 A OCCURS 99999 TIMES.\n"
                           "           10 B OCCURS 99999 TIMES.\n"
                           "               20 V PIC X.\n", "12", options(), out);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find(" more than the COBOL view's limit of 268435456"));
}

// --- COBOL view: IBM's copy book parser ---------------------------------------
//
// tools/port/harden-copybook.pl keeps IBM's parser within its buffers on malformed
// copy books and data; these are the reviewer's reproducers. Each crashed or wrote
// past a buffer before (under the guard allocator or a sanitizer); now each gives
// the view, with the defined text pinned where it is short.

TEST(Format_CobolMalformed, AGroupOccursDependingOnWithoutAMaximumShowsTheOccurrencesInTheData)
{
    // the count, 600, has no maximum to cap it; the buffer was sized for 10
    EXPECT_EQ(kHeading +
              "01     0   604           R\r\n"
              "05     0     4 NUMB      CNT                            0600\r\n"
              "05     4     1         1 ITEMS\r\n"
              "10     4     1 CHAR    1 V                              A\r\n"
              "05     5     1         2 ITEMS\r\n"
              "10     5     1 CHAR    2 V                              A\r\n"
              "05     6     1         3 ITEMS\r\n"
              "10     6     1 CHAR    3 V                              A\r\n"
              "05     7     1         4 ITEMS\r\n"
              "10     7     1 CHAR    4 V                              A\r\n"
              "05     8     1         5 ITEMS\r\n"
              "10     8     1 CHAR    5 V                              A\r\n"
              "05     9     1         6 ITEMS\r\n"
              "10     9     1 CHAR    6 V                              A\r\n"
              "Copy book size 604, Data area size 10" + kMismatch,
              cobol("       01 R.\n"
                    "         05 CNT PIC 9(4).\n"
                    "         05 ITEMS OCCURS 5 TIMES DEPENDING ON CNT.\n"
                    "            10 V PIC X.\n", "0600AAAAAA"));
}

TEST(Format_CobolMalformed, AGroupOccursStopsAtTheEndOfItsBuffer)
{
    // 100000 occurrences in the data, two lines each, in a buffer sized for 10
    std::string out = cobol("       01 R.\n"
                            "         05 CNT PIC 9(6).\n"
                            "         05 ITEMS OCCURS 5 TIMES DEPENDING ON CNT.\n"
                            "            10 V PIC X.\n", "100000" + std::string(100000, 'A'));
    const std::string end = "***** Exceeded memory buffer\r\nCopy book size 100006, Data area size 100006";
    ASSERT_GT(out.size(), end.size());
    EXPECT_EQ(end, out.substr(out.size() - end.size()));
    // (23 lines * 128) + 65536, and the 8192 beyond it
    EXPECT_LT(out.size(), (23u * 128) + 65536 + 8192);
}

TEST(Format_CobolMalformed, AStatementOfManyShortLinesStopsAtItsBuffer)
{
    // no period: 5000 lines and the blanks between them overran the statement
    std::string copybook = "       01  REC\n";
    for (int i = 0; i < 5000; i++)
    {
        copybook += "       X\n";
    }
    EXPECT_EQ(kHeading +
              "00     0     0           \r\n"
              "Copy book size 0, Data area size 6" + kMismatch,
              cobol(copybook, "aabbcc"));
}

TEST(Format_CobolMalformed, ALineThatStartsWithANulIsSkipped)
{
    EXPECT_EQ(kHeading +
              "01     0     1           A\r\n"
              "Copy book size 1, Data area size 6" + kMismatch,
              cobol(std::string("\0\n", 2) + "       01  A PIC X.\n", "aabbcc"));
}

TEST(Format_CobolMalformed, ASyncGroupWhoseFirstChildIsNotAlignedDoesNotDivideByZero)
{
    std::string out = cobol("       01 F0 BINARY OCCURS 17 TIMES SYNC.\n"
                            "       10 F1 COMP PIC.\n"
                            "       77 F2 REDEFINES F0 POINTER.\n", std::string(40, '\0'));
    EXPECT_EQ(0u, out.find(kHeading + "01     0     2         1 F0\r\n10     0     2         1 F1\r\n"));
}

TEST(Format_CobolMalformed, IndentingDeeperThanThirtyTwoLevelsStopsAtThirtyOneBlanks)
{
    std::string copybook = "       01  R.\n";
    for (int level = 2; level < 49; level++)
    {
        char line[32];
        snprintf(line, sizeof(line), "       %02d  LV%d.\n", level, level);
        copybook += line;
    }
    copybook += "       49  LEAF PIC X(5).\n";
    std::string out = cobol(copybook, "aabbcc", with(options(), true, false));
    EXPECT_NE(std::string::npos, out.find("\r\n" + std::string(30, ' ') + "31     0     5           LV31\r\n"));
    EXPECT_NE(std::string::npos, out.find("\r\n" + std::string(31, ' ') + "32     0     5           LV32\r\n"));
    EXPECT_NE(std::string::npos, out.find("\r\n" + std::string(31, ' ') + "33     0     5           LV33\r\n"));
    EXPECT_NE(std::string::npos, out.find("\r\n" + std::string(31, ' ') + "49     0     5 CHAR      LEAF                           aabbc\r\n"));
}

TEST(Format_CobolMalformed, APackedDecimalFieldLongerThanItsWorkAreaShowsWhatFits)
{
    // 101 bytes, shown as 202 digits in a 160 byte work area
    std::string out = cobol("       01 R.\n"
                            "         05 A PIC S9(200) COMP-3.\n", std::string(400, '\x11'));
    EXPECT_EQ(kHeading +
              "01     0   101           R\r\n"
              "***** RFHUtil format error - Invalid Packed Decimal data in next field\r\n"
              "05     0   101 PD        A                              " + std::string(158, '1') + "\r\n"
              "Copy book size 101, Data area size 400" + kMismatch,
              out);
}

TEST(Format_CobolMalformed, ANegativeLengthShowsNoValue)
{
    EXPECT_EQ(kHeading +
              "01     0    -5           R\r\n"
              "05     0    -5 CHAR      A\r\n"
              "Copy book size -5, Data area size 6" + kMismatch,
              cobol("       01 R.\n"
                    "         05 A PIC X(-5).\n", "aabbcc"));
}

TEST(Format_CobolMalformed, ALengthBeyondAnIntShowsNoValue)
{
    // atoi wraps the length here; Windows' atoi saturates at INT_MAX
    std::string out = cobol("       01 R.\n"
                            "         05 A PIC X(999999999999).\n", "aabbcc");
    EXPECT_NE(std::string::npos, out.find(" CHAR      A\r\n"));
    EXPECT_NE(std::string::npos, out.find(", Data area size 6" + kMismatch));
}

TEST(Format_CobolMalformed, AHugeLengthDoesNotOverflowTheOffsets)
{
    std::string out = cobol("       01 R.\n"
                            "         05 A PIC X(2000000000).\n"
                            "         05 B PIC X(2000000000).\n"
                            "         05 C PIC X.\n", "aabbcc");
    EXPECT_NE(std::string::npos, out.find("05     0 2000000000 CHAR      A                              aabbcc\r\n"));
}

TEST(Format_CobolMalformed, ACountWiderThanItsWorkAreaIsRead)
{
    // the count, PIC 9(300), overran its 256 byte work area
    std::string out = cobol("       01 R.\n"
                            "         05 CNT PIC 9(300).\n"
                            "         05 ITEMS PIC X OCCURS 1 TO 3 TIMES DEPENDING ON CNT.\n",
                            std::string(299, '0') + "2AB");
    EXPECT_NE(std::string::npos, out.find("05     0   300 NUMB      CNT                            " + std::string(128, '0') + "\r\n"));
    EXPECT_NE(std::string::npos, out.find("Copy book size 300, Data area size 302" + kMismatch));
}

TEST(Format_CobolMalformed, AFieldBeforeTheDataIsNotRead)
{
    // the tree puts F3 at offset -400 and its count before the data
    EXPECT_EQ(kHeading +
              "***** RFHUtil format error - Occurs value past end of input buffer\n"
              "***** RFHUtil format error - Occurs value below minimum - value=0 minimum=100\r\n"
              "***** RFHUtil format error - Occurs value below minimum - value=0 minimum=3\r\n"
              "01     0     5           F0\r\n"
              "03  -400     2           F3\r\n"
              "***** RFHUtil formating error - Copy book offset exceeds data area\r\n"
              "Copy book size 5, Data area size 1" + kMismatch,
              cobol("       01 F0 .\n"
                    "       10 F1 OCCURS 100 TO 1 TIMES DEPENDING ON F0 COMP-3 A(-1).\n"
                    "       15 F2 COMP-1.\n"
                    "       03 F3 G(0) SIGN TRAILING.\n"
                    "       77 F4 PACKED-DECIMAL COMP-2.\n"
                    "       10 F5 USAGE OCCURS 3 TO 9 TIMES DEPENDING ON F3 COMP-3.\n"
                    "       01 F6 A(9) X(100).\n"
                    "       77 F7 SIGN LEADING SEPARATE.\n"
                    "       03 F8 BINARY BINARY.\n", "\x99"));
}

TEST(Format_CobolMalformed, NestedOccursDependingOnStaysWithinTheData)
{
    std::string body = "99";
    for (int i = 0; i < 50; i++)
    {
        body += "99X";
    }
    std::string out = cobol("       01 R.\n"
                            "         05 C1 PIC 9(2).\n"
                            "         05 A OCCURS 1 TO 99 TIMES DEPENDING ON C1.\n"
                            "            10 C2 PIC 9(2).\n"
                            "            10 B OCCURS 1 TO 99 TIMES DEPENDING ON C2.\n"
                            "               15 V PIC X.\n", body);
    EXPECT_EQ(0u, out.find(kHeading + "01     0  9903           R\r\n05     0     2 NUMB      C1                             99\r\n"));
    const std::string end = "Copy book size 9903, Data area size 152" + kMismatch;
    ASSERT_GT(out.size(), end.size());
    EXPECT_EQ(end, out.substr(out.size() - end.size()));
}

TEST(Format_CobolMalformed, CheckingANumberShorterThanItsFieldReadsOnlyTheData)
{
    EXPECT_EQ(kHeading +
              "01     0   100           R\r\n"
              "***** RFHUtil format error - Invalid numeric data in next field\r\n"
              "05     0   100 NUMB      N                              12\r\n"
              "***** RFHUtil formating error - Copy book offset exceeds data area\r\n"
              "Copy book size 100, Data area size 2" + kMismatch,
              cobol("       01 R.\n"
                    "         05 N PIC 9(100).\n", "12", with(options(), false, true)));
}

TEST(Format_CobolMalformed, AGroupOccursStopsAfterTheEndOfTheData)
{
    // 1000 * 1000 occurrences and 100 bytes: one occurrence past the end is shown.
    // The copy book's 2001001 lines are within the COBOL view's limit.
    std::string out = cobol("       01 R.\n"
                            "         05 A OCCURS 1000 TIMES.\n"
                            "            10 B OCCURS 1000 TIMES.\n"
                            "               15 V PIC X.\n", std::string(100, 'A'));
    const std::string end = "10   100     1       101 B\r\n"
                            "15   100     1 CHAR  101 V\r\n"
                            "***** RFHUtil formating error - Copy book offset exceeds data area\r\n"
                            "Copy book size 1000000, Data area size 100" + kMismatch;
    ASSERT_GT(out.size(), end.size());
    EXPECT_EQ(end, out.substr(out.size() - end.size()));
    EXPECT_LT(out.size(), 16384u);
}

TEST(Format_CobolMalformed, AFieldPastTheEndOfTheDataEndsTheWalkOfItsLevel)
{
    // IBM's walk looped for ever: it went back to lastvar's VarNext, and the chain
    // 4 -> 6 -> 9 led into the level again. GoogleTest has no timeout; ctest's
    // TIMEOUT catches a regression.
    std::string out = cobol("       01 F0 SIGN TRAILING N(-1) VALUE 5.\n"
                            "       03 F1 POINTER OCCURS 0 TO 5 TIMES DEPENDING ON F100 COMP-1.\n"
                            "       03 F2 .\n"
                            "       03 F3 G(17).\n"
                            "       10 F4 COMP USAGE SIGN LEADING SEPARATE.\n"
                            "       02 F5 PIC OCCURS 9 TO 5 TIMES DEPENDING ON F3 9(1).\n"
                            "       10 F6 BINARY.\n"
                            "       49 F7 .\n"
                            "       02 F8 OCCURS 100 TO 2 TIMES DEPENDING ON F2 INDEX VALUE 5.\n", std::string("\xF1\x30\x00", 3));
    EXPECT_EQ(kHeading +
              "***** RFHUtil format error - Occurs value below minimum - value=0 minimum=9\r\n"
              "***** RFHUtil format error - Occurs value below minimum - value=0 minimum=100\r\n"
              "01     0   798           F0\r\n"
              "03     0     4           F1\r\n"
              "***** RFHUtil formating error - Copy book offset exceeds data area\r\n"
              "Copy book size 798, Data area size 3" + kMismatch,
              out);
}

TEST(Format_CobolMalformed, AnOccurrenceOfALargerRedefinitionPastTheDataIsNotListed)
{
    // Windows also lists G(4), at offset 6, with an empty value
    EXPECT_EQ(kHeading +
              "01     0     4           R\r\n"
              "05     0     4 CHAR      A                              1234\r\n"
              "05     0     8           B                                                               A\r\n"
              "***** Redefine larger than original definition\r\n"
              "10     0     2         1 G\r\n"
              "15     0     2 CHAR    1 V                              12\r\n"
              "10     2     2         2 G\r\n"
              "15     2     2 CHAR    2 V                              34\r\n"
              "10     4     2         3 G\r\n"
              "15     4     2 CHAR    3 V\r\n"
              "***** RFHUtil formating error - Copy book offset exceeds data area\r\n"
              "Copy book size 4, Data area size 4",
              cobol("       01 R.\n"
                    "         05 A PIC X(4).\n"
                    "         05 B REDEFINES A.\n"
                    "           10 G OCCURS 4 TIMES.\n"
                    "             15 V PIC X(2).\n", "1234"));
}

// --- COBOL view: IBM's behaviour, pinned ---------------------------------------

TEST(Format_CobolIbm, AnOccursOfAFixedCountListsEachOccurrence)
{
    EXPECT_EQ(kHeading +
              "01     0     6           R\r\n"
              "05     0     2 CHAR    1 A                              aa\r\n"
              "05     2     2 CHAR    2 A                              bb\r\n"
              "05     4     2 CHAR    3 A                              cc\r\n"
              "Copy book size 6, Data area size 6",
              cobol("       01  R.\n"
                    "           05  A  PIC X(2) OCCURS 3 TIMES.\n", "aabbcc"));
}

TEST(Format_CobolIbm, ARedefinitionNamesTheFieldItRedefines)
{
    EXPECT_EQ(kHeading +
              "01     0     4           R\r\n"
              "05     0     4 CHAR      A                              1234\r\n"
              "05     0     4 NUMB      B                              1234                             A\r\n"
              "Copy book size 4, Data area size 4",
              cobol("       01  R.\n"
                    "           05  A  PIC X(4).\n"
                    "           05  B  REDEFINES A PIC 9(4).\n", "1234"));
}

TEST(Format_CobolIbm, EbcdicDisplayFieldsAreTranslated)
{
    EXPECT_EQ(kHeading +
              "01     0     5           R\r\n"
              "05     0     5 CHAR      N                              HELLO\r\n"
              "Copy book size 5, Data area size 5",
              cobol("       01  R.\n"
                    "           05  N  PIC X(5).\n", ebcdic("HELLO"), options(500, CHAR_EBCDIC)));
}

TEST(Format_CobolIbm, ADSignMakesAPackedNumberNegative)
{
    RenderOptions o = options();
    o.pdNumericFormat = NUMERIC_HOST;
    EXPECT_EQ(kHeading +
              "01     0     2           R\r\n"
              "05     0     2 PD        P                              -123\r\n"
              "Copy book size 2, Data area size 2",
              cobol("       01  R.\n"
                    "           05  P  PIC S9(3) COMP-3.\n", "\x12\x3D", o));
}

TEST(Format_CobolIbm, CheckingCallsASignedEbcdicNumberInvalid)
{
    // IBM's check runs on the translated text, where the sign zone D3 is an L
    EXPECT_EQ(kHeading +
              "01     0     3           R\r\n"
              "***** RFHUtil format error - Invalid numeric data in next field\r\n"
              "05     0     3 NUMB      Z                              123-\r\n"
              "Copy book size 3, Data area size 3",
              cobol("       01  R.\n"
                    "           05  Z  PIC S9(3).\n", "\xF1\xF2\xD3", with(options(500, CHAR_EBCDIC), false, true)));
}

TEST(Format_CobolIbm, DataLongerThanTheCopybookIsReported)
{
    EXPECT_EQ(kHeading +
              "01     0     2           R\r\n"
              "05     0     2 CHAR      A                              aa\r\n"
              "Copy book size 2, Data area size 6" + kMismatch,
              cobol("       01  R.\n"
                    "           05  A  PIC X(2).\n", "aabbcc"));
}

TEST(Format_CobolIbm, AnEmptyBodyShowsNothing)
{
    EXPECT_EQ("", cobol("       01  R.\n"
                        "           05  A  PIC X(2).\n", ""));
}

TEST(Format_Character, EdiTrailingBlanksStopAtTheEndOfTheData)
{
    RenderOptions o = options();
    o.edi = true;
    EXPECT_NO_FATAL_FAILURE(render(message("UNA:+.? '   "), View::Character, o));
}

// --- size limit -----------------------------------------------------------------

namespace
{
    // the process's peak resident memory so far, in bytes
    long long peakResidentBytes()
    {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        return (long long)usage.ru_maxrss * 1024;
    }
}

TEST(Format_Limit, BodyOverMQsMaximumMessageLengthFailsBeforeItIsCopied)
{
    Message m = message("");
    m.body.assign(kMaxRenderBody + 1, 'x');
    long long before = peakResidentBytes();

    for (View view : { View::Character, View::Hex, View::Json, View::Cobol })
    {
        std::string out = "stale";
        RenderOptions o = options();
        o.copybookPath = "unused.cpy";
        Result r = format::render(m, view, o, out);
        EXPECT_FALSE(r.ok());
        EXPECT_EQ("the message body is 104857601 bytes, more than the Data tab's limit of 104857600 bytes"
                  " (MQ's maximum message length)", r.text);
        EXPECT_TRUE(out.empty()) << out.size() << " bytes of output";
    }

    // a copy of the body would raise the peak by 100 MB; the peak only grows, so a
    // larger peak from earlier in the process cannot make this fail
    EXPECT_LT(peakResidentBytes() - before, 16LL * 1024 * 1024);
}

// --- display code page ----------------------------------------------------------

namespace
{
    // Saves RFHUTIL_ACP and restores it (or removes it) when the test ends.
    struct AcpEnvGuard
    {
        AcpEnvGuard()
        {
            const char * old = getenv("RFHUTIL_ACP");
            had = (old != nullptr);
            if (had) saved = old;
        }
        ~AcpEnvGuard()
        {
            if (had) setenv("RFHUTIL_ACP", saved.c_str(), 1); else unsetenv("RFHUTIL_ACP");
        }
        std::string saved;
        bool had;
    };
}

TEST(Format_Character, BytesTheDisplayCodePageDoesNotShowAreDots)
{
    // comsubs.cpp's table: 932 does not show 0x80, 0xA0 and 0xFD-0xFF; 1252 does
    // not show 0x81, 0x8D, 0x8F, 0x90, 0x98 and 0x9D
    AcpEnvGuard guard;
    const std::string body = "\x80\xa0\xfd\x81" "A";
    RenderOptions japanese = options();
    japanese.display = { 932, 932 };
    RenderOptions western = options();
    western.display = { 1252, 437 };

    // the table follows display.ansi, not the process ANSI code page
    unsetenv("RFHUTIL_ACP");
    EXPECT_EQ(std::string("00000000 ...\x81" "A"), render(message(body), View::Character, japanese));
    EXPECT_EQ(std::string("00000000 \x80\xa0\xfd." "A"), render(message(body), View::Character, western));

    setenv("RFHUTIL_ACP", "932", 1);
    EXPECT_EQ(std::string("00000000 ...\x81" "A"), render(message(body), View::Character, japanese));
    EXPECT_EQ(std::string("00000000 \x80\xa0\xfd." "A"), render(message(body), View::Character, western));

    // and the scope ends with the render
    EXPECT_EQ(932, rfh_get_acp());
    unsetenv("RFHUTIL_ACP");
    EXPECT_EQ(1252, rfh_get_acp());
}

// --- locate -------------------------------------------------------------------
//
// Expected positions are worked out by hand from the Windows layouts (DataFormatter
// getHexData / getCharacterData / getBothData: a 9-character offset label, then 32
// characters, or 16 bytes as hex digits with a blank after every 4 bytes, or the hex
// followed by 16 characters with a blank after every 8) and from the Windows
// formulas getLineNumber and getLineOffset (DataArea.cpp:12793, 12872).

namespace
{
    struct Spot
    {
        int line;
        int column;
        int length;
    };

    bool where(View view, const RenderOptions & o, const Message & m, size_t offset, Spot & spot)
    {
        TextPos pos;
        pos.line = pos.column = pos.length = -7;
        bool found = locate(view, o, m, offset, pos);
        spot = { pos.line, pos.column, pos.length };
        return found;
    }

    std::string lineOf(const std::string & text, int line)
    {
        size_t start = 0;
        for (int i = 0; i < line; i++)
        {
            start = text.find("\r\n", start);
            if (start == std::string::npos)
            {
                return "";
            }
            start += 2;
        }
        size_t end = text.find("\r\n", start);
        return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }

    std::string letters(size_t n)
    {
        std::string s;
        for (size_t i = 0; i < n; i++)
        {
            s += (char)('A' + i % 26);
        }
        return s;
    }

    void expectSpot(View view, const RenderOptions & o, const Message & m, size_t offset, Spot want)
    {
        Spot got;
        ASSERT_TRUE(where(view, o, m, offset, got)) << "offset " << offset;
        EXPECT_EQ(want.line, got.line) << "offset " << offset;
        EXPECT_EQ(want.column, got.column) << "offset " << offset;
        EXPECT_EQ(want.length, got.length) << "offset " << offset;
    }
}

TEST(Locate, CharacterViewIsThirtyTwoToALineAfterNineCharacters)
{
    // getLineNumber: ofs / 32; getLineOffset: ofs % 32 + 9
    Message m = message(letters(100));
    RenderOptions o = options();
    expectSpot(View::Character, o, m, 0, { 0, 9, 1 });
    expectSpot(View::Character, o, m, 31, { 0, 40, 1 });
    expectSpot(View::Character, o, m, 32, { 1, 9, 1 });
    expectSpot(View::Character, o, m, 33, { 1, 10, 1 });
    expectSpot(View::Character, o, m, 47, { 1, 24, 1 });
    expectSpot(View::Character, o, m, 48, { 1, 25, 1 });
    expectSpot(View::Character, o, m, 99, { 3, 12, 1 });
}

TEST(Locate, HexViewSelectsTheTwoDigitsOfTheByte)
{
    // getLineOffset: d = (ofs % 16) * 2; column d + 9, + 10, + 11 or + 12 by quarter
    Message m = message(letters(40));
    RenderOptions o = options();
    expectSpot(View::Hex, o, m, 0, { 0, 9, 2 });
    expectSpot(View::Hex, o, m, 3, { 0, 15, 2 });
    expectSpot(View::Hex, o, m, 4, { 0, 18, 2 });
    expectSpot(View::Hex, o, m, 5, { 0, 20, 2 });
    expectSpot(View::Hex, o, m, 8, { 0, 27, 2 });
    expectSpot(View::Hex, o, m, 15, { 0, 42, 2 });
    expectSpot(View::Hex, o, m, 16, { 1, 9, 2 });
    expectSpot(View::Hex, o, m, 25, { 1, 29, 2 });
    expectSpot(View::Hex, o, m, 39, { 2, 24, 2 });
}

TEST(Locate, BothViewPointsAtTheCharacterNotTheHexDigits)
{
    // Windows: ofs % 16 + 9 (+ 1 from 8 on), which is inside the hex digits and one
    // byte-position early in the text; the text starts at column 9 + 36 = 45, with a
    // blank after every 8 characters
    Message m = message(letters(40));
    RenderOptions o = options();
    expectSpot(View::Both, o, m, 0, { 0, 45, 1 });
    expectSpot(View::Both, o, m, 7, { 0, 52, 1 });
    expectSpot(View::Both, o, m, 8, { 0, 54, 1 });
    expectSpot(View::Both, o, m, 15, { 0, 61, 1 });
    expectSpot(View::Both, o, m, 16, { 1, 45, 1 });
    expectSpot(View::Both, o, m, 39, { 2, 52, 1 });
}

TEST(Locate, EveryOffsetPointsAtItsCharacterInTheCharacterView)
{
    for (size_t size : { 1, 31, 32, 33, 47, 48, 100 })
    {
        Message m = message(letters(size));
        RenderOptions o = options();
        std::string text = render(m, View::Character, o);
        for (size_t i = 0; i < size; i++)
        {
            Spot s;
            ASSERT_TRUE(where(View::Character, o, m, i, s)) << size << " " << i;
            EXPECT_EQ(1, s.length);
            EXPECT_EQ(m.body[i], (unsigned char)lineOf(text, s.line)[s.column]) << size << " " << i;
        }
    }
}

TEST(Locate, EveryOffsetPointsAtItsDigitsInTheHexView)
{
    for (size_t size : { 1, 15, 16, 17, 33, 100 })
    {
        Message m = message(std::string(size, 'x'));
        for (size_t i = 0; i < size; i++)
        {
            m.body[i] = (unsigned char)(i * 7 + 3);
        }
        RenderOptions o = options();
        std::string text = render(m, View::Hex, o);
        char digits[3];
        for (size_t i = 0; i < size; i++)
        {
            Spot s;
            ASSERT_TRUE(where(View::Hex, o, m, i, s)) << size << " " << i;
            snprintf(digits, sizeof(digits), "%02X", m.body[i]);
            EXPECT_EQ(2, s.length);
            EXPECT_EQ(std::string(digits), lineOf(text, s.line).substr(s.column, 2)) << size << " " << i;
        }
    }
}

TEST(Locate, EveryOffsetPointsAtItsCharacterInTheBothView)
{
    for (size_t size : { 1, 15, 16, 17, 33, 100 })
    {
        Message m = message(letters(size));
        RenderOptions o = options();
        std::string text = render(m, View::Both, o);
        for (size_t i = 0; i < size; i++)
        {
            Spot s;
            ASSERT_TRUE(where(View::Both, o, m, i, s)) << size << " " << i;
            EXPECT_EQ(1, s.length);
            EXPECT_EQ(m.body[i], (unsigned char)lineOf(text, s.line)[s.column]) << size << " " << i;
        }
    }
}

TEST(Locate, CrLfOnPutsTheOffsetOnItsLineAtColumnZero)
{
    // "00000000 line one" / "00000010 line two": Windows counts the LF bytes before the
    // offset and uses column 0; the CR and the LF belong to the line they end
    Message m = message("line one\r\nline two");
    RenderOptions o = options();
    o.crlf = true;
    for (size_t i : { 0, 5, 7, 8, 9 })
    {
        expectSpot(View::Character, o, m, i, { 0, 0, 1 });
    }
    for (size_t i : { 10, 11, 17 })
    {
        expectSpot(View::Character, o, m, i, { 1, 0, 1 });
    }
}

TEST(Locate, CrLfOnSkipsBlankLinesAsTheViewDoes)
{
    // the view drops every leading CR and LF of a line, so "a CR LF CR LF b" has two
    // lines; Windows' count of LF bytes puts the b on line 2, which is not there
    Message m = message("a\r\n\r\nb\nc");
    RenderOptions o = options();
    o.crlf = true;
    EXPECT_EQ("00000000 a\r\n00000005 b\r\n00000007 c", render(m, View::Character, o));
    expectSpot(View::Character, o, m, 0, { 0, 0, 1 });
    expectSpot(View::Character, o, m, 5, { 1, 0, 1 });
    expectSpot(View::Character, o, m, 7, { 2, 0, 1 });
}

TEST(Locate, EdiOnPutsTheOffsetOnItsSegmentLineAtColumnZero)
{
    // lines start at offsets 0, 9 and 22; Windows says line 0 for all of them
    Message m = message("UNA:+.? 'UNB+UNOC:3+X'UNH+1'");
    RenderOptions o = options();
    o.edi = true;
    expectSpot(View::Character, o, m, 0, { 0, 0, 1 });
    expectSpot(View::Character, o, m, 8, { 0, 0, 1 });
    expectSpot(View::Character, o, m, 9, { 1, 0, 1 });
    expectSpot(View::Character, o, m, 25, { 2, 0, 1 });
}

TEST(Locate, EdiOnForDataThatIsNotEdiIsStillColumnZero)
{
    // the option decides the column, as in getLineOffset, but the line is the one shown
    Message m = message(letters(70));
    RenderOptions o = options();
    o.edi = true;
    expectSpot(View::Character, o, m, 40, { 1, 0, 1 });
}

TEST(Locate, ByteOrderMarkIsNotShownSoLaterOffsetsMoveBack)
{
    // Windows ignores the option: it puts 'a' (offset 2) at column 11, which is past the 'b'
    Message m = message("\xfe\xff" "ab");
    RenderOptions o = options();
    o.bom = true;
    expectSpot(View::Character, o, m, 0, { 0, 0, 0 });
    expectSpot(View::Character, o, m, 1, { 0, 9, 1 });
    expectSpot(View::Character, o, m, 2, { 0, 10, 1 });
    expectSpot(View::Character, o, m, 3, { 0, 11, 1 });
    expectSpot(View::Both, o, m, 0, { 0, 0, 0 });
    expectSpot(View::Both, o, m, 3, { 0, 47, 1 });
    // without the option the mark is shown
    expectSpot(View::Character, options(), m, 2, { 0, 11, 1 });
}

TEST(Locate, ByteOrderMarkOptionDoesNotChangeTheHexView)
{
    Message m = message("\xfe\xff" "ab");
    RenderOptions o = options();
    o.bom = true;
    expectSpot(View::Hex, o, m, 0, { 0, 9, 2 });
    expectSpot(View::Hex, o, m, 2, { 0, 13, 2 });
}

TEST(Locate, OptionWithoutAMarkChangesNothing)
{
    Message m = message("abc");
    RenderOptions o = options();
    o.bom = true;
    expectSpot(View::Character, o, m, 1, { 0, 10, 1 });
}

TEST(Locate, DoubleByteCharactersCountAsOneOfTheThirtyTwo)
{
    // 31 a, a double-byte character (offsets 31, 32), then b c: the view has
    // "00000000 " + 31 a + 2 bytes and "00000033 bc". Windows' ofs / 32 and ofs % 32 + 9 put the b
    // (offset 33) on the c
    std::string body(31, 'a');
    body += "\x82\xa0";
    body += "bc";
    Message m = message(body, 932);
    RenderOptions o = options(932, CHAR_JAPANESE);
    expectSpot(View::Character, o, m, 30, { 0, 39, 1 });
    expectSpot(View::Character, o, m, 31, { 0, 40, 1 });
    expectSpot(View::Character, o, m, 32, { 0, 41, 1 });
    expectSpot(View::Character, o, m, 33, { 1, 9, 1 });
    expectSpot(View::Character, o, m, 34, { 1, 10, 1 });
}

TEST(Locate, EbcdicDataHasTheSameLayout)
{
    Message m = message(ebcdic("Hello, world!"), 500, 785);
    RenderOptions o = options(500, CHAR_EBCDIC);
    expectSpot(View::Character, o, m, 7, { 0, 16, 1 });
    expectSpot(View::Hex, o, m, 7, { 0, 24, 2 });
}

TEST(Locate, UnicodeDataIsPositionedByBytes)
{
    Message m = message(std::string("a\0b\0c\0", 6), 1200);
    RenderOptions o = options(1200, CHAR_ASCII);
    expectSpot(View::Character, o, m, 1, { 0, 10, 1 });
    expectSpot(View::Character, o, m, 4, { 0, 13, 1 });
}

TEST(Locate, NulBytesTakeAColumnEach)
{
    Message m = message(std::string("a\0b\0c", 5));
    RenderOptions o = options();
    EXPECT_EQ("00000000 a.b.c", render(m, View::Character, o));
    expectSpot(View::Character, o, m, 2, { 0, 11, 1 });
    expectSpot(View::Character, o, m, 4, { 0, 13, 1 });
}

TEST(Locate, EmptyBodyHasNoPosition)
{
    Message m = message("");
    Spot s;
    for (View v : { View::Character, View::Hex, View::Both })
    {
        EXPECT_FALSE(where(v, options(), m, 0, s));
    }
}

TEST(Locate, OutsideTheBodyIsFalseAndLeavesThePositionAlone)
{
    Message m = message(letters(20));
    TextPos pos;
    pos.line = 4;
    pos.column = 5;
    pos.length = 6;
    EXPECT_FALSE(locate(View::Hex, options(), m, 20, pos));
    EXPECT_FALSE(locate(View::Hex, options(), m, (size_t)-1, pos));
    EXPECT_FALSE(locate(View::Character, options(), m, 21, pos));
    EXPECT_EQ(4, pos.line);
    EXPECT_EQ(5, pos.column);
    EXPECT_EQ(6, pos.length);
    EXPECT_TRUE(locate(View::Character, options(), m, 19, pos));
}

TEST(Locate, UnsupportedViewsAreFalse)
{
    Message m = message("<a>b</a>");
    Spot s;
    for (View v : { View::Xml, View::Parsed, View::Cobol, View::Json, View::Fixed })
    {
        EXPECT_FALSE(where(v, options(), m, 0, s));
    }
}

TEST(Locate, TextRenderedEarlierGivesTheSamePosition)
{
    Message m = message(letters(70));
    RenderOptions o = options();
    for (View v : { View::Character, View::Hex, View::Both })
    {
        std::string text = render(m, v, o);
        for (size_t i : { 0, 1, 31, 32, 69 })
        {
            TextPos a, b;
            ASSERT_TRUE(locate(v, o, m, i, a));
            ASSERT_TRUE(locate(v, o, m, text, i, b));
            EXPECT_EQ(a.line, b.line);
            EXPECT_EQ(a.column, b.column);
            EXPECT_EQ(a.length, b.length);
        }
    }
}

TEST(Locate, TextThatIsNotTheViewIsFalseNotAGuess)
{
    Message m = message(letters(40));
    RenderOptions o = options();
    TextPos pos;
    EXPECT_FALSE(locate(View::Character, o, m, "", 3, pos));
    EXPECT_FALSE(locate(View::Character, o, m, "no label here", 3, pos));
    EXPECT_FALSE(locate(View::Hex, o, m, "00000000 ", 3, pos));
    // a label further on than the offset leaves no line holding it
    EXPECT_FALSE(locate(View::Hex, o, m, "00000100 3031", 3, pos));
    // a line too short for the byte
    EXPECT_FALSE(locate(View::Character, o, m, "00000000 AB", 5, pos));
    EXPECT_FALSE(locate(View::Hex, o, m, "00000000 3031", 5, pos));
}

TEST(Locate, LabelsOfNineDigitsAreReadAndShiftTheColumns)
{
    // the offset label is %8.8d, so a body over 99,999,999 bytes has longer labels and
    // the text one column further right (Windows' formulas do not allow for it)
    Message m = message("xy");
    RenderOptions o = options();
    TextPos pos;
    ASSERT_TRUE(locate(View::Character, o, m, std::string_view("000000000 xy"), 1, pos));
    EXPECT_EQ(0, pos.line);
    EXPECT_EQ(11, pos.column);
    ASSERT_TRUE(locate(View::Hex, o, m, std::string_view("000000000 7879"), 1, pos));
    EXPECT_EQ(12, pos.column);
}

TEST(Locate, SixteenMegabytesIsFast)
{
    // linear in the text: one pass over the lines, nothing quadratic
    const size_t size = 16u * 1024 * 1024;
    Message m = message(std::string(size, 'x'));
    RenderOptions o = options();
    for (View v : { View::Character, View::Hex, View::Both })
    {
        std::string text = render(m, v, o);
        auto start = std::chrono::steady_clock::now();
        for (size_t i : { size - 1, size / 2, (size_t)0 })
        {
            TextPos pos;
            ASSERT_TRUE(locate(v, o, m, text, i, pos));
        }
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        EXPECT_LT(seconds, 2.0) << "view " << (int)v;
    }
    // and with the render folded in
    TextPos pos;
    auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(locate(View::Character, o, m, size - 1, pos));
    EXPECT_EQ((int)((size - 1) / 32), pos.line);
    EXPECT_EQ((int)((size - 1) % 32 + 9), pos.column);
    EXPECT_LT(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), 10.0);
}

TEST(Locate, ExactSizeBuffersAreNotReadPastTheirEnd)
{
    // memcheck.Locate runs this under the guard allocator: a read one byte past the
    // end of an exact-size body or text faults
    for (size_t size : { 1, 7, 16, 17, 33 })
    {
        Message m = message(letters(size));
        m.body.shrink_to_fit();
        for (View v : { View::Character, View::Hex, View::Both })
        {
            RenderOptions o = options();
            std::string text = render(m, v, o);
            std::unique_ptr<char[]> exact(new char[text.size()]);
            memcpy(exact.get(), text.data(), text.size());
            for (size_t i = 0; i < size; i++)
            {
                TextPos pos;
                ASSERT_TRUE(locate(v, o, m, std::string_view(exact.get(), text.size()), i, pos));
                ASSERT_LT((size_t)pos.line, text.size());
            }
            // and truncations of the text at every length
            for (size_t cut = 0; cut <= text.size(); cut++)
            {
                std::unique_ptr<char[]> part(new char[cut ? cut : 1]);
                memcpy(part.get(), text.data(), cut);
                TextPos pos;
                locate(v, o, m, std::string_view(part.get(), cut), size - 1, pos);
            }
        }
    }
}
