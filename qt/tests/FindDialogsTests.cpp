// FindDialogsTests.cpp - Find, Find Hex and Go To (FindDialogs.h) and the search on the Data tab (DataTab).
//
// Expectations come from the Windows source: rfhutilView.cpp findHelper 1905 and hexFindHelper 2069, DataArea.cpp
// findNext 12649, HexFind.cpp hexDatavalid 236 and OnOK 111, Goto.cpp OnOK 100, MSGDATA.cpp moveDisplay 1142, and the
// inventory 5.5. The four Windows defects the owner decided to fix are pinned as fixed: a match ending at the end of
// the data is found, Up starts at the last possible match, and a hex search is not translated to EBCDIC.
// A hit is checked by the text it selects (the character, or the two hex digits), not only by format::locate.

#include <QtTest>

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QTabWidget>

#include <memory>
#include <string>

#include "AppDocument.h"
#include "comsubs.h"
#include "DataTab.h"
#include "FindDialogs.h"
#include "Operations.h"
#include "SessionController.h"
#include "StatusLog.h"
#include "fake_mq.h"
#include "rfh_format.h"

using namespace rfhqt;
using rfh::format::View;

namespace
{
    class ModalGuard
    {
    public:
        ModalGuard()
        {
            QObject::connect(&timer, &QTimer::timeout, [this]()
            {
                if (QWidget * w = QApplication::activeModalWidget())
                {
                    unexpected++;
                    w->close();
                }
            });
            timer.start(20);
        }
        QTimer timer;
        int unexpected = 0;
    };

    struct Harness
    {
        Harness() : controller(rfh_test::fakeApi()), ops(doc, controller, log)
        {
            tab.reset(new DataTab(doc, ops));
            ShellState s = doc.shell();
            s.tab = Tab::Data;
            doc.setShell(s);
            host.addTab(tab.get(), "Data");
            host.addTab(&other, "Other");
            host.resize(1000, 600);
            host.show();
            QVERIFY(QTest::qWaitForWindowActive(&host));
        }

        ~Harness()
        {
            host.removeTab(0);
            tab->setParent(nullptr);
        }

        void setBody(const std::string & body)
        {
            rfh::Message m;
            m.body.assign(body.begin(), body.end());
            doc.setMessage(std::move(m), "Message Data", false);
        }

        void useView(View v)
        {
            ShellState s = doc.shell();
            s.view = v;
            doc.setShell(s);
        }

        void useChars(int charFormat)
        {
            rfh::format::RenderOptions o = doc.render();
            o.charFormat = charFormat;
            doc.setRender(o);
        }

        QPlainTextEdit * edit() const { return tab->findChild<QPlainTextEdit *>("dataEdit"); }
        FindDialog * findDialog() const { return tab->findChild<FindDialog *>("findDialog"); }
        HexFindDialog * hexDialog() const { return tab->findChild<HexFindDialog *>("hexFindDialog"); }

        // opens Find with Ctrl+F and returns it
        FindDialog * openFind()
        {
            QTest::keyClick(&host, Qt::Key_F, Qt::ControlModifier);
            return findDialog();
        }

        // one Find Next with `text` in the given direction
        void findNext(const QString & text, bool down = true)
        {
            FindDialog * d = findDialog();
            if (d == nullptr)
            {
                d = openFind();
            }
            QVERIFY(d != nullptr);
            d->findChild<QLineEdit *>("findEdit")->setText(text);
            (down ? d->findChild<QRadioButton *>("downRadio") : d->findChild<QRadioButton *>("upRadio"))->setChecked(true);
            d->findChild<QPushButton *>("findNextButton")->click();
        }

        QString selected() const { return edit()->textCursor().selectedText(); }
        int block() const { return edit()->textCursor().blockNumber(); }

        // the position of the selection start, as (block, column)
        QPair<int, int> start() const
        {
            QTextCursor c = edit()->textCursor();
            c.setPosition(c.selectionStart());
            return qMakePair(c.blockNumber(), c.positionInBlock());
        }

        QTabWidget host;
        QWidget other;
        AppDocument doc;
        StatusLog log;
        SessionController controller;
        Operations ops;
        std::unique_ptr<DataTab> tab;
    };

    // 100 bytes, an "AB" at 5, 40 and 95, 'x' elsewhere
    std::string threeHits()
    {
        std::string s(100, 'x');
        for (int at : { 5, 40, 95 })
        {
            s[at] = 'A';
            s[at + 1] = 'B';
        }
        return s;
    }
}

class FindDialogsTests : public QObject
{
    Q_OBJECT

private slots:
    // ---- the dialogs by themselves ---------------------------------------------------------------------------------

    void findDialogHasTheWindowsControls()
    {
        FindDialog d;
        QVERIFY(d.findChild<QLineEdit *>("findEdit") != nullptr);
        QVERIFY(d.findChild<QRadioButton *>("upRadio") != nullptr);
        QVERIFY(d.findChild<QRadioButton *>("downRadio")->isChecked());
        QCOMPARE(d.findChild<QPushButton *>("findNextButton")->text(), QString("&Find Next"));
        QCOMPARE(d.findChild<QPushButton *>("cancelButton")->text(), QString("Cancel"));
        QVERIFY(!d.isModal());
        QVERIFY(d.findChildren<QCheckBox *>().isEmpty());		// no match case, no whole word
        QVERIFY(!d.findChild<QPushButton *>("findNextButton")->isEnabled());		// nothing to find
        d.findChild<QLineEdit *>("findEdit")->setText("a");
        QVERIFY(d.findChild<QPushButton *>("findNextButton")->isEnabled());
        QVERIFY(d.windowTitle().contains("Find"));
    }

