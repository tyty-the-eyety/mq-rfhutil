// MqmdTabTests.cpp - the MQMD tab (MqmdTab) on a real AppDocument.
//
// Expectations are written by hand from the Windows dialog (RFHUtil/rfhutil.rc IDD_MQMD, MQMDPAGE.cpp:
// OnInitDialog 227-300, OnSetActive 396-455, OnSetfocusMsgtype 777, OnSetfocusApplType 995, SetId 1787,
// OnResetIds 506, OnCopyMsgid 472), IdEdit.cpp:150-290 and the inventory 2.2, 4.1, 4.2.

#include <QtTest>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>

#include <cstring>
#include <string>

#include <QClipboard>
#include <QGuiApplication>

#include "AppDocument.h"
#include "IdEdit.h"
#include "MqmdTab.h"
#include "rfh_mqmd_fields.h"

using namespace rfhqt;

namespace
{
    template <typename T>
    T * need(QWidget & parent, const char * name)
    {
        T * child = parent.findChild<T *>(QString::fromLatin1(name));
        if (child == nullptr)
        {
            printf("missing widget %s\n", name);
            fflush(stdout);
        }
        return child;
    }

    // every member of MqmdFields, compared one by one so that a failure names the member
    QStringList differences(const rfh::MqmdFields & a, const rfh::MqmdFields & b)
    {
        QStringList d;
#define TEXT(m) if (a.m != b.m) d << QStringLiteral(#m " [%1] != [%2]").arg(QString::fromStdString(a.m), QString::fromStdString(b.m))
#define NUM(m) if (a.m != b.m) d << QStringLiteral(#m " %1 != %2").arg(a.m).arg(b.m)
        TEXT(format); TEXT(ccsid); TEXT(msgType); TEXT(expiry); TEXT(feedback); TEXT(priority); TEXT(backoutCount);
        TEXT(replyToQ); TEXT(replyToQMgr); TEXT(userId); TEXT(accountingToken); TEXT(applIdentity); TEXT(applOrigin);
        TEXT(putApplName); TEXT(putApplType); TEXT(dateTime); TEXT(seqNo); TEXT(offset); TEXT(originalLength);
        NUM(persistence); NUM(encoding); NUM(pdEncoding); NUM(floatEncoding);
        NUM(reportException); NUM(reportExpiration); NUM(reportCoa); NUM(reportCod);
        NUM(reportPan); NUM(reportNan); NUM(reportActivity); NUM(reportDiscard); NUM(reportPassMsgId);
        NUM(reportPassCorrelId); NUM(reportPassDiscardAndExpiry); NUM(groupYes); NUM(groupLast);
        NUM(segmentYes); NUM(segmentLast); NUM(segmentAllowed);
#undef TEXT
#undef NUM
        if (memcmp(a.msgId, b.msgId, sizeof(a.msgId)) != 0) d << QStringLiteral("msgId");
        if (memcmp(a.correlId, b.correlId, sizeof(a.correlId)) != 0) d << QStringLiteral("correlId");
        if (memcmp(a.groupId, b.groupId, sizeof(a.groupId)) != 0) d << QStringLiteral("groupId");
        return d;
    }

    void compare(const rfh::MqmdFields & got, const rfh::MqmdFields & want)
    {
        const QStringList d = differences(got, want);
        if (!d.isEmpty())
        {
            printf("MqmdFields differ in: %s\n", qPrintable(d.join(QStringLiteral("; "))));
            fflush(stdout);
        }
        QVERIFY2(d.isEmpty(), qPrintable(d.join(QStringLiteral("; "))));
    }

    rfh::MqmdFields populated()
    {
        rfh::MqmdFields f;
        f.format = "MQSTR";
        f.ccsid = "1208";
        f.msgType = "8 Datagram";
        f.expiry = "600";
        f.feedback = "3";
        f.priority = "5";
        f.backoutCount = "2";
        f.replyToQ = "REPLY.Q";
        f.replyToQMgr = "REPLY.QM";
        f.userId = "tyron";
        f.accountingToken = "0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20";
        f.applIdentity = "IDENTITY";
        f.applOrigin = "ORIG";
        f.putApplName = "putter";
        f.putApplType = "6 Unix";
        f.dateTime = "2026/10/18 12:34:56.78";
        f.seqNo = "7";
        f.offset = "100";
        f.originalLength = "250";
        f.persistence = MQPERSIST_YES;
        f.encoding = NUMERIC_HOST;
        f.pdEncoding = NUMERIC_HOST;
        f.floatEncoding = FLOAT_390;
        f.reportException = REPORT_YES;
        f.reportExpiration = REPORT_DATA;
        f.reportCoa = REPORT_FULL;
        f.reportCod = REPORT_NONE;
        f.reportPan = f.reportNan = f.reportActivity = f.reportDiscard = true;
        f.reportPassMsgId = f.reportPassCorrelId = f.reportPassDiscardAndExpiry = true;
        f.groupYes = f.groupLast = true;
        f.segmentYes = f.segmentLast = f.segmentAllowed = true;
        for (int i = 0; i < 24; i++)
        {
            f.msgId[i] = (MQBYTE)(0x10 + i);
            f.correlId[i] = (MQBYTE)(0x40 + i);
            f.groupId[i] = (MQBYTE)(0xE0 + i);
        }
        return f;
    }

