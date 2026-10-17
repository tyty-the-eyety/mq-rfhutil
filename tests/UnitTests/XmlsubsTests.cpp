// XmlsubsTests.cpp — unit tests for RFHUtil/xmlsubs.cpp
// Covers: escape sequence handling, XML validation, delimiter finding,
//         comment skipping.

#include "stdafx.h"
#include "rfhutil.h"
#include <gtest/gtest.h>
#include "comsubs.h"
#include "xmlsubs.h"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// removeEscSeq — unescape XML entities in-place
// ---------------------------------------------------------------------------

TEST(XmlsubsEscape, AmpersandEntity)
{
    char buf[] = "&amp;";
    removeEscSeq(buf);
    EXPECT_STREQ("&", buf);
}

TEST(XmlsubsEscape, LtEntity)
{
    char buf[] = "&lt;";
    removeEscSeq(buf);
    EXPECT_STREQ("<", buf);
}

TEST(XmlsubsEscape, GtEntity)
{
    char buf[] = "&gt;";
    removeEscSeq(buf);
    EXPECT_STREQ(">", buf);
}

TEST(XmlsubsEscape, AposEntity)
{
    char buf[] = "&apos;";
    removeEscSeq(buf);
    EXPECT_STREQ("'", buf);
}

TEST(XmlsubsEscape, QuotEntity)
{
    char buf[] = "&quot;";
    removeEscSeq(buf);
    EXPECT_STREQ("\"", buf);
}

TEST(XmlsubsEscape, NoEscape)
{
    char buf[] = "hello world";
    removeEscSeq(buf);
    EXPECT_STREQ("hello world", buf);
}

TEST(XmlsubsEscape, MultipleEntities)
{
    char buf[] = "&lt;root&gt;";
    removeEscSeq(buf);
    EXPECT_STREQ("<root>", buf);
}

// ---------------------------------------------------------------------------
// insertEscChars — escape special XML characters into output buffer
// ---------------------------------------------------------------------------

TEST(XmlsubsEscape, InsertEscAmpersand)
{
    char out[32] = {};
    int len = insertEscChars(out, "&", 1);
    EXPECT_GT(len, 0);
    EXPECT_STREQ("&amp;", out);
}

TEST(XmlsubsEscape, InsertEscLt)
{
    char out[32] = {};
    insertEscChars(out, "<", 1);
    EXPECT_STREQ("&lt;", out);
}

TEST(XmlsubsEscape, InsertEscRoundTrip)
{
    // Insert then remove should give back the original
    const char *original = "a < b && b > c";
    char escaped[256] = {};
    char restored[256] = {};

    insertEscChars(escaped, original, (int)strlen(original));
    strcpy(restored, escaped);
    removeEscSeq(restored);

    EXPECT_STREQ(original, restored);
}

TEST(XmlsubsEscape, InsertEscPlainText)
{
    char out[64] = {};
    int len = insertEscChars(out, "hello", 5);
    EXPECT_EQ(5, len);
    EXPECT_STREQ("hello", out);
}

// ---------------------------------------------------------------------------
// checkIfXml — bracket/tag validation
// ---------------------------------------------------------------------------

TEST(XmlsubsCheck, ValidXml)
{
    const char *xml = "<root><child>value</child></root>";
    char *errmsg = nullptr;
    int rc = checkIfXml((const unsigned char*)xml, (int)strlen(xml), &errmsg);
    EXPECT_EQ(0, rc);   // 0 = valid XML
}

TEST(XmlsubsCheck, EmptyString)
{
    char *errmsg = nullptr;
    int rc = checkIfXml((const unsigned char*)"", 0, &errmsg);
    EXPECT_NE(0, rc);   // non-zero = not valid XML
}

TEST(XmlsubsCheck, NotXml)
{
    const char *txt = "This is plain text without any angle brackets";
    char *errmsg = nullptr;
    int rc = checkIfXml((const unsigned char*)txt, (int)strlen(txt), &errmsg);
    EXPECT_NE(0, rc);   // non-zero = not valid XML
}

// ---------------------------------------------------------------------------
// processComment — skip over <!-- ... --> blocks
// ---------------------------------------------------------------------------

TEST(XmlsubsComment, WellFormedComment)
{
    const char *comment = "<!-- this is a comment -->";
    int len = processComment(comment, (int)strlen(comment));
    EXPECT_EQ((int)strlen(comment), len);
}

