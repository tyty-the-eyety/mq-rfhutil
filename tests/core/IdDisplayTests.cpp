// IdDisplayTests.cpp - rfh::id_display against MQMDPAGE's ID display and edit rules.
//
// Every expectation is built by hand from the Windows source, not produced by the code under test:
//   SetId (display)            RFHUtil/MQMDPAGE.cpp:1787-1881
//   setMsgId(int)              RFHUtil/MQMDPAGE.cpp:3931-4026   (called on focus loss, 3902-3921)
//   setCorrelId(int)           RFHUtil/MQMDPAGE.cpp:3381-3476   (called from OnUpdateCorrelId, 3548-3631)
//   setGroupId()               RFHUtil/MQMDPAGE.cpp:1891-1983   (called from OnUpdateGroupId, 704-775)
//   setMaxFieldLengths         RFHUtil/MQMDPAGE.cpp:670-702
//   OnResetIds                 RFHUtil/MQMDPAGE.cpp:506-551
//   HexToAscii, AsciiToHex     RFHUtil/comsubs.cpp:142-185, getHexCharValue 111-134
//   eatab / aetab              RFHUtil/comsubs.h:123-191

#include <gtest/gtest.h>

#include <string.h>

#include <string>

#include "rfh_id_display.h"

using rfh::id_display::applyEdit;
using rfh::id_display::Field;
using rfh::id_display::isHexDigit;
using rfh::id_display::isZero;
using rfh::id_display::maxLength;
using rfh::id_display::Mode;
using rfh::id_display::reset;
using rfh::id_display::toText;

namespace
{
    void fill(MQBYTE24 id, const unsigned char * bytes, size_t count, unsigned char rest = 0)
    {
        memset(id, rest, 24);
        memcpy(id, bytes, count);
    }

    std::string bytesOf(const MQBYTE24 id)
    {
        return std::string(reinterpret_cast<const char *>(id), 24);
    }

    std::string raw(const unsigned char * bytes, size_t count, unsigned char rest = 0)
    {
        std::string s(24, static_cast<char>(rest));
        memcpy(&s[0], bytes, count);
        return s;
    }
}

// ---- display ----

TEST(IdDisplay, HexIs48UpperCaseDigits)
{
    // SetId 1844-1851: AsciiToHex with HEX_NUMBERS "0123456789ABCDEF" (comsubs.h:35)
    const unsigned char bytes[] = { 0x00, 0x01, 0xAB, 0xCD, 0xEF, 0x7F, 0x80, 0xFF };
    MQBYTE24 id;
    fill(id, bytes, sizeof(bytes));
    EXPECT_EQ(std::string("0001ABCDEF7F80FF") + std::string(32, '0'), toText(id, Mode::Hex));
}

TEST(IdDisplay, AsciiShowsControlsAsBlanks)
{
    // SetId 1807-1824: bytes below ' ' become blanks; 0x7F and 0x80 and above are kept
    const unsigned char bytes[] = { 'A', 0x01, 0x00, 0x1F, 'B', 0x20, 0x7F, 0x80, 0xFF };
    MQBYTE24 id;
    fill(id, bytes, sizeof(bytes));
    std::string expected = std::string("A   B ") + "\x7F" + "\x80" + "\xFF" + std::string(15, ' ');
    EXPECT_EQ(expected, toText(id, Mode::Ascii));
    EXPECT_EQ(24u, toText(id, Mode::Ascii).size());
}

TEST(IdDisplay, EbcdicTranslates)
{
    // SetId 1826-1842 with eatab: C1 A, C2 B, F0 0, F1 1, 40 blank, 4B '.', 04 '.', 01 control -> blank,
    // 00 -> blank, 81 a, 7A ':', 5B '$'
    const unsigned char bytes[] = { 0xC1, 0xC2, 0xF0, 0xF1, 0x40, 0x4B, 0x04, 0x01, 0x00, 0x81, 0x7A, 0x5B };
    MQBYTE24 id;
    fill(id, bytes, sizeof(bytes), 0x40);
    EXPECT_EQ(std::string("AB01") + " " + ".." + "  " + "a:$" + std::string(12, ' '), toText(id, Mode::Ebcdic));
}

TEST(IdDisplay, EbcdicKeepsTranslatedBytesAbove0x7F)
{
    // eatab[0x41] is 0xA0, which the "< ' '" test of 1834-1840 leaves alone
    MQBYTE24 id;
    memset(id, 0x40, 24);
    id[0] = 0x41;
    EXPECT_EQ(std::string("\xA0") + std::string(23, ' '), toText(id, Mode::Ebcdic));
}