    class Harness
    {
    public:
        Harness() : tab(doc)
        {
            tab.resize(1000, 700);
            tab.show();
        }

        QLineEdit * edit(const char * name) { return need<QLineEdit>(tab, name); }
        QComboBox * combo(const char * name) { return need<QComboBox>(tab, name); }
        QAbstractButton * button(const char * name) { return need<QAbstractButton>(tab, name); }
        QButtonGroup * group(const char * name) { return need<QButtonGroup>(tab, name); }

        // types into a line edit from the start, as the keyboard does
        void typeAtStart(QLineEdit * e, const QString & text)
        {
            e->setCursorPosition(0);
            QTest::keyClicks(e, text);
        }

        void showMode(const char * radio)
        {
            QAbstractButton * b = button(radio);
            QVERIFY(b != nullptr);
            b->click();
        }

        AppDocument doc;
        MqmdTab tab;
    };
}

class MqmdTabTests : public QObject
{
    Q_OBJECT

private slots:
    void roundTripEveryMemberOfMqmdFields()
    {
        Harness h;
        const rfh::MqmdFields want = populated();
        h.tab.setFields(want);
        compare(h.tab.fields(), want);
    }

    void roundTripSurvivesEveryIdDisplay()
    {
        Harness h;
        const rfh::MqmdFields want = populated();
        h.tab.setFields(want);
        for (const char * mode : { "dispAsciiRadio", "dispEbcdicRadio", "dispHexRadio" })
        {
            h.showMode(mode);
            compare(h.tab.fields(), want);
        }
    }

    void defaultsEqualClear()
    {
        Harness h;
        rfh::MqmdFields want;
        rfh::mqmd_fields::clear(want);
        // the fresh tab shows MqmdFields' defaults; Float Fmt is left as clear leaves it
        compare(h.tab.fields(), want);

        rfh::MqmdFields set = populated();
        h.tab.setFields(set);
        rfh::mqmd_fields::clear(set);
        h.tab.setFields(set);
        compare(h.tab.fields(), set);
    }

    void reportRadiosRoundTripNoSelection()
    {
        Harness h;
        const char * groups[] = { "reportExceptGroup", "reportExpireGroup", "reportCoaGroup", "reportCodGroup" };
        for (const char * name : groups)
        {
            QButtonGroup * g = h.group(name);
            QVERIFY2(g != nullptr, name);
            QCOMPARE(g->buttons().size(), 4);
            QCOMPARE(g->checkedId(), -1);        // no forced default
        }

        rfh::MqmdFields f;
        f.reportException = REPORT_NONE;
        f.reportExpiration = REPORT_YES;
        f.reportCoa = REPORT_DATA;
        f.reportCod = REPORT_FULL;
        h.tab.setFields(f);
        QCOMPARE(h.group("reportExceptGroup")->checkedId(), (int)REPORT_NONE);
        QCOMPARE(h.group("reportExpireGroup")->checkedId(), (int)REPORT_YES);
        QCOMPARE(h.group("reportCoaGroup")->checkedId(), (int)REPORT_DATA);
        QCOMPARE(h.group("reportCodGroup")->checkedId(), (int)REPORT_FULL);
        compare(h.tab.fields(), f);

        f.reportException = f.reportExpiration = f.reportCoa = f.reportCod = -1;
        h.tab.setFields(f);
        for (const char * name : groups)
        {
            QCOMPARE(h.group(name)->checkedId(), -1);
        }
        compare(h.tab.fields(), f);
    }

