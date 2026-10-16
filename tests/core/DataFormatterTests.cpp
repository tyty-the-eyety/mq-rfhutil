// DataFormatterTests.cpp - DataFormatter (RFHUtil/DataFormatter.cpp) used directly.
// The views themselves are tested through format::render in FormatTests.cpp.

#include <gtest/gtest.h>

#include <string.h>

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
