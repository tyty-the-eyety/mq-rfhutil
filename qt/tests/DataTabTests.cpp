// DataTabTests.cpp - the Data tab (DataTab) on a real AppDocument and Operations.
//
// Expectations come from the Windows dialog (RFHUtil/rfhutil.rc IDD_DATA, MSGDATA.cpp updateMsgdata 288-432,
// OnSetActive 495-540, OnCopybook 659, OnCobol 188-215) and the inventory 2.3, 5.1-5.4, 7.1. The text of a view
// is what format::render makes of the message, decoded from display.ansi; the tests decode ASCII output by hand.

#include <QtTest>

#include <QScopeGuard>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>

#include <string>
#include <vector>

#include "AppDocument.h"
#include "AppSettings.h"
#include "DataTab.h"
#include "Operations.h"
#include "SessionController.h"
#include "StatusLog.h"
#include "fake_mq.h"
#include "rfh_codepage.h"
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

    rfh::Message messageOf(const std::string & body)
    {
        rfh::Message m;
        m.body.assign(body.begin(), body.end());
        return m;
    }

    const View kViews[] = { View::Character, View::Hex, View::Both, View::Xml, View::Parsed, View::Cobol, View::Json, View::Fixed };
    const char * const kViewRadios[] = {
        "viewCharacterRadio", "viewHexRadio", "viewBothRadio", "viewXmlRadio", "viewParsedRadio", "viewCobolRadio",
        "viewJsonRadio", "viewFixRadio" };

    // what the tab must show for ASCII output: the render, with CR LF as the edit's line break
    QString expected(const rfh::Message & m, View view, const rfh::format::RenderOptions & o)
    {
        std::string out;
        const rfh::Result r = rfh::format::render(m, view, o, out);
        QString text = r.ok() ? QString::fromLatin1(out.data(), (qsizetype)out.size()) : QString::fromStdString(r.text);
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        return text;
    }

    struct Harness
    {
        Harness() : controller(rfh_test::fakeApi()), ops(doc, controller, log), settings(dir.filePath("rfhutil.conf"))
        {
            tab.reset(new DataTab(doc, ops, &settings));
            tab->setCopybookChooser([this](QWidget *, const QString & directory)
            {
                chooserCalls++;
                chooserDirectory = directory;
                return chooserAnswer;
            });
            ShellState s = doc.shell();
            s.tab = Tab::Data;
            doc.setShell(s);
            tab->show();		// a hidden tab does not render
        }

        template <typename T>
        T * w(const char * name) const
        {
            T * found = tab->findChild<T *>(name);
            if (found == nullptr)
            {
                printf("no widget named %s\n", name);
                fflush(stdout);
            }
            return found;
        }

        QPlainTextEdit * edit() const { return w<QPlainTextEdit>("dataEdit"); }
        QRadioButton * radio(const char * name) const { return w<QRadioButton>(name); }
        QCheckBox * check(const char * name) const { return w<QCheckBox>(name); }
        QPushButton * button(const char * name) const { return w<QPushButton>(name); }

        void setBody(const std::string & body, const char * source = "Message Data")
        {
            doc.setMessage(messageOf(body), source, false);
        }

        void useView(View v)
        {
            ShellState s = doc.shell();
            s.view = v;
            doc.setShell(s);
        }

        QString shown() const { return edit()->toPlainText(); }

        QTemporaryDir dir;
        AppDocument doc;
        StatusLog log;
        SessionController controller;
        Operations ops;
        AppSettings settings;
        std::unique_ptr<DataTab> tab;
        int chooserCalls = 0;
        QString chooserAnswer, chooserDirectory;
    };
}

class DataTabTests : public QObject
{
    Q_OBJECT

private slots:
    // ---- the views ------------------------------------------------------------------------------------------------