    void reportButtonIdsFollowReportConstants()
    {
        Harness h;
        const char * names[][4] = {
            { "reportExceptNoneRadio", "reportExceptYesRadio", "reportExceptDataRadio", "reportExceptFullRadio" },
            { "reportExpireNoneRadio", "reportExpireYesRadio", "reportExpireDataRadio", "reportExpireFullRadio" },
            { "reportCoaNoneRadio", "reportCoaYesRadio", "reportCoaDataRadio", "reportCoaFullRadio" },
            { "reportCodNoneRadio", "reportCodYesRadio", "reportCodDataRadio", "reportCodFullRadio" } };
        const char * groups[] = { "reportExceptGroup", "reportExpireGroup", "reportCoaGroup", "reportCodGroup" };
        const int values[] = { REPORT_NONE, REPORT_YES, REPORT_DATA, REPORT_FULL };
        for (int g = 0; g < 4; g++)
        {
            for (int b = 0; b < 4; b++)
            {
                QAbstractButton * button = h.button(names[g][b]);
                QVERIFY2(button != nullptr, names[g][b]);
                QCOMPARE(h.group(groups[g])->id(button), values[b]);
            }
        }
    }

    void clickingAReportRadioEditsTheField()
    {
        Harness h;
        QSignalSpy edited(&h.tab, &MqmdTab::fieldsEdited);
        h.button("reportCoaFullRadio")->click();
        QCOMPARE(h.tab.fields().reportCoa, (int)REPORT_FULL);
        QCOMPARE(h.tab.fields().reportCod, -1);
        QVERIFY(edited.count() >= 1);
        QCOMPARE(h.doc.mqmd().reportCoa, (int)REPORT_FULL);
    }

    void formatComboTexts()
    {
        Harness h;
        // MQMDPAGE.cpp:249-267, the 8 character MQ constants
        const QStringList want = {
            QStringLiteral("MQSTR   "), QStringLiteral("MQDEAD  "), QStringLiteral("MQCICS  "), QStringLiteral("MQIMS   "),
            QStringLiteral("MQIMSVS "), QStringLiteral("MQPCF   "), QStringLiteral("MQADMIN "), QStringLiteral("MQCMD1  "),
            QStringLiteral("MQCMD2  "), QStringLiteral("MQHDIST "), QStringLiteral("MQHEPCF "), QStringLiteral("MQEVENT "),
            QStringLiteral("MQHREF  "), QStringLiteral("MQHRF   "), QStringLiteral("MQHRF2  "), QStringLiteral("MQTRIG  "),
            QStringLiteral("MQHWIH  "), QStringLiteral("MQXMIT  "), QStringLiteral("MQCHCOM ") };
        QComboBox * c = h.combo("formatCombo");
        QStringList got;
        for (int i = 0; i < c->count(); i++) got << c->itemText(i);
        QCOMPARE(got, want);
        QVERIFY(c->isEditable());
        QCOMPARE(c->lineEdit()->maxLength(), 8);
    }

    void msgTypeComboTextsAreSortedAsText()
    {
        Harness h;
        // CBS_SORT: 1, 112, 113, 2, 4, 8
        const QStringList want = { QStringLiteral("1 Request"), QStringLiteral("112 FR_MQE"), QStringLiteral("113 MQE"),
                                   QStringLiteral("2 Reply"), QStringLiteral("4 Report"), QStringLiteral("8 Datagram") };
        QComboBox * c = h.combo("msgTypeCombo");
        QStringList got;
        for (int i = 0; i < c->count(); i++) got << c->itemText(i);
        QCOMPARE(got, want);
        QVERIFY(c->isEditable());
        QCOMPARE(c->lineEdit()->maxLength(), 11);        // LimitText(11)

        // the received type shows by core's msgTypeText
        rfh::MqmdFields f;
        f.msgType = rfh::mqmd_fields::msgTypeText(MQMT_REPLY);
        h.tab.setFields(f);
        QCOMPARE(c->currentText(), QStringLiteral("2 Reply"));
        QCOMPARE(h.tab.fields().msgType, std::string("2 Reply"));
    }

    void applTypeComboTexts()
    {
        Harness h;
        // inventory 4.1: the list of OnSetfocusApplType, from getApplTypeDisplay
        const QStringList want = {
            "0 None", "1 CICS", "2 OS/390 or z/OS", "3 IMS", "4 OS/2", "5 PC Dos", "6 Unix", "7 QMGR", "8 OS/400",
            "9 Windows", "10 CICS/VSE", "11 32-bit Windows", "12 VMS", "13 Guardian or Non Stop", "14 VOS",
            "19 IMS Bridge", "20 XCF", "21 CICS Bridge", "22 Notes", "25 User", "26 Broker", "28 Java", "29 DQM",
            "30 Chan Init", "31 WLM", "32 Batch", "33 RRS Batch", "34 SIBus", "35 System" };
        QComboBox * c = h.combo("applTypeCombo");
        QStringList got;
        for (int i = 0; i < c->count(); i++) got << c->itemText(i);
        QCOMPARE(got, want);
        QVERIFY(c->isEditable());
    }

