// CopybookTests.cpp - CCopybook parser and formatData, without rfh_format.
//
// Mirrors the Format_Cobol, Format_CobolIbm and Format_CobolMalformed suites
// in FormatTests.cpp, calling CCopybook directly so these tests compile and run
// on Windows (UnitTests.vcxproj) where rfh_format is not available.
//
// Buffer sizing follows rfh_format.cpp: (getMaxLines() * 128) + 65536, with
// 8192 bytes of overflow margin that formatData may legitimately write into.

#include <gtest/gtest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#include <process.h>
#endif

#include <memory>
#include <string>

#include "comsubs.h"
#include "Copybook.h"
#include "rfhconst.h"

namespace
{
    const std::string kHeading =
        "Level  Ofs  Len Type  Occ Variable Name                  Value\r\n";
    const std::string kMismatch =
        "\r\n***** RFHUtil error - Data length does not match copybook";

    // --- portable temp-file helper -------------------------------------------

    class CopybookFile
    {
    public:
        CopybookFile()
        {
            static int seq = 0;
#ifdef _WIN32
            char dir[MAX_PATH];
            GetTempPathA(MAX_PATH, dir);
            snprintf(path_, sizeof(path_), "%srfhutil_cb_%lu_%d.cpy",
                     dir, (unsigned long)GetCurrentProcessId(), ++seq);
#else
            snprintf(path_, sizeof(path_), "/tmp/rfhutil_cb_%d_%d.cpy",
                     (int)getpid(), ++seq);
#endif
        }

        explicit CopybookFile(const std::string & text) : CopybookFile()
        {
            write(text);
        }

        ~CopybookFile() { remove(path_); }

        CopybookFile(const CopybookFile &) = delete;
        CopybookFile & operator=(const CopybookFile &) = delete;

        void write(const std::string & text)
        {
            FILE * f = fopen(path_, "wb");
            if (f)
            {
                fwrite(text.data(), 1, text.size(), f);
                fclose(f);
            }
        }

        const char * path() const { return path_; }

    private:
        char path_[512];
    };

    // --- render helper -------------------------------------------------------

    // Calls parseCopyBook + formatData; returns "" for empty data, kHeading +
    // formatted text otherwise. Fails the test if parseCopyBook returns non-zero.
    std::string cobol(const std::string & copybook,
                      const std::string & data,
                      int charFormat  = CHAR_ASCII,
                      int numFormat   = NUMERIC_PC,
                      int pdFormat    = NUMERIC_HOST,
                      int ccsid       = 0,
                      int indent      = 0,
                      int checkData   = 0)
    {
        CopybookFile file(copybook);

        CCopybook cb;
        cb.resetCopybook();
        int rc = cb.parseCopyBook(file.path());
        if (rc != 0)
        {
            ADD_FAILURE() << "parseCopyBook returned " << rc;
            return "";
        }

        if (data.empty())
            return "";

        // buffer: same calculation as rfh_format.cpp
        int64_t maxSize = ((int64_t)cb.getMaxLines() * 128) + 65536;
        std::unique_ptr<char[]> buf(new char[(size_t)maxSize + 8192]);
        buf[0] = 0;

        cb.formatData(buf.get(), (int)maxSize,
                      reinterpret_cast<const unsigned char *>(data.data()),
                      (unsigned int)data.size(),
                      charFormat, numFormat, pdFormat, ccsid, indent, checkData);

        return kHeading + buf.get();
    }

    std::string ebcdic(const std::string & ascii)
    {
        std::string out(ascii.size(), '\0');
        AsciiToEbcdic(const_cast<unsigned char *>(
                          reinterpret_cast<const unsigned char *>(ascii.data())),
                      (int)ascii.size(),
                      reinterpret_cast<unsigned char *>(&out[0]));
        return out;
    }