TEST(IdDisplay, MaxLengths)
{
    // setMaxFieldLengths 676-687
    EXPECT_EQ(48u, maxLength(Mode::Hex));
    EXPECT_EQ(24u, maxLength(Mode::Ascii));
    EXPECT_EQ(24u, maxLength(Mode::Ebcdic));
}

TEST(IdDisplay, ResetGivesZeroIdsAndTheirText)
{
    // OnResetIds 518-536
    MQBYTE24 id;
    memset(id, 0xAA, 24);
    reset(id);
    EXPECT_EQ(std::string(24, '\0'), bytesOf(id));
    EXPECT_EQ(std::string(48, '0'), toText(id, Mode::Hex));
    EXPECT_EQ(std::string(24, ' '), toText(id, Mode::Ascii));
}

TEST(IdDisplay, IsZeroNeedsEveryByteZero)
{
    // MQMDPAGE.cpp:2496 and 2502 compare all 24 bytes with MQMI_NONE / MQCI_NONE
    MQBYTE24 id;
    memset(id, 0, 24);
    EXPECT_TRUE(isZero(id));
    for (int i = 0; i < 24; i++)
    {
        memset(id, 0, 24);
        id[i] = 1;
        EXPECT_FALSE(isZero(id)) << "byte " << i;
    }
}

TEST(IdDisplay, HexDigitsAcceptBothCases)
{
    // CIdEdit::OnChar 272-277 upper-cases the key, then accepts 0-9 and A-F
    for (char c = '0'; c <= '9'; c++) EXPECT_TRUE(isHexDigit(c)) << c;
    for (char c = 'A'; c <= 'F'; c++) EXPECT_TRUE(isHexDigit(c)) << c;
    for (char c = 'a'; c <= 'f'; c++) EXPECT_TRUE(isHexDigit(c)) << c;
    EXPECT_FALSE(isHexDigit('G'));
    EXPECT_FALSE(isHexDigit('g'));
    EXPECT_FALSE(isHexDigit(' '));
    EXPECT_FALSE(isHexDigit('/'));
    EXPECT_FALSE(isHexDigit(':'));
    EXPECT_FALSE(isHexDigit('\0'));
}

TEST(IdDisplay, HexRoundTripsEveryByteValue)
{
    const char * digits = "0123456789ABCDEF";
    for (int v = 0; v < 256; v++)
    {
        MQBYTE24 id;
        memset(id, v, 24);
        std::string expected;
        for (int i = 0; i < 24; i++)
        {
            expected += digits[v >> 4];
            expected += digits[v & 15];
        }
        std::string text = toText(id, Mode::Hex);
        ASSERT_EQ(expected, text) << v;

        MQBYTE24 back;
        memset(back, 0x55, 24);
        std::string error;
        ASSERT_TRUE(applyEdit(Field::CorrelId, back, text, "", Mode::Hex, error)) << v;
        ASSERT_EQ(bytesOf(id), bytesOf(back)) << v;
    }
}

// ---- Hex edits ----

TEST(IdDisplay, CorrelHexEditPadsWithZeros)
{
    // OnUpdateCorrelId 3570-3579 appends '0' up to 48, then setCorrelId 3461-3462 converts 24 bytes
    MQBYTE24 id;
    memset(id, 0xFF, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::CorrelId, id, "ABC", "", Mode::Hex, error));
    const unsigned char expected[] = { 0xAB, 0xC0 };
    EXPECT_EQ(raw(expected, 2), bytesOf(id));
}

TEST(IdDisplay, CorrelHexEditOfEmptyTextGivesAllZeros)
{
    // empty text is padded to 48 zeros by the same loop
    MQBYTE24 id;
    memset(id, 0xFF, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::CorrelId, id, "", "", Mode::Hex, error));
    EXPECT_EQ(std::string(24, '\0'), bytesOf(id));
}

TEST(IdDisplay, GroupHexEditPadsWithZeros)
{
    // OnUpdateGroupId 721-733
    MQBYTE24 id;
    memset(id, 0xFF, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::GroupId, id, "12345", "", Mode::Hex, error));
    const unsigned char expected[] = { 0x12, 0x34, 0x50 };
    EXPECT_EQ(raw(expected, 3), bytesOf(id));
}

TEST(IdDisplay, MsgHexEditDoesNotPadAndKeepsTheRemainingBytes)
{
    // there is no EN_UPDATE handler for the message id (MQMDPAGE.cpp:200-217): setMsgId 4011-4012 converts
    // only length/2 bytes
    MQBYTE24 id;
    memset(id, 0xFF, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::MsgId, id, "ABCD", "", Mode::Hex, error));
    const unsigned char expected[] = { 0xAB, 0xCD };
    EXPECT_EQ(raw(expected, 2, 0xFF), bytesOf(id));
}

