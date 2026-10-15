// SharedSourcesTests.cpp — the shared RFHUtil sources (comsubs, ...) on top of the
// platform layer: the Win32 calls they used to make now go through core/platform,
// and these tests pin that the results are the ones Windows produced.

#include <gtest/gtest.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "rfh_platform.h"
#include "rfh_codepage.h"
#include "rfh_trace.h"
#include "comsubs.h"
#include "Names.h"
#include "xmlsubs.h"
#include "XMLParse.h"
#include "JsonParse.h"

#define REQUIRE_CODEPAGE(ccsid) \
    do { if (!rfh_is_codepage_installed(ccsid)) GTEST_SKIP() << "code page " << (ccsid) << " is not installed"; } while (0)

TEST(SharedSources_Time, GetTimeIsMonotonic)
{
    MY_TIME_T start = GetTime();
    MY_TIME_T end = GetTime();
    EXPECT_GE(end, start);
    EXPECT_GE(DiffTime(start, end), 0.0);
}

TEST(SharedSources_Time, DiffTimeScalesToMicroseconds)
{
    MY_TIME_T oneSecond = (MY_TIME_T)rfh_hrtime_frequency();
    EXPECT_DOUBLE_EQ(1000000.0, DiffTime(0, oneSecond));
}

TEST(SharedSources_Ucs2, RoundLength2PadsOddLengthLittleEndian)
{
    // roundLength2 used wcslen on 16-bit data; on Linux wchar_t is 32 bits.
    RFH_UCS2 buffer[8] = { 'a', 'b', 'c', 0, 0, 0, 0, 0 };
    int len = roundLength2(reinterpret_cast<unsigned char *>(buffer), 0 /* NUMERIC_PC */);
    EXPECT_EQ(4, len);
    EXPECT_EQ((RFH_UCS2)' ', buffer[3]);
}

TEST(SharedSources_Ucs2, MultiByteToUcs2RoundTrips)
{
    unsigned char text[] = "RFH2 folder";
    RFH_UCS2 ucs[32];
    unsigned char back[32] = { 0 };

    int chars = MultiByteToUCS2(reinterpret_cast<unsigned char *>(ucs), sizeof(ucs), text, (int)strlen((char *)text));
    ASSERT_EQ((int)strlen((char *)text), chars);
    EXPECT_EQ(chars, UCS2ToMultiByte(reinterpret_cast<unsigned char *>(ucs), chars * 2, back, sizeof(back)));
    EXPECT_STREQ("RFH2 folder", reinterpret_cast<char *>(back));
}

TEST(SharedSources_Codepage, IsCodePageInstalledUsesCodePageServices)
{
    EXPECT_EQ(rfh_is_codepage_installed(500), isCodePageInstalled(500));
    EXPECT_FALSE(isCodePageInstalled(99999));
}

TEST(SharedSources_Codepage, EbcdicCharToAsciiCharTranslatesOneCharacter)
{
    REQUIRE_CODEPAGE(500);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0x81 };   // 'a' in EBCDIC 500
    EXPECT_EQ(1, EbcdicCharToAsciiChar(500, 1252, in, out));
    EXPECT_EQ('a', out[0]);
}

TEST(SharedSources_Codepage, TranslateEbcdicDataUsesCodePage500)
{
    REQUIRE_CODEPAGE(500);
    const unsigned char in[] = { 0xC8, 0x85, 0x93, 0x93, 0x96 };   // "Hello" in EBCDIC 500
    unsigned char out[sizeof(in) + 1] = { 0 };
    translateEbcdicData(out, in, sizeof(in), 500);
    EXPECT_STREQ("Hello", reinterpret_cast<char *>(out));
}

TEST(SharedSources_Codepage, MatchEbcdicToAsciiKeepsHistoricCcsid37Behaviour)
{
    // Upstream writes "case 037:", which C reads as octal 31, so CCSID 37 has
    // never matched an ANSI code page. Kept for compatibility; reported upstream.
    EXPECT_EQ(0, matchEbcdicToAscii(37));
    EXPECT_EQ(1252, matchEbcdicToAscii(500));
}