    void hexFindValidation()
    {
        HexFindDialog d;
        QCOMPARE(d.windowTitle(), QString("Hex Finder"));
        QLineEdit * edit = d.findChild<QLineEdit *>("hexEdit");
        QLabel * error = d.findChild<QLabel *>("errorLabel");
        QPushButton * find = d.findChild<QPushButton *>("findButton");
        QSignalSpy requests(&d, &HexFindDialog::findRequested);
        QVERIFY(find->isDefault());
        QVERIFY(d.findChild<QRadioButton *>("downRadio")->isChecked());

        find->click();
        QCOMPARE(error->text(), QString("No data entered"));
        QCOMPARE(requests.count(), 0);
        edit->setText("ABC");
        find->click();
        QCOMPARE(error->text(), QString("Odd number of characters"));
        edit->setText("G");			// odd is checked before the characters
        find->click();
        QCOMPARE(error->text(), QString("Odd number of characters"));
        edit->setText("AG");
        find->click();
        QCOMPARE(error->text(), QString("Invalid hex character(s)"));
        edit->setText("4 ");
        find->click();
        QCOMPARE(error->text(), QString("Invalid hex character(s)"));
        QCOMPARE(requests.count(), 0);
        edit->setText("4142");
        find->click();
        QVERIFY(error->text().isEmpty());		// the previous message is reset
        QCOMPARE(requests.count(), 1);
        QCOMPARE(d.bytes(), QByteArray("AB"));
        QVERIFY(d.searchDown());
        d.findChild<QRadioButton *>("upRadio")->setChecked(true);
        QVERIFY(!d.searchDown());
    }

    void hexEditUpperCasesEveryInputAndHoldsSixteen()
    {
        HexFindDialog d;
        QLineEdit * edit = d.findChild<QLineEdit *>("hexEdit");
        edit->setText("abcdef");
        QCOMPARE(edit->text(), QString("ABCDEF"));
        edit->clear();
        QTest::keyClicks(edit, "a1b2");
        QCOMPARE(edit->text(), QString("A1B2"));
        edit->clear();
        edit->setText("0123456789abcdef0");
        QVERIFY2(edit->text().size() <= 16, qPrintable(edit->text()));
        edit->clear();
        QTest::keyClicks(edit, "0123456789abcdef01");
        QCOMPARE(edit->text(), QString("0123456789ABCDEF"));
        QCOMPARE(d.bytes().size() * 2, 16);
        d.findChild<QPushButton *>("findButton")->click();
        QCOMPARE(d.bytes().toHex().toUpper(), QByteArray("0123456789ABCDEF"));
    }

    void returnPressesTheDefaultButton()
    {
        FindDialog find;
        find.show();
        QVERIFY(QTest::qWaitForWindowActive(&find));
        QSignalSpy findRequests(&find, &FindDialog::findRequested);
        QLineEdit * findEdit = find.findChild<QLineEdit *>("findEdit");
        QTest::keyClicks(findEdit, "x");
        QTest::keyClick(findEdit, Qt::Key_Return);
        QCOMPARE(findRequests.count(), 1);
        QTest::keyClick(findEdit, Qt::Key_Return);
        QCOMPARE(findRequests.count(), 2);

        HexFindDialog hex;
        hex.show();
        QVERIFY(QTest::qWaitForWindowActive(&hex));
        QSignalSpy hexRequests(&hex, &HexFindDialog::findRequested);
        QLineEdit * hexEdit = hex.findChild<QLineEdit *>("hexEdit");
        QTest::keyClicks(hexEdit, "4");
        QTest::keyClick(hexEdit, Qt::Key_Return);		// odd: refused
        QCOMPARE(hexRequests.count(), 0);
        QTest::keyClicks(hexEdit, "1");
        QTest::keyClick(hexEdit, Qt::Key_Return);
        QCOMPARE(hexRequests.count(), 1);
        QCOMPARE(hex.bytes(), QByteArray("A"));

        GotoDialog go(10);
        go.show();
        QVERIFY(QTest::qWaitForWindowActive(&go));
        QTest::keyClicks(go.findChild<QLineEdit *>("offsetEdit"), "7");
        QTest::keyClick(go.findChild<QLineEdit *>("offsetEdit"), Qt::Key_Return);
        QCOMPARE(go.result(), (int)QDialog::Accepted);
        QCOMPARE(go.offset(), (qint64)7);
    }

    void hexCancelClosesTheDialog()
    {
        HexFindDialog d;
        d.show();
        QSignalSpy done(&d, &QDialog::finished);
        d.findChild<QPushButton *>("cancelButton")->click();
        QCOMPARE(done.count(), 1);
        QVERIFY(!d.isVisible());
    }

    void gotoDialogValidation()
    {
        GotoDialog d(100);
        QCOMPARE(d.windowTitle(), QString("Go to offset"));
        QCOMPARE(d.findChild<QLabel *>("promptLabel")->text(), QString("Enter offset (0 - 99)"));
        QLineEdit * edit = d.findChild<QLineEdit *>("offsetEdit");
        QLabel * error = d.findChild<QLabel *>("errorLabel");
        QPushButton * ok = d.findChild<QPushButton *>("okButton");
        QVERIFY(ok->isDefault());

        // a key that is not a digit is refused, as ES_NUMBER does; text that gets past the validator (setText) is
        // refused by OK
        QTest::keyClicks(edit, "1x2-");
        QCOMPARE(edit->text(), QString("12"));
        edit->setText("12a");
        ok->click();
        QCOMPARE(error->text(), QString("Offset must be a number"));
        QCOMPARE(d.result(), (int)QDialog::Rejected);
        edit->setText("-5");
        ok->click();
        QCOMPARE(error->text(), QString("Offset must be a number"));
        edit->setText("100");
        ok->click();
        QCOMPARE(error->text(), QString("Offset too large or < 0"));
        QCOMPARE(d.result(), (int)QDialog::Rejected);
        edit->setText("99999999999999999999999999");
        ok->click();
        QCOMPARE(error->text(), QString("Offset too large or < 0"));
        edit->setText("99");
        ok->click();
        QVERIFY(error->text().isEmpty());
        QCOMPARE(d.result(), (int)QDialog::Accepted);
        QCOMPARE(d.offset(), (qint64)99);
    }

