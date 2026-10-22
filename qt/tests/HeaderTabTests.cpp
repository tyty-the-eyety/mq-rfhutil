// HeaderTabTests.cpp - AppDocument header parsing and the setHeaders() roundtrip.
//
// Tests the path wired in Commit 9: parseHeaders() called from setMessage();
// setHeaders() rebuilds msg.headers via buildHeaders(), marks dirty, and emits
// messageChanged(). Also verifies the clearData() / clearAll() header semantics.
//
// The golden-file tests (guarded by RFH_GOLDEN_DIR) read real binary files through
// rfh::fileio::read(), set them on AppDocument, and check the parsed field values.

#include <QtTest>
#include <QString>

#include "AppDocument.h"
#include "rfh_parsed_headers.h"
#include "rfh_message_file.h"
#include "rfh_mqmd_fields.h"

using namespace rfhqt;

namespace
{
    // Build a message whose header bytes are an MQRFH2 with a <mcd><Msd>xml</Msd></mcd>
    // folder and, optionally, a <usr><k>v</k></usr> folder.
    rfh::Message rfh2Message(bool withUsr = false)
    {
        rfh::ParsedHeaders ph;
        ph.rfh2.included      = true;
        ph.rfh2.encoding      = 0;      // NUMERIC_PC
        ph.rfh2.ccsid         = 1208;
        ph.rfh2.nameValueCcsid = 1208;
        ph.rfh2.format        = "MQSTR";
        ph.rfh2.hasMcd        = true;
        ph.rfh2.mcd.msgDomain = "xml";
        if (withUsr)
        {
            ph.rfh2.hasUsr  = true;
            ph.rfh2.usrXml  = "<usr><k>v</k></usr>";  // full folder XML including root element
        }

        rfh::Message m;
        m.headers              = rfh::buildHeaders(ph);
        m.body                 = {'<', 'a', '>', 'h', 'i', '<', '/', 'a', '>'};
        m.mqmd.Encoding        = 546;   // MQENC_INTEGER_REVERSED set → initialEncoding = 0 (NUMERIC_PC)
        m.mqmd.CodedCharSetId  = 1208;
        return m;
    }

#ifdef RFH_GOLDEN_DIR
    QString goldenPath(const char * name)
    {
        return QString(RFH_GOLDEN_DIR) + "/" + name;
    }
#endif
}

class HeaderTabTests : public QObject
{
    Q_OBJECT

private slots:
    // ---- header parsing from setMessage ---------------------------------------------------------

    void emptyMessageHasNoHeaders()
    {
        AppDocument doc;
        rfh::Message m;
        m.body = {'h', 'i'};
        doc.setMessage(std::move(m), "test");
        QVERIFY(!doc.headers().hasAny());
        QVERIFY(doc.headers().warnings.empty());
    }

    void rfh2McdFolderIsParsedonSetMessage()
    {
        AppDocument doc;
        doc.setMessage(rfh2Message(), "test");
        QVERIFY(doc.headers().rfh2.included);
        QVERIFY(doc.headers().rfh2.hasMcd);
        QCOMPARE(doc.headers().rfh2.mcd.msgDomain, std::string("xml"));
        QVERIFY(!doc.headers().rfh2.hasUsr);
        QVERIFY(!doc.headers().dlq.included);
        QVERIFY(!doc.headers().cics.included);
        QVERIFY(!doc.headers().ims.included);
    }

    void rfh2UsrFolderIsParsedonSetMessage()
    {
        AppDocument doc;
        doc.setMessage(rfh2Message(/*withUsr=*/true), "test");
        QVERIFY(doc.headers().rfh2.included);
        QVERIFY(doc.headers().rfh2.hasUsr);
        const std::string & usr = doc.headers().rfh2.usrXml;
        QVERIFY2(usr.find("<k>") != std::string::npos, usr.c_str());
    }