TEST(IdDisplay, MsgHexEditIgnoresTheLastOddDigit)
{
    // maxLen / 2 = 1 byte for "ABC" (4012)
    MQBYTE24 id;
    memset(id, 0xFF, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::MsgId, id, "ABC", "", Mode::Hex, error));
    const unsigned char expected[] = { 0xAB };
    EXPECT_EQ(raw(expected, 1, 0xFF), bytesOf(id));
}

TEST(IdDisplay, MsgHexEditOfEmptyTextChangesNothing)
{
    // maxLen 0, so HexToAscii converts 0 bytes
    MQBYTE24 id;
    memset(id, 0x5A, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::MsgId, id, "", "", Mode::Hex, error));
    EXPECT_EQ(std::string(24, '\x5A'), bytesOf(id));
}

TEST(IdDisplay, HexEditAcceptsLowerCase)
{
    // getHexCharValue (comsubs.cpp:126) upper-cases before testing A-F
    MQBYTE24 id;
    memset(id, 0, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::CorrelId, id, "abcdef", "", Mode::Hex, error));
    const unsigned char expected[] = { 0xAB, 0xCD, 0xEF };
    EXPECT_EQ(raw(expected, 3), bytesOf(id));
}

TEST(IdDisplay, HexEditTakesANonHexDigitAsAZeroNibble)
{
    // Windows does not reject it: getHexCharValue returns 0 for any character that is not 0-9 or A-F
    // (comsubs.cpp:114-132). Only the key filter of CIdEdit (IdEdit.cpp:269-290) stops typing it.
    MQBYTE24 id;
    memset(id, 0xFF, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::CorrelId, id, "GXAB1G", "", Mode::Hex, error));
    EXPECT_TRUE(error.empty());
    const unsigned char expected[] = { 0x00, 0xAB, 0x10 };
    EXPECT_EQ(raw(expected, 3), bytesOf(id));
}

TEST(IdDisplay, HexEditLongerThan48IsRejected)
{
    // The edit box is limited to 48 (MQMDPAGE.cpp:690-692) and paste is cut to 48 (IdEdit.cpp:192-196), so
    // Windows never converts more; its HexToAscii call would write past the 24-byte id. Rejected here.
    MQBYTE24 id;
    memset(id, 0x77, 24);
    std::string error;
    EXPECT_FALSE(applyEdit(Field::CorrelId, id, std::string(49, 'A'), "", Mode::Hex, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(std::string(24, '\x77'), bytesOf(id));
}

// ---- Ascii edits ----

TEST(IdDisplay, AsciiEditReplacesOnlyChangedCharacters)
{
    // setCorrelId 3405-3428: a character equal to the previous text keeps the binary byte, so 0x01 survives
    const unsigned char bytes[] = { 'A', 'B', 0x01, 'D' };
    MQBYTE24 id;
    fill(id, bytes, sizeof(bytes));
    std::string previous = toText(id, Mode::Ascii);
    ASSERT_EQ("AB D" + std::string(20, ' '), previous);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::CorrelId, id, "XB D" + std::string(20, ' '), previous, Mode::Ascii, error));
    const unsigned char expected[] = { 'X', 'B', 0x01, 'D' };
    EXPECT_EQ(raw(expected, 4), bytesOf(id));
}

TEST(IdDisplay, CorrelAsciiEditPadsWithBlanksSoUntouchedBytesSurvive)
{
    // OnUpdateCorrelId 3590-3594 pads to 24 with blanks before setCorrelId; "AX" becomes "AX" + 22 blanks,
    // which differs from the previous text only at index 1
    const unsigned char bytes[] = { 'A', 'B', 0x01 };
    MQBYTE24 id;
    fill(id, bytes, sizeof(bytes));
    std::string previous = toText(id, Mode::Ascii);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::CorrelId, id, "AX", previous, Mode::Ascii, error));
    const unsigned char expected[] = { 'A', 'X', 0x01 };
    EXPECT_EQ(raw(expected, 3), bytesOf(id));
}

TEST(IdDisplay, CorrelAsciiEditLongerThan24IsCutToTheFirst24AndApplied)
{
    // OnUpdateCorrelId 3597-3611 truncates with SetAt(24, 0) and carries on to setCorrelId (3615)
    MQBYTE24 id;
    memset(id, 0, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::CorrelId, id, std::string(30, 'Q'), std::string(24, ' '), Mode::Ascii, error));
    EXPECT_EQ(std::string(24, 'Q'), bytesOf(id));
}

