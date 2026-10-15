// SharedSourcesTests.cpp — the shared RFHUtil sources (comsubs, ...) on top of the
// platform layer: the Win32 calls they used to make now go through core/platform,
// and these tests pin that the results are the ones Windows produced.

#include <gtest/gtest.h>

#include <string.h>

#include <string>
#include <vector>

#include "rfh_platform.h"
#include "rfh_codepage.h"
#include "rfh_trace.h"
#include "comsubs.h"

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

TEST(SharedSources_Trace, TraceFormatsSixtyFourBitCounters)
{
    // %I64d is MSVC-only; the shared sources now use PRId64.
    RecordingTrace recorder;
    TraceGuard guard(&recorder);

    void * ptr = rfhMalloc(16, "TESTTAG ");
    rfhFree(ptr);

    ASSERT_FALSE(recorder.lines.empty());
    EXPECT_EQ(std::string::npos, recorder.lines.front().find("I64d"));
    EXPECT_NE(std::string::npos, recorder.lines.front().find("mallocReqs="));
}
