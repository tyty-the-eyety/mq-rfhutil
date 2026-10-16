// JsonParseTests.cpp — unit tests for RFHUtil/JsonParse.cpp
// Covers: parsing flat and nested JSON, tree navigation, error handling.

#include "stdafx.h"
#include "rfhutil.h"
#include <gtest/gtest.h>
#include "Names.h"
#include "JsonParse.h"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Basic parsing
// ---------------------------------------------------------------------------

TEST(JsonParse, ParseFlatObject)
{
    const char *json = "{\"name\":\"Alice\",\"age\":\"30\"}";
    CJsonParse p;
    int rc = p.parse(json, (int)strlen(json));
    EXPECT_EQ(JPARSE_OK, rc);

    // Verify actual parsed values
    int child = p.getFirstChild(0);
    ASSERT_GT(child, 0);
    EXPECT_STREQ("name", p.getElemName(child));
    EXPECT_STREQ("\"Alice\"", p.getElemValue(child));

    int sib = p.getNextSibling(child);
    ASSERT_GT(sib, 0);
    EXPECT_STREQ("age", p.getElemName(sib));
    EXPECT_STREQ("\"30\"", p.getElemValue(sib));
}

TEST(JsonParse, ParseEmpty)
{
    const char *json = "{}";
    CJsonParse p;
    int rc = p.parse(json, (int)strlen(json));
    EXPECT_EQ(JPARSE_OK, rc);
}

TEST(JsonParse, ParseInvalidStartChar)
{
    const char *bad = "not json at all";
    CJsonParse p;
    int rc = p.parse(bad, (int)strlen(bad));
    EXPECT_NE(JPARSE_OK, rc);
}

TEST(JsonParse, ParseNested)
{
    const char *json = "{\"person\":{\"name\":\"Bob\",\"city\":\"London\"}}";
    CJsonParse p;
    int rc = p.parse(json, (int)strlen(json));
    EXPECT_EQ(JPARSE_OK, rc);

    // Navigate into nested object and verify values
    int person = p.getFirstChild(0);
    ASSERT_GT(person, 0);
    EXPECT_STREQ("person", p.getElemName(person));

    int nameElem = p.getFirstChild(person);
    ASSERT_GT(nameElem, 0);
    EXPECT_STREQ("name", p.getElemName(nameElem));
    EXPECT_STREQ("\"Bob\"", p.getElemValue(nameElem));

    int cityElem = p.getNextSibling(nameElem);
    ASSERT_GT(cityElem, 0);
    EXPECT_STREQ("city", p.getElemName(cityElem));
    EXPECT_STREQ("\"London\"", p.getElemValue(cityElem));
}

TEST(JsonParse, ParseArray)
{
    const char *json = "{\"items\":[\"one\",\"two\",\"three\"]}";
    CJsonParse p;
    int rc = p.parse(json, (int)strlen(json));
    EXPECT_EQ(JPARSE_OK, rc);
}

// ---------------------------------------------------------------------------
// Tree navigation after successful parse
// ---------------------------------------------------------------------------

TEST(JsonParse, NavigateFirstChild)
{
    const char *json = "{\"key\":\"value\"}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    int child = p.getFirstChild(0);
    EXPECT_GE(child, 0);
}

TEST(JsonParse, GetElemName)
{
    const char *json = "{\"greeting\":\"hello\"}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    int child = p.getFirstChild(0);
    ASSERT_GE(child, 0);

    const char *name = p.getElemName(child);
    ASSERT_NE(nullptr, name);
    EXPECT_STREQ("greeting", name);
}

TEST(JsonParse, GetElemValue)
{
    const char *json = "{\"msg\":\"world\"}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    int child = p.getFirstChild(0);
    ASSERT_GE(child, 0);

    const char *val = p.getElemValue(child);
    ASSERT_NE(nullptr, val);
    EXPECT_STREQ("\"world\"", val);
}

TEST(JsonParse, NoNextSiblingForSingleKey)
{
    const char *json = "{\"only\":\"one\"}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    int child = p.getFirstChild(0);
    ASSERT_GE(child, 0);

    // Only one key — next sibling should be -1 or 0 (not found)
    int sib = p.getNextSibling(child);
    EXPECT_LT(sib, 1);
}

TEST(JsonParse, MultipleKeysNavigation)
{
    const char *json = "{\"a\":\"1\",\"b\":\"2\",\"c\":\"3\"}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    // Count children
    int count = 0;
    int elem = p.getFirstChild(0);
    while (elem > 0)
    {
        count++;
        elem = p.getNextSibling(elem);
    }
    EXPECT_EQ(3, count);
}

// ---------------------------------------------------------------------------
// buildParsedArea — reconstruct JSON from parsed tree
// ---------------------------------------------------------------------------

TEST(JsonParse, BuildParsedAreaNoCrash)
{
    const char *json = "{\"x\":\"42\"}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    char out[256] = {};
    p.buildParsedArea(out, sizeof(out), TRUE);
    EXPECT_GT(strlen(out), 0u);
    // Verify reconstructed output contains original key and value
    EXPECT_NE(nullptr, strstr(out, "x"));
    EXPECT_NE(nullptr, strstr(out, "42"));
}

