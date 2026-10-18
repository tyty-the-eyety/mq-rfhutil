// ConnectStringTests.cpp - rfh::mq::parseServerString against hand-built strings.
//
// Windows source: DataArea::connect2QM, DataArea.cpp:10953-11087. Each expectation is derived from that
// code or from the documented MQ limits (channel 20, connection name 264, queue manager name 48, port
// 1-65535), never from running the parser. Where Windows hides an error or runs past a buffer, the
// expected result here is a rejection with a message (see the table in rfh_connect_string.h).

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rfh_connect_string.h"

using namespace rfh;
using namespace rfh::mq;

namespace
{
    ConnectOptions sentinel()
    {
        ConnectOptions o;
        o.mode = Mode::Bindings;
        o.qmName = "SENT.QM";
        o.channel = "SENT.CH";
        o.connName = "sentinel(1)";
        o.ccdtUrl = "file:///sentinel.json";
        o.heartbeat = 17;
        o.keepAlive = 3;
        o.userId = "sentinel-user";
        o.password = "sentinel-password";
        return o;
    }

    bool same(const ConnectOptions & a, const ConnectOptions & b)
    {
        return a.mode == b.mode && a.qmName == b.qmName && a.channel == b.channel && a.connName == b.connName &&
               a.ccdtUrl == b.ccdtUrl && a.heartbeat == b.heartbeat && a.keepAlive == b.keepAlive &&
               a.userId == b.userId && a.password == b.password;
    }

    void expectParsed(const std::string & text, const std::string & qm, const std::string & channel,
                      const std::string & conn)
    {
        ConnectOptions options = sentinel();
        std::string error = "stale";
        ASSERT_TRUE(parseServerString(text, options, error)) << "text: " << text << " error: " << error;
        EXPECT_TRUE(error.empty()) << text;
        EXPECT_EQ(options.mode, Mode::Client) << text;
        EXPECT_EQ(options.qmName, qm) << text;
        EXPECT_EQ(options.channel, channel) << text;
        EXPECT_EQ(options.connName, conn) << text;

        // everything else belongs to the caller
        ConnectOptions expected = sentinel();
        EXPECT_EQ(options.ccdtUrl, expected.ccdtUrl);
        EXPECT_EQ(options.heartbeat, expected.heartbeat);
        EXPECT_EQ(options.keepAlive, expected.keepAlive);
        EXPECT_EQ(options.userId, expected.userId);
        EXPECT_EQ(options.password, expected.password);
    }

    // Rejected: false, a message that contains `fragment`, and options exactly as before.
    void expectRejected(const std::string & text, const std::string & fragment)
    {
        ConnectOptions options = sentinel();
        std::string error;
        EXPECT_FALSE(parseServerString(text, options, error)) << "text accepted: " << text;
        EXPECT_NE(error.find(fragment), std::string::npos) << "text: " << text << " error: " << error;
        EXPECT_TRUE(same(options, sentinel())) << "options changed for: " << text;
    }

    void expectPlain(const std::string & text)
    {
        ConnectOptions options = sentinel();
        std::string error = "stale";
        EXPECT_TRUE(parseServerString(text, options, error)) << text << " error: " << error;
        EXPECT_TRUE(error.empty()) << text;
        EXPECT_TRUE(same(options, sentinel())) << "options changed for: " << text;
    }
}

TEST(ConnectString, PlainNameIsUntouched)
{
    expectPlain("");
    expectPlain("QM1");
    expectPlain("TEST_QM");
    expectPlain("   ");
    expectPlain("  QM1  ");
    expectPlain("QM.WITH%PERCENT");
    expectPlain("QM(1)");              // no "/" and no "[": still a name for the caller
    expectPlain("h\xC3\xA9");          // not ASCII, but not a connection string either
}

TEST(ConnectString, ChannelSlashTcpHostPort)
{
    // DataArea.cpp:10994-11064: channel before the first "/", transport to the second, rest is the connection name
    expectParsed("SYSTEM.DEF.SVRCONN/TCP/localhost(1414)", "", "SYSTEM.DEF.SVRCONN", "localhost(1414)");
    expectParsed("A/TCP/10.0.0.1(1)", "", "A", "10.0.0.1(1)");
}

TEST(ConnectString, QmWithBracketedChannel)
{
    // 10966-10987: QM name before "[", the connection string up to "]". Windows then connects with the name ""
    // (11005); here the QM name is kept so MQ checks it.
    expectParsed("TEST_QM[APP.CH/TCP/h(1415)]", "TEST_QM", "APP.CH", "h(1415)");
    expectParsed("[APP.CH/TCP/h(1415)]", "", "APP.CH", "h(1415)");   // Windows writes mqserver[-1] here
    expectParsed("  TEST_QM[APP.CH/TCP/h(1415)]\r\n", "TEST_QM", "APP.CH", "h(1415)");
}

