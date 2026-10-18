// ConnectSecurityTests.cpp - what MqSession::connect puts into MQCNO, MQCD, MQSCO and MQCSP.
//
// The fake MQI records each MQCONNX with copies of the structures and the strings they point to
// (rfh_test::ConnxRecord). Expectations are derived from Windows (DataArea::connect2QM, DataArea.cpp
// 10636-11231; ConnUser.cpp 59-79 for the dialog limits) and from the MQ structure layouts, never from
// running the code under test. Where Windows silently cuts a value, ignores an option or overruns a
// buffer, the expectation here is a rejection or a warning (see the comments on ConnectOptions).
//
// Every password in this file is a made-up marker. No test writes a password to a file or to its output:
// comparisons use EXPECT_TRUE on a boolean, so a failure never prints the password.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include "rfh_connect_security.h"
#include "rfh_mq_session.h"
#include "rfh_trace.h"

using namespace rfh;

#include "fake_mq.h"

namespace
{
    using rfh_test::ConnxRecord;
    using rfh_test::fakeApi;

    rfh_test::FakeMq & fake = rfh_test::fakeMq();

    const char * const kMarker = "PwMarker-7f3a9c1e";   // appears nowhere but as a password

    std::string text(size_t count, char c = 'x')
    {
        return std::string(count, c);
    }

    template <size_t N> std::string field(const MQCHAR (&f)[N])
    {
        return rfh_test::fake_detail::trimmed(f, N);
    }

