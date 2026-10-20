// ParsedHeadersTests.cpp — unit tests for rfh::parseDlq/buildDlq,
// parseCics/buildCics, parseIms/buildIms, parseRfh1/buildRfh1,
// parseRfh2/buildRfh2, and the parseHeaders top-level walker.
//
// Tests are self-contained: they build known binary structs at IBM's documented
// offsets using mq_layout.h, then verify the parsed fields and the round-trip
// (parse → build → parse again gives identical fields).

#include <gtest/gtest.h>

#include <cmqc.h>
#include <cmqpsc.h>
#include <string.h>

#include "rfhconst.h"
#include "rfh_parsed_headers.h"
#include "mq_layout.h"

using namespace rfh;
using mq_layout::Order;
namespace L = mq_layout;

namespace
{
    // -----------------------------------------------------------------------
    // Build a minimal MQDLH at the documented offsets (see cmqc.h tagMQDLH).
    // -----------------------------------------------------------------------
    std::vector<unsigned char> makeDlq(Order order = Order::Little,
                                        int reason = 0,
                                        const std::string & destQ = "TEST.QUEUE",
                                        const std::string & destQM = "QM1")
    {
        L::Bytes b(172, ' ');
        b.putChars(0, 4, "DLH ");
        b.putLong(4, 1, order);                          // Version
        b.putLong(8, reason, order);                     // Reason
        b.putChars(12, 48, destQ);                       // DestQName
        b.putChars(60, 48, destQM);                      // DestQMgrName
        // Encoding: MQENC_INTEGER_REVERSED (=546) | MQENC_DECIMAL_REVERSED (=546>>4) |
        //           MQENC_FLOAT_IEEE_REVERSED — usual x86 native encoding
        int enc = (order == Order::Little) ? 546 : 785;  // MQENC_NATIVE LE vs BE
        b.putLong(108, enc, order);                      // Encoding
        b.putLong(112, 1208, order);                     // CodedCharSetId
        b.putChars(116, 8, "MQSTR");                     // Format
        b.putLong(124, 6, order);                        // PutApplType (MQAT_UNIX)
        b.putChars(128, 28, "my_app");                   // PutApplName
        b.putChars(156, 8, "20260101");                  // PutDate
        b.putChars(164, 8, "12000000");                  // PutTime
        return b.data;
    }

    // -----------------------------------------------------------------------
    // Build a minimal MQRFH2 with a single mcd folder.
    // Variable part: [4-byte MQLONG len][XML bytes]
    // -----------------------------------------------------------------------
    std::vector<unsigned char> makeRfh2WithMcd(Order order = Order::Little,
                                                const std::string & msgDomain = "jms_text",
                                                int nvCcsid = 1208)
    {
        std::string folder = "<mcd><Msd>" + msgDomain + "</Msd></mcd>";
        // folder bytes as ASCII (nvCcsid=1208)
        std::vector<unsigned char> bytes(folder.begin(), folder.end());

        // MQRFH2 fixed = 36 bytes; variable = 4 + folder.size()
        int strucLen = 36 + 4 + (int)bytes.size();
        L::Bytes b(strucLen, 0);
        b.putChars(0, 4, "RFH ");                           // StrucId
        b.putLong(4, 2, order);                             // Version (RFH2)
        b.putLong(8, strucLen, order);                      // StrucLength
        int enc = (order == Order::Little) ? 546 : 785;
        b.putLong(12, enc, order);                          // Encoding
        b.putLong(16, 1208, order);                         // CodedCharSetId
        b.putChars(20, 8, "MQHRF2  ");                      // Format (pad to 8)
        b.putLong(28, 0, order);                            // Flags
        b.putLong(32, nvCcsid, order);                      // NameValueCCSID

        // Sub-folder: length + XML
        b.putLong(36, (int)bytes.size(), order);
        b.putBytes(40, bytes.data(), bytes.size());

        return b.data;
    }