TEST(ConnectString, HostWithoutPort)
{
    // 11057-11061: the connection name is whatever follows the second "/"; a port is optional for MQ (default 1414)
    expectParsed("CH/TCP/myhost", "", "CH", "myhost");
    expectParsed("CH/TCP/my.host.example.com", "", "CH", "my.host.example.com");
}

TEST(ConnectString, MultipleHosts)
{
    expectParsed("CH/TCP/host1(1414),host2(1415)", "", "CH", "host1(1414),host2(1415)");
    expectParsed("CH/TCP/host1,host2(1415)", "", "CH", "host1,host2(1415)");
    expectParsed("QM[CH/TCP/a(1),b(2),c(3)]", "QM", "CH", "a(1),b(2),c(3)");
}

TEST(ConnectString, IpV6Forms)
{
    // MQ writes an IPv6 address without brackets and the port in parentheses
    expectParsed("CH/TCP/fe80::1(1414)", "", "CH", "fe80::1(1414)");
    expectParsed("CH/TCP/::1(1414)", "", "CH", "::1(1414)");
    expectParsed("CH/TCP/2001:db8::1", "", "CH", "2001:db8::1");
    expectParsed("CH/TCP/fe80::1%eth0(1414)", "", "CH", "fe80::1%eth0(1414)");
    expectParsed("CH/TCP/a(1),2001:db8::2(1415)", "", "CH", "a(1),2001:db8::2(1415)");
    // a bracketed literal is passed on unchanged (Windows would take its "[" for the queue manager bracket)
    expectParsed("CH/TCP/[::1](1414)", "", "CH", "[::1](1414)");
    expectParsed("QM[CH/TCP/[::1](1414)]", "QM", "CH", "[::1](1414)");
    expectRejected("CH/TCP/[::1(1414)", "connection name");
    expectRejected("CH/TCP/::1](1414)", "connection name");
    expectRejected("CH/TCP/[](1414)", "connection name");
    expectRejected("CH/TCP/[[::1]](1414)", "connection name");
    expectRejected("CH/TCP/[::1]x(1414)", "connection name");
}

TEST(ConnectString, TransportCaseAndSupport)
{
    // 11020: Windows compares "TCP" exactly; "tcp" only works there because every unknown transport becomes TCP
    expectParsed("CH/TCP/h(1)", "", "CH", "h(1)");
    expectParsed("CH/tcp/h(1)", "", "CH", "h(1)");
    expectParsed("CH/Tcp/h(1)", "", "CH", "h(1)");
    expectParsed("sys.def.svrconn/tcp/HOST.Example.COM(1414)", "", "sys.def.svrconn", "HOST.Example.COM(1414)");

    // 11024-11047: Windows sets these transports; the Linux client speaks TCP only
    for (const char * t : { "LOCAL", "LU62", "NETBIOS", "SPX", "DECNET", "UDP", "local", "Lu62" })
    {
        expectRejected(std::string("CH/") + t + "/h(1)", "transport");
    }
    // anything else Windows turns into TCP without a word
    expectRejected("CH/TCPX/h(1)", "transport");
    expectRejected("CH/FOO/h(1)", "transport");
    expectRejected("CH/ftp/h(1)", "transport");
    expectRejected("CH//h(1)", "transport");
}

TEST(ConnectString, MissingParts)
{
    expectRejected("CH/", "transport");
    expectRejected("CH/TCP", "connection name");
    expectRejected("CH/TCP/", "connection name");
    expectRejected("/TCP/h(1)", "channel name");
    expectRejected("/", "channel name");
    expectRejected("//", "channel name");
    expectRejected("///", "channel name");
    expectRejected("/TCP/", "channel name");
    expectRejected("QM[]", "CHANNEL/TCP/");
    expectRejected("QM[CH]", "CHANNEL/TCP/");           // Windows ignores the bracket text (11000)
    expectRejected("QM[CH/TCP]", "connection name");
    expectRejected("QM[/TCP/h(1)]", "channel name");
}

TEST(ConnectString, RepeatedAndExtraSlashes)
{
    expectRejected("CH//TCP/h(1)", "transport");           // transport "" (Windows: TCP)
    expectRejected("CH/TCP//h(1)", "connection name");     // Windows passes "/h(1)" to MQ
    expectRejected("CH/TCP/h(1)/", "connection name");
    expectRejected("CH/TCP/h/(1)", "connection name");
    expectRejected("CH/TCP/h(1)/x", "connection name");
    expectRejected("CH///h(1)", "transport");
}