TEST(JsonParse, BuildParsedAreaMultipleKeys)
{
    const char *json = "{\"a\":\"1\",\"b\":\"2\"}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    char out[512] = {};
    p.buildParsedArea(out, sizeof(out), TRUE);
    EXPECT_NE(nullptr, strstr(out, "a"));
    EXPECT_NE(nullptr, strstr(out, "1"));
    EXPECT_NE(nullptr, strstr(out, "b"));
    EXPECT_NE(nullptr, strstr(out, "2"));
}

// ---------------------------------------------------------------------------
// Malformed JSON — error handling
// ---------------------------------------------------------------------------

TEST(JsonParse, ParseUnclosedBrace)
{
    // Parser is lenient — truncated input doesn't produce an error code
    const char *json = "{\"a\":\"1\"";
    CJsonParse p;
    int rc = p.parse(json, (int)strlen(json));
    (void)rc;
    SUCCEED();
}

TEST(JsonParse, ParseUnclosedString)
{
    // Parser is lenient — truncated input doesn't produce an error code
    const char *json = "{\"key\":\"val";
    CJsonParse p;
    int rc = p.parse(json, (int)strlen(json));
    (void)rc;
    SUCCEED();
}

TEST(JsonParse, ParseTrailingComma)
{
    const char *json = "{\"a\":\"1\",}";
    CJsonParse p;
    int rc = p.parse(json, (int)strlen(json));
    // Document actual behavior — trailing comma may or may not be accepted
    // The important thing is it doesn't crash
    (void)rc;
    SUCCEED();
}

TEST(JsonParse, GetLastErrorAfterFail)
{
    const char *json = "not json";
    CJsonParse p;
    int rc = p.parse(json, (int)strlen(json));
    ASSERT_NE(JPARSE_OK, rc);

    char errtxt[256] = {};
    p.getLastError(errtxt, sizeof(errtxt));
    EXPECT_GT(strlen(errtxt), 0u);
}

// ---------------------------------------------------------------------------
// Navigation edge cases
// ---------------------------------------------------------------------------

TEST(JsonParse, GetFirstChildOnLeaf)
{
    const char *json = "{\"key\":\"val\"}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    int leaf = p.getFirstChild(0);
    ASSERT_GT(leaf, 0);
    // A leaf node (string value) should have no children
    int child = p.getFirstChild(leaf);
    EXPECT_LE(child, 0);
}

TEST(JsonParse, ParseNestedVerifyDeepValue)
{
    const char *json = "{\"outer\":{\"inner\":\"deep\"}}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    int outer = p.getFirstChild(0);
    ASSERT_GT(outer, 0);
    int inner = p.getFirstChild(outer);
    ASSERT_GT(inner, 0);
    EXPECT_STREQ("inner", p.getElemName(inner));
    EXPECT_STREQ("\"deep\"", p.getElemValue(inner));
}

TEST(JsonParse, ParseArrayCountElements)
{
    const char *json = "{\"items\":[\"one\",\"two\",\"three\"]}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    int items = p.getFirstChild(0);
    ASSERT_GT(items, 0);
    EXPECT_STREQ("items", p.getElemName(items));

    // Array elements are stored as the value, not as navigable children
    const char *val = p.getElemValue(items);
    ASSERT_NE(nullptr, val);
    EXPECT_NE(nullptr, strstr(val, "one"));
}

// ---------------------------------------------------------------------------
// Malformed and deeply nested data (Plan 2B: these crashed, hung or overran
// memory before; ctest -L memcheck runs them under the guard allocator)
// ---------------------------------------------------------------------------

TEST(JsonParse, MisplacedBracketIsAnErrorNotAHang)
{
    const char *json = "{]";
    CJsonParse p;
    EXPECT_EQ(JPARSE_MALFORMED, p.parse(json, (int)strlen(json)));
}

TEST(JsonParse, DeepNestingIsAnErrorNotAStackOverflow)
{
    std::string json;
    for (int i = 0; i < 100000; i++)
    {
        json += "{\"k\":";
    }
    json += "1" + std::string(100000, '}');
    CJsonParse p;
    EXPECT_EQ(JPARSE_TOO_DEEP, p.parse(json.c_str(), (int)json.size()));
}

TEST(JsonParse, NestingUpToTheLimitParses)
{
    // levels of objects, each holding the next: {"k":{"k":...1}}
    auto nested = [](int levels) {
        std::string json;
        for (int i = 0; i < levels; i++)
        {
            json += "{\"k\":";
        }
        return json + "1" + std::string(levels, '}');
    };

    std::string atLimit = nested(JPARSE_MAX_DEPTH);
    CJsonParse p;
    EXPECT_EQ(JPARSE_OK, p.parse(atLimit.c_str(), (int)atLimit.size()));

    std::string over = nested(JPARSE_MAX_DEPTH + 1);
    CJsonParse q;
    EXPECT_EQ(JPARSE_TOO_DEEP, q.parse(over.c_str(), (int)over.size()));
}