    void gotoMessageForANonNumberIsReachableOnlyByPastingPastTheValidator()
    {
        // The validator keeps letters out; Windows' "Offset must be a number" is the check behind it.
        QCOMPARE(GotoDialog::check(QString("12a"), 100).second, QString("Offset must be a number"));
        QCOMPARE(GotoDialog::check(QString("-1"), 100).second, QString("Offset must be a number"));
        QCOMPARE(GotoDialog::check(QString("100"), 100).second, QString("Offset too large or < 0"));
        QCOMPARE(GotoDialog::check(QString("99"), 100).second, QString());
        QCOMPARE(GotoDialog::check(QString("0"), 100).first, (qint64)0);
        // Windows accepts an empty box as 0 (atoi of "")
        QCOMPARE(GotoDialog::check(QString(""), 100).second, QString());
        QCOMPARE(GotoDialog::check(QString(""), 100).first, (qint64)0);
    }

    void gotoCancelRejects()
    {
        GotoDialog d(10);
        d.show();
        d.findChild<QLineEdit *>("offsetEdit")->setText("3");
        d.findChild<QPushButton *>("cancelButton")->click();
        QCOMPARE(d.result(), (int)QDialog::Rejected);
    }

    // ---- the byte search itself ----------------------------------------------------------------------------------------

    void searchBytesRules()
    {
        const std::string data = "xxABxxABxxAB";
        QCOMPARE(searchBytes(data, "AB", true, -1), (qint64)2);
        QCOMPARE(searchBytes(data, "AB", true, 2), (qint64)6);
        QCOMPARE(searchBytes(data, "AB", true, 6), (qint64)10);		// ends at the end: found
        QCOMPARE(searchBytes(data, "AB", true, 10), (qint64)-1);
        QCOMPARE(searchBytes(data, "AB", false, -1), (qint64)10);		// up starts at the last possible match
        QCOMPARE(searchBytes(data, "AB", false, 10), (qint64)6);
        QCOMPARE(searchBytes(data, "AB", false, 2), (qint64)-1);
        QCOMPARE(searchBytes(std::string("AAA"), "AA", true, -1), (qint64)0);
        QCOMPARE(searchBytes(std::string("AAA"), "AA", true, 0), (qint64)1);		// overlapping
        QCOMPARE(searchBytes(std::string("AAA"), "AA", true, 1), (qint64)-1);
        QCOMPARE(searchBytes(std::string("AB"), "ABC", true, -1), (qint64)-1);
        QCOMPARE(searchBytes(std::string("AB"), "ABC", false, -1), (qint64)-1);
        QCOMPARE(searchBytes(std::string(), "A", true, -1), (qint64)-1);
        QCOMPARE(searchBytes(std::string("A"), "A", true, -1), (qint64)0);
        QCOMPARE(searchBytes(std::string("A"), "A", false, -1), (qint64)0);
        QCOMPARE(searchBytes(data, "", true, -1), (qint64)-1);		// the dialog never sends an empty needle
        QCOMPARE(searchBytes(std::string("a\0b", 3), std::string("\0b", 2), true, -1), (qint64)1);	// NUL bytes are bytes
    }

    // ---- Find on the Data tab ---------------------------------------------------------------------------------------------

    void findGoesThroughThreeHitsThenMissesThenStartsAgain()
    {
        ModalGuard guard;
        Harness h;
        h.setBody(threeHits());
        for (int round = 0; round < 2; round++)
        {
            int n = 0;
            for (int at : { 5, 40, 95 })
            {
                h.findNext("AB");
                QVERIFY2(h.selected() == "A", qPrintable(QString("round %1 hit %2 selected [%3]").arg(round).arg(n).arg(h.selected())));
                QCOMPARE(h.start().first, at / 32);
                QCOMPARE(h.start().second, at % 32 + 9);
                n++;
            }
            h.findNext("AB");		// the miss: the view goes to the top, nothing is selected, no message box
            QVERIFY(!h.edit()->textCursor().hasSelection());
            QCOMPARE(h.edit()->textCursor().position(), 0);
            QCOMPARE(h.edit()->verticalScrollBar()->value(), 0);
            QCOMPARE(guard.unexpected, 0);
        }
    }

    void aHitEndingAtTheLastByteIsFound()
    {
        Harness h;
        h.setBody(std::string(98, 'x') + "AB");
        h.findNext("AB");
        QCOMPARE(h.selected(), QString("A"));
        QCOMPARE(h.start().first, 98 / 32);
        QCOMPARE(h.start().second, 98 % 32 + 9);
        h.findNext("AB");
        QVERIFY(!h.edit()->textCursor().hasSelection());		// and then the miss
        // a single byte at the very end, and the whole data as the needle
        h.setBody(std::string(40, 'x') + "Z");
        h.findNext("Z");
        QCOMPARE(h.selected(), QString("Z"));
        h.setBody("ABC");
        h.findNext("ABC");
        QCOMPARE(h.selected(), QString("A"));
    }