    // Helper: parse headers from a flat byte buffer
    ParsedHeaders parse(const std::vector<unsigned char> & data,
                        int ccsid = 1208, int encoding = NUMERIC_PC)
    {
        ParsedHeaders out;
        parseHeaders(data.data(), data.size(), ccsid, encoding, out);
        return out;
    }
}

// ===========================================================================
// DLQ parse
// ===========================================================================

TEST(ParsedHeaders_Dlq, ParsesBasicFields)
{
    auto data = makeDlq(Order::Little, 2082 /* MQRC_NOT_AUTHORIZED */, "DEAD.Q", "QM2");
    std::vector<std::string> w;
    DlqFields f;
    size_t n = parseDlq(data.data(), data.size(), 1208, NUMERIC_PC, f, w);
    EXPECT_EQ(172u, n);
    ASSERT_TRUE(f.included);
    EXPECT_EQ(2082, f.reason);
    EXPECT_EQ("DEAD.Q", f.destQueueName);
    EXPECT_EQ("QM2",    f.destQueueManagerName);
    EXPECT_EQ(1208,     f.ccsid);
    EXPECT_EQ(NUMERIC_PC, f.encoding);
    EXPECT_EQ("MQSTR",  f.format);
    EXPECT_EQ(6,        f.putApplType);
    EXPECT_EQ("my_app", f.putApplName);
    EXPECT_EQ("20260101", f.putDate);
    EXPECT_EQ("12000000", f.putTime);
    EXPECT_TRUE(w.empty());
}

TEST(ParsedHeaders_Dlq, RejectsWrongStrucId)
{
    auto data = makeDlq();
    data[0] = 'X';
    std::vector<std::string> w;
    DlqFields f;
    size_t n = parseDlq(data.data(), data.size(), 1208, NUMERIC_PC, f, w);
    EXPECT_EQ(0u, n);
    EXPECT_FALSE(f.included);
}

TEST(ParsedHeaders_Dlq, RejectsTooShort)
{
    auto data = makeDlq();
    data.resize(10);
    std::vector<std::string> w;
    DlqFields f;
    size_t n = parseDlq(data.data(), data.size(), 1208, NUMERIC_PC, f, w);
    EXPECT_EQ(0u, n);
    EXPECT_FALSE(f.included);
}

// ===========================================================================
// DLQ build round-trip
// ===========================================================================

TEST(ParsedHeaders_Dlq, RoundTrip)
{
    auto data = makeDlq(Order::Little, 2082, "DEAD.Q", "QM2");
    std::vector<std::string> w;
    DlqFields f;
    parseDlq(data.data(), data.size(), 1208, NUMERIC_PC, f, w);
    ASSERT_TRUE(f.included);

    std::vector<unsigned char> rebuilt;
    buildDlq(f, rebuilt);
    EXPECT_EQ(data, rebuilt);
}

TEST(ParsedHeaders_Dlq, BuildNoopWhenNotIncluded)
{
    DlqFields f;
    std::vector<unsigned char> out;
    buildDlq(f, out);
    EXPECT_TRUE(out.empty());
}

// ===========================================================================
// RFH2 parse: mcd folder
// ===========================================================================

TEST(ParsedHeaders_Rfh2, ParsesMcdFolder)
{
    auto data = makeRfh2WithMcd(Order::Little, "jms_text");
    std::vector<std::string> w;
    Rfh2Fields f;
    size_t n = parseRfh2(data.data(), data.size(), 1208, NUMERIC_PC, f, w);

    ASSERT_GT(n, 0u);
    ASSERT_TRUE(f.included);
    EXPECT_TRUE(f.hasMcd);
    EXPECT_EQ("jms_text", f.mcd.msgDomain);
    EXPECT_EQ("",         f.mcd.msgSet);
    EXPECT_EQ("",         f.mcd.msgType);
    EXPECT_EQ("",         f.mcd.msgFmt);
    EXPECT_FALSE(f.hasJms);
    EXPECT_FALSE(f.hasUsr);
    EXPECT_TRUE(w.empty());
}