namespace
{
    class RecordingTrace : public RfhTrace
    {
    public:
        std::vector<std::string> lines;
        BOOL isTraceEnabled() override { return TRUE; }
        void logTraceEntry(const char * traceInfo) override { lines.push_back(traceInfo); }
        void dumpTraceData(const char *, const unsigned char *, unsigned int) override {}
    };

    // Restores the no-op sink even when an assertion fails.
    struct TraceGuard
    {
        explicit TraceGuard(RfhTrace * trace) { rfhSetTrace(trace); }
        ~TraceGuard() { rfhSetTrace(nullptr); }
    };
}

TEST(SharedSources_Trace, TraceGoesToInstalledSink)
{
    RecordingTrace recorder;
    TraceGuard guard(&recorder);

    void * ptr = rfhMalloc(16, "TESTTAG ");
    ASSERT_NE(nullptr, ptr);
    rfhFree(ptr);

    ASSERT_FALSE(recorder.lines.empty());
    EXPECT_NE(std::string::npos, recorder.lines.front().find("Entering rfhMalloc()"));
}

namespace
{
    // Reads the number after "key" in a trace line; false when it is not directly
    // followed by a digit (the old %I64d printed padding spaces under glibc).
    bool numberAfter(const std::string & line, const char * key, long long & value)
    {
        size_t pos = line.find(key);
        if (pos == std::string::npos)
            return false;
        pos += strlen(key);
        if (pos >= line.size() || line[pos] < '0' || line[pos] > '9')
            return false;
        value = strtoll(line.c_str() + pos, nullptr, 10);
        return true;
    }
}

TEST(SharedSources_Trace, TraceFormatsSixtyFourBitCounters)
{
    // %I64d is MSVC-only; the shared sources now use PRId64.
    RecordingTrace recorder;
    TraceGuard guard(&recorder);

    void * ptr1 = rfhMalloc(16, "TESTTAG ");
    void * ptr2 = rfhMalloc(16, "TESTTAG ");
    rfhFree(ptr1);
    rfhFree(ptr2);

    std::vector<std::string> entries;
    for (const std::string & line : recorder.lines)
    {
        if (line.find("Entering rfhMalloc()") != std::string::npos)
            entries.push_back(line);
    }
    ASSERT_EQ(2u, entries.size());

    long long reqs1 = 0, reqs2 = 0, bytes1 = 0, bytes2 = 0;
    ASSERT_TRUE(numberAfter(entries[0], "mallocReqs=", reqs1)) << entries[0];
    ASSERT_TRUE(numberAfter(entries[1], "mallocReqs=", reqs2)) << entries[1];
    ASSERT_TRUE(numberAfter(entries[0], "totalBytes=", bytes1)) << entries[0];
    ASSERT_TRUE(numberAfter(entries[1], "totalBytes=", bytes2)) << entries[1];
    EXPECT_EQ(reqs1 + 1, reqs2);
    EXPECT_EQ(bytes1 + 16, bytes2);
    EXPECT_EQ(std::string::npos, entries[0].find("I64d"));
}

TEST(SharedSources_Chars, PlainCharIsSigned)
{
    // Historic Windows behaviour (MSVC plain char is signed): the XML parser rejects
    // bytes >= 0x80 that arrive as a negative char. The shared sources are built with
    // -fsigned-char so aarch64 and other unsigned-char targets behave the same way.
    const char xml[] = "<a>caf\xC3\xA9</a>";
    CXMLParse parser;
    EXPECT_EQ(PARSE_BAD_CHARS, parser.parse(xml, (int)strlen(xml)));
}

TEST(SharedSources_Ucs2, FindCrLfWReturnsIndexOfFirstLineBreak)
{
    const RFH_UCS2 text[] = { 'a', 'b', '\r', '\n', 0 };
    EXPECT_EQ(2, findcrlfW(text, 4));
    EXPECT_EQ(2, findcrlfW(text, 2));   // no break in range: returns maxchar
}