    void directionUpWalksBackFromTheEnd()
    {
        Harness h;
        h.setBody(threeHits());
        h.findNext("AB", false);
        QCOMPARE(h.start().first, 95 / 32);
        QCOMPARE(h.start().second, 95 % 32 + 9);
        h.findNext("AB", false);
        QCOMPARE(h.start().first, 40 / 32);
        QCOMPARE(h.start().second, 40 % 32 + 9);
        h.findNext("AB", false);
        QCOMPARE(h.start().first, 0);
        QCOMPARE(h.start().second, 5 + 9);
        h.findNext("AB", false);
        QVERIFY(!h.edit()->textCursor().hasSelection());
        h.findNext("AB", false);
        QCOMPARE(h.start().second, 95 % 32 + 9);		// starts again from the end
        // a match that ends at the last byte is the first one found upwards
        h.setBody(std::string(98, 'x') + "AB");
        h.findNext("AB", false);
        QCOMPARE(h.selected(), QString("A"));
        QCOMPARE(h.start().second, 98 % 32 + 9);
    }

    void changingDirectionContinuesFromTheLastHit()
    {
        Harness h;
        h.setBody(threeHits());
        h.findNext("AB");
        h.findNext("AB");		// at 40
        h.findNext("AB", false);		// up from 40: the hit at 5
        QCOMPARE(h.start().first, 0);
        QCOMPARE(h.start().second, 5 + 9);
    }

    void overlappingMatchesAreAllFound()
    {
        Harness h;
        h.setBody("xAAAx");
        h.findNext("AA");
        QCOMPARE(h.start().second, 1 + 9);
        h.findNext("AA");
        QCOMPARE(h.start().second, 2 + 9);
        h.findNext("AA");
        QVERIFY(!h.edit()->textCursor().hasSelection());
    }

    void aNewTextOrViewStartsAgainFromTheTop()
    {
        Harness h;
        h.setBody(threeHits());
        h.findNext("AB");
        h.findNext("AB");		// at 40
        h.findNext("xAB");		// a different text: from the top, so the first "xAB" (4)
        QCOMPARE(h.start().second, 4 + 9);
        h.findNext("xAB");		// continues
        QCOMPARE(h.start().first, 39 / 32);
        QCOMPARE(h.start().second, 39 % 32 + 9);
        h.useView(View::Hex);		// a different view
        h.findNext("xAB");		// from the top again, not after 39
        QCOMPARE(h.start().first, 0);
        QCOMPARE(h.selected(), QString("78"));
        // and a new message
        h.useView(View::Character);
        h.findNext("AB");
        h.findNext("AB");
        h.setBody(threeHits());
        h.findNext("AB");
        QCOMPARE(h.start().second, 5 + 9);
    }

    void theNeedleIsAtMost255Bytes()
    {
        Harness h;
        h.setBody(std::string(300, 'A'));
        FindDialog * d = h.openFind();
        d->findChild<QLineEdit *>("findEdit")->setText(QString(300, 'A'));
        int hits = 0;
        for (int i = 0; i < 100; i++)
        {
            d->findChild<QPushButton *>("findNextButton")->click();
            if (!h.edit()->textCursor().hasSelection())
            {
                break;
            }
            hits++;
        }
        QCOMPARE(hits, 300 - 255 + 1);		// the first 255 bytes, at offsets 0 to 45
    }

    void ebcdicTextIsTranslatedBeforeTheSearch()
    {
        Harness h;
        h.setBody("\xC8\xC5\xD3\xD3\xD6\x40\xE6\xD6\xD9\xD3\xC4");		// "HELLO WORLD" in EBCDIC
        h.useChars(CHAR_EBCDIC);
        h.findNext("LL");
        QCOMPARE(h.selected(), QString("L"));
        QCOMPARE(h.start().second, 2 + 9);
        h.useChars(CHAR_ASCII);
        h.findNext("LL");		// as ASCII the raw bytes are not "LL"
        QVERIFY(!h.edit()->textCursor().hasSelection());
    }

    void hexViewHighlightIsTheTwoDigitsOfTheFirstByte()
    {
        Harness h;
        h.setBody("xxABxx");
        h.useView(View::Hex);
        h.findNext("AB");
        QCOMPARE(h.selected(), QString("41"));
        QCOMPARE(h.start().first, 0);
        QCOMPARE(h.start().second, 9 + 4);		// the third byte: two digits per byte, 9 characters of offset
        h.useView(View::Both);
        h.findNext("AB");
        QCOMPARE(h.selected().size(), 1);		// the Both view selects the byte's character
        QCOMPARE(h.selected(), QString("A"));
    }

    void theHitLineIsTheFirstVisibleLine()
    {
        Harness h;
        std::string body;
        for (int i = 0; i < 3200; i++)
        {
            body += (char)('a' + i % 26);
        }
        body[50 * 32 + 3] = 'Q';
        body[50 * 32 + 4] = 'Z';
        h.setBody(body);
        h.findNext("QZ");
        QCOMPARE(h.start().first, 50);
        QCOMPARE(h.edit()->cursorForPosition(QPoint(1, 1)).blockNumber(), 50);
        QCOMPARE(h.selected(), QString("Q"));
        // a miss goes back to the top
        h.findNext("QZ");
        QCOMPARE(h.edit()->cursorForPosition(QPoint(1, 1)).blockNumber(), 0);
    }

