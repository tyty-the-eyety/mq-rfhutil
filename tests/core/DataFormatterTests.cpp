// DataFormatterTests.cpp - DataFormatter (RFHUtil/DataFormatter.cpp) used directly.
// The views themselves are tested through format::render in FormatTests.cpp.

#include <gtest/gtest.h>

#include <dlfcn.h>
#include <signal.h>
#include <string.h>

#include <memory>
#include <string>

#include "DataFormatter.h"
#include "rfhconst.h"

namespace
{
    const DisplayCodepages kDisplay = { 1252, 437 };

    void prepare(DataFormatter & formatter, const char * text)
    {
        formatter.setData((const unsigned char *)text, (unsigned int)strlen(text));
        formatter.fileCcsid = 819;
        formatter.display = kDisplay;
    }

    // The formatter is allocated with new so that, under the guard allocator
    // (ctest -L memcheck), the memory beyond its work areas is a guard page.
    std::unique_ptr<DataFormatter> makeFormatter(const std::string & data)
    {
        std::unique_ptr<DataFormatter> formatter(new DataFormatter);
        formatter->setData((const unsigned char *)data.data(), (unsigned int)data.size());
        formatter->fileCcsid = 819;
        formatter->display = kDisplay;
        return formatter;
    }

    bool guarded()
    {
        return dlsym(RTLD_DEFAULT, "rfh_guardmalloc_active") != NULL;
    }
}

TEST(DataFormatter, HexViewOfTwoBytes)
{
    DataFormatter formatter;
    prepare(formatter, "AB");
    formatter.getHexData();
    ASSERT_NE(nullptr, formatter.m_data_hex);
    EXPECT_STREQ("00000000 4142", (const char *)formatter.m_data_hex);
}

TEST(DataFormatter, SecondCallReusesTheOutput)
{
    DataFormatter formatter;
    prepare(formatter, "AB");
    formatter.getHexData();
    unsigned char * first = formatter.m_data_hex;
    formatter.getHexData();
    EXPECT_EQ(first, formatter.m_data_hex);
}

TEST(DataFormatter, SetDataCopiesAndPadsTheData)
{
    char text[] = "AB";
    DataFormatter formatter;
    formatter.setData((const unsigned char *)text, 2);
    text[0] = 'X';
    EXPECT_NE((unsigned char *)text, formatter.fileData);
    EXPECT_EQ((const unsigned char *)text, formatter.source());
    EXPECT_EQ(2u, formatter.fileSize);
    EXPECT_EQ('A', formatter.fileData[0]);
    for (unsigned int i = 0; i < kLookAheadPad; i++)
    {
        EXPECT_EQ(0, formatter.fileData[2 + i]);
    }
}

TEST(DataFormatter, FixedViewHasADisplayTableWithoutACharacterViewFirst)
{
    DataFormatter formatter;
    prepare(formatter, "8=FIX\x01" "9=1\x01");
    formatter.getFixData(CHAR_ASCII);
    ASSERT_NE(nullptr, formatter.m_data_fix);
    EXPECT_STREQ("8=FIX\r\n9=1\r\n", (const char *)formatter.m_data_fix);
}

TEST(DataFormatter, ParsedViewBoundsALongAttributeList)
{
    auto formatter = makeFormatter("<a " + std::string(95000, 'X') + ">x</a>");
    formatter->getParsedData(CHAR_ASCII);
    EXPECT_NE(nullptr, formatter->m_data_parsed);
}

TEST(DataFormatter, ParsedViewBoundsALongTagName)
{
    auto formatter = makeFormatter("<" + std::string(200000, 'X') + ">");
    formatter->getParsedData(CHAR_ASCII);
    ASSERT_NE(nullptr, formatter->m_data_parsed);
}