TEST(ConnectString, MalformedConnectionNames)
{
    expectRejected("CH/TCP/h(1414", "connection name");    // missing ")"
    expectRejected("CH/TCP/h1414)", "connection name");
    expectRejected("CH/TCP/h()", "port");
    expectRejected("CH/TCP/h(1414)x", "connection name");  // trailing garbage
    expectRejected("CH/TCP/h(1414)(1415)", "connection name");
    expectRejected("CH/TCP/h((1414))", "connection name");
    expectRejected("CH/TCP/(1414)", "host");
    expectRejected("CH/TCP/h(1),", "connection name");
    expectRejected("CH/TCP/,h(1)", "connection name");
    expectRejected("CH/TCP/h(1),,g(2)", "connection name");
    expectRejected("CH/TCP/,", "connection name");
    expectRejected("CH/TCP/h(1)g(2)", "connection name");
    expectRejected("QM[CH/TCP/h(1)", "']'");                // missing "]"; Windows accepts (10979)
    expectRejected("QM[CH/TCP/h(1)]x", "']'");              // text after "]"; Windows drops it
    expectRejected("QM[CH/TCP/h(1)]]", "connection name");
    expectRejected("QM[[CH/TCP/h(1)]]", "channel name");
    expectRejected("QM[CH/TCP/h(1)][CH/TCP/g(2)]", "connection name");
    expectRejected("CH/TCP/h(1)]", "connection name");
    expectRejected("[", "']'");
    expectRejected("QM[", "']'");
}

TEST(ConnectString, Ports)
{
    expectParsed("CH/TCP/h(1)", "", "CH", "h(1)");
    expectParsed("CH/TCP/h(1414)", "", "CH", "h(1414)");
    expectParsed("CH/TCP/h(65535)", "", "CH", "h(65535)");
    expectParsed("CH/TCP/h(01414)", "", "CH", "h(01414)");         // text kept as written
    expectParsed("CH/TCP/h(0000001414)", "", "CH", "h(0000001414)");

    expectRejected("CH/TCP/h(0)", "port");
    expectRejected("CH/TCP/h(000)", "port");
    expectRejected("CH/TCP/h(65536)", "port");
    expectRejected("CH/TCP/h(70000)", "port");
    expectRejected("CH/TCP/h(99999999999999999999999999)", "port");
    expectRejected("CH/TCP/h(4294967297)", "port");                // 2^32 + 1 would wrap to 1 in 32 bits
    expectRejected("CH/TCP/h(18446744073709551617)", "port");      // 2^64 + 1 would wrap to 1 in 64 bits
    expectRejected("CH/TCP/h(-1)", "port");
    expectRejected("CH/TCP/h(+1414)", "port");
    expectRejected("CH/TCP/h(1.5)", "port");
    expectRejected("CH/TCP/h(abc)", "port");
    expectRejected("CH/TCP/h(0x50)", "port");
    expectRejected("CH/TCP/h(14e2)", "port");
    expectRejected("CH/TCP/h(1414),g(0)", "port");
    expectRejected("CH/TCP/h(1414),g(70000)", "port");
}

TEST(ConnectString, WhitespaceIsTrimmed)
{
    expectParsed("  CH/TCP/h(1)  ", "", "CH", "h(1)");
    expectParsed("\tCH/TCP/h(1)\t", "", "CH", "h(1)");
    expectParsed("CH/TCP/h(1)\r\n", "", "CH", "h(1)");
    expectParsed("\r\nCH/TCP/h(1)", "", "CH", "h(1)");
    expectParsed(" \v\f QM[CH/TCP/h(1)] \n ", "QM", "CH", "h(1)");

    // only the ends: Windows would carry the other whitespace into the MQCD fields
    expectRejected("CH /TCP/h(1)", "channel name");
    expectRejected("CH/ TCP/h(1)", "transport");
    expectRejected("CH/TCP /h(1)", "transport");
    expectRejected("CH/TCP/ h(1)", "connection name");
    expectRejected("CH/TCP/h (1)", "connection name");
    expectRejected("CH/TCP/h( 1)", "connection name");
    expectRejected("CH/TCP/h(1), g(2)", "connection name");
    expectRejected("CH/TCP/h(1) ,g(2)", "connection name");
    expectRejected("QM [CH/TCP/h(1)]", "queue manager name");
    expectRejected("QM[ CH/TCP/h(1)]", "channel name");
    expectRejected("QM[CH/TCP/h(1) ]", "connection name");
    expectRejected("CH/TCP/h(1)\r\nCH2/TCP/g(2)", "connection name");
    expectRejected("CH\r\n/TCP/h(1)", "channel name");

    // a string of nothing but whitespace is an empty name, not a connection string
    expectPlain(" \t\r\n ");
}