TEST(ParsedHeaders_Rfh2, ParsesMcdAndJmsFolders)
{
    std::string mcdFolder = "<mcd><Msd>jms_bytes</Msd></mcd>";
    std::string jmsFolder = "<jms><Dst>queue:///MYQUEUE</Dst><Dlv>2</Dlv></jms>";

    std::vector<unsigned char> mcdBytes(mcdFolder.begin(), mcdFolder.end());
    std::vector<unsigned char> jmsBytes(jmsFolder.begin(), jmsFolder.end());

    int strucLen = 36 + 4 + (int)mcdBytes.size() + 4 + (int)jmsBytes.size();
    L::Bytes b(strucLen, 0);
    b.putChars(0, 4, "RFH ");
    b.putLong(4, 2, Order::Little);
    b.putLong(8, strucLen, Order::Little);
    b.putLong(12, 546, Order::Little);   // MQENC_NATIVE LE
    b.putLong(16, 1208, Order::Little);
    b.putChars(20, 8, "MQHRF2  ");
    b.putLong(28, 0, Order::Little);
    b.putLong(32, 1208, Order::Little);  // NameValueCCSID

    size_t pos = 36;
    b.putLong(pos, (int)mcdBytes.size(), Order::Little); pos += 4;
    b.putBytes(pos, mcdBytes.data(), mcdBytes.size());   pos += mcdBytes.size();
    b.putLong(pos, (int)jmsBytes.size(), Order::Little); pos += 4;
    b.putBytes(pos, jmsBytes.data(), jmsBytes.size());

    std::vector<std::string> w;
    Rfh2Fields f;
    parseRfh2(b.data.data(), b.data.size(), 1208, NUMERIC_PC, f, w);

    ASSERT_TRUE(f.included);
    ASSERT_TRUE(f.hasMcd);
    ASSERT_TRUE(f.hasJms);
    EXPECT_EQ("jms_bytes",      f.mcd.msgDomain);
    EXPECT_EQ("queue:///MYQUEUE", f.jms.dst);
    EXPECT_EQ("2",               f.jms.deliveryMode);
}

TEST(ParsedHeaders_Rfh2, ParsesUsrFolder)
{
    std::string usrFolder = "<usr><myField>hello</myField></usr>";
    std::vector<unsigned char> usrBytes(usrFolder.begin(), usrFolder.end());

    int strucLen = 36 + 4 + (int)usrBytes.size();
    L::Bytes b(strucLen, 0);
    b.putChars(0, 4, "RFH ");
    b.putLong(4, 2, Order::Little);
    b.putLong(8, strucLen, Order::Little);
    b.putLong(12, 546, Order::Little);
    b.putLong(16, 1208, Order::Little);
    b.putChars(20, 8, "        ");
    b.putLong(28, 0, Order::Little);
    b.putLong(32, 1208, Order::Little);
    b.putLong(36, (int)usrBytes.size(), Order::Little);
    b.putBytes(40, usrBytes.data(), usrBytes.size());

    std::vector<std::string> w;
    Rfh2Fields f;
    parseRfh2(b.data.data(), b.data.size(), 1208, NUMERIC_PC, f, w);

    ASSERT_TRUE(f.included);
    ASSERT_TRUE(f.hasUsr);
    EXPECT_EQ(usrFolder, f.usrXml);
}

// ===========================================================================
// RFH2 build round-trip
// ===========================================================================