TEST(DataFormatter, ParsedViewKeepsTheValueOfTheLongestTagNameItHolds)
{
    // 8192 characters fit the tag name work area; '/' and the name must fit the end tag's
    for (size_t length : { 8192, 8193, 9000, 200000 })
    {
        std::string name(length, 'n');
        auto formatter = makeFormatter("<" + name + ">x</" + name + ">");
        formatter->getParsedData(CHAR_ASCII);
        ASSERT_NE(nullptr, formatter->m_data_parsed) << length;
        const std::string parsed = (const char *)formatter->m_data_parsed;
        EXPECT_EQ(std::string::npos, parsed.find("end name does not match")) << length;
        if (length == 8192)
        {
            EXPECT_NE(std::string::npos, parsed.find("='x'")) << length;
        }
    }
}

TEST(DataFormatter, ParsedViewBoundsALongEndTagName)
{
    auto formatter = makeFormatter("<a>x</" + std::string(95000, 'X') + ">");
    formatter->getParsedData(CHAR_ASCII);
    EXPECT_NE(nullptr, formatter->m_data_parsed);
}

TEST(DataFormatter, ParsedViewBoundsALongValue)
{
    auto formatter = makeFormatter("<a>" + std::string(300000, 'v') + "</a>");
    formatter->getParsedData(CHAR_ASCII);
    EXPECT_NE(nullptr, formatter->m_data_parsed);
}

TEST(DataFormatter, ALongTableDirectoryIsNotUsed)
{
    // an EBCDIC character view looks for MQ's conversion table in this directory
    auto formatter = makeFormatter("\xC1\xC2\xC3");
    formatter->fileCcsid = 500;
    formatter->m_char_format = CHAR_EBCDIC;
    formatter->conversionTableDir = std::string(600, 'd');
    EXPECT_FALSE(formatter->tablesUsable());
    formatter->getCharacterData(CHAR_EBCDIC, IGNORE_CRLF, 0, 0, FALSE);
    EXPECT_NE(nullptr, formatter->m_data_ascii);
}

TEST(DataFormatter, ATableDirectoryThatFitsIsUsable)
{
    DataFormatter formatter;
    formatter.conversionTableDir = std::string(kTablePathMax - 33, 'd');
    EXPECT_TRUE(formatter.tablesUsable());
    formatter.conversionTableDir = std::string(kTablePathMax - 32, 'd');
    EXPECT_FALSE(formatter.tablesUsable());
    formatter.conversionTableDir.clear();
    EXPECT_FALSE(formatter.tablesUsable());
}

TEST(DataFormatter, SetDataAllocatesExactlyTheDataAndItsPad)
{
    if (!guarded())
    {
        GTEST_SKIP() << "runs only under the guard allocator (ctest -L memcheck)";
    }
    EXPECT_EXIT({
        // 48 + kLookAheadPad is a multiple of the allocator's 16-byte alignment, so
        // the block ends exactly where the guard page begins
        auto formatter = makeFormatter(std::string(48, 'A'));
        volatile unsigned char last = formatter->fileData[48 + kLookAheadPad - 1];
        (void)last;
        fputs("in bounds ok\n", stderr);
        fflush(stderr);
        volatile unsigned char past = formatter->fileData[48 + kLookAheadPad];
        (void)past;
        _exit(0);
    }, ::testing::KilledBySignal(SIGSEGV), "in bounds ok");
}

TEST(DataFormatter, UnicodeDataThatCannotBeConvertedStaysWithinTheData)
{
    // little-endian UCS-2 whose first unit is 0: the C-locale conversion gives up
    // and the code falls back to translating each unit to code page 1252
    const char text[] = { 0, 0, '<', 0, 'a', 0, '>', 0, 'x', 0, '<', 0, '/', 0, 'a', 0, '>', 0 };
    auto formatter = makeFormatter(std::string(text, sizeof(text)));
    formatter->fileCcsid = 1200;
    formatter->getXmlData(CHAR_ASCII);
    ASSERT_NE(nullptr, formatter->m_data_xml);
    // the first unit is 0, so the fallback gives an empty string: no stack bytes
    EXPECT_STREQ("", (const char *)formatter->m_data_xml);
}