    void structuredViewsSearchTheRenderedText()
    {
        Harness h;
        h.setBody("{\"alpha\":1,\"beta\":2}");
        h.useView(View::Json);
        const QString shown = h.edit()->toPlainText();
        QVERIFY(shown.contains("beta"));
        h.findNext("beta");
        QCOMPARE(h.selected(), QString("b"));		// one character
        const QTextCursor c = h.edit()->textCursor();
        QCOMPARE(h.edit()->toPlainText().mid(c.selectionStart(), 4), QString("beta"));
        h.findNext("beta");
        // no wrap; the miss goes to the start and, in these views, selects the first character (moveDisplay(0, 0, 1))
        QCOMPARE(h.selected(), QString("a"));
        QCOMPARE(h.edit()->textCursor().selectionStart(), 0);

        h.setBody("<a><b>one</b><b>two</b></a>");
        h.useView(View::Xml);
        h.findNext("<b>");
        const int first = h.edit()->textCursor().selectionStart();
        QCOMPARE(h.edit()->toPlainText().mid(first, 3), QString("<b>"));
        h.findNext("<b>");
        QVERIFY(h.edit()->textCursor().selectionStart() > first);
        QCOMPARE(h.edit()->toPlainText().mid(h.edit()->textCursor().selectionStart(), 3), QString("<b>"));
        h.findNext("<b>");
        QCOMPARE(h.edit()->textCursor().selectionStart(), 0);		// the miss
        QCOMPARE(h.selected(), h.edit()->toPlainText().left(1));
        // text at the very end of the rendered text is found
        const QString xml = h.edit()->toPlainText().trimmed();
        h.findNext(xml.right(4));
        QCOMPARE(h.edit()->textCursor().selectionStart(), (int)h.edit()->toPlainText().lastIndexOf(xml.right(4)));
    }

    void findOnTheCobolViewDoesNothing()
    {
        Harness h;
        h.setBody(threeHits());
        FindDialog * d = h.openFind();
        QVERIFY(d != nullptr);
        h.useView(View::Cobol);
        d->findChild<QLineEdit *>("findEdit")->setText("AB");
        d->findChild<QPushButton *>("findNextButton")->click();
        QVERIFY(!h.edit()->textCursor().hasSelection());
    }

    // ---- Find Hex ------------------------------------------------------------------------------------------------------------

    void findHexFindsTheBytes()
    {
        Harness h;
        h.setBody("xxABxx");
        QTest::keyClick(&h.host, Qt::Key_Z, Qt::ControlModifier);
        HexFindDialog * d = h.hexDialog();
        QVERIFY(d != nullptr);
        d->findChild<QLineEdit *>("hexEdit")->setText("4142");
        d->findChild<QPushButton *>("findButton")->click();
        QCOMPARE(h.selected(), QString("A"));
        QCOMPARE(h.start().first, 0);
        QCOMPARE(h.start().second, 2 + 9);
        d->findChild<QPushButton *>("findButton")->click();		// no second hit: the miss goes to the top
        QVERIFY(!h.edit()->textCursor().hasSelection());
        h.useView(View::Hex);
        d->findChild<QPushButton *>("findButton")->click();
        QCOMPARE(h.selected(), QString("41"));
        // invalid input searches nothing
        d->findChild<QLineEdit *>("hexEdit")->setText("4");
        d->findChild<QPushButton *>("findButton")->click();
        QCOMPARE(d->findChild<QLabel *>("errorLabel")->text(), QString("Odd number of characters"));
    }

    void findHexIsNotTranslatedToEbcdic()
    {
        Harness h;
        h.setBody("\xC8\xC5\xD3\xD3\xD6");
        h.useChars(CHAR_EBCDIC);
        QTest::keyClick(&h.host, Qt::Key_Z, Qt::ControlModifier);
        HexFindDialog * d = h.hexDialog();
        d->findChild<QLineEdit *>("hexEdit")->setText("D3D6");		// the raw bytes of "LO"
        d->findChild<QPushButton *>("findButton")->click();
        QCOMPARE(h.selected(), QString("L"));
        QCOMPARE(h.start().second, 3 + 9);
        // and the 41 42 that would be "AB" after translating is not found
        h.setBody("\xC1\xC2");
        d->findChild<QLineEdit *>("hexEdit")->setText("4142");
        d->findChild<QPushButton *>("findButton")->click();
        QVERIFY(!h.edit()->textCursor().hasSelection());
        d->findChild<QLineEdit *>("hexEdit")->setText("C1C2");
        d->findChild<QPushButton *>("findButton")->click();
        QCOMPARE(h.selected(), QString("A"));
    }

    void findHexDirectionUp()
    {
        Harness h;
        h.setBody("ABxxABxxAB");
        QTest::keyClick(&h.host, Qt::Key_Z, Qt::ControlModifier);
        HexFindDialog * d = h.hexDialog();
        d->findChild<QLineEdit *>("hexEdit")->setText("4142");
        d->findChild<QRadioButton *>("upRadio")->setChecked(true);
        d->findChild<QPushButton *>("findButton")->click();
        QCOMPARE(h.start().second, 8 + 9);
        d->findChild<QPushButton *>("findButton")->click();
        QCOMPARE(h.start().second, 4 + 9);
    }

    // ---- the needle -----------------------------------------------------------------------------------------------

    void theNeedleCutIsExactlyAt255Bytes()
    {
        Harness h;
        h.setBody(std::string(300, 'A'));
        auto hits = [&](int needleLength)
        {
            FindDialog * d = h.findDialog() ? h.findDialog() : h.openFind();
            d->findChild<QLineEdit *>("findEdit")->setText(QString(needleLength, 'A'));
            int n = 0;
            for (int i = 0; i < 400; i++)
            {
                d->findChild<QPushButton *>("findNextButton")->click();
                if (!h.edit()->textCursor().hasSelection())
                {
                    break;
                }
                n++;
            }
            return n;
        };
        QCOMPARE(hits(254), 300 - 254 + 1);
        QCOMPARE(hits(255), 300 - 255 + 1);
        QCOMPARE(hits(256), 300 - 255 + 1);		// the 256th byte is cut
        QCOMPARE(hits(300), 300 - 255 + 1);
    }

