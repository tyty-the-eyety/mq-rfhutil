// PlatformTests.cpp — tests for the portable platform layer (core/platform).
// They run on every platform: Windows exercises the Win32 code paths and Linux
// the iconv/POSIX ones, against the same expectations.

#include <gtest/gtest.h>

#include <stdlib.h>
#include <string>
#include <vector>

#include "rfh_platform.h"
#include "rfh_codepage.h"
#include "rfh_trace.h"

// glibc always ships the IBM code pages; a Windows host may not have the EBCDIC
// ones installed, so conversion tests skip there instead of failing.
#define REQUIRE_CODEPAGE(ccsid) \
    do { if (!rfh_is_codepage_installed(ccsid)) GTEST_SKIP() << "code page " << (ccsid) << " is not installed"; } while (0)

// ---------------------------------------------------------------------------
// rfh_strupr / rfh_ucs2len
// ---------------------------------------------------------------------------

TEST(Platform_Strupr, UppercasesInPlace)
{
    char text[] = "Queue.Name_1";
    EXPECT_STREQ("QUEUE.NAME_1", rfh_strupr(text));
    EXPECT_STREQ("QUEUE.NAME_1", text);
}

TEST(Platform_Strupr, NullIsSafe)
{
    EXPECT_EQ(nullptr, rfh_strupr(nullptr));
}

TEST(Platform_Ucs2len, CountsSixteenBitUnits)
{
    const RFH_UCS2 text[] = { 'M', 'Q', 0x20AC, 0 };
    EXPECT_EQ(3u, rfh_ucs2len(text));
}

TEST(Platform_Ucs2len, NullIsZero)
{
    EXPECT_EQ(0u, rfh_ucs2len(nullptr));
}

// ---------------------------------------------------------------------------
// Time and paths
// ---------------------------------------------------------------------------

TEST(Platform_Time, CounterIsMonotonic)
{
    uint64_t start = rfh_hrtime_now();
    uint64_t end = rfh_hrtime_now();
    EXPECT_GE(end, start);
    EXPECT_GT(rfh_hrtime_frequency(), 0u);
}

TEST(Platform_Time, UtcFieldsAreInRange)
{
    RfhUtcTime t = rfh_utc_now();
    EXPECT_GE(t.year, 2024);
    EXPECT_GE(t.month, 1);
    EXPECT_LE(t.month, 12);
    EXPECT_GE(t.day, 1);
    EXPECT_LE(t.day, 31);
    EXPECT_LE(t.hour, 23);
    EXPECT_LE(t.minute, 59);
    EXPECT_LE(t.second, 60);
    EXPECT_LE(t.millisecond, 999);
}

TEST(Platform_DumpPath, UsesPlatformDirectory)
{
#ifdef _WIN32
    EXPECT_STREQ("c:\\rfhdump.txt", rfh_dump_path("rfhdump.txt"));
#else
    EXPECT_STREQ("/tmp/rfhdump.txt", rfh_dump_path("rfhdump.txt"));
#endif
}

// ---------------------------------------------------------------------------
// Code pages
// ---------------------------------------------------------------------------

TEST(Platform_Codepage, CommonCodePagesAreInstalled)
{
#ifdef _WIN32
    GTEST_SKIP() << "installed code pages depend on the Windows host";
#endif
    EXPECT_TRUE(rfh_is_codepage_installed(37));
    EXPECT_TRUE(rfh_is_codepage_installed(500));
    EXPECT_TRUE(rfh_is_codepage_installed(1252));
}

TEST(Platform_Codepage, WindowsAliasesAreInstalled)
{
#ifdef _WIN32
    GTEST_SKIP() << "installed code pages depend on the Windows host";
#endif
    EXPECT_TRUE(rfh_is_codepage_installed(20273));
    EXPECT_TRUE(rfh_is_codepage_installed(28591));
}

TEST(Platform_Codepage, ConvertsWindowsEbcdicAliasToAnsi)
{
    REQUIRE_CODEPAGE(20273);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0xC1 };   // 'A' in EBCDIC 273
    ASSERT_EQ(1, rfh_convert_char(20273, 1252, in, out, 2));
    EXPECT_EQ('A', out[0]);
}