TEST(XmlsubsComment, MalformedComment)
{
    // No closing --> — should return the length searched without crashing
    const char *bad = "<!-- unclosed comment";
    int len = processComment(bad, (int)strlen(bad));
    EXPECT_GE(len, 0);
}

// ---------------------------------------------------------------------------
// findDelim — find delimiter respecting an escape character
// ---------------------------------------------------------------------------

TEST(XmlsubsDelim, FindComma)
{
    const unsigned char data[] = "hello,world";
    int pos = findDelim(data, (int)strlen((char*)data), ',', '\\');
    EXPECT_EQ(6, pos);   // 1-indexed position (index 5 + 1)
}

TEST(XmlsubsDelim, EscapedDelim)
{
    // Escaped comma should be skipped
    const unsigned char data[] = "hel\\,lo,world";
    int pos = findDelim(data, (int)strlen((char*)data), ',', '\\');
    EXPECT_EQ(8, pos);   // second comma at index 7, returns 7+1=8
}

TEST(XmlsubsDelim, NoDelim)
{
    const unsigned char data[] = "hello";
    int pos = findDelim(data, (int)strlen((char*)data), ',', '\\');
    EXPECT_EQ((int)strlen((char*)data), pos);   // not found → returns maxchar
}

// ---------------------------------------------------------------------------
// findXmlValue — extract value from XML element
// ---------------------------------------------------------------------------

TEST(XmlsubsFind, FindExistingKey)
{
    const char *xml = "<name>Alice</name>";
    char result[64] = {};
    findXmlValue(xml, "name", result, sizeof(result));
    EXPECT_STREQ("Alice", result);
}

TEST(XmlsubsFind, FindMissingKey)
{
    const char *xml = "<name>Alice</name>";
    char result[64] = { 'X' };
    // findXmlValue leaves buffer untouched when key is not found
    findXmlValue(xml, "missing", result, sizeof(result));
    EXPECT_EQ('X', result[0]);
}

// ---------------------------------------------------------------------------
// processDTD — skip over <!DOCTYPE ...> declarations
// ---------------------------------------------------------------------------

TEST(XmlsubsDTD, SimpleDTD)
{
    const char *dtd = "<!DOCTYPE root>";
    int len = processDTD(dtd, (int)strlen(dtd));
    EXPECT_EQ((int)strlen(dtd), len);
}

TEST(XmlsubsDTD, NotDTD)
{
    const char *xml = "<root/>";
    int len = processDTD(xml, (int)strlen(xml));
    EXPECT_EQ(0, len);
}

// ---------------------------------------------------------------------------
// removeCrLf — strip CR/LF characters from data
// ---------------------------------------------------------------------------

TEST(XmlsubsCrLf, RemoveCrLf)
{
    unsigned char data[] = "hello\r\nworld\n";
    int newLen = removeCrLf(data, (int)strlen((char*)data));
    EXPECT_EQ(10, newLen);
    EXPECT_EQ(0, memcmp(data, "helloworld", 10));
}

TEST(XmlsubsCrLf, NoCrLf)
{
    unsigned char data[] = "hello";
    int newLen = removeCrLf(data, (int)strlen((char*)data));
    EXPECT_EQ(5, newLen);
    EXPECT_EQ(0, memcmp(data, "hello", 5));
}

// ---------------------------------------------------------------------------
// setEscChar — decode a single XML entity starting at '&'
// ---------------------------------------------------------------------------

TEST(XmlsubsSetEsc, DecodeLt)
{
    unsigned char input[] = "&lt;rest";
    unsigned char output[1] = {};
    int len = setEscChar(input, output);
    EXPECT_EQ('<', output[0]);
    EXPECT_EQ(4, len);
}

TEST(XmlsubsSetEsc, DecodeAmp)
{
    unsigned char input[] = "&amp;rest";
    unsigned char output[1] = {};
    int len = setEscChar(input, output);
    EXPECT_EQ('&', output[0]);
    EXPECT_EQ(5, len);
}