    bool contains(const std::string & haystack, const std::string & needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    bool leaks(const Result & r, const std::string & secret)
    {
        if (contains(r.text, secret))
        {
            return true;
        }
        for (size_t i = 0; i < r.warnings.size(); i++)
        {
            if (contains(r.warnings[i], secret))
            {
                return true;
            }
        }
        return false;
    }

    bool anyWarningContains(const Result & r, const std::string & fragment)
    {
        for (size_t i = 0; i < r.warnings.size(); i++)
        {
            if (contains(r.warnings[i], fragment))
            {
                return true;
            }
        }
        return false;
    }

    int32_t longAt(const void * structure, size_t offset)
    {
        int32_t value;
        memcpy(&value, static_cast<const unsigned char *>(structure) + offset, sizeof(value));
        return value;
    }

    class ConnectSecurityTest : public ::testing::Test
    {
    protected:
        ConnectSecurityTest() : session(fakeApi()) {}

        void SetUp() override { fake.reset(); }

        static ConnectOptions client()
        {
            ConnectOptions o;
            o.mode = Mode::Client;
            o.qmName = "QM1";
            o.channel = "TEST.CHL";
            o.connName = "host.example(1414)";
            return o;
        }

        static ConnectOptions tls()
        {
            ConnectOptions o = client();
            o.sslEnabled = true;
            o.cipherSpec = "TLS_RSA_WITH_AES_128_CBC_SHA256";
            return o;
        }

        // Connects and expects the call to have reached MQ exactly once more.
        Result connectOk(const ConnectOptions & options)
        {
            Result r = session.connect(options);
            EXPECT_TRUE(r.ok()) << r.text;
            return r;
        }

        // Expects a rejection that never reached MQ, with `fragment` in the message.
        void expectRejected(const ConnectOptions & options, const std::string & fragment)
        {
            const int before = fake.connxCalls;
            Result r = session.connect(options);
            EXPECT_FALSE(r.ok());
            EXPECT_EQ(before, fake.connxCalls) << "MQCONNX must not be called: " << r.text;
            EXPECT_TRUE(contains(r.text, fragment)) << "message: " << r.text << " wanted: " << fragment;
            EXPECT_FALSE(session.isConnected());
        }

        // The newest MQCONNX record. When a connect was wrongly rejected there is none: that fails this test
        // and hands back an empty record, so one bad test does not abort the whole binary.
        const ConnxRecord & last() const
        {
            static const ConnxRecord none;
            if (fake.connxLog.empty())
            {
                ADD_FAILURE() << "MQCONNX was never called";
                return none;
            }
            return fake.connxLog.back();
        }

        MqSession session;
    };

    // A trace sink that keeps every line.
    class CapturingTrace : public RfhTrace
    {
    public:
        CapturingTrace() { verboseTrace = TRUE; }
        BOOL isTraceEnabled() override { return TRUE; }
        void logTraceEntry(const char * traceInfo) override { lines.push_back(traceInfo); }
        void dumpTraceData(const char * label, const unsigned char * data, unsigned int length) override
        {
            lines.push_back(std::string(label) + ":" + std::string(reinterpret_cast<const char *>(data), length));
        }
        std::vector<std::string> lines;
    };
}

// ---- TLS ----

TEST_F(ConnectSecurityTest, SslFieldsReachTheChannelDefinition)
{
    ConnectOptions o = tls();
    o.sslClientValidation = true;
    connectOk(o);

    ASSERT_EQ(1u, fake.connxLog.size());
    EXPECT_TRUE(last().hasClientConn);
    EXPECT_TRUE(last().hasSslConfig);
    EXPECT_EQ("TLS_RSA_WITH_AES_128_CBC_SHA256", field(last().cd.SSLCipherSpec));
    EXPECT_EQ(MQSCA_REQUIRED, last().cd.SSLClientAuth);
    // structure versions: the CD carries the SSL fields (version 7 and above) and the CNO points at the SCO (version 4 and above)
    // Windows raises the CD to exactly version 7 for TLS (DataArea.cpp 10932-10935); an older client library
    // refuses a version it does not know (MQRC_CD_ERROR), so the core does not claim version 12.
    EXPECT_EQ(MQCD_VERSION_7, last().cd.Version);
    EXPECT_EQ(MQCD_LENGTH_7, last().cd.StrucLength);
    EXPECT_GE(last().cno.Version, MQCNO_VERSION_4);
    EXPECT_NE(nullptr, last().cno.SSLConfigPtr);
    EXPECT_EQ(0, memcmp(last().sco.StrucId, MQSCO_STRUC_ID, 4));
}

TEST_F(ConnectSecurityTest, ClientValidationOffMakesClientAuthOptional)
{
    connectOk(tls());
    EXPECT_EQ(MQSCA_OPTIONAL, last().cd.SSLClientAuth);   // Windows 10887-10896
}

TEST_F(ConnectSecurityTest, CipherSpecAndPeerNameAreSet)
{
    ConnectOptions o = tls();
    o.sslPeerName = "CN=qm1,O=Example,C=GB";
    connectOk(o);

    EXPECT_EQ(o.sslPeerName, last().sslPeerName);
    EXPECT_EQ((MQLONG)o.sslPeerName.size(), last().cd.SSLPeerNameLength);
    EXPECT_NE(nullptr, last().cd.SSLPeerNamePtr);
}

TEST_F(ConnectSecurityTest, CipherSpecOf32CharactersFillsTheFieldWithoutOverrunning)
{
    // Windows cuts a 32 character spec to 31 (10877); the field holds 32 and MQ accepts a full one.
    ConnectOptions o = tls();
    o.cipherSpec = text(32, 'C');
    connectOk(o);

    EXPECT_EQ(0, memcmp(last().cd.SSLCipherSpec, o.cipherSpec.data(), 32));
    EXPECT_EQ(nullptr, last().cd.SSLPeerNamePtr);     // the next field is untouched (offset 1720 after the 32 bytes at 1688)
    EXPECT_EQ(0, last().cd.SSLPeerNameLength);
}

TEST_F(ConnectSecurityTest, ACipherSpecOf33CharactersIsRejected)
{
    ConnectOptions o = tls();
    o.cipherSpec = text(33);
    expectRejected(o, "cipher spec");
}

TEST_F(ConnectSecurityTest, TlsWithAChannelButNoCipherSpecIsRejected)
{
    // Windows connects without TLS when the cipher spec is empty (10873), whatever the check box says.
    ConnectOptions o = tls();
    o.cipherSpec.clear();
    expectRejected(o, "cipher spec");
}

TEST_F(ConnectSecurityTest, PeerNameBoundsAre1024AndNoNul)
{
    ConnectOptions o = tls();
    o.sslPeerName = text(1024);
    connectOk(o);
    EXPECT_EQ(1024, last().cd.SSLPeerNameLength);
    EXPECT_EQ(o.sslPeerName, last().sslPeerName);

    session.disconnect();
    o.sslPeerName = text(1025);
    expectRejected(o, "peer name");

    o.sslPeerName = std::string("CN=a\0b", 6);
    expectRejected(o, "peer name");
}

TEST_F(ConnectSecurityTest, KeyRepositoryGoesIntoTheSslConfig)
{
    ConnectOptions o = tls();
    o.keyRepository = "/var/mqm/ssl/key";
    connectOk(o);

    ASSERT_TRUE(last().hasSslConfig);
    EXPECT_EQ("/var/mqm/ssl/key", field(last().sco.KeyRepository));
    EXPECT_EQ(MQSCO_VERSION_1, last().sco.Version);     // nothing below needs more
    EXPECT_EQ(MQSCO_RESET_COUNT_DEFAULT, last().sco.KeyResetCount);
    EXPECT_EQ(MQSSL_FIPS_NO, last().sco.FipsRequired);
}

TEST_F(ConnectSecurityTest, KeyRepositoryLimitIs255AndNotSilentlyIgnored)
{
    ConnectOptions o = tls();
    o.keyRepository = text(255, 'k');
    connectOk(o);
    EXPECT_EQ(0, memcmp(last().sco.KeyRepository, o.keyRepository.data(), 255));

    session.disconnect();
    o.keyRepository = text(256, 'k');   // Windows ignores a repository of 255 or more (10900) and connects with MQ's default
    expectRejected(o, "key repository");
}

TEST_F(ConnectSecurityTest, EmptyKeyRepositoryLeavesMqsDefault)
{
    connectOk(tls());
    ASSERT_TRUE(last().hasSslConfig);
    EXPECT_EQ('\0', last().sco.KeyRepository[0]);
    EXPECT_EQ("", field(last().sco.KeyRepository));
}

TEST_F(ConnectSecurityTest, FipsRequiredAndResetCount)
{
    ConnectOptions o = tls();
    o.fipsRequired = true;
    o.sslResetCount = 40000;
    connectOk(o);

    EXPECT_GE(last().sco.Version, MQSCO_VERSION_2);     // FipsRequired is a version 2 field (10918-10926)
    EXPECT_EQ(MQSSL_FIPS_YES, last().sco.FipsRequired);
    EXPECT_EQ(40000, last().sco.KeyResetCount);
}

TEST_F(ConnectSecurityTest, ResetCountBounds)
{
    ConnectOptions o = tls();
    o.sslResetCount = 999999999;
    connectOk(o);
    EXPECT_EQ(999999999, last().sco.KeyResetCount);

    session.disconnect();
    o.sslResetCount = 1000000000;
    expectRejected(o, "reset count");
    o.sslResetCount = -1;
    expectRejected(o, "reset count");
}

TEST_F(ConnectSecurityTest, CertificateLabelNeedsVersion5)
{
    ConnectOptions o = tls();
    o.certificateLabel = "ibmwebspheremqrfhutil";
    connectOk(o);

    EXPECT_GE(last().sco.Version, MQSCO_VERSION_5);
    EXPECT_EQ("ibmwebspheremqrfhutil", field(last().sco.CertificateLabel));

    session.disconnect();
    o.certificateLabel = text(64);
    connectOk(o);
    EXPECT_EQ(0, memcmp(last().sco.CertificateLabel, o.certificateLabel.data(), 64));

    session.disconnect();
    o.certificateLabel = text(65);
    expectRejected(o, "certificate label");
}

TEST_F(ConnectSecurityTest, SecurityExitAndDataAreSet)
{
    ConnectOptions o = client();
    o.securityExit = "mylib(SecExit)";
    o.securityData = "token=1";
    connectOk(o);

    EXPECT_EQ("mylib(SecExit)", field(last().cd.SecurityExit));
    EXPECT_EQ("token=1", field(last().cd.SecurityUserData));
    EXPECT_FALSE(last().hasSslConfig);    // a security exit does not need TLS
}

TEST_F(ConnectSecurityTest, SecurityExitAndDataLimits)
{
    ConnectOptions o = client();
    o.securityExit = text(128, 'e');
    o.securityData = text(32, 'd');
    connectOk(o);
    EXPECT_EQ(0, memcmp(last().cd.SecurityExit, o.securityExit.data(), 128));
    EXPECT_EQ(0, memcmp(last().cd.SecurityUserData, o.securityData.data(), 32));

    session.disconnect();
    o.securityExit = text(129);
    expectRejected(o, "security exit");
    o.securityExit = "x";
    o.securityData = text(33);
    expectRejected(o, "security data");
}

TEST_F(ConnectSecurityTest, SecurityDataWithoutAnExitIsIgnoredWithAWarning)
{
    // Windows uses the data only inside the exit branch (10829-10855) and says nothing.
    ConnectOptions o = client();
    o.securityData = "orphan";
    Result r = connectOk(o);

    EXPECT_EQ("", field(last().cd.SecurityUserData));
    EXPECT_TRUE(anyWarningContains(r, "security data"));
}

TEST_F(ConnectSecurityTest, LocalAddressIsSet)
{
    ConnectOptions o = client();
    o.localAddress = "192.0.2.7(5000)";
    connectOk(o);
    EXPECT_EQ("192.0.2.7(5000)", field(last().cd.LocalAddress));

    session.disconnect();
    o.localAddress = text(48, 'a');
    connectOk(o);
    EXPECT_EQ(0, memcmp(last().cd.LocalAddress, o.localAddress.data(), 48));

    session.disconnect();
    o.localAddress = text(49, 'a');      // Windows cuts it to 48 (11070)
    expectRejected(o, "local address");
}

TEST_F(ConnectSecurityTest, NothingIsSetWhenSslIsOff)
{
    // The dialog keeps the TLS fields while the box is unticked; Windows ignores them then (10873) and so do we, quietly.
    ConnectOptions o = client();
    o.sslEnabled = false;
    o.cipherSpec = "TLS_RSA_WITH_AES_128_CBC_SHA256";
    o.keyRepository = "/var/mqm/ssl/key";
    o.sslPeerName = "CN=qm1";
    o.sslClientValidation = true;
    o.fipsRequired = true;
    o.sslResetCount = 1000;
    o.certificateLabel = "label";
    Result r = connectOk(o);

    EXPECT_FALSE(last().hasSslConfig);
    EXPECT_EQ(nullptr, last().cno.SSLConfigPtr);
    EXPECT_EQ("", field(last().cd.SSLCipherSpec));
    EXPECT_EQ(nullptr, last().cd.SSLPeerNamePtr);
    EXPECT_EQ(0, last().cd.SSLPeerNameLength);
    // Windows ignores them silently; here a warning names what was ignored (the box may have been unticked by mistake)
    EXPECT_TRUE(anyWarningContains(r, "TLS is not enabled"));
    EXPECT_TRUE(anyWarningContains(r, "cipher spec"));
    EXPECT_TRUE(anyWarningContains(r, "key repository"));
    EXPECT_TRUE(anyWarningContains(r, "peer name"));
}

TEST_F(ConnectSecurityTest, UntickedTlsWithNoTlsFieldsHasNoWarning)
{
    ConnectOptions o = client();
    o.sslClientValidation = true;       // a bare flag with nothing to apply it to is not worth a warning
    Result r = connectOk(o);
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(ConnectSecurityTest, UntickedTlsWarnsOnlyAboutTheFieldsThatAreSet)
{
    ConnectOptions o = client();
    o.keyRepository = "/var/mqm/ssl/key";
    Result r = connectOk(o);
    EXPECT_TRUE(anyWarningContains(r, "key repository"));
    EXPECT_FALSE(anyWarningContains(r, "cipher spec"));
    EXPECT_FALSE(anyWarningContains(r, "peer name"));
    EXPECT_EQ(nullptr, last().cno.SSLConfigPtr);
}

TEST_F(ConnectSecurityTest, NothingIsSetByDefault)
{
    connectOk(client());
    EXPECT_FALSE(last().hasSslConfig);
    EXPECT_FALSE(last().hasSecurityParms);
    EXPECT_EQ("", field(last().cd.SecurityExit));
    EXPECT_EQ("", field(last().cd.LocalAddress));
    EXPECT_EQ("TEST.CHL", field(last().cd.ChannelName));
    EXPECT_EQ("host.example(1414)", field(last().cd.ConnectionName));
    // Windows keeps the CD at version 6 until TLS needs more (10715-10735, 10932); nothing here lies beyond version 6
    EXPECT_EQ(MQCD_VERSION_6, last().cd.Version);
    EXPECT_EQ(MQCD_LENGTH_6, last().cd.StrucLength);
}

TEST_F(ConnectSecurityTest, ALocalAddressNeedsChannelDefinitionVersion7)
{
    // LocalAddress lies after MQCD_LENGTH_6; a version 6 definition would drop it silently
    ConnectOptions o = client();
    o.localAddress = "192.0.2.7(5000)";
    connectOk(o);
    EXPECT_EQ(MQCD_VERSION_7, last().cd.Version);
    EXPECT_EQ(MQCD_LENGTH_7, last().cd.StrucLength);
}

TEST_F(ConnectSecurityTest, ANonDefaultKeepAliveNeedsChannelDefinitionVersion7)
{
    ConnectOptions o = client();
    o.keepAlive = 30;
    connectOk(o);
    EXPECT_EQ(30, last().cd.KeepAliveInterval);
    EXPECT_EQ(MQCD_VERSION_7, last().cd.Version);
    EXPECT_EQ(MQCD_LENGTH_7, last().cd.StrucLength);
}

TEST_F(ConnectSecurityTest, EveryChannelDefinitionFieldTheCoreSetsLiesInsideVersion7)
{
    // The core writes these MQCD fields (session and security code). The version 7 length must cover the end of each.
    const size_t ends[] = {
        offsetof(MQCD, ChannelName) + sizeof(MQCD::ChannelName),
        offsetof(MQCD, ConnectionName) + sizeof(MQCD::ConnectionName),
        offsetof(MQCD, MaxMsgLength) + sizeof(MQCD::MaxMsgLength),
        offsetof(MQCD, TransportType) + sizeof(MQCD::TransportType),
        offsetof(MQCD, HeartbeatInterval) + sizeof(MQCD::HeartbeatInterval),
        offsetof(MQCD, KeepAliveInterval) + sizeof(MQCD::KeepAliveInterval),
        offsetof(MQCD, SecurityExit) + sizeof(MQCD::SecurityExit),
        offsetof(MQCD, SecurityUserData) + sizeof(MQCD::SecurityUserData),
        offsetof(MQCD, LocalAddress) + sizeof(MQCD::LocalAddress),
        offsetof(MQCD, SSLCipherSpec) + sizeof(MQCD::SSLCipherSpec),
        offsetof(MQCD, SSLPeerNamePtr) + sizeof(MQCD::SSLPeerNamePtr),
        offsetof(MQCD, SSLPeerNameLength) + sizeof(MQCD::SSLPeerNameLength),
        offsetof(MQCD, SSLClientAuth) + sizeof(MQCD::SSLClientAuth),
    };
    for (size_t i = 0; i < sizeof(ends) / sizeof(ends[0]); i++)
    {
        EXPECT_LE(ends[i], (size_t)MQCD_LENGTH_7) << "field " << i;
    }
    // and the fields TLS adds are really past version 6, which is why TLS needs version 7
    EXPECT_GT(offsetof(MQCD, SSLCipherSpec) + sizeof(MQCD::SSLCipherSpec), (size_t)MQCD_LENGTH_6);
}

TEST_F(ConnectSecurityTest, BindingsModeIgnoresTheClientOnlyFields)
{
    ConnectOptions o = tls();
    o.mode = Mode::Bindings;
    o.channel.clear();
    o.connName.clear();
    o.keyRepository = "/var/mqm/ssl/key";
    o.securityExit = "exit";
    o.localAddress = "1.2.3.4";
    o.cipherSpec = text(100);       // over-long, but never used: not validated
    Result r = connectOk(o);

    EXPECT_FALSE(last().hasClientConn);
    EXPECT_FALSE(last().hasSslConfig);
    EXPECT_TRUE((last().cno.Options & MQCNO_LOCAL_BINDING) != 0);
    EXPECT_TRUE(anyWarningContains(r, "client"));
}

TEST_F(ConnectSecurityTest, BindingsModeWithNoClientFieldsHasNoWarning)
{
    ConnectOptions o;
    o.mode = Mode::Bindings;
    Result r = connectOk(o);
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(ConnectSecurityTest, ACcdtConnectionTakesTheKeyRepositoryButNotAChannelDefinition)
{
    ConnectOptions o;
    o.mode = Mode::Client;
    o.qmName = "QM1";
    o.ccdtUrl = "file:///etc/mqm/ccdt.json";
    o.sslEnabled = true;
    o.keyRepository = "/var/mqm/ssl/key";
    o.sslPeerName = "CN=qm1";
    o.securityExit = "exit";
    o.localAddress = "1.2.3.4";
    Result r = connectOk(o);

    EXPECT_FALSE(last().hasClientConn);
    EXPECT_TRUE(last().hasSslConfig);
    EXPECT_EQ("/var/mqm/ssl/key", field(last().sco.KeyRepository));
    EXPECT_EQ(o.ccdtUrl, last().ccdtUrl);
    EXPECT_TRUE(anyWarningContains(r, "peer name"));
    EXPECT_TRUE(anyWarningContains(r, "security exit"));
    EXPECT_TRUE(anyWarningContains(r, "local address"));
}

TEST_F(ConnectSecurityTest, ACipherSpecWithoutAChannelIsRejected)
{
    // It would be ignored: the CCDT or MQSERVER decides, and the user would believe the link is encrypted.
    ConnectOptions o;
    o.mode = Mode::Client;
    o.qmName = "QM1";
    o.ccdtUrl = "file:///etc/mqm/ccdt.json";
    o.sslEnabled = true;
    o.cipherSpec = "ANY_TLS12";
    expectRejected(o, "cipher spec");
}

TEST_F(ConnectSecurityTest, FieldsWithAnEmbeddedNulAreRejected)
{
    ConnectOptions o = tls();
    o.keyRepository = std::string("/a\0b", 4);
    expectRejected(o, "key repository");
    o = tls();
    o.cipherSpec = std::string("TLS\0X", 5);
    expectRejected(o, "cipher spec");
    o = client();
    o.securityExit = std::string("a\0b", 3);
    expectRejected(o, "security exit");
    o = client();
    o.localAddress = std::string("a\0b", 3);
    expectRejected(o, "local address");
}

// ---- user id and password (MQCSP) ----

TEST_F(ConnectSecurityTest, UserAndPasswordGoIntoTheCsp)
{
    ConnectOptions o = client();
    o.userId = "mqapp";
    o.password = kMarker;
    connectOk(o);

    ASSERT_TRUE(last().hasSecurityParms);
    EXPECT_EQ(MQCSP_VERSION_1, last().csp.Version);
    EXPECT_EQ(MQCSP_AUTH_USER_ID_AND_PWD, last().csp.AuthenticationType);
    EXPECT_EQ(5, last().csp.CSPUserIdLength);
    EXPECT_EQ("mqapp", last().userId);
    EXPECT_EQ((MQLONG)strlen(kMarker), last().csp.CSPPasswordLength);
    EXPECT_TRUE(last().password == std::vector<char>(kMarker, kMarker + strlen(kMarker)));
    EXPECT_GE(last().cno.Version, MQCNO_VERSION_5);     // SecurityParmsPtr is a version 5 field (10785)
    EXPECT_EQ(MQCNO_VERSION_6, last().cno.Version);
    EXPECT_NE(nullptr, last().cno.SecurityParmsPtr);
}

TEST_F(ConnectSecurityTest, TheCspHasTheDocumentedLayout)
{
    if (sizeof(void *) != 8)
    {
        GTEST_SKIP() << "offsets below are for 64-bit";
    }
    ConnectOptions o = client();
    o.userId = "mqapp";
    o.password = "0123456789";
    connectOk(o);

    // MQCSP on 64-bit: StrucId@0, Version@4, AuthenticationType@8, Reserved1@12, CSPUserIdPtr@16, CSPUserIdOffset@24,
    // CSPUserIdLength@28, Reserved2@32, CSPPasswordPtr@40, CSPPasswordOffset@48, CSPPasswordLength@52
    static_assert(sizeof(MQCSP) == 104, "MQCSP is 104 bytes on 64-bit");
    const MQCSP & csp = last().csp;
    EXPECT_EQ(0, memcmp(&csp, "CSP ", 4));
    EXPECT_EQ(1, longAt(&csp, 4));
    EXPECT_EQ(1, longAt(&csp, 8));
    EXPECT_EQ(0, longAt(&csp, 24));
    EXPECT_EQ(5, longAt(&csp, 28));
    EXPECT_EQ(0, longAt(&csp, 48));
    EXPECT_EQ(10, longAt(&csp, 52));
    EXPECT_NE(nullptr, csp.CSPUserIdPtr);
    EXPECT_NE(nullptr, csp.CSPPasswordPtr);
    EXPECT_NE(csp.CSPPasswordPtr, static_cast<const void *>(o.password.data()))
        << "MQ must not be handed the caller's own buffer";

    // MQSCO key fields on 64-bit: KeyResetCount@536, FipsRequired@540, CertificateLabel@564 (checked with TLS on)
    session.disconnect();
    ConnectOptions t = tls();
    t.fipsRequired = true;
    t.sslResetCount = 77;
    t.certificateLabel = "lbl";
    connectOk(t);
    static_assert(sizeof(MQSCO) == 648, "MQSCO is 648 bytes on 64-bit");
    EXPECT_EQ(77, longAt(&last().sco, 536));
    EXPECT_EQ(MQSSL_FIPS_YES, longAt(&last().sco, 540));
    EXPECT_EQ(0, memcmp(reinterpret_cast<const char *>(&last().sco) + 564, "lbl", 3));
    EXPECT_EQ(0, memcmp(&last().sco, "SCO ", 4));
    // MQCD: SSLCipherSpec@1688, SSLPeerNameLength@1728, SSLClientAuth@1732
    static_assert(sizeof(MQCD) == 1992, "MQCD is 1992 bytes on 64-bit");
    EXPECT_EQ(0, memcmp(reinterpret_cast<const char *>(&last().cd) + 1688, t.cipherSpec.data(), t.cipherSpec.size()));
    EXPECT_EQ(MQSCA_OPTIONAL, longAt(&last().cd, 1732));
}

TEST_F(ConnectSecurityTest, ACspIsSentInBindingsModeToo)
{
    ConnectOptions o;
    o.mode = Mode::Bindings;
    o.userId = "mqapp";
    o.password = "pw";
    connectOk(o);
    EXPECT_TRUE(last().hasSecurityParms);   // Windows builds the CSP outside the client-only code (10752-10789)
}

TEST_F(ConnectSecurityTest, NoPasswordMeansNoCsp)
{
    ConnectOptions o = client();
    o.userId = "mqapp";
    Result r = connectOk(o);

    EXPECT_FALSE(last().hasSecurityParms);      // the IDPWOS rule, Windows 10769-10772
    EXPECT_EQ(nullptr, last().cno.SecurityParmsPtr);
    EXPECT_TRUE(anyWarningContains(r, "user id"));      // Windows says nothing
}

TEST_F(ConnectSecurityTest, APasswordWithoutAUserIdIsNotSentAndWarns)
{
    ConnectOptions o = client();
    o.password = kMarker;
    Result r = connectOk(o);

    EXPECT_FALSE(last().hasSecurityParms);      // Windows skips the whole block without a user id (10754)
    EXPECT_TRUE(anyWarningContains(r, "password"));
    EXPECT_FALSE(leaks(r, kMarker));
}

TEST_F(ConnectSecurityTest, EmptyCredentialsMeanNoCspAndNoWarning)
{
    Result r = connectOk(client());
    EXPECT_FALSE(last().hasSecurityParms);
    EXPECT_TRUE(r.warnings.empty());
}

TEST_F(ConnectSecurityTest, CredentialLimitsAre255And1024)
{
    ConnectOptions o = client();
    o.userId = text(255, 'u');
    o.password = text(1024, 'p');
    connectOk(o);
    EXPECT_EQ(255, last().csp.CSPUserIdLength);
    EXPECT_EQ(1024, last().csp.CSPPasswordLength);

    session.disconnect();
    o.userId = text(256, 'u');
    expectRejected(o, "user id");
    o.userId = "u";
    o.password = text(1025, 'p');
    expectRejected(o, "password");
}

TEST_F(ConnectSecurityTest, ABinaryPasswordIsSentWholeIncludingNul)
{
    ConnectOptions o = client();
    o.userId = "u";
    o.password = std::string("ab\0cd", 5);
    connectOk(o);
    EXPECT_EQ(5, last().csp.CSPPasswordLength);
    EXPECT_EQ(std::vector<char>({'a', 'b', '\0', 'c', 'd'}), last().password);
}

TEST_F(ConnectSecurityTest, AllOptionsTogether)
{
    ConnectOptions o = tls();
    o.userId = "mqapp";
    o.password = kMarker;
    o.keyRepository = "/k";
    o.sslPeerName = "CN=x";
    o.sslClientValidation = true;
    o.fipsRequired = true;
    o.sslResetCount = 9;
    o.certificateLabel = "l";
    o.securityExit = "x";
    o.securityData = "d";
    o.localAddress = "a";
    Result r = connectOk(o);

    EXPECT_TRUE(r.warnings.empty());
    EXPECT_EQ(MQCNO_VERSION_6, last().cno.Version);
    EXPECT_TRUE(last().hasClientConn && last().hasSslConfig && last().hasSecurityParms);
    EXPECT_TRUE((last().cno.Options & MQCNO_CLIENT_BINDING) != 0);
    EXPECT_TRUE((last().cno.Options & MQCNO_HANDLE_SHARE_NO_BLOCK) != 0);
    EXPECT_EQ("QM1", last().qmName);
}

// ---- passwords stay in memory ----

TEST_F(ConnectSecurityTest, PasswordIsNeverInTheResultOfAFailingConnect)
{
    ConnectOptions o = tls();
    o.userId = "mqapp";
    o.password = kMarker;

    // MQ refuses the connection
    fake.connxRc = MQRC_NOT_AUTHORIZED;
    Result refused = session.connect(o);
    EXPECT_FALSE(refused.ok());
    EXPECT_EQ(1, fake.connxCalls);
    EXPECT_FALSE(leaks(refused, kMarker));
    fake.connxRc = MQRC_NONE;

    // MQ connects with a warning
    fake.connxRc = MQRC_ALREADY_CONNECTED;
    fake.connxCc = MQCC_WARNING;
    Result warned = session.connect(o);
    EXPECT_FALSE(leaks(warned, kMarker));
    session.disconnect();
    fake.connxRc = MQRC_NONE;

    // every rejection before MQ is called
    struct Case { void (*mutate)(ConnectOptions &); };
    const Case cases[] =
    {
        { [](ConnectOptions & x) { x.cipherSpec = std::string(40, 'c'); } },
        { [](ConnectOptions & x) { x.cipherSpec.clear(); } },
        { [](ConnectOptions & x) { x.keyRepository = std::string(300, 'k'); } },
        { [](ConnectOptions & x) { x.sslPeerName = std::string(2000, 'p'); } },
        { [](ConnectOptions & x) { x.sslResetCount = -5; } },
        { [](ConnectOptions & x) { x.securityExit = std::string(200, 'e'); } },
        { [](ConnectOptions & x) { x.localAddress = std::string(60, 'l'); } },
        { [](ConnectOptions & x) { x.userId = std::string(300, 'u'); } },
        { [](ConnectOptions & x) { x.channel = std::string(21, 'c'); } },
        { [](ConnectOptions & x) { x.qmName = std::string(49, 'q'); } },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        ConnectOptions bad = o;
        cases[i].mutate(bad);
        const int before = fake.connxCalls;
        Result r = session.connect(bad);
        EXPECT_FALSE(r.ok()) << "case " << i;
        EXPECT_EQ(before, fake.connxCalls) << "case " << i;
        EXPECT_FALSE(leaks(r, kMarker)) << "case " << i;
    }

    // a password that is itself too long: the message gives lengths, not the value
    ConnectOptions longPassword = o;
    longPassword.password = std::string(1025, 'p') + kMarker;
    Result tooLong = session.connect(longPassword);
    EXPECT_FALSE(tooLong.ok());
    EXPECT_FALSE(leaks(tooLong, kMarker));
    EXPECT_FALSE(contains(tooLong.text, std::string(10, 'p')));

    // already connected
    fake.reset();
    ASSERT_TRUE(session.connect(o).ok());
    Result again = session.connect(o);
    EXPECT_FALSE(again.ok());
    EXPECT_FALSE(leaks(again, kMarker));
}

TEST_F(ConnectSecurityTest, PasswordIsNeverInTheResultWhenBindingsAreMissing)
{
    MqApi api = fakeApi();
    api.bindingsAvailable = false;
    MqSession noBindings(api);
    ConnectOptions o;
    o.mode = Mode::Bindings;
    o.userId = "mqapp";
    o.password = kMarker;
    Result r = noBindings.connect(o);
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(leaks(r, kMarker));

    MqApi incomplete = fakeApi();
    incomplete.cmit = nullptr;
    MqSession broken(incomplete);
    Result b = broken.connect(o);
    EXPECT_FALSE(b.ok());
    EXPECT_FALSE(leaks(b, kMarker));
}

TEST_F(ConnectSecurityTest, PasswordIsNeverTraced)
{
    CapturingTrace trace;
    rfhSetTrace(&trace);

    ConnectOptions o = tls();
    o.userId = "mqapp";
    o.password = kMarker;
    o.keyRepository = "/var/mqm/ssl/key";
    Result ok = session.connect(o);
    session.disconnect();
    fake.connxRc = MQRC_NOT_AUTHORIZED;
    Result refused = session.connect(o);

    rfhSetTrace(nullptr);
    ASSERT_TRUE(ok.ok());
    EXPECT_FALSE(refused.ok());
    ASSERT_FALSE(trace.lines.empty()) << "connect should leave a trace line";
    bool sawPasswordSet = false;
    for (size_t i = 0; i < trace.lines.size(); i++)
    {
        EXPECT_FALSE(contains(trace.lines[i], kMarker)) << "line " << i;
        sawPasswordSet = sawPasswordSet || contains(trace.lines[i], "password=set");
    }
    EXPECT_TRUE(sawPasswordSet);
    // the user id and TLS settings are not secret and are traced, as Windows does (10821, 10945)
    bool sawUser = false;
    for (size_t i = 0; i < trace.lines.size(); i++)
    {
        sawUser = sawUser || contains(trace.lines[i], "mqapp");
    }
    EXPECT_TRUE(sawUser);
}

TEST_F(ConnectSecurityTest, ACcdtUrlWithCredentialsIsNeverTraced)
{
    CapturingTrace trace;
    rfhSetTrace(&trace);

    ConnectOptions o;
    o.mode = Mode::Client;
    o.qmName = "QM1";
    o.ccdtUrl = std::string("https://ccdtuser:") + kMarker + "@host.example/ccdt.json";
    Result r = session.connect(o);

    rfhSetTrace(nullptr);
    ASSERT_TRUE(r.ok()) << r.text;
    ASSERT_FALSE(trace.lines.empty());
    bool sawCcdt = false;
    for (size_t i = 0; i < trace.lines.size(); i++)
    {
        EXPECT_FALSE(contains(trace.lines[i], kMarker)) << "line " << i;
        EXPECT_FALSE(contains(trace.lines[i], "ccdtuser")) << "line " << i;
        sawCcdt = sawCcdt || contains(trace.lines[i], "ccdt=set");
    }
    EXPECT_TRUE(sawCcdt);
    EXPECT_EQ(o.ccdtUrl, last().ccdtUrl);     // MQ still gets the URL as given
}

TEST_F(ConnectSecurityTest, NoCcdtIsTracedAsNotSet)
{
    CapturingTrace trace;
    rfhSetTrace(&trace);
    session.connect(client());
    rfhSetTrace(nullptr);
    bool saw = false;
    for (size_t i = 0; i < trace.lines.size(); i++)
    {
        saw = saw || contains(trace.lines[i], "ccdt=not set");
    }
    EXPECT_TRUE(saw);
}

TEST_F(ConnectSecurityTest, WipingTheScratchBufferZeroesItAndForgetsItsLength)
{
    std::vector<char> scratch(kMarker, kMarker + strlen(kMarker));
    const char * data = scratch.data();
    const size_t size = scratch.size();
    detail::wipePasswordScratch(scratch);
    EXPECT_TRUE(scratch.empty());           // the length of the last password is not observable
    ASSERT_GE(scratch.capacity(), size);    // the storage is kept, and it is zero
    EXPECT_TRUE(std::all_of(data, data + size, [](char c) { return c == '\0'; }));
}

TEST_F(ConnectSecurityTest, TheSessionsPasswordCopyIsZeroedAfterConnect)
{
    ConnectOptions o = client();
    o.userId = "mqapp";
    o.password = kMarker;
    connectOk(o);

    // MQ saw the password during the call ...
    ASSERT_EQ(strlen(kMarker), last().password.size());
    EXPECT_TRUE(last().password == std::vector<char>(kMarker, kMarker + strlen(kMarker)));
    // ... and the buffer it was given is zero now. The session keeps that buffer (zeroed) until it is destroyed,
    // so the address is still valid memory.
    const char * seen = static_cast<const char *>(last().passwordPtr);
    ASSERT_NE(nullptr, seen);
    EXPECT_TRUE(std::all_of(seen, seen + strlen(kMarker), [](char c) { return c == '\0'; }));
    EXPECT_NE(seen, o.password.data());
    // the caller's own string is untouched: it is theirs (see wipePassword)
    EXPECT_EQ(kMarker, o.password);
}

TEST_F(ConnectSecurityTest, TheSessionsPasswordCopyIsZeroedAfterAFailedConnect)
{
    ConnectOptions o = client();
    o.userId = "mqapp";
    o.password = kMarker;
    fake.connxRc = MQRC_NOT_AUTHORIZED;
    EXPECT_FALSE(session.connect(o).ok());

    const char * seen = static_cast<const char *>(last().passwordPtr);
    ASSERT_NE(nullptr, seen);
    EXPECT_TRUE(std::all_of(seen, seen + strlen(kMarker), [](char c) { return c == '\0'; }));
}

TEST_F(ConnectSecurityTest, ALongerPasswordOnTheNextConnectLeavesNoRemnant)
{
    ConnectOptions o = client();
    o.userId = "u";
    o.password = text(100, 'a');
    connectOk(o);
    session.disconnect();
    o.password = "short";
    connectOk(o);
    const char * seen = static_cast<const char *>(last().passwordPtr);
    ASSERT_NE(nullptr, seen);
    EXPECT_EQ(5u, last().password.size());
    // the old, longer contents are gone from the whole buffer the session keeps
    EXPECT_TRUE(std::all_of(seen, seen + 5, [](char c) { return c == '\0'; }));
}

TEST(ConnectOptionsWipe, WipePasswordZeroesTheBytesAndEmptiesTheString)
{
    ConnectOptions o;
    o.password = std::string(40, 'S');     // long enough to live on the heap
    char * bytes = &o.password[0];
    o.wipePassword();

    EXPECT_TRUE(o.password.empty());
    EXPECT_TRUE(std::all_of(bytes, bytes + 40, [](char c) { return c == '\0'; }));
}

TEST(ConnectOptionsWipe, WipePasswordOnAnEmptyPasswordIsHarmless)
{
    ConnectOptions o;
    o.wipePassword();
    EXPECT_TRUE(o.password.empty());
}

// ---- no leaks ----

TEST_F(ConnectSecurityTest, ConnectWithEveryOptionLeavesNothingOpenAfterDisconnect)
{
    {
        MqSession local(fakeApi());
        ConnectOptions o = tls();
        o.userId = "mqapp";
        o.password = kMarker;
        o.securityExit = "x";
        o.localAddress = "a";
        o.certificateLabel = "l";
        ASSERT_TRUE(local.connect(o).ok());
        ASSERT_TRUE(local.disconnect().ok());
    }
    EXPECT_EQ(1, fake.connxCalls);
    EXPECT_EQ(1, fake.discCalls);
    EXPECT_TRUE(fake.liveHandles.empty());
}

TEST_F(ConnectSecurityTest, AFailedValidationLeavesNoConnection)
{
    ConnectOptions o = tls();
    o.cipherSpec = text(33);
    EXPECT_FALSE(session.connect(o).ok());
    EXPECT_EQ(0, fake.connxCalls);
    EXPECT_EQ(0, fake.discCalls);
    EXPECT_FALSE(session.isConnected());
    // and the session still connects afterwards
    ASSERT_TRUE(session.connect(tls()).ok());
    EXPECT_TRUE(session.isConnected());
}