    void ebcdicTextIsNotTranslatedInTheRenderedTextViews()
    {
        Harness h;
        std::string ascii = "<a><b>one</b><b>two</b></a>";
        std::string ebcdic(ascii.size(), '\0');
        AsciiToEbcdic((unsigned char *)ascii.data(), (unsigned int)ascii.size(), (unsigned char *)&ebcdic[0]);
        h.setBody(ebcdic);
        h.useChars(CHAR_EBCDIC);
        h.useView(View::Xml);
        QVERIFY2(h.edit()->toPlainText().contains("<b>"), qPrintable(h.edit()->toPlainText()));
        h.findNext("<b>");
        QVERIFY(h.edit()->textCursor().hasSelection());
        const int at = h.edit()->textCursor().selectionStart();
        QCOMPARE(h.edit()->toPlainText().mid(at, 3), QString("<b>"));
        // the same text in the byte views is translated, so it is found in the raw EBCDIC bytes
        h.useView(View::Character);
        h.findNext("<b>");
        QCOMPARE(h.selected(), QString("<"));
    }

    void theCharacterSetPicksTheNeedleCodePageToo()
    {
        Harness h;
        if (!h.tab->findChild<QRadioButton *>("charJapaneseRadio")->isEnabled())
        {
            QSKIP("code page 932 is not installed here");
        }
        h.setBody(std::string("ab\x82\xA0" "cd"));
        h.tab->findChild<QRadioButton *>("charJapaneseRadio")->click();
        h.findNext(QString::fromUtf8("\xE3\x81\x82"));		// the hiragana a
        QCOMPARE(h.selected(), QString::fromUtf8("\xE3\x81\x82"));
        QCOMPARE(h.start().second, 9 + 2);
    }

    void aChangeOfTheRenderedTextStartsTheSearchAgain()
    {
        Harness h;
        h.setBody("<a><b>1</b><b>2</b></a>");
        h.useView(View::Xml);
        h.findNext("<b>");
        const int first = h.edit()->textCursor().selectionStart();
        h.findNext("<b>");
        QVERIFY(h.edit()->textCursor().selectionStart() > first);
        // the text changes (the body shown as EBCDIC) and changes back
        h.useChars(CHAR_EBCDIC);
        h.useChars(CHAR_ASCII);
        h.findNext("<b>");		// from the top again, not a miss after the second hit
        QVERIFY(h.edit()->textCursor().hasSelection());
        QCOMPARE(h.edit()->textCursor().selectionStart(), first);
    }

    // ---- CR/LF and EDI: the real byte -------------------------------------------------------------------------------

    void crlfAndEdiSelectTheByteForFindFindHexAndGoTo()
    {
        Harness h;
        h.setBody("line one\r\nline two");
        rfh::format::RenderOptions o = h.doc.render();
        o.crlf = true;
        h.doc.setRender(o);
        QCOMPARE(h.edit()->toPlainText(), QString("00000000 line one\n00000010 line two"));
        h.findNext("two");
        QCOMPARE(h.selected(), QString("t"));
        QCOMPARE(h.start().first, 1);
        QCOMPARE(h.start().second, 9 + 5);
        h.tab->goToOffset(15);
        QCOMPARE(h.selected(), QString("t"));
        QCOMPARE(h.start().second, 14);
        h.tab->goToOffset(0);
        QCOMPARE(h.selected(), QString("l"));
        QCOMPARE(h.start().second, 9);
        // the CR is not in the text: the cursor is at the end of its line and selects nothing
        h.tab->goToOffset(8);
        QVERIFY(!h.edit()->textCursor().hasSelection());
        QCOMPARE(h.edit()->textCursor().blockNumber(), 0);
        QCOMPARE(h.edit()->textCursor().positionInBlock(), 17);

        // Find Hex of the same bytes
        h.findDialog()->findChild<QPushButton *>("cancelButton")->click();
        QTRY_VERIFY(h.findDialog() == nullptr);
        h.host.activateWindow();
        QTest::keyClick(&h.host, Qt::Key_Z, Qt::ControlModifier);
        HexFindDialog * hex = h.hexDialog();
        QVERIFY(hex != nullptr);
        hex->findChild<QLineEdit *>("hexEdit")->setText("776F");		// "wo"
        hex->findChild<QPushButton *>("findButton")->click();
        QCOMPARE(h.selected(), QString("w"));
        QCOMPARE(h.start().second, 9 + 6);
        hex->findChild<QPushButton *>("cancelButton")->click();
        QTRY_VERIFY(h.hexDialog() == nullptr);

        o.crlf = false;
        o.edi = true;
        h.doc.setRender(o);
        h.setBody("UNA:+.? 'UNB+UNOC:3+X'UNH+1'");
        h.findNext("UNH");
        QCOMPARE(h.selected(), QString("U"));
        QCOMPARE(h.start().first, 2);
        h.tab->goToOffset(25);
        QCOMPARE(h.selected(), QString("+"));
        h.tab->goToOffset(27);
        QCOMPARE(h.selected(), QString("'"));
        h.tab->goToOffset(10);
        QCOMPARE(h.selected(), QString("N"));
    }