TEST(ConnectString, LimitsOfMq)
{
    const std::string c20(20, 'C');
    const std::string c21(21, 'C');
    expectParsed(c20 + "/TCP/h(1)", "", c20, "h(1)");
    expectRejected(c21 + "/TCP/h(1)", "channel name");         // Windows leaves it blank (11050)
    expectRejected("QM[" + c21 + "/TCP/h(1)]", "channel name"); // Windows copies it past ChannelName (11053)
    expectRejected("QM[" + std::string(300, 'C') + "/TCP/h(1)]", "channel name");

    const std::string q48(48, 'Q');
    const std::string q49(49, 'Q');
    expectParsed(q48 + "[CH/TCP/h(1)]", q48, "CH", "h(1)");
    expectRejected(q49 + "[CH/TCP/h(1)]", "queue manager name");

    const std::string host258(258, 'a');
    expectParsed("CH/TCP/" + host258 + "(1414)", "", "CH", host258 + "(1414)");        // 264 bytes
    expectRejected("CH/TCP/" + host258 + "a(1414)", "connection name");                // 265 bytes
    expectParsed("CH/TCP/" + std::string(264, 'a'), "", "CH", std::string(264, 'a'));
    expectRejected("CH/TCP/" + std::string(265, 'a'), "connection name");              // Windows leaves it blank (11057)
    expectRejected("CH/TCP/" + std::string(100000, 'a'), "connection name");
    // two hosts that together are 264 and 265 bytes
    const std::string h1(130, 'a');
    expectParsed("CH/TCP/" + h1 + "," + std::string(133, 'b'), "", "CH", h1 + "," + std::string(133, 'b'));
    expectRejected("CH/TCP/" + h1 + "," + std::string(134, 'b'), "connection name");

    // Windows ignores a string of 1023 bytes or more (10960) and connects to ""
    expectRejected("CH/TCP/" + std::string(1100, 'h') + "(1)", "connection name");
}

TEST(ConnectString, EmbeddedNulIsRejected)
{
    expectRejected(std::string("CH/TCP/h(1)\0", 12), "NUL");
    expectRejected(std::string("CH/TCP/h\0(1)", 12), "NUL");
    expectRejected(std::string("CH\0/TCP/h(1)", 12), "NUL");
    expectRejected(std::string("QM\0[CH/TCP/h(1)]", 16), "NUL");
    expectRejected(std::string("\0CH/TCP/h(1)", 12), "NUL");
    expectRejected(std::string("QM\0", 3), "NUL");        // even a plain name: Windows would cut the string at the NUL
    expectRejected(std::string(1, '\0'), "NUL");
}

TEST(ConnectString, NonAsciiAndControlBytesAreRejected)
{
    expectRejected("CH/TCP/h\xC3\xA9(1)", "non-ASCII");
    expectRejected("CH\xC3\xA9/TCP/h(1)", "non-ASCII");
    expectRejected("QM\xC3\xA9[CH/TCP/h(1)]", "non-ASCII");
    expectRejected("CH/TCP/h(\xD9\xA1)", "non-ASCII");      // an Arabic-Indic digit is not a port digit
    expectRejected("CH/TC\xD0\xA0/h(1)", "transport");
    expectRejected("CH/TCP/h\xFF(1)", "non-ASCII");
    expectRejected("CH/TCP/h\x01(1)", "control");
    expectRejected("CH\x1F/TCP/h(1)", "control");
    expectRejected("CH/TCP/h\x7F(1)", "control");
    expectRejected("CH/TCP/h(1)\x1B", "control");
}

TEST(ConnectString, CaseIsPreserved)
{
    expectParsed("MiXeD.Ch/TCP/Host.Name(1414)", "", "MiXeD.Ch", "Host.Name(1414)");
    expectParsed("MiXeD_Qm[MiXeD.Ch/TCP/Host.Name(1414)]", "MiXeD_Qm", "MiXeD.Ch", "Host.Name(1414)");
}

TEST(ConnectString, SlashInsideAQueueManagerNameIsNotABracketForm)
{
    // The first "[" must come before the first "/" for the bracket form. "A/B[...]" is read as channel "A",
    // transport "B[CH", which is not a transport.
    expectRejected("A/B[CH/TCP/h(1)]", "transport");
}