TEST(ParsedHeaders_Rfh2, RoundTripMcd)
{
    auto data = makeRfh2WithMcd(Order::Little, "jms_text");
    std::vector<std::string> w;
    Rfh2Fields f;
    parseRfh2(data.data(), data.size(), 1208, NUMERIC_PC, f, w);
    ASSERT_TRUE(f.included);

    std::vector<unsigned char> rebuilt;
    buildRfh2(f, rebuilt);

    // The rebuilt struct should parse back to the same fields
    Rfh2Fields f2;
    parseRfh2(rebuilt.data(), rebuilt.size(), 1208, NUMERIC_PC, f2, w);
    ASSERT_TRUE(f2.included);
    EXPECT_EQ(f.mcd.msgDomain, f2.mcd.msgDomain);
    EXPECT_EQ(f.ccsid,         f2.ccsid);
    EXPECT_EQ(f.nameValueCcsid, f2.nameValueCcsid);
}

TEST(ParsedHeaders_Rfh2, RoundTripUsrVerbatim)
{
    std::string usrXml = "<usr><x>42</x></usr>";
    Rfh2Fields f;
    f.included = true;
    f.encoding = NUMERIC_PC;
    f.ccsid    = 1208;
    f.nameValueCcsid = 1208;
    f.hasUsr   = true;
    f.usrXml   = usrXml;

    std::vector<unsigned char> built;
    buildRfh2(f, built);

    std::vector<std::string> w;
    Rfh2Fields f2;
    parseRfh2(built.data(), built.size(), 1208, NUMERIC_PC, f2, w);
    ASSERT_TRUE(f2.included);
    ASSERT_TRUE(f2.hasUsr);
    EXPECT_EQ(usrXml, f2.usrXml);
}

// ===========================================================================
// IMS parse
// ===========================================================================

TEST(ParsedHeaders_Ims, ParsesBasicFields)
{
    // MQIIH at documented offsets: strucLen=84
    L::Bytes b(84, ' ');
    b.putChars(0, 4, "IIH ");
    b.putLong(4, 1, Order::Little);           // Version
    b.putLong(8, 84, Order::Little);          // StrucLength
    b.putLong(12, 546, Order::Little);        // Encoding (MQENC_NATIVE LE)
    b.putLong(16, 1208, Order::Little);       // CodedCharSetId
    b.putChars(20, 8, "MQSTR   ");            // Format
    b.putLong(28, 0, Order::Little);          // Flags
    b.putChars(32, 8, "LTERM1  ");            // LTermOverride
    b.putChars(40, 8, "MAPNAME ");            // MFSMapName
    b.putChars(48, 8, "        ");            // ReplyToFormat
    b.putChars(56, 8, "PASS1   ");            // Authenticator
    // TranInstanceId at 64: zeros
    b.data[80] = (unsigned char)0x43;         // TranState = MQITS_IN_CONVERSATION
    b.data[81] = (unsigned char)0x30;         // CommitMode = MQICM_COMMIT_THEN_SEND
    b.data[82] = (unsigned char)0x43;         // SecurityScope = MQISS_CHECK

    std::vector<std::string> w;
    ImsFields f;
    size_t n = parseIms(b.data.data(), b.data.size(), 1208, NUMERIC_PC, f, w);
    EXPECT_EQ(84u, n);
    ASSERT_TRUE(f.included);
    EXPECT_EQ(1208, f.ccsid);
    EXPECT_EQ(NUMERIC_PC, f.encoding);
    EXPECT_EQ("LTERM1",  f.lTerm);
    EXPECT_EQ("MAPNAME", f.mapName);
    EXPECT_EQ("PASS1",   f.authenticator);
    EXPECT_EQ(0x43,      f.transState);
    EXPECT_EQ(0x30,      f.commitMode);
    EXPECT_EQ(0x43,      f.securityScope);
    EXPECT_TRUE(w.empty());
}