TEST(XmlsubsSetEsc, NumericReferencesAreNotDecoded)
{
    // IBM's behaviour, pinned and reported upstream: the '#' branch tests
    // bufferin[0], which is the '&' at every caller, so a numeric character
    // reference is shown as its ampersand and the rest follows as text
    unsigned char decimal[] = "&#65;rest";
    unsigned char out1[1] = {};
    EXPECT_EQ(1, setEscChar(decimal, out1));
    EXPECT_EQ('&', out1[0]);

    unsigned char hex[] = "&#x41;rest";
    unsigned char out2[1] = {};
    EXPECT_EQ(1, setEscChar(hex, out2));
    EXPECT_EQ('&', out2[0]);
}

// ---------------------------------------------------------------------------
// findEndBracket — find closing '>' in tag
// ---------------------------------------------------------------------------

TEST(XmlsubsBracket, FindEndBracket)
{
    char data[] = "id=\"1\">content";
    char *result = findEndBracket(data);
    ASSERT_NE(nullptr, result);
    EXPECT_EQ('>', *result);
    EXPECT_EQ(6, (int)(result - data));
}

TEST(XmlsubsBracket, FindEndBracketAtStart)
{
    char data[] = ">rest";
    char *result = findEndBracket(data);
    EXPECT_EQ(data, result);
}

// ---------------------------------------------------------------------------
// processAttributes on malformed data (Plan 2B: these overran memory before;
// ctest -L memcheck runs them under the guard allocator)
// ---------------------------------------------------------------------------

TEST(XmlsubsAttributes, ValuesAreListedUnderTheElement)
{
    char element[] = "root";
    char varName[] = "";
    char attrs[] = "a=\"1\" b='two'";
    char out[256] = "";
    processAttributes(element, varName, attrs, out, sizeof(out));
    EXPECT_STREQ("root.(XML.attr)a='1'\r\nroot.(XML.attr)b='two'\r\n", out);
}

TEST(XmlsubsAttributes, UnterminatedQuoteEndsAtTheEndOfTheText)
{
    char element[] = "root";
    char varName[] = "";
    char attrs[] = "a=\"open";
    char out[256] = "";
    processAttributes(element, varName, attrs, out, sizeof(out));
    EXPECT_STREQ("root.(XML.attr)a='open'\r\n", out);
}

TEST(XmlsubsAttributes, ValueLongerThan8KIsKept)
{
    char element[] = "r";
    char varName[] = "";
    std::string value(12000, 'v');
    std::string attrs = "a='" + value + "'";
    std::vector<char> out(20000, 0);
    processAttributes(element, varName, &attrs[0], out.data(), out.size());
    EXPECT_EQ("r.(XML.attr)a='" + value + "'\r\n", std::string(out.data()));
}

TEST(XmlsubsAttributes, ListLongerThanTheOutputIsCutOff)
{
    char element[] = "r";
    char varName[] = "";
    std::string attrs;
    for (int i = 0; i < 4096; i++)
    {
        attrs += "a=1 ";
    }
    std::vector<char> out(1024, 0);
    processAttributes(element, varName, &attrs[0], out.data(), out.size());
    // filled to the last byte before the terminator, starting with the first attribute
    EXPECT_EQ((size_t)1023, strlen(out.data()));
    EXPECT_EQ(0, strncmp("r.(XML.attr)a='1'\r\nr.(XML.attr)a='1'\r\n", out.data(), 38));
}

// ---------------------------------------------------------------------------
// Reads stop at the end of the data (Plan 2B: xmlsubs.cpp compared up to 18
// bytes ahead without checking what remained). Each test passes every prefix of
// its texts, so the data ends at each point of each comparison. ctest -L memcheck
// runs them under the guard allocator, where a read past the end faults.
// ---------------------------------------------------------------------------

namespace
{
    // Holds text at the very end of a heap block whose size is a multiple of 16,
    // so under the guard allocator the guard page follows its last byte.
    class ExactTail
    {
    public:
        explicit ExactTail(const std::string & text)
            : m_block(text.empty() ? 16 : (text.size() + 15) / 16 * 16, 'x'),
              m_size(text.size())
        {
            if (m_size > 0)
            {
                memcpy(bytes(), text.data(), m_size);
            }
        }

        unsigned char * bytes() { return m_block.data() + m_block.size() - m_size; }
        char * chars() { return (char *)bytes(); }
        int size() const { return (int)m_size; }

    private:
        std::vector<unsigned char> m_block;
        size_t m_size;
    };

    // UTF-16LE of ASCII text, as the Unicode checks in xmlsubs.cpp expect
    std::string wide(const std::string & ascii)
    {
        std::string out;
        for (size_t i = 0; i < ascii.size(); i++)
        {
            out += ascii[i];
            out += '\0';
        }
        return out;
    }