TEST(Platform_Codepage, ConvertsWindowsLatin1Alias)
{
    REQUIRE_CODEPAGE(28591);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0xE9 };
    ASSERT_EQ(1, rfh_convert_char(28591, 1252, in, out, 2));
    EXPECT_EQ((char)0xE9, out[0]);
}

TEST(Platform_Codepage, OutputTooSmallConvertsNothing)
{
#ifdef _WIN32
    GTEST_SKIP() << "Windows installed code pages and buffer handling differ";
#endif
    REQUIRE_CODEPAGE(1252);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0xE9 };   // U+00E9 needs 2 bytes in UTF-8
    EXPECT_EQ(0, rfh_convert_char(1252, 1208, in, out, 1));
}

TEST(Platform_Codepage, UnknownCodePageIsNotInstalled)
{
    EXPECT_FALSE(rfh_is_codepage_installed(99999));
    EXPECT_FALSE(rfh_is_codepage_installed(0));
}

TEST(Platform_Codepage, ConvertsEbcdicLetterToAnsi)
{
    REQUIRE_CODEPAGE(500);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0xC1 };   // 'A' in EBCDIC 500
    ASSERT_EQ(1, rfh_convert_char(500, 1252, in, out, 2));
    EXPECT_EQ('A', out[0]);
}

TEST(Platform_Codepage, ConvertsEbcdicEuroIn1140)
{
    REQUIRE_CODEPAGE(1140);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0x9F };   // euro sign in EBCDIC 1140
    ASSERT_EQ(1, rfh_convert_char(1140, 1252, in, out, 2));
    EXPECT_EQ((char)0x80, out[0]);       // euro sign in Windows-1252
}

TEST(Platform_Codepage, UnrepresentableCharacterBecomesPeriod)
{
    REQUIRE_CODEPAGE(1140);
    REQUIRE_CODEPAGE(437);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0x9F };   // euro sign in EBCDIC 1140; code page 437 has none
    ASSERT_EQ(1, rfh_convert_char(1140, 437, in, out, 2));
    EXPECT_EQ('.', out[0]);
}

TEST(Platform_Codepage, MissingCodePageConvertsNothing)
{
    char out[2] = { 0, 0 };
    const char in[1] = { 'A' };
    EXPECT_EQ(0, rfh_convert_char(99999, 1252, in, out, 2));
    EXPECT_EQ(0, rfh_convert_char(0, 1252, in, out, 2));
}