    void emptyHeaderBytesLeaveNoHeaders()
    {
        AppDocument doc;
        rfh::Message m;
        m.headers.clear();
        m.body.assign(10, 'x');
        doc.setMessage(std::move(m), "test");
        QVERIFY(!doc.headers().hasAny());
    }

    // ---- setHeaders() signals and effects -------------------------------------------------------

    void setHeadersEmitsMessageChangedAndMarksDirty()
    {
        AppDocument doc;
        doc.setMessage(rfh2Message(), "test", /*dirty=*/false);
        QVERIFY(!doc.dirty());

        QSignalSpy msg(&doc, &AppDocument::messageChanged);

        rfh::ParsedHeaders ph = doc.headers();
        ph.rfh2.mcd.msgDomain = "jms";
        doc.setHeaders(std::move(ph));

        QCOMPARE(msg.count(), 1);
        QVERIFY(doc.dirty());
        QCOMPARE(doc.headers().rfh2.mcd.msgDomain, std::string("jms"));
    }

    void setHeadersRebuildsRawBytesAsRfh2()
    {
        AppDocument doc;
        rfh::ParsedHeaders ph;
        ph.rfh2.included       = true;
        ph.rfh2.encoding       = 0;
        ph.rfh2.ccsid          = 1208;
        ph.rfh2.nameValueCcsid = 1208;
        ph.rfh2.format         = "MQSTR";
        doc.setMessage(rfh::Message(), "test");
        doc.setHeaders(std::move(ph));

        const auto & hdr = doc.message().headers;
        QVERIFY(!hdr.empty());
        // MQRFH2 struct id is "RFH "
        QCOMPARE(hdr.at(0), (unsigned char)'R');
        QCOMPARE(hdr.at(1), (unsigned char)'F');
        QCOMPARE(hdr.at(2), (unsigned char)'H');
        QCOMPARE(hdr.at(3), (unsigned char)' ');
    }

    void setHeadersRoundTrips()
    {
        // Build headers, put them in a message, set on AppDocument.
        // Calling setHeaders(doc.headers()) must reproduce the original bytes.
        rfh::ParsedHeaders ph;
        ph.rfh2.included       = true;
        ph.rfh2.encoding       = 0;
        ph.rfh2.ccsid          = 1208;
        ph.rfh2.nameValueCcsid = 1208;
        ph.rfh2.format         = "MQSTR";
        ph.rfh2.hasMcd         = true;
        ph.rfh2.mcd.msgDomain  = "xml";
        ph.rfh2.hasUsr         = true;
        ph.rfh2.usrXml         = "<usr><k>v</k></usr>";

        rfh::Message m;
        m.headers             = rfh::buildHeaders(ph);
        m.mqmd.Encoding       = 546;
        m.mqmd.CodedCharSetId = 1208;
        const auto firstBytes = m.headers;

        AppDocument doc;
        doc.setMessage(std::move(m), "test");
        doc.setHeaders(doc.headers());

        QCOMPARE(doc.message().headers, firstBytes);
    }

    void setHeadersWithNothingIncludedClearsRawBytes()
    {
        // buildHeaders() returns empty when !hasAny(): clearing the tabs clears the raw bytes too.
        AppDocument doc;
        doc.setMessage(rfh2Message(), "test");
        QVERIFY(!doc.message().headers.empty());

        doc.setHeaders(rfh::ParsedHeaders{});
        QVERIFY(doc.message().headers.empty());
        QVERIFY(!doc.headers().hasAny());
    }

    // ---- clearData / clearAll -------------------------------------------------------------------

    void clearDataPreservesHeadersAndTheirParsedForm()
    {
        // Windows resetDataArea keeps the header tabs; clearData must not clear parsedHeaders.
        AppDocument doc;
        doc.setMessage(rfh2Message(), "test");
        QVERIFY(doc.headers().rfh2.included);

        doc.clearData();

        QVERIFY(doc.headers().rfh2.included);
        QVERIFY(!doc.message().headers.empty());
    }