    const std::vector<std::string> & markupTexts()
    {
        static const std::vector<std::string> texts = {
            "<!DOCTYPE a>",
            "<!DOCTYPE a [<!ENTITY e SYSTEM \"x\">]>",
            "<!DOCTYPE a [<!ENTITY % e PUBLIC 'p' 's'>]>",
            "<!DOCTYPE a [\"]\"]>",
            "<!-- c -->",
            "<a><![CDATA[x]]></a>",
            "<a><!-- c --><b/></a>",
            "<!DOCTYPE a [<!ENTITY e \"v\">]><a/>",
            wide("<!DOCTYPE a [x]>"),
            wide("<!-- c -->"),
            wide("<a><!-- c --></a>"),
            wide("<!DOCTYPE a><a/>"),
        };
        return texts;
    }
}

TEST(XmlsubsLookAhead, ProcessDTDStopsAtTheEndOfTheData)
{
    for (const std::string & text : markupTexts())
    {
        for (size_t n = 0; n <= text.size(); n++)
        {
            ExactTail t(text.substr(0, n));
            EXPECT_GE(processDTD(t.chars(), t.size()), 0) << n;
        }
    }
}

TEST(XmlsubsLookAhead, ProcessDTDOfCompleteDeclarations)
{
    const char * dtds[] = {
        "<!DOCTYPE a>",
        "<!DOCTYPE a [<!ENTITY e SYSTEM \"x\">]>",
        "<!DOCTYPE a [<!ENTITY % e PUBLIC 'p' 's'>]>",
        "<!DOCTYPE a [\"]\"]>",
    };
    for (const char * dtd : dtds)
    {
        ExactTail t(dtd);
        EXPECT_EQ((int)strlen(dtd), processDTD(t.chars(), t.size())) << dtd;
    }
    std::string w = wide("<!DOCTYPE a [x]>");
    ExactTail t(w);
    EXPECT_EQ((int)w.size(), processDTD(t.chars(), t.size()));
}

TEST(XmlsubsLookAhead, ProcessCommentStopsAtTheEndOfTheData)
{
    for (const std::string & text : markupTexts())
    {
        for (size_t n = 0; n <= text.size(); n++)
        {
            ExactTail t(text.substr(0, n));
            EXPECT_GE(processComment(t.chars(), t.size()), 0) << n;
        }
    }
    std::string w = wide("<!-- c -->");
    ExactTail full(w);
    EXPECT_EQ((int)w.size(), processComment(full.chars(), full.size()));
    ExactTail narrow("<!-- c -->");
    EXPECT_EQ(10, processComment(narrow.chars(), narrow.size()));
}

TEST(XmlsubsLookAhead, CheckIfXmlStopsAtTheEndOfTheData)
{
    for (const std::string & text : markupTexts())
    {
        for (size_t n = 0; n <= text.size(); n++)
        {
            ExactTail t(text.substr(0, n));
            char * errmsg = nullptr;
            int rc = checkIfXml(t.bytes(), t.size(), &errmsg);
            EXPECT_GE(rc, 0) << n;
            EXPECT_LE(rc, 3) << n;
        }
    }
    ExactTail cdata("<a><![CDATA[<<]]></a>");
    char * errmsg = nullptr;
    EXPECT_EQ(0, checkIfXml(cdata.bytes(), cdata.size(), &errmsg));
}

TEST(XmlsubsLookAhead, RemoveUnneededXmlStopsAtTheEndOfTheData)
{
    for (const std::string & text : markupTexts())
    {
        for (size_t n = 0; n <= text.size(); n++)
        {
            ExactTail t(text.substr(0, n));
            std::vector<unsigned char> out(n + 1, 0);
            int len = removeUnneededXML(t.bytes(), (unsigned int)t.size(), out.data());
            EXPECT_GE(len, 1) << n;
            EXPECT_LE(len, (int)n + 1) << n;
        }
    }
    ExactTail t("<!DOCTYPE a [<!ENTITY e \"v\">]><a><!-- c --><b/></a>");
    std::vector<unsigned char> out(t.size() + 1, 0);
    removeUnneededXML(t.bytes(), (unsigned int)t.size(), out.data());
    EXPECT_STREQ("<a><b/></a>", (const char *)out.data());
}