    void controlLimitsAndValues()
    {
        Harness h;
        struct Limit { const char * name; int max; };
        const Limit limits[] = { { "ccsidEdit", 5 }, { "expiryEdit", 9 }, { "origLenEdit", 9 }, { "feedbackEdit", 8 },
                                 { "priorityEdit", 8 }, { "replyQueueEdit", 48 }, { "replyQmEdit", 48 }, { "userIdEdit", 12 },
                                 { "applIdentityEdit", 32 }, { "applOriginEdit", 4 }, { "applNameEdit", 28 },
                                 { "accountTokenEdit", 64 } };
        for (const Limit & l : limits)
        {
            QLineEdit * e = h.edit(l.name);
            QVERIFY2(e != nullptr, l.name);
            QCOMPARE(e->maxLength(), l.max);
        }

        // radio values: the Windows template positions
        QCOMPARE(h.group("persistGroup")->id(h.button("persistAsQueueRadio")), (int)MQPERSIST_AS_Q);
        QCOMPARE(h.group("encodingGroup")->id(h.button("encHostRadio")), (int)NUMERIC_HOST);
        QCOMPARE(h.group("pdEncodingGroup")->id(h.button("pdHostRadio")), (int)NUMERIC_HOST);
        QCOMPARE(h.group("floatGroup")->id(h.button("floatTnsRadio")), (int)FLOAT_TNS);
        QCOMPARE(h.group("idDisplayGroup")->id(h.button("dispHexRadio")), 2);
        QVERIFY(h.button("dispHexRadio")->isChecked());        // Hex is the default display
    }

    void digitsOnlyFields()
    {
        Harness h;
        for (const char * name : { "ccsidEdit", "expiryEdit", "offsetEdit" })
        {
            QLineEdit * e = h.edit(name);
            e->clear();
            QTest::keyClicks(e, QStringLiteral("1a2-3"));
            QCOMPARE(e->text(), QStringLiteral("123"));
        }
        // the other numeric fields accept any characters
        QLineEdit * priority = h.edit("priorityEdit");
        QTest::keyClicks(priority, QStringLiteral("a1"));
        QCOMPARE(priority->text(), QStringLiteral("a1"));
    }

    void hexIdsAcceptOnlyHexAndUpperCase()
    {
        Harness h;
        QLineEdit * correl = h.edit("correlIdEdit");
        QSignalSpy status(&h.tab, &MqmdTab::statusMessage);
        QCOMPARE(correl->text().size(), 48);
        h.typeAtStart(correl, QStringLiteral("ab-g1"));
        QVERIFY(correl->text().startsWith(QStringLiteral("AB1000")));        // overtype, upper case, two rejected
        QCOMPARE(correl->text().size(), 48);
        QCOMPARE(status.count(), 2);
        QVERIFY(!status.at(0).at(0).toString().isEmpty());
        const rfh::MqmdFields f = h.tab.fields();
        QCOMPARE((int)f.correlId[0], 0xAB);
        QCOMPARE((int)f.correlId[1], 0x10);
        QCOMPARE((int)f.correlId[2], 0x00);
    }

    void hexIdsTakeAtMostFortyEightDigits()
    {
        Harness h;
        QLineEdit * correl = h.edit("correlIdEdit");
        QCOMPARE(correl->maxLength(), 48);
        correl->setCursorPosition(48);
        QTest::keyClicks(correl, QStringLiteral("F"));
        QCOMPARE(correl->text().size(), 48);
        h.showMode("dispAsciiRadio");
        QCOMPARE(correl->maxLength(), 24);
        QCOMPARE(h.edit("msgIdEdit")->maxLength(), 24);
        QCOMPARE(h.edit("groupIdEdit")->maxLength(), 24);
        h.showMode("dispEbcdicRadio");
        QCOMPARE(correl->maxLength(), 24);
        h.showMode("dispHexRadio");
        QCOMPARE(correl->maxLength(), 48);
    }