    void randomBodiesNeverSelectTheWrongByteWithCrlfOrEdi()
    {
        Harness h;
        srand(2024);
        const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 :+.?*/-";
        int checked = 0;
        for (int mode = 0; mode < 2; mode++)
        {
            rfh::format::RenderOptions o = h.doc.render();
            o.crlf = (mode == 0);
            o.edi = (mode == 1);
            h.doc.setRender(o);
            for (int round = 0; round < 25; round++)
            {
                std::string body;
                const int size = 1 + rand() % 150;
                for (int i = 0; i < size; i++)
                {
                    const int r = rand() % 12;
                    if (r == 0) body += "\r\n";
                    else if (r == 1) body += '\n';
                    else if (r == 2) body += '\'';
                    else body += alphabet[rand() % (sizeof(alphabet) - 1)];
                }
                h.setBody(body);
                int expectedHits = 0;
                for (char c : body)
                {
                    expectedHits += (c == 'A');
                }
                // Go To every offset
                for (int i = 0; i < (int)body.size(); i++)
                {
                    h.tab->goToOffset(i);
                    const unsigned char b = (unsigned char)body[(size_t)i];
                    if (h.edit()->textCursor().hasSelection())
                    {
                        QCOMPARE(h.selected(), QString(QChar(b < 0x20 ? '.' : b)));
                        checked++;
                    }
                    else
                    {
                        QVERIFY2(b < 0x20, qPrintable(QString("mode %1 round %2 offset %3 selected nothing").arg(mode).arg(round).arg(i)));
                    }
                }
                // Find walks every 'A' and selects an 'A' each time
                int hits = 0;
                for (int i = 0; i < (int)body.size() + 2; i++)
                {
                    h.tab->searchFor("A", true);
                    if (!h.edit()->textCursor().hasSelection())
                    {
                        break;
                    }
                    QCOMPARE(h.selected(), QString("A"));
                    hits++;
                }
                QCOMPARE(hits, expectedHits);
            }
        }
        QVERIFY(checked > 1000);
    }

    // ---- a second Ctrl+F --------------------------------------------------------------------------------------------

