// FormatFuzzTests.cpp - every Data tab view over pseudo-random messages.
//
// The messages mix XML, JSON, EDI and FIX fragments with random bytes, so they
// reach the formatters' and IBM's parsers' error paths. Run normally, the test
// shows that no view crashes or hangs; under the guard allocator (ctest -L
// memcheck) it shows that none reads or writes outside its buffers. The seed is
// fixed, so a failure always reproduces. FORMAT_FUZZ_SEED picks another seed and
// FORMAT_FUZZ_ITERATIONS the number of messages (the memcheck run uses more).

#include <gtest/gtest.h>

#include <stdlib.h>
#include <string.h>

#include <string>

#include "copybook_file.h"
#include "rfh_format.h"

using namespace rfh;
using namespace rfh::format;

namespace
{
    const char * const kFragments[] = {
        "<", ">", "</", "/>", "<a>", "</a>", "<b x=\"1\">", "=", "\"", "'", "<!--", "-->",
        "<!DOCTYPE x>", "<![CDATA[", "]]>", "&amp;", "&lt;", "<?xml version=\"1.0\"?>",
        "{", "}", "[", "]", ":", ",", "true", "null", "\"k\"", "1.5e3",
        "UNA:+.? '", "UNB+", "ISA*", "\x01", "8=FIX", "\r\n", "\n", " ", "\t",
        "\xff", "\xfe", "\xef\xbb\xbf", "x" };

    const View kViews[] = { View::Character, View::Hex, View::Both, View::Xml,
        View::Parsed, View::Cobol, View::Json, View::Fixed };

    const int kCcsids[] = { 819, 1208, 500, 37, 1200, 932, 1252 };

    class Random
    {
    public:
        explicit Random(unsigned int seed) : state(seed) {}
        unsigned int next()
        {
            state = state * 1103515245u + 12345u;
            return (state >> 16) & 0x7fff;
        }
    private:
        unsigned int state;
    };

    const char * const kCopybooks[] = {
        // the fixed one
        "       01  REC.\n"
        "           05  A   PIC X(3).\n"
        "           05  B   PIC S9(5) COMP-3.\n"
        "           05  C   PIC S9(4) COMP OCCURS 4.\n"
        "           05  D   PIC 9(4)V99.\n",
        // OCCURS DEPENDING ON with no maximum
        "       01  REC.\n"
        "           05  N   PIC 9(4).\n"
        "           05  T   PIC X(2) OCCURS DEPENDING ON N.\n",
        // REDEFINES
        "       01  REC.\n"
        "           05  A   PIC X(8).\n"
        "           05  B   REDEFINES A.\n"
        "               10  B1  PIC 9(4) COMP.\n"
        "               10  B2  PIC S9(3) COMP-3.\n"
        "           05  C   PIC 9(3)V9.\n",
        // not a copy book at all
        "<?xml version=\"1.0\"?>\n<root><a x=\"1\">text</a></root>\n" };
}

TEST(FormatFuzz, NoViewFailsOnArbitraryData)
{
    const char * seedText = getenv("FORMAT_FUZZ_SEED");
    Random random(seedText ? (unsigned int)atoi(seedText) : 12345u);
    CopybookFile copybookFile;
    const char * iterationsText = getenv("FORMAT_FUZZ_ITERATIONS");
    const int iterations = iterationsText ? atoi(iterationsText) : 500;

    for (int iteration = 0; iteration < iterations; iteration++)
    {
        std::string body;
        size_t target = random.next() % 3000;
        while (body.size() < target)
        {
            if (random.next() % 4 == 0)
            {
                body += (char)(random.next() & 0xff);
            }
            else
            {
                body += kFragments[random.next() % (sizeof(kFragments) / sizeof(kFragments[0]))];
            }
        }

        Message m;
        m.mqmd.CodedCharSetId = kCcsids[random.next() % (sizeof(kCcsids) / sizeof(kCcsids[0]))];
        m.mqmd.Encoding = (random.next() & 1) ? 546 : 785;
        m.body.assign(body.begin(), body.end());

        std::vector<std::string> warnings;
        RenderOptions o;
        o.apply(defaultsFor(m, warnings));
        o.charFormat = (int)(random.next() % 9);
        o.crlf = random.next() & 1;
        o.edi = random.next() & 1;
        o.bom = random.next() & 1;
        o.indent = random.next() & 1;
        o.checkData = random.next() & 1;
        o.numericFormat = random.next() & 1;
        o.pdNumericFormat = random.next() & 1;
        const char * copybook = kCopybooks[random.next() % (sizeof(kCopybooks) / sizeof(kCopybooks[0]))];
        ASSERT_NO_FATAL_FAILURE(copybookFile.write(copybook));
        o.copybookPath = copybookFile.path();
        if (random.next() & 1)
        {
            o.conversionTableDir = "/opt/mqm/lib/iconv";     // used when present
        }

        for (View view : kViews)
        {
            std::string out;
            Result r = render(m, view, o, out);
            if (view == View::Cobol)
            {
                // a copy book error or the size limit may fail the view, with a reason
                if (!r.ok())
                {
                    ASSERT_FALSE(r.text.empty()) << "iteration " << iteration << ": COBOL failure without text";
                }
            }
            else
            {
                ASSERT_TRUE(r.ok()) << "iteration " << iteration << ", view " << (int)view << ": " << r.text;
            }
        }
    }
}