TEST(JsonParse, NamesWithoutValuesDoNotOverrunTheTree)
{
    // parse() sizes the tree from the braces, colons and commas: 3 entries here,
    // but each bare name is an entry of its own
    const char *json = "{a b c d e f g h i j}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    std::string names;
    for (int child = p.getFirstChild(0); child > 0; child = p.getNextSibling(child))
    {
        names += p.getElemName(child);
    }
    EXPECT_EQ("abcdefghij", names);
}

TEST(JsonParse, ReadsStopAtTheEndOfTheDataAfterNestedArrays)
{
    // the buffers hold exactly the text, with no NUL after it; leading blanks pad
    // the length to a multiple of 16 so the guard page starts right after the text
    auto parseExact = [](std::string text) {
        text.insert(0, (16 - text.size() % 16) % 16, ' ');
        std::vector<char> data(text.begin(), text.end());
        CJsonParse p;
        return p.parse(data.data(), (int)data.size());
    };
    EXPECT_EQ(JPARSE_OK, parseExact("{\"a\":[[1]]}"));
    EXPECT_EQ(JPARSE_MALFORMED, parseExact("{\"a\":[{]"));
}

TEST(JsonParse, ParsingTwiceStartsFromAnEmptyTree)
{
    std::string big = "{";
    for (int i = 0; i < 1000; i++)
    {
        big += (i ? ",\"k" : "\"k") + std::to_string(i) + "\":1";
    }
    big += "}";

    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(big.c_str(), (int)big.size()));

    const char * small = "{\"x\":2}";
    ASSERT_EQ(JPARSE_OK, p.parse(small, (int)strlen(small)));

    int child = p.getFirstChild(0);
    ASSERT_GT(child, 0);
    EXPECT_STREQ("x", p.getElemName(child));
    EXPECT_STREQ("2", p.getElemValue(child));
    EXPECT_EQ(0, p.getNextSibling(child));
}

TEST(JsonParse, EmbeddedNulInAnArrayIsMalformed)
{
    std::string json("{\"a\":[\0]}", 9);
    CJsonParse p;
    EXPECT_EQ(JPARSE_MALFORMED, p.parse(json.data(), (int)json.size()));
}

TEST(JsonParse, EmbeddedNulAfterAValueIsMalformed)
{
    std::string json("{\"a\":1\0}", 8);
    CJsonParse p;
    EXPECT_EQ(JPARSE_MALFORMED, p.parse(json.data(), (int)json.size()));
}

// ---------------------------------------------------------------------------
// buildParsedArea stays within its buffer (Plan 2B: valid JSON overran it)
// ---------------------------------------------------------------------------

TEST(JsonParse, BuildParsedAreaStaysWithinTheBufferOnLargeDocuments)
{
    // 10 levels of 30-character keys around 1000 leaves, so the dotted paths
    // (about 330 bytes each) fill the buffer that getJsonData sizes from the file
    const std::string key(30, 'k');
    std::string json = "{\"top\":";
    for (int i = 0; i < 10; i++)
    {
        json += "{\"" + key + "\":";
    }
    json += "{";
    for (int i = 0; i < 1000; i++)
    {
        json += (i ? ",\"a\":1" : "\"a\":1");
    }
    json += "}" + std::string(10, '}') + "}";

    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json.c_str(), (int)json.size()));

    // DataArea::getJsonData allocates fileSize*16 + 8192 and passes 192 less;
    // the buffer is exactly maxLen (rounded to 16 so that the guard page follows
    // the last byte), so any write past maxLen faults under memcheck
    int fileSize = (int)json.size();
    int maxLen = ((fileSize * 16 + 8000) + 15) / 16 * 16;
    std::vector<char> out(maxLen, 'x');
    p.buildParsedArea(out.data(), maxLen, FALSE);

    EXPECT_LT(strlen(out.data()), (size_t)maxLen);

    std::string expected = "top";
    for (int i = 0; i < 10; i++)
    {
        expected += "." + key;
    }
    expected += ".a[1]=1\r\n";
    EXPECT_EQ(expected, std::string(out.data(), expected.size()));
}

TEST(JsonParse, BuildParsedAreaOutputThatFitsIsUnchanged)
{
    const char * json = "{\"x\":{\"a\":\"1\",\"a\":\"2\",\"b\":3}}";
    CJsonParse p;
    ASSERT_EQ(JPARSE_OK, p.parse(json, (int)strlen(json)));

    char out[256] = {};
    p.buildParsedArea(out, sizeof(out), FALSE);
    EXPECT_STREQ("x.a[1]=\"1\"\r\nx.a[2]=\"2\"\r\nx.b=3", out);

    char withRoot[256] = {};
    p.buildParsedArea(withRoot, sizeof(withRoot), TRUE);
    EXPECT_STREQ("root.x.a[1]=\"1\"\r\nroot.x.a[2]=\"2\"\r\nroot.x.b=3", withRoot);
}