    void aSecondFindRaisesTheOpenDialogInsteadOfOpeningAnother()
    {
        Harness h;
        h.setBody("abc");
        FindDialog * d = h.openFind();
        QVERIFY(d != nullptr);
        QVERIFY(QTest::qWaitForWindowActive(d));
        h.host.activateWindow();
        QTRY_VERIFY(QApplication::activeWindow() == &h.host);
        QTest::keyClick(&h.host, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(h.tab->findChildren<FindDialog *>().size(), 1);
        QTRY_VERIFY(QApplication::activeWindow() == d);
    }

    // ---- Go To -------------------------------------------------------------------------------------------------------------------

    // opens Go To with the shortcut, answers it with `text` and returns whether it was accepted
    bool goTo(Harness & h, const QString & text)
    {
        struct Answer { bool answered = false; bool accepted = false; };
        auto answer = std::make_shared<Answer>();
        QTimer poll;
        poll.setInterval(10);
        QObject::connect(&poll, &QTimer::timeout, [answer, text, &poll]()
        {
            GotoDialog * d = qobject_cast<GotoDialog *>(QApplication::activeModalWidget());
            if (d == nullptr || answer->answered)
            {
                return;
            }
            poll.stop();
            d->findChild<QLineEdit *>("offsetEdit")->setText(text);
            d->findChild<QPushButton *>("okButton")->click();
            answer->accepted = d->result() == QDialog::Accepted;
            if (!answer->accepted)
            {
                d->findChild<QPushButton *>("cancelButton")->click();
            }
            answer->answered = true;
        });
        poll.start();
        h.host.activateWindow();		// a closed modal dialog leaves the window inactive for a moment
        (void)QTest::qWaitForWindowActive(&h.host);
        QTest::keyClick(&h.host, Qt::Key_G, Qt::ControlModifier);
        QElapsedTimer waited;
        waited.start();
        while (!answer->answered && waited.elapsed() < 3000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return answer->accepted;
    }

    void goToPutsTheCursorOnTheByte()
    {
        Harness h;
        std::string body;
        for (int i = 0; i < 80; i++)
        {
            body += (char)('a' + i % 26);
        }
        h.setBody(body);
        for (int ofs : { 0, 31, 32, 33 })
        {
            QVERIFY(goTo(h, QString::number(ofs)));
            QCOMPARE(h.selected(), QString(QChar((char)('a' + ofs % 26))));
            QCOMPARE(h.start().first, ofs / 32);
            QCOMPARE(h.start().second, ofs % 32 + 9);
            rfh::format::TextPos pos;
            QVERIFY(rfh::format::locate(View::Character, h.doc.render(), h.doc.message(), (size_t)ofs, pos));
            QCOMPARE(h.start().first, pos.line);
            QCOMPARE(h.start().second, pos.column);
        }
        h.useView(View::Hex);
        for (int ofs : { 0, 15, 16 })
        {
            QVERIFY(goTo(h, QString::number(ofs)));
            QCOMPARE(h.selected(), QString("%1").arg((int)(unsigned char)body[ofs], 2, 16, QLatin1Char('0')).toUpper());
            QCOMPARE(h.start().first, ofs / 16);
            rfh::format::TextPos pos;
            QVERIFY(rfh::format::locate(View::Hex, h.doc.render(), h.doc.message(), (size_t)ofs, pos));
            QCOMPARE(h.start().first, pos.line);
            QCOMPARE(h.start().second, pos.column);
            QCOMPARE(h.selected().size(), pos.length);
        }
        h.useView(View::Both);
        QVERIFY(goTo(h, "17"));
        QCOMPARE(h.selected(), QString(QChar((char)('a' + 17 % 26))));
        QCOMPARE(h.start().first, 1);
    }

    void goToRefusesAnOffsetPastTheData()
    {
        Harness h;
        h.setBody(std::string(40, 'x'));
        QVERIFY(!goTo(h, "40"));
        QVERIFY(!h.edit()->textCursor().hasSelection());
        QVERIFY(goTo(h, "39"));
        QCOMPARE(h.start().first, 1);
    }

    // ---- when the commands are available ----------------------------------------------------------------------------

    void commandsNeedTheDataTabDataAndTheRightView()
    {
        Harness h;
        auto count = [&]() { return h.tab->findChildren<FindDialog *>().size() + h.tab->findChildren<HexFindDialog *>().size(); };

        // no data
        QTest::keyClick(&h.host, Qt::Key_F, Qt::ControlModifier);
        QTest::keyClick(&h.host, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(count(), 0);
        QVERIFY(h.doc.enables().menuFind == false);

        h.setBody("some data");
        // other tab: nothing opens
        h.host.setCurrentIndex(1);
        QTest::keyClick(&h.host, Qt::Key_F, Qt::ControlModifier);
        QTest::keyClick(&h.host, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(count(), 0);
        h.host.setCurrentIndex(0);

        // Find works in the text views, not COBOL; Find Hex and Go To only in Character, Hex and Both
        for (View v : { View::Xml, View::Parsed, View::Json, View::Fixed })
        {
            h.useView(v);
            QTest::keyClick(&h.host, Qt::Key_Z, Qt::ControlModifier);
            QCOMPARE(h.tab->findChildren<HexFindDialog *>().size(), 0);
            QVERIFY(!h.doc.enables().menuFindHex);
            QVERIFY(!h.doc.enables().menuGoto);
            QVERIFY(h.doc.enables().menuFind);
        }
        h.useView(View::Cobol);
        QTest::keyClick(&h.host, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(count(), 0);

        h.useView(View::Hex);
        QTest::keyClick(&h.host, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(count(), 1);
        QVERIFY(h.doc.shell().findDialogOpen);
        // not while a Find dialog is open: neither a second Find nor Find Hex
        QTest::keyClick(&h.host, Qt::Key_F, Qt::ControlModifier);
        QTest::keyClick(&h.host, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(count(), 1);
        QVERIFY(!h.doc.enables().menuFind);
        QVERIFY(!h.doc.enables().menuFindHex);
        QVERIFY(!h.doc.enables().menuGoto);
        h.findDialog()->findChild<QPushButton *>("cancelButton")->click();
        QTRY_COMPARE(count(), 0);
        QVERIFY(!h.doc.shell().findDialogOpen);
        QVERIFY(h.doc.enables().menuFind);

        // Find Hex open: no Find
        QTest::keyClick(&h.host, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(count(), 1);
        QVERIFY(h.hexDialog() != nullptr);
        QVERIFY(h.doc.shell().findDialogOpen);
        QTest::keyClick(&h.host, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(count(), 1);
        h.hexDialog()->findChild<QPushButton *>("cancelButton")->click();
        QTRY_COMPARE(count(), 0);
        QVERIFY(!h.doc.shell().findDialogOpen);
    }

    void goToIsRefusedWhereItIsNotAvailable()
    {
        ModalGuard guard;		// a Go To dialog that opens when it should not is closed, and counted
        Harness h;
        // no data
        QTest::keyClick(&h.host, Qt::Key_G, Qt::ControlModifier);
        QTest::qWait(100);
        h.setBody("some data");
        // other tab
        h.host.setCurrentIndex(1);
        QTest::keyClick(&h.host, Qt::Key_G, Qt::ControlModifier);
        QTest::qWait(100);
        h.host.setCurrentIndex(0);
        // views Go To does not serve
        for (View v : { View::Xml, View::Parsed, View::Cobol, View::Json, View::Fixed })
        {
            h.useView(v);
            QTest::keyClick(&h.host, Qt::Key_G, Qt::ControlModifier);
            QTest::qWait(60);
        }
        // a Find dialog is open
        h.useView(View::Character);
        QVERIFY(h.openFind() != nullptr);
        QTest::keyClick(h.findDialog(), Qt::Key_G, Qt::ControlModifier);
        h.host.activateWindow();
        QTest::keyClick(&h.host, Qt::Key_G, Qt::ControlModifier);
        QTest::qWait(100);
        QCOMPARE(guard.unexpected, 0);
    }

    void escapeClosesAFindDialogAndFrees()
    {
        Harness h;
        h.setBody("abc");
        FindDialog * d = h.openFind();
        QVERIFY(d != nullptr);
        QTest::keyClick(d, Qt::Key_Escape);
        QTRY_VERIFY(h.findDialog() == nullptr);
        QVERIFY(!h.doc.shell().findDialogOpen);
    }

    void dataTabGoneWithADialogOpenClearsTheShellFlag()
    {
        AppDocument doc;
        StatusLog log;
        SessionController controller(rfh_test::fakeApi());
        Operations ops(doc, controller, log);
        {
            DataTab tab(doc, ops);
            rfh::Message m;
            m.body.assign(5, 'a');
            doc.setMessage(std::move(m), "Message Data", false);
            ShellState s = doc.shell();
            s.tab = Tab::Data;
            doc.setShell(s);
            tab.showFind();
            QVERIFY(doc.shell().findDialogOpen);
        }
        QVERIFY(!doc.shell().findDialogOpen);
    }
};

QTEST_MAIN(FindDialogsTests)
#include "FindDialogsTests.moc"