    void eachViewEqualsTheRender()
    {
        Harness h;
        QTemporaryFile book;
        QVERIFY(book.open());
        book.write("       01 REC.\n          05 NAME PIC X(4).\n          05 CODE PIC X(2).\n");
        book.flush();
        rfh::format::RenderOptions o = h.doc.render();
        o.copybookPath = book.fileName().toStdString();
        h.doc.setRender(o);
        h.setBody("<a><b>hi</b></a>");
        const std::string json = "{\"a\":[1,2],\"b\":\"x\"}";
        for (int i = 0; i < 8; i++)
        {
            const std::string body = (kViews[i] == View::Json) ? json : (kViews[i] == View::Cobol ? "ABCDEF" : "<a><b>hi</b></a>");
            h.setBody(body);
            h.radio(kViewRadios[i])->click();
            QVERIFY2(h.doc.shell().view == kViews[i], kViewRadios[i]);
            QVERIFY(h.radio(kViewRadios[i])->isChecked());
            const QString want = expected(messageOf(body), kViews[i], h.doc.render());
            QVERIFY2(!want.isEmpty(), kViewRadios[i]);
            QCOMPARE(h.shown(), want);
        }
        // the COBOL view really mapped the fields
        h.setBody("ABCDEF");
        h.radio("viewCobolRadio")->click();
        QVERIFY(h.shown().contains("NAME"));
        QVERIFY(h.shown().contains("ABCD"));
    }

    void captionShowsTheDataSource()
    {
        Harness h;
        QCOMPARE(h.w<QLabel>("captionLabel")->text(), QString("Message Data"));
        h.setBody("abc", "Message Data (3) from /tmp/x.dat");
        QCOMPARE(h.w<QLabel>("captionLabel")->text(), QString("Message Data (3) from /tmp/x.dat"));
        h.doc.clearData();
        QCOMPARE(h.w<QLabel>("captionLabel")->text(), QString("Message Data"));
        QVERIFY(h.shown().isEmpty());
    }

    void cp1252ByteShowsAsItsCharacter()
    {
        Harness h;
        h.setBody(std::string("caf\xE9"));
        QVERIFY2(h.shown().contains(QString::fromUtf8("caf\xC3\xA9")), qPrintable(h.shown()));
        h.radio("viewHexRadio")->click();
        QVERIFY(h.shown().contains("636166E9"));
    }

    void doubleByteBodyDecodesWithTheCodePageOfTheCharacterSet()
    {
        Harness h;
        if (!h.radio("charJapaneseRadio")->isEnabled())
        {
            QSKIP("code page 932 is not installed here");
        }
        // display.ansi stays 1252: the Japanese radio, not the display setting, picks Shift-JIS
        QCOMPARE(h.doc.render().display.ansi, 1252);
        h.setBody(std::string("ab\x82\xA0" "cd"));		// Shift-JIS "a b <hiragana a> c d"
        h.radio("charJapaneseRadio")->click();
        QVERIFY2(h.shown().contains(QString::fromUtf8("ab\xE3\x81\x82" "cd")), qPrintable(h.shown()));
        // back to ASCII: the bytes are two Windows-1252 characters
        h.radio("charAsciiRadio")->click();
        QVERIFY(!h.shown().contains(QString::fromUtf8("\xE3\x81\x82")));
        QVERIFY(h.shown().contains(QString::fromUtf8("\xE2\x80\x9A")));		// 0x82 is a low quote in 1252
    }