    void insertKeyTogglesOvertype()
    {
        Harness h;
        QLineEdit * correl = h.edit("correlIdEdit");
        h.showMode("dispAsciiRadio");
        correl->clear();
        QTest::keyClicks(correl, QStringLiteral("AB"));
        correl->setCursorPosition(1);
        QTest::keyClick(correl, Qt::Key_Insert);
        QTest::keyClicks(correl, QStringLiteral("x"));
        QCOMPARE(correl->text(), QStringLiteral("AxB"));
        QTest::keyClick(correl, Qt::Key_Insert);
        correl->setCursorPosition(0);
        QTest::keyClicks(correl, QStringLiteral("Z"));
        QCOMPARE(correl->text(), QStringLiteral("ZxB"));
    }

    void idDisplaySwitchRedrawsFromTheBytes()
    {
        Harness h;
        rfh::MqmdFields f;
        const char * text = "ABC";
        memcpy(f.msgId, text, 3);
        f.msgId[3] = 0x02;        // shown as a blank
        f.correlId[0] = 0xC1;     // EBCDIC 'A'
        f.groupId[0] = 'G';
        h.tab.setFields(f);
        QCOMPARE(h.edit("msgIdEdit")->text(), QStringLiteral("41424302") + QString(40, QLatin1Char('0')));
        h.showMode("dispAsciiRadio");
        QCOMPARE(h.edit("msgIdEdit")->text(), QStringLiteral("ABC") + QString(21, QLatin1Char(' ')));
        QCOMPARE(h.edit("groupIdEdit")->text().left(2), QStringLiteral("G "));
        h.showMode("dispEbcdicRadio");
        QCOMPARE(h.edit("correlIdEdit")->text().left(1), QStringLiteral("A"));
        QCOMPARE(h.edit("correlIdEdit")->text().size(), 24);
        compare(h.tab.fields(), f);        // redrawing changes no byte
    }

    void theMsgIdReachesTheDocumentOnEveryChange()
    {
        // Operations reads doc.mqmd() and never asks the tab: the document must not wait for the box to lose focus
        Harness h;
        QLineEdit * msg = h.edit("msgIdEdit");
        msg->setFocus();
        h.typeAtStart(msg, QStringLiteral("4142"));
        QVERIFY(msg->hasFocus() || !h.tab.isActiveWindow());        // still in the box, as with a shortcut flow
        QCOMPARE((int)h.doc.mqmd().msgId[0], 0x41);
        QCOMPARE((int)h.doc.mqmd().msgId[1], 0x42);

        // what a put would send, built from the document alone
        MQMD2 mqmd = { MQMD2_DEFAULT };
        rfh::PutContext context;
        rfh::mqmd_fields::toPutMqmd(h.doc.mqmd(), context, mqmd);
        QCOMPARE((int)mqmd.MsgId[0], 0x41);
        QCOMPARE((int)mqmd.MsgId[1], 0x42);

        h.showMode("dispAsciiRadio");
        QCOMPARE(msg->text().left(3), QStringLiteral("AB "));
        QCOMPARE((int)h.tab.fields().msgId[1], 0x42);
    }

    void fieldsAloneCommitsAnEditThatNoSignalAnnounced()
    {
        Harness h;
        h.edit("msgIdEdit")->setText(QStringLiteral("AB") + QString(46, QLatin1Char('0')));        // setText sends no textEdited
        QCOMPARE((int)h.doc.mqmd().msgId[0], 0);
        QCOMPARE((int)h.tab.fields().msgId[0], 0xAB);
        QCOMPARE((int)h.tab.fields().msgId[0], 0xAB);        // asking does not change the tab
    }

    void correlAndGroupReachTheDocumentOnEveryChange()
    {
        Harness h;
        h.typeAtStart(h.edit("correlIdEdit"), QStringLiteral("C1"));
        h.typeAtStart(h.edit("groupIdEdit"), QStringLiteral("D2"));
        QCOMPARE((int)h.doc.mqmd().correlId[0], 0xC1);
        QCOMPARE((int)h.doc.mqmd().groupId[0], 0xD2);
    }

    void hexModeHoldsEveryInputPathToTheHexRule()
    {
        Harness h;
        IdEdit * correl = need<IdEdit>(h.tab, "correlIdEdit");
        correl->clear();
        correl->insert(QStringLiteral("zz"));
        QCOMPARE(correl->text(), QString());        // refused whole, as a validator does
        correl->insert(QStringLiteral("a1z"));
        QCOMPARE(correl->text(), QString());
        correl->insert(QStringLiteral("ab1"));        // lower case is accepted and upper-cased
        QCOMPARE(correl->text(), QStringLiteral("AB1"));
        h.showMode("dispAsciiRadio");
        correl->clear();
        correl->insert(QStringLiteral("zz"));        // other modes take any text
        QCOMPARE(correl->text(), QStringLiteral("zz"));
    }