TEST(Platform_Codepage, Ucs2ToCLocaleIsLatin1Identity)
{
    const RFH_UCS2 ucs[] = { 'M', 'Q', 0xE9, 0 };   // "MQé"
    char mbs[8] = { 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x' };
    EXPECT_EQ(3, rfh_ucs2_to_c_locale(ucs, mbs, sizeof(mbs)));
    EXPECT_EQ('M', mbs[0]);
    EXPECT_EQ((char)0xE9, mbs[2]);
    EXPECT_EQ(0, mbs[3]);
}

TEST(Platform_Codepage, Ucs2ToCLocaleStopsAtOutputLimit)
{
    const RFH_UCS2 ucs[] = { 'A', 'B', 0x20AC, 0 };
    char mbs[4] = { 'x', 'x', 'S', 'x' };
    EXPECT_EQ(2, rfh_ucs2_to_c_locale(ucs, mbs, 2));
    EXPECT_EQ('A', mbs[0]);
    EXPECT_EQ('B', mbs[1]);
    EXPECT_EQ('S', mbs[2]);   // no terminator written, nothing past the limit touched
}

TEST(Platform_Codepage, Ucs2ToCLocaleExactFitHasNoTerminator)
{
    const RFH_UCS2 ucs[] = { 'A', 'B', 0 };
    char mbs[3] = { 'x', 'x', 'S' };
    EXPECT_EQ(2, rfh_ucs2_to_c_locale(ucs, mbs, 2));
    EXPECT_EQ('S', mbs[2]);
}

TEST(Platform_Codepage, CLocaleToUcs2ExactFitHasNoTerminator)
{
    RFH_UCS2 ucs[3] = { 'x', 'x', 'S' };
    EXPECT_EQ(2, rfh_c_locale_to_ucs2("AB", ucs, 2));
    EXPECT_EQ((RFH_UCS2)'S', ucs[2]);
}

#ifndef _WIN32
TEST(Platform_Codepage, GetAcpHonoursEnvironment)
{
    const char * old = getenv("RFHUTIL_ACP");
    std::string saved = old ? old : "";
    bool had = (old != nullptr);

    setenv("RFHUTIL_ACP", "437", 1);
    EXPECT_EQ(437, rfh_get_acp());
    unsetenv("RFHUTIL_ACP");
    EXPECT_EQ(1252, rfh_get_acp());
    setenv("RFHUTIL_ACP", "abc", 1);
    EXPECT_EQ(1252, rfh_get_acp());

    if (had) setenv("RFHUTIL_ACP", saved.c_str(), 1); else unsetenv("RFHUTIL_ACP");
}
#endif

TEST(Platform_Codepage, Ucs2ToCLocaleRejectsAbove255)
{
    const RFH_UCS2 ucs[] = { 'M', 0x20AC, 0 };
    char mbs[8] = { 0 };
    EXPECT_EQ(-1, rfh_ucs2_to_c_locale(ucs, mbs, sizeof(mbs)));
}

TEST(Platform_Codepage, CLocaleToUcs2IsLatin1Identity)
{
    const char mbs[] = { 'M', 'Q', (char)0xE9, 0 };
    RFH_UCS2 ucs[8];
    EXPECT_EQ(3, rfh_c_locale_to_ucs2(mbs, ucs, 8));
    EXPECT_EQ((RFH_UCS2)'M', ucs[0]);
    EXPECT_EQ((RFH_UCS2)0xE9, ucs[2]);
    EXPECT_EQ((RFH_UCS2)0, ucs[3]);
}

// ---------------------------------------------------------------------------
// Trace sink
// ---------------------------------------------------------------------------

namespace
{
    class RecordingTrace : public RfhTrace
    {
    public:
        std::vector<std::string> lines;
        BOOL isTraceEnabled() override { return TRUE; }
        void logTraceEntry(const char * traceInfo) override { lines.push_back(traceInfo); }
        std::string dumpLabel;
        unsigned int dumpLength = 0;
        void dumpTraceData(const char * label, const unsigned char *, unsigned int length) override { dumpLabel = label; dumpLength = length; }
    };

    class VerboseTrace : public RfhTrace
    {
    public:
        VerboseTrace() { verboseTrace = TRUE; }
        BOOL isTraceEnabled() override { return TRUE; }
        void logTraceEntry(const char *) override {}
        void dumpTraceData(const char *, const unsigned char *, unsigned int) override {}
    };
}

TEST(Platform_Trace, InheritedVerboseFlagIsVisible)
{
    VerboseTrace sink;
    rfhSetTrace(&sink);
    BOOL verbose = rfhGetTrace()->verboseTrace;
    rfhSetTrace(nullptr);
    EXPECT_TRUE(verbose);
}

TEST(Platform_Trace, DumpIsPassedThrough)
{
    RecordingTrace recorder;
    const unsigned char data[3] = { 1, 2, 3 };
    rfhSetTrace(&recorder);
    rfhGetTrace()->dumpTraceData("lbl", data, 3);
    rfhSetTrace(nullptr);
    EXPECT_EQ("lbl", recorder.dumpLabel);
    EXPECT_EQ(3u, recorder.dumpLength);
}

TEST(Platform_Trace, DefaultSinkIsDisabledNoOp)
{
    RfhTrace * trace = rfhGetTrace();
    ASSERT_NE(nullptr, trace);
    EXPECT_FALSE(trace->isTraceEnabled());
    trace->logTraceEntry("ignored");
}

TEST(Platform_Trace, InstalledSinkReceivesEntries)
{
    RecordingTrace recorder;
    rfhSetTrace(&recorder);
    rfhGetTrace()->logTraceEntry("hello");
    rfhSetTrace(nullptr);

    ASSERT_EQ(1u, recorder.lines.size());
    EXPECT_EQ("hello", recorder.lines[0]);
}

TEST(Platform_Trace, NullRestoresNoOpSink)
{
    RecordingTrace recorder;
    rfhSetTrace(&recorder);
    rfhSetTrace(nullptr);
    EXPECT_FALSE(rfhGetTrace()->isTraceEnabled());
}