TEST(ConnectString, FailureLeavesPartialFactsOut)
{
    // a failed parse changes nothing, even when the early parts were fine
    ConnectOptions options = sentinel();
    std::string error;
    EXPECT_FALSE(parseServerString("QM[CH/TCP/h(99999)]", options, error));
    EXPECT_TRUE(same(options, sentinel()));
    EXPECT_FALSE(error.empty());
}

TEST(ConnectString, SuccessReplacesEarlierConnectionFacts)
{
    ConnectOptions options = sentinel();
    std::string error;
    ASSERT_TRUE(parseServerString("CH2/TCP/h2(2)", options, error));
    ASSERT_TRUE(parseServerString("CH/TCP/h(1)", options, error));
    EXPECT_EQ(options.qmName, "");             // no queue manager given: not the one from the earlier call
    EXPECT_EQ(options.channel, "CH");
    EXPECT_EQ(options.connName, "h(1)");
}

namespace
{
    std::vector<std::string> corpus()
    {
        return { "SYSTEM.DEF.SVRCONN/TCP/localhost(1414)", "QM[CH/TCP/host1(1414),host2(1415)]",
                 "CH/TCP/fe80::1(1414)", "QM[CH/TCP/[::1](1414)]", "  CH/TCP/h(1)\r\n", "CH/tcp/h",
                 "[CH/TCP/h(65535)]", "CH/LOCAL/x", "A/B[CH/TCP/h(1)]", "", "/", "[", "]", "QM1" };
    }

    // The text sits in a heap block of exactly its size (plus the string's own NUL), so under the guard
    // allocator a read past the end faults at once.
    void parseExact(const std::string & source)
    {
        std::unique_ptr<std::string> text(new std::string(source.data(), source.size()));
        text->shrink_to_fit();
        ConnectOptions options;
        std::string error;
        const bool ok = parseServerString(*text, options, error);
        if (ok)
        {
            EXPECT_TRUE(error.empty());
        }
        else
        {
            EXPECT_FALSE(error.empty()) << source;
        }
    }
}

TEST(ConnectString, EveryPrefixAndSuffixOfValidStringsOnExactBuffers)
{
    // run under the guard allocator by memcheck.ConnectString
    for (const std::string & s : corpus())
    {
        for (std::size_t n = 0; n <= s.size(); n++)
        {
            parseExact(s.substr(0, n));
            parseExact(s.substr(n));
        }
    }
}

TEST(ConnectString, MutatedStringsKeepTheInvariants)
{
    // 8000 seeded mutations: replace, insert or delete one byte. Hand-written invariants: no crash; a
    // rejection says why and changes nothing; an acceptance yields in-range, byte-for-byte substrings.
    static const char kBytes[] = "/[]()-,: \t\r\n.%_+0123456789abcXYZ\xC3\xA9\x7F\x01";
    const std::string interesting = std::string(kBytes, sizeof(kBytes) - 1) + std::string(1, '\0');
    std::uint64_t seed = 0x9E3779B97F4A7C15ull;
    auto next = [&seed]() { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return static_cast<unsigned>(seed >> 33); };

    const std::vector<std::string> base = corpus();
    for (int i = 0; i < 8000; i++)
    {
        std::string s = base[next() % base.size()];
        const char c = interesting[next() % interesting.size()];
        switch (next() % 3)
        {
        case 0:
            if (!s.empty())
            {
                s[next() % s.size()] = c;
            }
            break;
        case 1:
            s.insert(s.begin() + (next() % (s.size() + 1)), c);
            break;
        default:
            if (!s.empty())
            {
                s.erase(next() % s.size(), 1);
            }
            break;
        }

        ConnectOptions options = sentinel();
        std::string error;
        if (parseServerString(s, options, error))
        {
            EXPECT_TRUE(error.empty());
            if (!same(options, sentinel()))
            {
                EXPECT_EQ(options.mode, Mode::Client);
                EXPECT_FALSE(options.channel.empty());
                EXPECT_LE(options.channel.size(), 20u);
                EXPECT_LE(options.qmName.size(), 48u);
                EXPECT_FALSE(options.connName.empty());
                EXPECT_LE(options.connName.size(), 264u);
                EXPECT_NE(s.find(options.channel), std::string::npos);
                EXPECT_NE(s.find(options.connName), std::string::npos);
                EXPECT_EQ(options.channel.find('\0'), std::string::npos);
                EXPECT_EQ(options.connName.find('\0'), std::string::npos);
            }
        }
        else
        {
            EXPECT_FALSE(error.empty());
            EXPECT_TRUE(same(options, sentinel()));
        }
    }
}