    void clearAllResetsHeadersAndRawBytes()
    {
        AppDocument doc;
        doc.setMessage(rfh2Message(), "test");
        QVERIFY(doc.headers().hasAny());

        doc.clearAll();

        QVERIFY(!doc.headers().hasAny());
        QVERIFY(doc.message().headers.empty());
    }

    // ---- golden file parsing --------------------------------------------------------------------
    // These tests require RFH_GOLDEN_DIR to be set (see qt/tests/CMakeLists.txt).
    // They read real binary files produced by make_goldens and check the parsed header fields.
#ifdef RFH_GOLDEN_DIR

    void goldenRfh2ParsesCorrectly()
    {
        // mqmd-rfh2-text.bin: MQMD + RFH2 with <mcd><Msd>xml</Msd></mcd> and <usr><k>v</k></usr> + "<a>hi</a>"
        rfh::fileio::ReadOptions opts;
        rfh::MqmdFields fields;
        rfh::fileio::ReadResult result;
        const auto r = rfh::fileio::read(goldenPath("mqmd-rfh2-text.bin").toStdString(), opts, fields, result);
        QVERIFY2(r.ok(), r.text.c_str());

        AppDocument doc;
        doc.setMessage(std::move(result.message), result.dataSource);
        QVERIFY(doc.headers().rfh2.included);
        QVERIFY(doc.headers().rfh2.hasMcd);
        QCOMPARE(doc.headers().rfh2.mcd.msgDomain, std::string("xml"));
        QVERIFY(doc.headers().rfh2.hasUsr);
        QVERIFY2(doc.headers().rfh2.usrXml.find("<k>") != std::string::npos,
                 doc.headers().rfh2.usrXml.c_str());
        QVERIFY(!doc.headers().dlq.included);
        QVERIFY(doc.headers().warnings.empty());
    }

    void goldenDlhRfh2ParsesCorrectly()
    {
        // mqmd-dlh-rfh2-text.bin: MQMD + DLH (reason 2085, destQ ORIG.Q, destQM QM1) + RFH2 + body
        rfh::fileio::ReadOptions opts;
        rfh::MqmdFields fields;
        rfh::fileio::ReadResult result;
        const auto r = rfh::fileio::read(goldenPath("mqmd-dlh-rfh2-text.bin").toStdString(), opts, fields, result);
        QVERIFY2(r.ok(), r.text.c_str());

        AppDocument doc;
        doc.setMessage(std::move(result.message), result.dataSource);
        QVERIFY(doc.headers().dlq.included);
        QCOMPARE(doc.headers().dlq.reason, 2085);
        QCOMPARE(doc.headers().dlq.destQueueName, std::string("ORIG.Q"));
        QCOMPARE(doc.headers().dlq.destQueueManagerName, std::string("QM1"));
        QVERIFY(doc.headers().rfh2.included);
    }

    void goldenRfh2OnlyParsesCorrectly()
    {
        // rfh2-only.bin: no MQMD, just RFH2 + "Body after a leading RFH2, no MQMD."
        rfh::fileio::ReadOptions opts;
        rfh::MqmdFields fields;
        rfh::fileio::ReadResult result;
        const auto r = rfh::fileio::read(goldenPath("rfh2-only.bin").toStdString(), opts, fields, result);
        QVERIFY2(r.ok(), r.text.c_str());
        QVERIFY(!result.hadMqmd);

        AppDocument doc;
        doc.setMessage(std::move(result.message), result.dataSource);
        QVERIFY(doc.headers().rfh2.included);
        QVERIFY(!doc.headers().dlq.included);
        QVERIFY(!doc.headers().cics.included);
    }

#endif // RFH_GOLDEN_DIR
};

QTEST_MAIN(HeaderTabTests)
#include "HeaderTabTests.moc"