TEST(XmlsubsLookAhead, SetEscCharStopsAtTheEndOfTheText)
{
    // setEscChar has no length: its text ends at a terminator
    const char * entities[] = { "&lt;", "&gt;", "&amp;", "&apos;", "&quot;", "&Quot;x" };
    const char expected[] = { '<', '>', '&', '\'', '"', '"' };
    const int lengths[] = { 4, 4, 5, 6, 6, 6 };
    for (size_t e = 0; e < sizeof(entities) / sizeof(entities[0]); e++)
    {
        std::string entity = entities[e];
        for (size_t n = 0; n <= entity.size(); n++)
        {
            ExactTail t(entity.substr(0, n) + std::string(1, '\0'));
            unsigned char out[1] = { 0 };
            int len = setEscChar(t.bytes(), out);
            if (n == entity.size())
            {
                EXPECT_EQ(expected[e], (char)out[0]) << entity;
                EXPECT_EQ(lengths[e], len) << entity;
            }
            else if (n < (size_t)lengths[e])
            {
                // a cut-off entity is shown as its ampersand
                EXPECT_EQ('&', (char)out[0]) << entity << " " << n;
                EXPECT_EQ(1, len) << entity << " " << n;
            }
        }
    }
}

TEST(XmlsubsLookAhead, ProcessTagStopsAtTheEndOfTheText)
{
    std::string tag = "<Command>Publish</Command>";
    for (size_t n = 0; n <= tag.size(); n++)
    {
        ExactTail t(tag.substr(0, n) + std::string(1, '\0'));
        char value[32] = { 0 };
        int found = 0;
        char * end = processTag(t.chars(), "<Command>", value, sizeof(value), &found);
        EXPECT_GE(end, t.chars()) << n;
        EXPECT_LT(end, t.chars() + t.size()) << n;
    }
    ExactTail t(tag + std::string(1, '\0'));
    char value[32] = { 0 };
    int found = 0;
    char * end = processTag(t.chars(), "<Command>", value, sizeof(value), &found);
    EXPECT_EQ(1, found);
    EXPECT_STREQ("Publish", value);
    EXPECT_EQ(0, end[0]);
}

TEST(XmlsubsLookAhead, ProcessTagKeepsItsTerminatorInsideTheValueBuffer)
{
    // the value buffer is exactly maxLen bytes: a value that does not fit is cut
    // to maxLen - 1 characters and terminated, rather than terminated past the end
    char text[] = "<Command>RegisterSubscriber</Command>";
    ExactTail value(std::string(16, '\0'));
    int found = 0;
    processTag(text, "<Command>", value.chars(), value.size(), &found);
    EXPECT_EQ(1, found);
    EXPECT_STREQ("RegisterSubscri", value.chars());

    // a value that fits is kept whole
    char fits[] = "<Command>Register</Command>";
    ExactTail value2(std::string(16, '\0'));
    found = 0;
    processTag(fits, "<Command>", value2.chars(), value2.size(), &found);
    EXPECT_STREQ("Register", value2.chars());
}

TEST(XmlsubsLookAhead, FindXmlValueStopsAtTheEndOfTheText)
{
    const char * texts[] = { "<name", "<name x", "<name>", "<name>Al" };
    const char * values[] = { "", "", "", "Al" };
    for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++)
    {
        ExactTail t(std::string(texts[i]) + std::string(1, '\0'));
        char result[16] = { 'X', 0 };
        findXmlValue(t.chars(), "name", result, sizeof(result));
        EXPECT_STREQ(values[i], result) << texts[i];
    }
}

TEST(XmlsubsLookAhead, RemoveVarNameOfALongerEndTagDoesNotReadBeforeTheName)
{
    // the end tag is longer than the whole name, so there is nothing to compare;
    // IBM's code compared from before the start of xmlVarName (a heap read before
    // the block, which AddressSanitizer reports and the guard allocator cannot see)
    std::vector<char> name(2, '\0');
    name[0] = 'a';
    char endTag[] = "/abcdef";
    EXPECT_EQ(1, removeVarName(name.data(), endTag));
    EXPECT_STREQ("a", name.data());

    char path[] = "root.abc";
    char tag[] = "/abc";
    EXPECT_EQ(0, removeVarName(path, tag));
    EXPECT_STREQ("root", path);
}