    void settingsCodePageIsTheDisplayCodePage()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath("c.conf"));
        settings.setDisplayCodePage(932);
        AppDocument doc;
        StatusLog log;
        SessionController controller(rfh_test::fakeApi());
        Operations ops(doc, controller, log);
        DataTab tab(doc, ops, &settings);
        QCOMPARE(doc.render().display.ansi, 932);
    }

    // ---- when it renders -------------------------------------------------------------------------------------------

    void aChangeRendersOnce()
    {
        Harness h;
        h.setBody("<a>hi</a>");
        int n = h.tab->renderCount();
        h.radio("viewHexRadio")->click();
        QCOMPARE(h.tab->renderCount(), n + 1);
        h.radio("viewHexRadio")->click();			// same view: nothing changes
        QCOMPARE(h.tab->renderCount(), n + 1);
        h.radio("charEbcdicRadio")->click();
        QCOMPARE(h.tab->renderCount(), n + 2);
        h.radio("viewCharacterRadio")->click();
        QCOMPARE(h.tab->renderCount(), n + 3);
        h.check("crlfCheck")->click();
        QCOMPARE(h.tab->renderCount(), n + 4);
        h.check("ediCheck")->click();
        QCOMPARE(h.tab->renderCount(), n + 5);
        h.check("indentCheck")->click();
        QCOMPARE(h.tab->renderCount(), n + 6);
        h.setBody("other");
        QCOMPARE(h.tab->renderCount(), n + 7);
        // a document change that is not display data does not render
        h.doc.setConnected(true);
        h.doc.setQueueName("Q");
        QCOMPARE(h.tab->renderCount(), n + 7);
    }

    void optionsGoToTheDocument()
    {
        Harness h;
        h.check("crlfCheck")->click();
        QVERIFY(h.doc.render().crlf);
        h.check("ediCheck")->click();
        QVERIFY(h.doc.render().edi);
        h.check("indentCheck")->click();
        QVERIFY(h.doc.render().indent);
        h.check("validateCheck")->click();
        QVERIFY(h.doc.render().checkData);
        QVERIFY(!h.doc.render().bom);
        h.radio("intHostRadio")->click();
        QCOMPARE(h.doc.render().numericFormat, (int)NUMERIC_HOST);
        h.radio("pdHostRadio")->click();
        QCOMPARE(h.doc.render().pdNumericFormat, (int)NUMERIC_HOST);
        h.radio("charKoreanRadio")->click();
        QCOMPARE(h.doc.render().charFormat, (int)CHAR_KOREAN);
        h.radio("charAsciiRadio")->click();
        QCOMPARE(h.doc.render().charFormat, (int)CHAR_ASCII);
    }

    void widgetsFollowTheDocument()
    {
        Harness h;
        rfh::format::RenderOptions o = h.doc.render();
        o.charFormat = CHAR_EBCDIC;
        o.numericFormat = NUMERIC_HOST;
        o.pdNumericFormat = NUMERIC_HOST;
        o.crlf = true;
        o.edi = true;
        o.indent = true;
        o.checkData = true;
        h.doc.setRender(o);
        QVERIFY(h.radio("charEbcdicRadio")->isChecked());
        QVERIFY(h.radio("intHostRadio")->isChecked());
        QVERIFY(h.radio("pdHostRadio")->isChecked());
        QVERIFY(h.check("crlfCheck")->isChecked());
        QVERIFY(h.check("ediCheck")->isChecked());
        QVERIFY(h.check("indentCheck")->isChecked());
        QVERIFY(h.check("validateCheck")->isChecked());
        // a message with a character set no button stands for (CHAR_UNICODE) leaves every character radio off
        o.charFormat = CHAR_UNICODE;
        h.doc.setRender(o);
        for (const char * name : { "charAsciiRadio", "charEbcdicRadio", "charChineseRadio", "charKoreanRadio", "charTradChineseRadio",
                                   "charJapaneseRadio", "charThaiRadio", "charRussianRadio" })
        {
            QVERIFY2(!h.radio(name)->isChecked(), name);
        }
        QCOMPARE(h.doc.render().charFormat, (int)CHAR_UNICODE);
        // a view set from outside is shown
        h.useView(View::Json);
        QVERIFY(h.radio("viewJsonRadio")->isChecked());
    }

    void ebcdicBodyIsTranslated()
    {
        Harness h;
        h.setBody("\xC8\xC5\xD3\xD3\xD6");
        QVERIFY(!h.shown().contains("HELLO"));
        h.radio("charEbcdicRadio")->click();
        QVERIFY2(h.shown().contains("HELLO"), qPrintable(h.shown()));
    }

    // ---- COBOL -------------------------------------------------------------------------------------------------------

    void cobolWithoutACopyBookAsksAndKeepsTheViewWhenCancelled()
    {
        Harness h;
        h.setBody("ABCDEF");
        h.radio("viewHexRadio")->click();
        h.chooserAnswer.clear();
        h.radio("viewCobolRadio")->click();
        QCOMPARE(h.chooserCalls, 1);
        QCOMPARE(h.doc.shell().view, View::Hex);
        QVERIFY(h.radio("viewHexRadio")->isChecked());
        QVERIFY(!h.radio("viewCobolRadio")->isChecked());
        QVERIFY(h.doc.render().copybookPath.empty());
    }

    void cobolWithACopyBookShowsTheTable()
    {
        Harness h;
        QTemporaryFile book;
        QVERIFY(book.open());
        book.write("       01 REC.\n          05 NAME PIC X(4).\n          05 CODE PIC X(2).\n");
        book.flush();
        h.setBody("ABCDEF");
        h.chooserAnswer = book.fileName();
        h.radio("viewCobolRadio")->click();
        QCOMPARE(h.chooserCalls, 1);
        QCOMPARE(h.doc.shell().view, View::Cobol);
        QCOMPARE(QString::fromStdString(h.doc.render().copybookPath), book.fileName());
        QVERIFY(h.shown().contains("NAME"));
        QVERIFY(h.shown().contains("CODE"));
        QCOMPARE(h.shown(), expected(messageOf("ABCDEF"), View::Cobol, h.doc.render()));
        // with the copy book set, the radio no longer asks
        h.radio("viewHexRadio")->click();
        h.radio("viewCobolRadio")->click();
        QCOMPARE(h.chooserCalls, 1);
        QCOMPARE(h.doc.shell().view, View::Cobol);
    }

    void copyBookButtonAsksEvenWithOneSetAndRendersOnce()
    {
        Harness h;
        QTemporaryFile book1, book2;
        QVERIFY(book1.open() && book2.open());
        book1.write("       01 REC.\n          05 NAME PIC X(4).\n          05 CODE PIC X(2).\n");
        book1.flush();
        book2.write("       01 REC.\n          05 FIRST PIC X(3).\n          05 SECOND PIC X(3).\n");
        book2.flush();
        h.setBody("ABCDEF");
        h.chooserAnswer = book1.fileName();
        h.radio("viewCobolRadio")->click();
        QVERIFY(h.shown().contains("NAME"));
        h.chooserAnswer = book2.fileName();
        const int n = h.tab->renderCount();
        h.button("copybookButton")->click();
        QCOMPARE(h.chooserCalls, 2);
        QCOMPARE(QString::fromStdString(h.doc.render().copybookPath), book2.fileName());
        QVERIFY(h.shown().contains("FIRST"));
        QCOMPARE(h.tab->renderCount(), n + 1);
        // cancelled: nothing changes
        h.chooserAnswer.clear();
        h.button("copybookButton")->click();
        QCOMPARE(QString::fromStdString(h.doc.render().copybookPath), book2.fileName());
        QCOMPARE(h.tab->renderCount(), n + 1);
    }

    void copyBookDialogStartsInTheRememberedDirectory()
    {
        Harness h;
        QTemporaryDir books;
        h.settings.setLastDirectory("copybook", books.path());
        h.button("copybookButton")->click();
        QCOMPARE(h.chooserDirectory, books.path());
        // a chosen file is remembered
        QFile f(books.filePath("a.cpy"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("       01 R.\n          05 A PIC X.\n");
        f.close();
        h.chooserAnswer = books.filePath("a.cpy");
        h.button("copybookButton")->click();
        QCOMPARE(h.settings.lastDirectory("copybook"), books.path());
    }

    void renderFailureShowsItsText()
    {
        Harness h;
        h.setBody("ABC");
        rfh::format::RenderOptions o = h.doc.render();
        o.copybookPath = "/nonexistent/dir/missing.cpy";
        h.doc.setRender(o);
        h.useView(View::Cobol);
        std::string out;
        const rfh::Result r = rfh::format::render(h.doc.message(), View::Cobol, h.doc.render(), out);
        QVERIFY(!r.ok());
        QVERIFY(!r.text.empty());
        QCOMPARE(h.shown(), QString::fromStdString(r.text));
        // and a later good render replaces it
        h.radio("viewHexRadio")->click();
        QVERIFY(h.shown().contains("414243"));
    }

    // ---- when it renders -----------------------------------------------------------------------------------------

    void aHiddenTabRendersOnceWhenShown()
    {
        AppDocument doc;
        StatusLog log;
        SessionController controller(rfh_test::fakeApi());
        Operations ops(doc, controller, log);
        DataTab tab(doc, ops);
        rfh::format::RenderOptions o = doc.render();
        o.charFormat = CHAR_EBCDIC;
        doc.setRender(o);
        doc.setMessage(messageOf("hello"), "Message Data", false);
        doc.setMessage(messageOf("again"), "Message Data", false);
        QCOMPARE(tab.renderCount(), 0);
        tab.show();
        QCOMPARE(tab.renderCount(), 1);
        QVERIFY(tab.findChild<QPlainTextEdit *>("dataEdit")->toPlainText().length() > 0);
        tab.hide();
        doc.setMessage(messageOf("third"), "Message Data", false);
        QCOMPARE(tab.renderCount(), 1);
        tab.show();
        QCOMPARE(tab.renderCount(), 2);
        tab.show();
        QCOMPARE(tab.renderCount(), 2);
    }

    void aMessageWithItsFormatsRendersOnce()
    {
        Harness h;
        h.setBody("old");
        const int n = h.tab->renderCount();
        rfh::format::RenderOptions o = h.doc.render();
        o.charFormat = CHAR_EBCDIC;
        rfh::MqmdFields fields;
        h.doc.acceptMessage(messageOf("\xC8\xC5"), "Message Data (2) msg 1 of 1 from Q", true, fields, o);
        QCOMPARE(h.tab->renderCount(), n + 1);
        QVERIFY(h.shown().contains("HE"));
        QVERIFY(h.radio("charEbcdicRadio")->isChecked());
    }

    void readingAFileRendersOnce()
    {
        Harness h;
        QTemporaryDir dir;
        QFile f(dir.filePath("in.dat"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("file contents");
        f.close();
        h.doc.setMessage(messageOf("old"), "Message Data", false);
        const int n = h.tab->renderCount();
        QSignalSpy flows(&h.ops, &Operations::flowFinished);
        h.ops.openFile(f.fileName());
        QTRY_COMPARE_WITH_TIMEOUT(flows.count(), 1, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy(), 5000);
        QVERIFY(h.shown().contains("file contents"));
        QCOMPARE(h.tab->renderCount(), n + 1);
    }

    void largeBodiesAreAnnouncedBeforeTheyAreFormatted()
    {
        Harness h;
        QStringList lines;
        QObject::connect(h.tab.get(), &DataTab::statusMessage, [&](const QString & t) { lines << t; });
        h.setBody(std::string(DataTab::LargeBodyBytes - 1, 'x'));
        QVERIFY(lines.isEmpty());
        h.setBody(std::string(DataTab::LargeBodyBytes + 1, 'x'));
        QCOMPARE(lines.size(), 1);
        QVERIFY2(lines[0].contains("seconds"), qPrintable(lines[0]));
        QVERIFY(lines[0].contains(QString::number(DataTab::LargeBodyBytes + 1)));
    }

    // ---- messages for the status box ----------------------------------------------------------------------------

    void renderWarningsGoToTheStatusBox()
    {
        Harness h;
        QStringList lines;
        QObject::connect(h.tab.get(), &DataTab::statusMessage, [&](const QString & t) { lines << t; });
        h.setBody("not json");
        h.radio("viewJsonRadio")->click();
        QCOMPARE(lines, QStringList() << "Error parsing user data");
        QVERIFY(h.shown().isEmpty());
    }

    void theCopyBookIsOnlyReportedReadWhenTheRenderReadIt()
    {
        Harness h;
        QStringList lines;
        QObject::connect(h.tab.get(), &DataTab::statusMessage, [&](const QString & t) { lines << t; });
        QTemporaryFile book;
        QVERIFY(book.open());
        book.write("       01 REC.\n          05 NAME PIC X(4).\n          05 CODE PIC X(2).\n");
        book.flush();
        h.setBody("ABCDEF");
        // chosen while another view is shown: not read yet, so nothing is claimed
        h.chooserAnswer = book.fileName();
        h.button("copybookButton")->click();
        QVERIFY(lines.isEmpty());
        h.radio("viewCobolRadio")->click();
        QCOMPARE(lines, QStringList() << QString("copybook file read (%1)").arg(book.fileName()));
        QVERIFY(h.shown().contains("NAME"));

        // a file that cannot be read is an error, not "read"
        lines.clear();
        h.chooserAnswer = "/nonexistent/dir/missing.cpy";
        h.button("copybookButton")->click();
        QCOMPARE(lines.size(), 1);
        QVERIFY2(lines[0].contains("error reading copy book file"), qPrintable(lines[0]));
        QVERIFY(!lines[0].startsWith("copybook file read"));
    }

    void aSearchOnAFailedViewIsReportedAndDoesNotMove()
    {
        Harness h;
        QStringList lines;
        QObject::connect(h.tab.get(), &DataTab::statusMessage, [&](const QString & t) { lines << t; });
        h.setBody(std::string(rfh::format::kMaxRenderBody + 1, 'x'));		// every view refuses it
        QVERIFY(!h.shown().isEmpty());
        const QString failure = h.shown();
        lines.clear();
        h.tab->goToOffset(5);
        QCOMPARE(lines.size(), 1);
        QVERIFY2(lines[0].contains(failure), qPrintable(lines[0]));
        QVERIFY(!h.edit()->textCursor().hasSelection());
        h.tab->searchFor("x", true);
        QCOMPARE(lines.size(), 2);
        QVERIFY2(lines[1].contains(failure), qPrintable(lines[1]));
        QVERIFY(!h.edit()->textCursor().hasSelection());
    }

    // ---- fixed controls ----------------------------------------------------------------------------------------------

    void editingAndTheBomBoxAreNotPorted()
    {
        Harness h;
        h.setBody("abc");
        QVERIFY(h.edit()->isReadOnly());
        QVERIFY(!h.check("allowEditCheck")->isEnabled());
        QVERIFY(!h.button("writeQButton")->isEnabled());
        QCOMPARE(h.check("allowEditCheck")->toolTip(), QString("Not yet ported"));
        QCOMPARE(h.button("writeQButton")->toolTip(), QString("Not yet ported"));
        QVERIFY(!h.check("bomCheck")->isVisibleTo(h.tab.get()));
        QVERIFY(!h.check("bomCheck")->isEnabled());
        // still so after the enables are recomputed with a queue and a browse
        h.doc.setConnected(true);
        BrowseState b;
        b.active = true;
        b.position = 2;
        h.doc.setBrowse(b);
        QVERIFY(!h.check("allowEditCheck")->isEnabled());
        QVERIFY(!h.button("writeQButton")->isEnabled());
        QVERIFY(h.edit()->isReadOnly());
        QVERIFY(h.edit()->lineWrapMode() == QPlainTextEdit::NoWrap);
    }

    void topAndBottomMoveTheCursor()
    {
        Harness h;
        h.tab->setVisible(true);
        h.setBody(std::string(3200, 'x'));
        h.tab->scrollBottom();
        QCOMPARE(h.edit()->textCursor().blockNumber(), h.edit()->blockCount() - 1);
        QVERIFY(h.edit()->verticalScrollBar()->value() > 0);
        h.tab->scrollTop();
        QCOMPARE(h.edit()->textCursor().position(), 0);
        QCOMPARE(h.edit()->verticalScrollBar()->value(), 0);
    }

    void characterSetRadiosNeedTheirCodePage()
    {
        Harness h;
        struct Row { const char * name; int cp; };
        for (const Row & r : { Row{ "charChineseRadio", 936 }, Row{ "charKoreanRadio", 949 }, Row{ "charTradChineseRadio", 950 },
                               Row{ "charJapaneseRadio", 932 }, Row{ "charThaiRadio", 874 }, Row{ "charRussianRadio", 1251 } })
        {
            QCOMPARE(h.radio(r.name)->isEnabled(), rfh_is_codepage_installed(r.cp) != 0);
        }
        QVERIFY(h.radio("charAsciiRadio")->isEnabled());
        QVERIFY(h.radio("charEbcdicRadio")->isEnabled());
    }

    void browseButtonsFollowTheBrowseStateAndCallTheOperations()
    {
        Harness h;
        QVERIFY(!h.button("browseNextButton")->isEnabled());
        QVERIFY(!h.button("browsePrevButton")->isEnabled());
        h.doc.setConnected(true);
        BrowseState b;
        b.active = true;
        b.position = 1;
        h.doc.setBrowse(b);
        QVERIFY(h.button("browseNextButton")->isEnabled());
        QVERIFY(!h.button("browsePrevButton")->isEnabled());		// at the first message
        b.position = 3;
        h.doc.setBrowse(b);
        QVERIFY(h.button("browsePrevButton")->isEnabled());
        QSignalSpy flows(&h.ops, &Operations::flowFinished);
        h.button("browseNextButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(flows.count(), 1, 5000);
        QCOMPARE(flows.at(0).at(0).toString(), QString("browseNext"));
        QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy(), 5000);
        // the flow ended the browse (not connected); put the state back for Prev
        h.doc.setConnected(true);
        h.doc.setBrowse(b);
        flows.clear();
        h.button("browsePrevButton")->click();
        QTRY_COMPARE_WITH_TIMEOUT(flows.count(), 1, 5000);
        QCOMPARE(flows.at(0).at(0).toString(), QString("browsePrev"));
        QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy(), 5000);
    }

    // ---- keys --------------------------------------------------------------------------------------------------------

    void shortcutsChooseViewsAndOptionsOnlyWhileTheDataTabIsShown()
    {
        Harness h;
        h.setBody("abc");
        QTabWidget host;
        QWidget other;
        host.addTab(h.tab.get(), "Data");
        host.addTab(&other, "Other");
        host.resize(1000, 700);
        host.show();
        QVERIFY(QTest::qWaitForWindowActive(&host));
        const auto release = qScopeGuard([&]() { host.removeTab(0); h.tab->setParent(nullptr); });

        struct Key { Qt::Key key; Qt::KeyboardModifier mod; const char * radio; };
        const Key keys[] = {
            { Qt::Key_Q, Qt::ControlModifier, "viewParsedRadio" }, { Qt::Key_B, Qt::ControlModifier, "viewBothRadio" },
            { Qt::Key_T, Qt::ControlModifier, "viewCharacterRadio" }, { Qt::Key_L, Qt::ControlModifier, "viewXmlRadio" },
            { Qt::Key_H, Qt::ControlModifier, "viewHexRadio" }, { Qt::Key_O, Qt::AltModifier, "viewJsonRadio" },
            { Qt::Key_F, Qt::AltModifier, "viewFixRadio" }, { Qt::Key_E, Qt::ControlModifier, "charEbcdicRadio" },
            { Qt::Key_A, Qt::AltModifier, "charAsciiRadio" }, { Qt::Key_E, Qt::AltModifier, "charEbcdicRadio" },
            { Qt::Key_K, Qt::AltModifier, "charKoreanRadio" }, { Qt::Key_S, Qt::AltModifier, "charChineseRadio" },
            { Qt::Key_T, Qt::AltModifier, "charTradChineseRadio" }, { Qt::Key_H, Qt::AltModifier, "charThaiRadio" },
            { Qt::Key_J, Qt::AltModifier, "charJapaneseRadio" } };
        host.setCurrentIndex(0);
        h.radio("charAsciiRadio")->click();
        for (const Key & k : keys)
        {
            QRadioButton * r = h.radio(k.radio);
            if (!r->isEnabled())
            {
                continue;		// the code page is not installed here
            }
            // start from a different choice in the same group, so that the key has something to do
            const QString name = k.radio;
            h.radio(name.startsWith("view") ? (name == "viewFixRadio" ? "viewCharacterRadio" : "viewFixRadio")
                                            : (name == "charAsciiRadio" ? "charEbcdicRadio" : "charAsciiRadio"))->click();
            QVERIFY(!r->isChecked());
            QTest::keyClick(&host, k.key, k.mod);
            // a mnemonic presses its button with a short animation, so it takes a moment
            QTRY_VERIFY2_WITH_TIMEOUT(r->isChecked(), qPrintable(QString("%1 for %2").arg(QKeySequence(k.key | k.mod).toString(), k.radio)), 2000);
        }
        // the Ctrl+K COBOL key asks for a copy book (the stub cancels, so the view stays)
        h.radio("viewHexRadio")->click();
        h.chooserCalls = 0;
        QTest::keyClick(&host, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(h.chooserCalls, 1);
        QCOMPARE(h.doc.shell().view, View::Hex);
        // Ctrl+Y and Alt+Y read a copy book
        QTest::keyClick(&host, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(h.chooserCalls, 2);
        QTest::keyClick(&host, Qt::Key_Y, Qt::AltModifier);		// the Copybook button's mnemonic: pressed with an animation
        QTRY_COMPARE_WITH_TIMEOUT(h.chooserCalls, 3, 2000);
        // Ctrl+D and Ctrl+I toggle the packed decimal and integer formats
        QVERIFY(h.radio("pdPcRadio")->isChecked());
        QTest::keyClick(&host, Qt::Key_D, Qt::ControlModifier);
        QVERIFY(h.radio("pdHostRadio")->isChecked());
        QTest::keyClick(&host, Qt::Key_D, Qt::ControlModifier);
        QVERIFY(h.radio("pdPcRadio")->isChecked());
        QVERIFY(h.radio("intPcRadio")->isChecked());
        QTest::keyClick(&host, Qt::Key_I, Qt::ControlModifier);
        QVERIFY(h.radio("intHostRadio")->isChecked());
        QTest::keyClick(&host, Qt::Key_I, Qt::ControlModifier);
        QVERIFY(h.radio("intPcRadio")->isChecked());

        // another tab current: the same keys do nothing
        host.setCurrentIndex(1);
        h.radio("viewHexRadio")->click();
        h.radio("charAsciiRadio")->click();
        h.chooserCalls = 0;
        for (const Key & k : keys)
        {
            QTest::keyClick(&host, k.key, k.mod);
        }
        QTest::keyClick(&host, Qt::Key_K, Qt::ControlModifier);
        QTest::keyClick(&host, Qt::Key_Y, Qt::ControlModifier);
        QTest::keyClick(&host, Qt::Key_D, Qt::ControlModifier);
        QTest::qWait(300);		// longer than a mnemonic's button animation
        QCOMPARE(h.doc.shell().view, View::Hex);
        QCOMPARE(h.doc.render().charFormat, (int)CHAR_ASCII);
        QCOMPARE(h.doc.render().pdNumericFormat, (int)NUMERIC_PC);
        QCOMPARE(h.chooserCalls, 0);
    }

    void browseAndClearKeys()
    {
        Harness h;
        QTabWidget host;
        host.addTab(h.tab.get(), "Data");
        host.resize(1000, 700);
        host.show();
        QVERIFY(QTest::qWaitForWindowActive(&host));
        const auto release = qScopeGuard([&]() { host.removeTab(0); h.tab->setParent(nullptr); });
        h.setBody("abc");
        h.doc.setConnected(true);
        BrowseState b;
        b.active = true;
        b.position = 3;
        h.doc.setBrowse(b);
        QSignalSpy flows(&h.ops, &Operations::flowFinished);
        auto expectFlow = [&](Qt::Key key, Qt::KeyboardModifier mod, const char * flow)
        {
            h.doc.setConnected(true);		// every flow here ends the browse; start each from the same state
            h.doc.setBrowse(b);
            host.activateWindow();
            flows.clear();
            QTest::keyClick(&host, key, mod);
            QTRY_VERIFY_WITH_TIMEOUT(flows.count() > 0, 5000);
            QCOMPARE(flows.at(0).at(0).toString(), QString(flow));
            QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy(), 5000);
        };
        expectFlow(Qt::Key_N, Qt::ControlModifier, "browseNext");
        expectFlow(Qt::Key_R, Qt::ControlModifier, "browsePrev");
        expectFlow(Qt::Key_M, Qt::ControlModifier, "endBrowse");
        expectFlow(Qt::Key_V, Qt::AltModifier, "endBrowse");
        h.doc.setBrowse(BrowseState());
        QVERIFY(h.doc.hasData());
        QTest::keyClick(&host, Qt::Key_X, Qt::AltModifier);
        QTRY_VERIFY_WITH_TIMEOUT(!h.doc.hasData(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!h.controller.busy(), 5000);
    }

    // ---- speed (informational) -------------------------------------------------------------------------------------

    // Set RFHUTIL_MEASURE=1 to print where the time of a 10 MB body goes in each view (skipped otherwise).
    void tenMegabytesWhereTheTimeGoes()
    {
        if (qEnvironmentVariableIsEmpty("RFHUTIL_MEASURE"))
        {
            QSKIP("set RFHUTIL_MEASURE=1 to measure");
        }
        Harness h;
        h.tab->setVisible(true);
        std::string big(10 * 1024 * 1024, 'x');
        for (size_t i = 0; i < big.size(); i += 61)
        {
            big[i] = (char)('A' + (i / 61) % 26);
        }
        const rfh::Message m = messageOf(big);
        for (View v : { View::Character, View::Hex, View::Both })
        {
            QElapsedTimer t;
            t.start();
            std::string out;
            QVERIFY(rfh::format::render(m, v, h.doc.render(), out).ok());
            const qint64 renderMs = t.restart();
            std::vector<RFH_UCS2> units(out.size());
            const int n = rfh_MultiByteToWideChar(1252, 0, out.data(), (int)out.size(), units.data(), (int)out.size());
            QString text(reinterpret_cast<const QChar *>(units.data()), n);
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            const qint64 decodeMs = t.restart();
            h.edit()->setPlainText(text);
            const qint64 setMs = t.restart();
            QCoreApplication::processEvents();
            const qint64 layoutMs = t.restart();
            printf("10 MB view %d: render %lld ms, decode %lld ms, setPlainText %lld ms, events %lld ms; %zu bytes out, %d lines\n",
                   (int)v, (long long)renderMs, (long long)decodeMs, (long long)setMs, (long long)layoutMs, out.size(), h.edit()->blockCount());
            fflush(stdout);
        }
    }
};

QTEST_MAIN(DataTabTests)
#include "DataTabTests.moc"