TEST(IdDisplay, GroupAsciiEditLongerThan24IsRejected)
{
    // OnUpdateGroupId 741-756 cuts the text and skips setGroupId (760), so the binary id is not changed
    MQBYTE24 id;
    memset(id, 0x33, 24);
    std::string error;
    EXPECT_FALSE(applyEdit(Field::GroupId, id, std::string(25, 'Q'), std::string(24, ' '), Mode::Ascii, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(std::string(24, '\x33'), bytesOf(id));
}

TEST(IdDisplay, MsgAsciiEditUsesOnlyTheFirst24Characters)
{
    // setMsgId 3958: the loop stops at MQ_MSG_ID_LENGTH, and no handler checks the length
    MQBYTE24 id;
    memset(id, 0, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::MsgId, id, std::string(26, 'Z'), std::string(24, ' '), Mode::Ascii, error));
    EXPECT_EQ(std::string(24, 'Z'), bytesOf(id));
}

TEST(IdDisplay, GroupAsciiEditShorterThanBeforeZeroesTheRest)
{
    // setGroupId 1918-1936: at index 2 the typed text ends (NUL) and the previous text has a blank, so the
    // byte becomes the NUL; every index above the typed length is set to 0
    const unsigned char bytes[] = { 'A', 'B', 0x01, 0x02, 0x03 };
    MQBYTE24 id;
    fill(id, bytes, sizeof(bytes));
    std::string previous = toText(id, Mode::Ascii);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::GroupId, id, "AB", previous, Mode::Ascii, error));
    const unsigned char expected[] = { 'A', 'B' };
    EXPECT_EQ(raw(expected, 2), bytesOf(id));
}

TEST(IdDisplay, GroupAsciiEditPinsTheByteAtTheTypedLengthWhenBothTextsEndThere)
{
    // IBM quirk, pinned: the test is "i > maxLen" (1921), so at i == maxLen the NUL terminators of both
    // strings compare equal and the byte stays; only the bytes above it are zeroed. Assumes the previous
    // text ends with a NUL too (CString does).
    const unsigned char bytes[] = { 'A', 'B', 0x01, 0x02, 0x03 };
    MQBYTE24 id;
    fill(id, bytes, sizeof(bytes));
    std::string error;
    ASSERT_TRUE(applyEdit(Field::GroupId, id, "AB", "AB", Mode::Ascii, error));
    const unsigned char expected[] = { 'A', 'B', 0x01 };
    EXPECT_EQ(raw(expected, 3), bytesOf(id));
}

// ---- Ebcdic edits ----

TEST(IdDisplay, EbcdicEditConvertsFromAscii)
{
    // setCorrelId 3430-3457: a changed character is converted with aetab ('D' 0x44 -> 0xC4); the unchanged
    // 0x01 and the unchanged C1 stay
    const unsigned char bytes[] = { 0xC1, 0x01, 0xC3 };
    MQBYTE24 id;
    fill(id, bytes, sizeof(bytes));
    std::string previous = toText(id, Mode::Ebcdic);
    ASSERT_EQ("A C" + std::string(21, ' '), previous);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::CorrelId, id, "A D" + std::string(21, ' '), previous, Mode::Ebcdic, error));
    const unsigned char expected[] = { 0xC1, 0x01, 0xC4 };
    EXPECT_EQ(raw(expected, 3), bytesOf(id));
}

TEST(IdDisplay, EbcdicEditConvertsDigitsAndBlanks)
{
    // aetab: '0' -> F0, '1' -> F1, ' ' -> 40, '.' (0x2E) -> 4B
    MQBYTE24 id;
    memset(id, 0, 24);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::CorrelId, id, "01 ." + std::string(19, ' '), std::string(24, ' '), Mode::Ebcdic, error));
    // a blank typed over a previous blank is unchanged, so index 2 and the trailing bytes stay 0x00
    const unsigned char expectedBytes[] = { 0xF0, 0xF1, 0x00, 0x4B };
    EXPECT_EQ(raw(expectedBytes, 4), bytesOf(id));
}

TEST(IdDisplay, GroupEbcdicEditShorterThanBeforeZeroesTheRest)
{
    // setGroupId 1940-1967: index 1 differs (NUL against a blank), and aetab[0] is 0x00
    const unsigned char bytes[] = { 0xC1, 0xC2, 0xC3 };
    MQBYTE24 id;
    fill(id, bytes, sizeof(bytes), 0x40);
    std::string previous = toText(id, Mode::Ebcdic);
    std::string error;
    ASSERT_TRUE(applyEdit(Field::GroupId, id, "A", previous, Mode::Ebcdic, error));
    const unsigned char expected[] = { 0xC1 };
    EXPECT_EQ(raw(expected, 1), bytesOf(id));
}

TEST(IdDisplay, SuccessClearsTheError)
{
    MQBYTE24 id;
    memset(id, 0, 24);
    std::string error = "left over";
    ASSERT_TRUE(applyEdit(Field::CorrelId, id, "00", "", Mode::Hex, error));
    EXPECT_TRUE(error.empty());
}