TEST(SharedSources_Ucs2, FindDelimWReturnsIndexAfterDelimiter)
{
    const RFH_UCS2 plain[] = { 'a', 'b', ';', 'c', 0 };
    EXPECT_EQ(3, findDelimW(plain, 4, ';', '\\'));

    // an escaped delimiter is skipped; the match is the next unescaped one
    const RFH_UCS2 escaped[] = { 'a', '\\', ';', 'b', ';', 0 };
    EXPECT_EQ(5, findDelimW(escaped, 5, ';', '\\'));
}

TEST(SharedSources_Ucs2, Char1208ToWcsDecodesTwoByteSequence)
{
    const unsigned char in[] = { 0xC3, 0xA9 };   // U+00E9
    RFH_UCS2 out = 0;
    EXPECT_EQ(2, char1208toWCS(&out, in));
    EXPECT_EQ((RFH_UCS2)0x00E9, out);
}

TEST(SharedSources_Ucs2, Char1208ToWcsOneByteMasksTopTwoBitsHistoric)
{
    // upstream bug, kept for compatibility; reported upstream: a one-byte sequence is
    // masked with 0x3f, so 'A' (0x41) decodes to 0x0001 instead of 0x0041.
    const unsigned char in[] = { 0x41 };
    RFH_UCS2 out = 0xFFFF;
    EXPECT_EQ(1, char1208toWCS(&out, in));
    EXPECT_EQ((RFH_UCS2)0x0001, out);
}

TEST(SharedSources_Ucs2, Char1208ToWcsThreeByteUsesLogicalOrHistoric)
{
    // upstream bug, kept for compatibility; reported upstream: the 3-byte case joins
    // the last byte with || instead of |, so the result is 0 or 1. U+20AC (E2 82 AC)
    // decodes to 0x0001, and E0 80 80 (U+0000) to 0x0000.
    const unsigned char euro[] = { 0xE2, 0x82, 0xAC };
    RFH_UCS2 out = 0xFFFF;
    EXPECT_EQ(3, char1208toWCS(&out, euro));
    EXPECT_EQ((RFH_UCS2)0x0001, out);

    const unsigned char nul[] = { 0xE0, 0x80, 0x80 };
    EXPECT_EQ(3, char1208toWCS(&out, nul));
    EXPECT_EQ((RFH_UCS2)0x0000, out);
}

TEST(SharedSources_Ucs2, Wcsto1252CopiesLowByteOfLatin1)
{
    const RFH_UCS2 in[] = { 0x00E9, 0 };
    unsigned char out[2] = { 0, 0 };
    EXPECT_EQ(1, WCSto1252(in, out));
    EXPECT_EQ(0xE9, out[0]);
}

TEST(SharedSources_Ucs2, Wcsto1252EuroSignHistoricLowByte)
{
    // upstream bug, kept for compatibility; reported upstream: the first test
    // (>= 0x00A0) catches U+20AC before the EURO SIGN branch, so the low byte 0xAC
    // is copied instead of the Windows-1252 byte 0x80.
    const RFH_UCS2 in[] = { 0x20AC, 0 };
    unsigned char out[2] = { 0, 0 };
    EXPECT_EQ(1, WCSto1252(in, out));
    EXPECT_EQ(0xAC, out[0]);
}

TEST(SharedSources_Json, GetLastErrorReturnsExactText)
{
    CJsonParse parser;
    const char json[] = "[1]";
    EXPECT_EQ(JPARSE_INVALID_START_CHAR, parser.parse(json, (int)strlen(json)));

    char text[100];
    parser.getLastError(text, sizeof(text));
    EXPECT_STREQ("Parse error - First character not a brace", text);
}

TEST(SharedSources_Json, GetLastErrorTruncatesToBufferSize)
{
    CJsonParse parser;
    const char json[] = "[1]";
    parser.parse(json, (int)strlen(json));

    char text[10];
    memset(text, 'x', sizeof(text));
    parser.getLastError(text, 6);
    EXPECT_STREQ("Parse", text);   // 5 characters plus the terminating NUL
    EXPECT_EQ('x', text[6]);       // nothing written past maxLen
}