    // a copy book with display, character, packed decimal and binary fields
    const std::string kCustomerCopybook =
        "       01  CUSTOMER-RECORD.\n"
        "           05  CUST-ID          PIC 9(5).\n"
        "           05  CUST-NAME        PIC X(10).\n"
        "           05  BALANCE          PIC S9(5)V99 COMP-3.\n"
        "           05  TXN-COUNT        PIC S9(4) COMP.\n";

    const std::string kCustomer =
        std::string("00042SMITH     ") + std::string("\x01\x23\x45\x6C", 4);
}

// --- Format_Cobol ------------------------------------------------------------

TEST(Copybook_Cobol, FieldsAreListedWithTheirOffsetsAndValues)
{
    EXPECT_EQ(kHeading +
              "01     0    21           CUSTOMER-RECORD\r\n"
              "05     0     5 NUMB      CUST-ID                        00042\r\n"
              "05     5    10 CHAR      CUST-NAME                      SMITH     \r\n"
              "05    15     4 PD        BALANCE                        0123456\r\n"
              "05    19     2 INT       TXN-COUNT                      0007\r\n"
              "Copy book size 21, Data area size 21",
              cobol(kCustomerCopybook, kCustomer + std::string("\x00\x07", 2),
                    CHAR_ASCII, NUMERIC_HOST));
}

TEST(Copybook_Cobol, BinaryFieldsFollowTheIntegerEncoding)
{
    std::string out = cobol(kCustomerCopybook, kCustomer + std::string("\x07\x00", 2),
                            CHAR_ASCII, NUMERIC_PC);
    EXPECT_NE(std::string::npos, out.find("TXN-COUNT                      0007\r\n"));
}

TEST(Copybook_Cobol, WithoutACopybookTheViewFails)
{
    CCopybook cb;
    cb.resetCopybook();
    int rc = cb.parseCopyBook("/no/such/file.cpy");
    EXPECT_NE(0, rc);
}

TEST(Copybook_Cobol, ARedefinitionOfAnUndefinedFieldFails)
{
    CopybookFile file("       01 R.\n"
                      "         05 B REDEFINES MISSING PIC X.\n");
    CCopybook cb;
    cb.resetCopybook();
    int rc = cb.parseCopyBook(file.path());
    EXPECT_NE(0, rc);
}

TEST(Copybook_Cobol, AnEmptyBodyShowsNothing)
{
    EXPECT_EQ("", cobol("       01  R.\n"
                        "           05  A  PIC X(2).\n", ""));
}

// --- Format_CobolIbm ---------------------------------------------------------

TEST(Copybook_CobolIbm, AnOccursOfAFixedCountListsEachOccurrence)
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

TEST(Copybook_CobolIbm, ARedefinitionNamesTheFieldItRedefines)
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

TEST(Copybook_CobolIbm, EbcdicDisplayFieldsAreTranslated)
{
    EXPECT_EQ(kHeading +
              "01     0     5           R\r\n"
              "05     0     5 CHAR      N                              HELLO\r\n"
              "Copy book size 5, Data area size 5",
              cobol("       01  R.\n"
                    "           05  N  PIC X(5).\n",
                    ebcdic("HELLO"), CHAR_EBCDIC));
}

TEST(Copybook_CobolIbm, ADSignMakesAPackedNumberNegative)
{
    EXPECT_EQ(kHeading +
              "01     0     2           R\r\n"
              "05     0     2 PD        P                              -123\r\n"
              "Copy book size 2, Data area size 2",
              cobol("       01  R.\n"
                    "           05  P  PIC S9(3) COMP-3.\n",
                    std::string("\x12\x3D", 2),
                    CHAR_ASCII, NUMERIC_PC, NUMERIC_HOST));
}

TEST(Copybook_CobolIbm, CheckingCallsASignedEbcdicNumberInvalid)
{
    EXPECT_EQ(kHeading +
              "01     0     3           R\r\n"
              "***** RFHUtil format error - Invalid numeric data in next field\r\n"
              "05     0     3 NUMB      Z                              123-\r\n"
              "Copy book size 3, Data area size 3",
              cobol("       01  R.\n"
                    "           05  Z  PIC S9(3).\n",
                    std::string("\xF1\xF2\xD3", 3),
                    CHAR_EBCDIC, NUMERIC_PC, NUMERIC_HOST, 500, 0, 1));
}