TEST(ParsedHeaders_Ims, BuildRoundTrip)
{
    // Build with zero-filled buffer (matches buildIms zero-init for binary fields)
    L::Bytes b(84, 0);
    b.putChars(0, 4, "IIH ");
    b.putLong(4, 1, Order::Little);
    b.putLong(8, 84, Order::Little);
    b.putLong(12, 546, Order::Little);
    b.putLong(16, 1208, Order::Little);
    b.putChars(20, 8, "MQSTR");
    b.putLong(28, 0, Order::Little);
    b.putChars(32, 8, "LTERM1");
    b.putChars(40, 8, "MAPNAME");
    b.putChars(48, 8, "");           // blank-fill ReplyToFormat
    b.putChars(56, 8, "PASS1");
    // TranInstanceId (64-79): zeros (not exposed in struct)
    b.data[80] = 0x43;
    b.data[81] = 0x30;
    b.data[82] = 0x43;
    // b.data[83] = 0 (Reserved)

    std::vector<std::string> w;
    ImsFields f;
    parseIms(b.data.data(), b.data.size(), 1208, NUMERIC_PC, f, w);
    ASSERT_TRUE(f.included);

    std::vector<unsigned char> rebuilt;
    buildIms(f, rebuilt);

    // Byte-level comparison; the unexposed TranInstanceId at offset 64 stays zero
    // in both original and rebuilt because the test uses zero-fill and buildIms
    // also zero-fills fields it does not encode.
    EXPECT_EQ(b.data, rebuilt);
}

// ===========================================================================
// parseHeaders top-level walker
// ===========================================================================

TEST(ParsedHeaders_Top, WalksDlqThenRfh2)
{
    auto dlqData  = makeDlq(Order::Little, 2082, "DEAD.Q", "QM2");
    auto rfh2Data = makeRfh2WithMcd(Order::Little, "jms_text");

    // Concatenate DLQ + RFH2 + some body
    std::vector<unsigned char> msg;
    msg.insert(msg.end(), dlqData.begin(), dlqData.end());
    msg.insert(msg.end(), rfh2Data.begin(), rfh2Data.end());
    msg.push_back('x');

    ParsedHeaders h = parse(msg);
    EXPECT_TRUE(h.dlq.included);
    EXPECT_EQ(2082, h.dlq.reason);
    EXPECT_TRUE(h.rfh2.included);
    EXPECT_TRUE(h.rfh2.hasMcd);
    EXPECT_EQ("jms_text", h.rfh2.mcd.msgDomain);
    EXPECT_FALSE(h.cics.included);
    EXPECT_FALSE(h.ims.included);
    EXPECT_FALSE(h.rfh1.included);
    EXPECT_TRUE(h.warnings.empty());
}

TEST(ParsedHeaders_Top, EmptyDataProducesNoHeaders)
{
    std::vector<unsigned char> data;
    ParsedHeaders h = parse(data);
    EXPECT_FALSE(h.hasAny());
}

TEST(ParsedHeaders_Top, PlainBodyProducesNoHeaders)
{
    // Plain "MQSTR" message body — no recognized StrucId
    std::vector<unsigned char> data = {'H', 'e', 'l', 'l', 'o'};
    ParsedHeaders h = parse(data);
    EXPECT_FALSE(h.hasAny());
}

// ===========================================================================
// buildHeaders top-level
// ===========================================================================

TEST(ParsedHeaders_Top, BuildHeadersEmptyWhenNoneIncluded)
{
    ParsedHeaders h;
    auto result = buildHeaders(h);
    EXPECT_TRUE(result.empty());
}

TEST(ParsedHeaders_Top, BuildHeadersRoundTripDlq)
{
    auto dlqData = makeDlq(Order::Little, 42, "Q1", "QM1");
    std::vector<std::string> w;
    DlqFields dlq;
    parseDlq(dlqData.data(), dlqData.size(), 1208, NUMERIC_PC, dlq, w);
    ASSERT_TRUE(dlq.included);

    ParsedHeaders h;
    h.dlq = dlq;
    auto rebuilt = buildHeaders(h);
    EXPECT_EQ(dlqData, rebuilt);
}