    void ctrlVPastesFromTheCaretAndTurnsNonHexIntoZero()
    {
        Harness h;
        QGuiApplication::clipboard()->setText(QStringLiteral("zz12"));
        QLineEdit * correl = h.edit("correlIdEdit");
        h.typeAtStart(correl, QStringLiteral("F"));
        correl->setCursorPosition(2);
        QTest::keyClick(correl, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(correl->text().left(6), QStringLiteral("F00012"));
        QCOMPARE((int)h.doc.mqmd().correlId[0], 0xF0);
        QCOMPARE((int)h.doc.mqmd().correlId[1], 0x00);
        QCOMPARE((int)h.doc.mqmd().correlId[2], 0x12);
    }

    void altJAndAltCPressTheirButtons()
    {
        Harness h;
        h.tab.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&h.tab));
        rfh::MqmdFields f = populated();
        h.tab.setFields(f);
        QTest::keyClick(&h.tab, Qt::Key_C, Qt::AltModifier);
        QVERIFY(memcmp(h.tab.fields().correlId, f.msgId, 24) == 0);
        QTest::keyClick(&h.tab, Qt::Key_J, Qt::AltModifier);
        const MQBYTE24 zero = { 0 };
        QVERIFY(memcmp(h.tab.fields().msgId, zero, 24) == 0);
        QVERIFY(memcmp(h.tab.fields().groupId, zero, 24) == 0);
    }

    void resetIdsRestoresOvertype()
    {
        Harness h;
        IdEdit * correl = need<IdEdit>(h.tab, "correlIdEdit");
        correl->setCursorPosition(0);
        QTest::keyClick(correl, Qt::Key_Insert);
        QVERIFY(!correl->overtype());
        h.button("resetIdsButton")->click();
        for (const char * name : { "msgIdEdit", "correlIdEdit", "groupIdEdit" })
        {
            QVERIFY2(need<IdEdit>(h.tab, name)->overtype(), name);
        }
    }

    void valuesNoButtonStandsForRoundTrip()
    {
        Harness h;
        rfh::MqmdFields f;
        f.persistence = 5;
        f.encoding = 9;
        f.floatEncoding = 7;
        f.reportException = 6;
        f.reportCod = 4;
        h.tab.setFields(f);
        QCOMPARE(h.tab.fields().persistence, 5);
        QCOMPARE(h.tab.fields().encoding, 9);
        QCOMPARE(h.tab.fields().floatEncoding, 7);
        QCOMPARE(h.tab.fields().reportException, 6);
        QCOMPARE(h.tab.fields().reportCod, 4);
        QCOMPARE(h.tab.fields().reportCoa, -1);
        QCOMPARE(h.group("persistGroup")->checkedId(), -1);
        // another change on the tab keeps them
        h.button("panCheck")->click();
        QCOMPARE(h.doc.mqmd().persistence, 5);
        QCOMPARE(h.doc.mqmd().reportException, 6);
    }

    void overLongTextIsCutByTheEditAndNotReportedAsAnEdit()
    {
        // documented choice: the edit limit cuts what is shown, as the Windows DDX does; setFields reports no edit
        Harness h;
        QSignalSpy edited(&h.tab, &MqmdTab::fieldsEdited);
        rfh::MqmdFields f;
        f.userId = "ABCDEFGHIJKLMNOP";        // 16 characters, limit 12
        f.originalLength = "12345678901";
        h.doc.setMqmd(f);
        QCOMPARE(edited.count(), 0);
        QCOMPARE(h.tab.fields().userId, std::string("ABCDEFGHIJKL"));
        QCOMPARE(h.tab.fields().originalLength, std::string("123456789"));
        QCOMPARE(h.doc.mqmd().userId, std::string("ABCDEFGHIJKLMNOP"));        // the document is not rewritten
    }

    void asciiEditsReplaceOnlyChangedCharacters()
    {
        Harness h;
        rfh::MqmdFields f;
        f.correlId[0] = 0x01;        // untouched control bytes must survive
        f.correlId[1] = 'B';
        f.correlId[2] = 'C';
        f.correlId[3] = 0x02;
        h.tab.setFields(f);
        h.showMode("dispAsciiRadio");
        QLineEdit * correl = h.edit("correlIdEdit");
        QCOMPARE(correl->text().left(4), QStringLiteral(" BC "));
        correl->setCursorPosition(1);
        QTest::keyClicks(correl, QStringLiteral("X"));
        const rfh::MqmdFields got = h.tab.fields();
        QCOMPARE((int)got.correlId[0], 0x01);
        QCOMPARE((int)got.correlId[1], (int)'X');
        QCOMPARE((int)got.correlId[2], (int)'C');
        QCOMPARE((int)got.correlId[3], 0x02);
    }

    void ebcdicEditsAreTranslated()
    {
        Harness h;
        h.showMode("dispEbcdicRadio");
        QLineEdit * correl = h.edit("correlIdEdit");
        h.typeAtStart(correl, QStringLiteral("A"));
        QCOMPARE((int)h.tab.fields().correlId[0], 0xC1);
    }

    void groupIdIsTakenInTheModeItWasShownIn()
    {
        Harness h;
        QLineEdit * group = h.edit("groupIdEdit");
        h.typeAtStart(group, QStringLiteral("41"));
        QCOMPARE((int)h.tab.fields().groupId[0], 0x41);
        h.showMode("dispAsciiRadio");        // the bytes must not be re-read from the new mode
        QCOMPARE((int)h.tab.fields().groupId[0], 0x41);
        QCOMPARE(group->text().left(1), QStringLiteral("A"));
    }

    void resetIdsZeroesTheThree()
    {
        Harness h;
        h.tab.setFields(populated());
        h.button("resetIdsButton")->click();
        const rfh::MqmdFields f = h.tab.fields();
        const MQBYTE24 zero = { 0 };
        QVERIFY(memcmp(f.msgId, zero, 24) == 0);
        QVERIFY(memcmp(f.correlId, zero, 24) == 0);
        QVERIFY(memcmp(f.groupId, zero, 24) == 0);
        QCOMPARE(h.edit("msgIdEdit")->text(), QString(48, QLatin1Char('0')));
        QCOMPARE(h.doc.mqmd().msgId[0], (MQBYTE)0);
    }

    void copyMsgIdToCorrelId()
    {
        Harness h;
        const rfh::MqmdFields want = populated();
        h.tab.setFields(want);
        h.button("copyMsgIdButton")->click();
        const rfh::MqmdFields f = h.tab.fields();
        QVERIFY(memcmp(f.correlId, want.msgId, 24) == 0);
        QCOMPARE(h.edit("correlIdEdit")->text(), h.edit("msgIdEdit")->text());
        QVERIFY(memcmp(f.groupId, want.groupId, 24) == 0);
    }

    void copyMsgIdStoresAPendingMsgIdEditFirst()
    {
        Harness h;
        h.typeAtStart(h.edit("msgIdEdit"), QStringLiteral("AA"));
        h.button("copyMsgIdButton")->click();
        QCOMPARE((int)h.tab.fields().correlId[0], 0xAA);
    }

    void aMessageMqmdShowsOnTheTab()
    {
        Harness h;
        MQMD2 received = { MQMD2_DEFAULT };
        received.Version = MQMD_VERSION_2;
        received.MsgType = MQMT_REQUEST;
        received.Persistence = MQPER_PERSISTENT;
        received.CodedCharSetId = 819;
        memcpy(received.Format, MQFMT_STRING, 8);
        memcpy(received.ReplyToQ, "MY.REPLY", 8);
        received.MsgId[0] = 0x77;
        rfh::MqmdFields shown;
        rfh::mqmd_fields::fromGetMqmd(received, shown);

        // Operations puts the tab's values into the document after a get; the tab follows the document
        h.doc.setMqmd(shown);
        compare(h.tab.fields(), shown);
        QCOMPARE(h.edit("ccsidEdit")->text(), QStringLiteral("819"));
        QCOMPARE(h.combo("msgTypeCombo")->currentText(), QStringLiteral("1 Request"));
        QVERIFY(h.button("persistYesRadio")->isChecked());
        QCOMPARE(h.edit("msgIdEdit")->text().left(2), QStringLiteral("77"));

        rfh::Message message;
        message.mqmd = received;
        h.doc.setMessage(std::move(message));
        compare(h.tab.fields(), shown);        // a new message alone leaves the tab values to the document
    }

    void documentChangesDoNotCountAsEdits()
    {
        Harness h;
        QSignalSpy edited(&h.tab, &MqmdTab::fieldsEdited);
        h.doc.setMqmd(populated());
        h.tab.setFields(populated());
        QCOMPARE(edited.count(), 0);
    }

    void editingEmitsFieldsEditedAndKeepsTheDocumentCurrent()
    {
        Harness h;
        QSignalSpy edited(&h.tab, &MqmdTab::fieldsEdited);
        QTest::keyClicks(h.edit("priorityEdit"), QStringLiteral("7"));
        QVERIFY(edited.count() >= 1);
        QCOMPARE(h.doc.mqmd().priority, std::string("7"));
        edited.clear();
        h.combo("formatCombo")->setCurrentIndex(5);
        QVERIFY(edited.count() >= 1);
        QCOMPARE(h.doc.mqmd().format, std::string("MQPCF   "));
        edited.clear();
        h.button("groupYesCheck")->click();
        QVERIFY(edited.count() >= 1);
        QVERIFY(h.doc.mqmd().groupYes);
        edited.clear();
        h.showMode("dispAsciiRadio");        // the display mode is not a field
        QCOMPARE(edited.count(), 0);
    }

    void aTypedMsgTypeIsKept()
    {
        Harness h;
        h.combo("msgTypeCombo")->setEditText(QStringLiteral("65"));
        QCOMPARE(h.tab.fields().msgType, std::string("65"));
    }

    void readOnlyRulesFollowTheContextOptions()
    {
        Harness h;
        auto ro = [&](const char * name) { return h.edit(name)->isReadOnly(); };
        // MQMDPAGE.cpp:396-455
        QVERIFY(ro("backoutEdit"));
        QVERIFY(!ro("msgIdEdit"));
        QVERIFY(ro("userIdEdit"));
        QVERIFY(ro("accountTokenEdit"));
        QVERIFY(ro("applIdentityEdit"));
        QVERIFY(ro("applOriginEdit"));
        QVERIFY(ro("applNameEdit"));
        QVERIFY(ro("dateTimeEdit"));
        QVERIFY(!h.combo("applTypeCombo")->isEnabled());

        ShellState s = h.doc.shell();
        s.setIdentity = true;
        h.doc.setShell(s);
        QVERIFY(!ro("userIdEdit"));
        QVERIFY(!ro("accountTokenEdit"));
        QVERIFY(ro("applIdentityEdit"));
        QVERIFY(ro("dateTimeEdit"));
        QVERIFY(!h.combo("applTypeCombo")->isEnabled());

        s = ShellState();
        s.alternateUser = true;
        h.doc.setShell(s);
        QVERIFY(!ro("userIdEdit"));
        QVERIFY(ro("applNameEdit"));

        s = ShellState();
        s.setAll = true;
        h.doc.setShell(s);
        QVERIFY(!ro("userIdEdit"));
        QVERIFY(!ro("accountTokenEdit"));
        QVERIFY(!ro("applIdentityEdit"));
        QVERIFY(!ro("applOriginEdit"));
        QVERIFY(!ro("applNameEdit"));
        QVERIFY(!ro("dateTimeEdit"));
        QVERIFY(h.combo("applTypeCombo")->isEnabled());
        QVERIFY(ro("backoutEdit"));

        h.doc.setShell(ShellState());
        QVERIFY(ro("userIdEdit"));
        QVERIFY(!h.combo("applTypeCombo")->isEnabled());
    }

    void readOnlyFieldsStillShowReceivedValues()
    {
        Harness h;
        h.tab.setFields(populated());
        QCOMPARE(h.edit("userIdEdit")->text(), QStringLiteral("tyron"));
        QCOMPARE(h.edit("dateTimeEdit")->text(), QStringLiteral("2026/10/18 12:34:56.78"));
        QCOMPARE(h.combo("applTypeCombo")->currentText(), QStringLiteral("6 Unix"));
    }

    void formatComboTakesFocus()
    {
        // WM_SETPAGEFOCUS puts the focus on the Format combo (MQMDPAGE.cpp:452)
        Harness h;
        h.tab.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&h.tab));
        h.tab.focusFirst();
        QVERIFY(h.combo("formatCombo")->lineEdit()->hasFocus() || h.combo("formatCombo")->hasFocus());
    }

    void backoutCountCannotBeEdited()
    {
        Harness h;
        QLineEdit * e = h.edit("backoutEdit");
        e->setText(QStringLiteral("0"));
        QTest::keyClicks(e, QStringLiteral("9"));
        QCOMPARE(e->text(), QStringLiteral("0"));
    }
};

QTEST_MAIN(MqmdTabTests)
#include "MqmdTabTests.moc"