TEST(Copybook_CobolIbm, DataLongerThanTheCopybookIsReported)
{
    EXPECT_EQ(kHeading +
              "01     0     2           R\r\n"
              "05     0     2 CHAR      A                              aa\r\n"
              "Copy book size 2, Data area size 6" + kMismatch,
              cobol("       01  R.\n"
                    "           05  A  PIC X(2).\n", "aabbcc"));
}

// --- Format_CobolMalformed ---------------------------------------------------

TEST(Copybook_CobolMalformed, AGroupOccursDependingOnWithoutAMaximumShowsTheOccurrencesInTheData)
{
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

TEST(Copybook_CobolMalformed, AGroupOccursStopsAtTheEndOfItsBuffer)
{
    std::string out = cobol("       01 R.\n"
                            "         05 CNT PIC 9(6).\n"
                            "         05 ITEMS OCCURS 5 TIMES DEPENDING ON CNT.\n"
                            "            10 V PIC X.\n",
                            "100000" + std::string(100000, 'A'));
    const std::string end =
        "***** Exceeded memory buffer\r\nCopy book size 100006, Data area size 100006";
    ASSERT_GT(out.size(), end.size());
    EXPECT_EQ(end, out.substr(out.size() - end.size()));
    EXPECT_LT(out.size(), (23u * 128) + 65536 + 8192);
}

TEST(Copybook_CobolMalformed, AStatementOfManyShortLinesStopsAtItsBuffer)
{
    std::string copybook = "       01  REC\n";
    for (int i = 0; i < 5000; i++)
        copybook += "       X\n";
    EXPECT_EQ(kHeading +
              "00     0     0           \r\n"
              "Copy book size 0, Data area size 6" + kMismatch,
              cobol(copybook, "aabbcc"));
}

TEST(Copybook_CobolMalformed, ALineThatStartsWithANulIsSkipped)
{
    EXPECT_EQ(kHeading +
              "01     0     1           A\r\n"
              "Copy book size 1, Data area size 6" + kMismatch,
              cobol(std::string("\0\n", 2) + "       01  A PIC X.\n", "aabbcc"));
}

TEST(Copybook_CobolMalformed, ASyncGroupWhoseFirstChildIsNotAlignedDoesNotDivideByZero)
{
    std::string out = cobol("       01 F0 BINARY OCCURS 17 TIMES SYNC.\n"
                            "       10 F1 COMP PIC.\n"
                            "       77 F2 REDEFINES F0 POINTER.\n",
                            std::string(40, '\0'));
    EXPECT_EQ(0u, out.find(kHeading +
                           "01     0     2         1 F0\r\n"
                           "10     0     2         1 F1\r\n"));
}

TEST(Copybook_CobolMalformed, IndentingDeeperThanThirtyTwoLevelsStopsAtThirtyOneBlanks)
{
    std::string copybook = "       01  R.\n";
    for (int level = 2; level < 49; level++)
    {
        char line[32];
        snprintf(line, sizeof(line), "       %02d  LV%d.\n", level, level);
        copybook += line;
    }
    copybook += "       49  LEAF PIC X(5).\n";
    std::string out = cobol(copybook, "aabbcc", CHAR_ASCII, NUMERIC_PC, NUMERIC_HOST, 0, 1);
    EXPECT_NE(std::string::npos,
              out.find("\r\n" + std::string(30, ' ') + "31     0     5           LV31\r\n"));
    EXPECT_NE(std::string::npos,
              out.find("\r\n" + std::string(31, ' ') + "49     0     5 CHAR      LEAF                           aabbc\r\n"));
}

TEST(Copybook_CobolMalformed, APackedDecimalFieldLongerThanItsWorkAreaShowsWhatFits)
{
    std::string out = cobol("       01 R.\n"
                            "         05 A PIC S9(200) COMP-3.\n",
                            std::string(400, '\x11'));
    EXPECT_EQ(kHeading +
              "01     0   101           R\r\n"
              "***** RFHUtil format error - Invalid Packed Decimal data in next field\r\n"
              "05     0   101 PD        A                              " +
              std::string(158, '1') + "\r\n"
              "Copy book size 101, Data area size 400" + kMismatch,
              out);
}

TEST(Copybook_CobolMalformed, ANegativeLengthShowsNoValue)
{
    EXPECT_EQ(kHeading +
              "01     0    -5           R\r\n"
              "05     0    -5 CHAR      A\r\n"
              "Copy book size -5, Data area size 6" + kMismatch,
              cobol("       01 R.\n"
                    "         05 A PIC X(-5).\n", "aabbcc"));
}

TEST(Copybook_CobolMalformed, ALengthBeyondAnIntShowsNoValue)
{
    std::string out = cobol("       01 R.\n"
                            "         05 A PIC X(999999999999).\n", "aabbcc");
    EXPECT_NE(std::string::npos, out.find(" CHAR      A\r\n"));
    EXPECT_NE(std::string::npos, out.find(", Data area size 6" + kMismatch));
}

TEST(Copybook_CobolMalformed, AHugeLengthDoesNotOverflowTheOffsets)
{
    std::string out = cobol("       01 R.\n"
                            "         05 A PIC X(2000000000).\n"
                            "         05 B PIC X(2000000000).\n"
                            "         05 C PIC X.\n", "aabbcc");
    EXPECT_NE(std::string::npos,
              out.find("05     0 2000000000 CHAR      A                              aabbcc\r\n"));
}

TEST(Copybook_CobolMalformed, ACountWiderThanItsWorkAreaIsRead)
{
    std::string out = cobol("       01 R.\n"
                            "         05 CNT PIC 9(300).\n"
                            "         05 ITEMS PIC X OCCURS 1 TO 3 TIMES DEPENDING ON CNT.\n",
                            std::string(299, '0') + "2AB");
    EXPECT_NE(std::string::npos,
              out.find("05     0   300 NUMB      CNT                            " +
                       std::string(128, '0') + "\r\n"));
    EXPECT_NE(std::string::npos,
              out.find("Copy book size 300, Data area size 302" + kMismatch));
}

TEST(Copybook_CobolMalformed, AFieldBeforeTheDataIsNotRead)
{
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

TEST(Copybook_CobolMalformed, NestedOccursDependingOnStaysWithinTheData)
{
    std::string body = "99";
    for (int i = 0; i < 50; i++)
        body += "99X";
    std::string out = cobol("       01 R.\n"
                            "         05 C1 PIC 9(2).\n"
                            "         05 A OCCURS 1 TO 99 TIMES DEPENDING ON C1.\n"
                            "            10 C2 PIC 9(2).\n"
                            "            10 B OCCURS 1 TO 99 TIMES DEPENDING ON C2.\n"
                            "               15 V PIC X.\n", body);
    EXPECT_EQ(0u, out.find(kHeading +
                           "01     0  9903           R\r\n"
                           "05     0     2 NUMB      C1                             99\r\n"));
    const std::string end = "Copy book size 9903, Data area size 152" + kMismatch;
    ASSERT_GT(out.size(), end.size());
    EXPECT_EQ(end, out.substr(out.size() - end.size()));
}

TEST(Copybook_CobolMalformed, CheckingANumberShorterThanItsFieldReadsOnlyTheData)
{
    EXPECT_EQ(kHeading +
              "01     0   100           R\r\n"
              "***** RFHUtil format error - Invalid numeric data in next field\r\n"
              "05     0   100 NUMB      N                              12\r\n"
              "***** RFHUtil formating error - Copy book offset exceeds data area\r\n"
              "Copy book size 100, Data area size 2" + kMismatch,
              cobol("       01 R.\n"
                    "         05 N PIC 9(100).\n", "12",
                    CHAR_ASCII, NUMERIC_PC, NUMERIC_HOST, 0, 0, 1));
}
