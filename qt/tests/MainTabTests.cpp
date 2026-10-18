// MainTabTests.cpp - the Main tab (MainTab), on the fake MQI with the real Operations, AppDocument and SessionController.
//
// Expectations are written by hand from the Windows dialog (RFHUtil/rfhutil.rc IDD_MAIN line 297, STRINGTABLE tool tips
// 1690-2460, General.cpp UpdatePageData 504-620, DoDataExchange 172-206, PreTranslateMessage 1411) and the inventory 2.1 and 7.1.

#include <QtTest>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QGroupBox>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "AppDocument.h"
#include "AppSettings.h"
#include "ConnUserDialog.h"
#include "MainTab.h"
#include "Operations.h"
#include "SessionController.h"
#include "StatusLog.h"
#include "fake_mq.h"
#include "pcf_server.h"

using namespace rfhqt;

namespace
{
    rfh_test::FakeMq & fake = rfh_test::fakeMq();

    const char * kQm = "QM1";
    const char * kQueue = "RFHUTIL.TEST.TAB";

    class Watchdog
    {
    public:
        explicit Watchdog(int seconds) : deadline(seconds), thread([this]() { run(); }) {}
        ~Watchdog()
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                done = true;
            }
            cv.notify_all();
            thread.join();
        }

    private:
        void run()
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!cv.wait_for(lock, std::chrono::seconds(deadline), [this]() { return done; }))
            {
                printf("WATCHDOG: the test hung for %d s; aborting\n", deadline);
                fflush(stdout);
                std::_Exit(3);
            }
        }
        int deadline;
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        std::thread thread;
    };

    class StubChooser : public FileChooser
    {
    public:
        QString openPath(QWidget *, const QString & directory) override { openDirectory = directory; return openAnswer; }
        QString savePath(QWidget *, const QString & directory) override { saveDirectory = directory; return saveAnswer; }
        QString openAnswer, saveAnswer, openDirectory, saveDirectory;
    };

    // Closes any modal dialog that a test did not expect, so a mistake fails instead of hanging.
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

    rfh_test::FakeMessage fakeMessage(const std::string & body)
    {
        rfh_test::FakeMessage m;
        memcpy(m.md.Format, MQFMT_STRING, 8);
        m.md.CodedCharSetId = 1208;
        m.md.Encoding = 546;
        memset(m.md.MsgId, 0x11, sizeof(m.md.MsgId));
        m.md.Persistence = MQPER_NOT_PERSISTENT;
        m.data.assign(body.begin(), body.end());
        return m;
    }

    struct Harness
    {
        explicit Harness(bool useSettings = true, const std::function<void(AppSettings &)> & prepare = nullptr)
            : settings(dir.filePath("rfhutil.conf")), controller(rfh_test::fakeApi()), ops(doc, controller, log),
              finished(&ops, &Operations::flowFinished)
        {
            if (useSettings && prepare)
            {
                prepare(settings);
            }
            tab.reset(new MainTab(doc, ops, log, useSettings ? &settings : nullptr));
            tab->setQueueManagerLister([]() { return QStringList() << "LOCAL.B" << "LOCAL.A"; });
            tab->setFileChooser(&chooser);
        }

        template <typename T>
        T * w(const char * name) const
        {
            T * found = tab->findChild<T *>(name);
            if (found == nullptr)
            {
                printf("no widget named %s\n", name);
            }
            return found;
        }

        QPushButton * button(const char * name) const { return w<QPushButton>(name); }
        QLineEdit * edit(const char * name) const { return w<QLineEdit>(name); }
        QCheckBox * check(const char * name) const { return w<QCheckBox>(name); }
        QRadioButton * radio(const char * name) const { return w<QRadioButton>(name); }
        QComboBox * combo(const char * name) const { return w<QComboBox>(name); }

        void type(const char * qm, const char * queue)
        {
            combo("qmCombo")->setEditText(qm);
            combo("queueCombo")->setEditText(queue);
        }

        bool run(const std::function<void()> & start)
        {
            finished.clear();
            start();
            return finished.count() > 0 || finished.wait(5000);
        }

        void settle()
        {
            QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 5000);
            QCoreApplication::processEvents();
        }

        QStringList entries() const
        {
            static const QRegularExpression stamp("^\\d\\d\\.\\d\\d\\.\\d\\d ");
            QStringList entries;
            for (const QString & line : log.text().split("\r\n"))
            {
                if (stamp.match(line).hasMatch())
                {
                    entries << line.mid(9);
                }
                else if (!entries.isEmpty())
                {
                    entries.last() += "\r\n" + line;
                }
            }
            return entries;
        }

        QTemporaryDir dir;
        AppSettings settings;
        AppDocument doc;
        StatusLog log;
        SessionController controller;
        Operations ops;
        QSignalSpy finished;
        StubChooser chooser;
        std::unique_ptr<MainTab> tab;
    };

    struct Row
    {
        const char * name;
        const char * caption;
        char mnemonic;
    };

    // The captions of rfhutil.rc:297-380, verbatim with their '&'.
    const Row kButtons[] = {
        { "readQButton", "Read &Q", 'q' }, { "writeQButton", "&Write Q", 'w' }, { "browseQButton", "&Browse Q", 'b' },
        { "startBrowseButton", "S&tart Browse", 't' }, { "browseNextButton", "Browse &Next", 'n' },
        { "browsePrevButton", "Browse P&rev", 'r' }, { "endBrowseButton", "End Browse", 0 }, { "closeQButton", "Close Q", 0 },
        { "saveQButton", "Save Q", 0 }, { "loadQButton", "&Load Q", 'l' }, { "moveQButton", "Move Q", 0 },
        { "purgeQButton", "Purg&e Q", 'e' }, { "displayQButton", "&Display Q", 'd' }, { "openFileButton", "&Open File", 'o' },
        { "saveFileButton", "&Save File", 's' }, { "clearDataButton", "Clear Data", 0 }, { "clearAllButton", "Clear All", 0 },
        { "loadNamesButton", "Load Names", 0 }, { "setConnIdButton", "Set Conn &Id", 'i' }, { "exitButton", "Exit", 0 },
    };
    const Row kChecks[] = {
        { "newMsgIdCheck", "New Msg Id", 0 }, { "getByMsgIdCheck", "Get by Msg Id", 0 }, { "getByCorrelIdCheck", "Get by Correlid", 0 },
        { "getByGroupIdCheck", "Get by Group Id", 0 }, { "setIdentityCheck", "Set Iden Context", 0 }, { "setAllCheck", "Set All Context", 0 },
        { "newCorrelIdCheck", "New Correl Id", 0 }, { "logicalOrderCheck", "Logical Order", 0 }, { "completeMsgCheck", "Complete Msg", 0 },
        { "allAvailCheck", "All Avail", 0 }, { "convertCheck", "Convert", 0 }, { "alternateUserCheck", "Alternate User Id", 0 },
    };
    const Row kRadios[] = {
        { "propsAsQueueRadio", "As Queue", 0 }, { "propsNoneRadio", "None", 0 }, { "propsYesRadio", "Yes", 0 },
        { "propsRfh2Radio", "RFH2", 0 }, { "propsCompatRadio", "Compat", 0 },
        { "bindAsQueueRadio", "As Queue", 0 }, { "bindOnOpenRadio", "Bind Open", 0 }, { "bindNotFixedRadio", "Not Fixed", 0 },
        { "bindGroupRadio", "Group", 0 },
        { "closeNoneRadio", "None", 0 }, { "closeDeleteRadio", "Delete", 0 }, { "closePurgeRadio", "Purge", 0 },
    };
    const Row kLabels[] = {
        { "qmLabel", "Queue Manager Name (to connect to)", 0 }, { "queueLabel", "Queue Name", 0 },
        { "remoteQmLabel", "Remote Queue Manager Name (remote queues only)", 0 }, { "selectorLabel", "Selector", 0 },
        { "fileCodePageLabel", "File Code Page", 0 }, { "fileNameLabel", "File Name", 0 }, { "fileSizeLabel", "Data Size", 0 },
        { "copyBookLabel", "COBOL Copy Book File Name", 0 }, { "queueTypeLabel", "Queue Type", 0 }, { "queueDepthLabel", "Queue depth", 0 },
    };
}

class MainTabTests : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        fake.reset();
        fake.queue(kQueue);
    }

    // ---- the controls -----------------------------------------------------------------------------------------------

    void everyControlHasTheWindowsCaptionAndMnemonic()
    {
        Harness h;
        for (const Row & r : kButtons)
        {
            QPushButton * b = h.button(r.name);
            QVERIFY2(b != nullptr, r.name);
            QCOMPARE(b->text(), QString(r.caption));
            QCOMPARE(QKeySequence::mnemonic(b->text()), r.mnemonic ? QKeySequence(Qt::ALT | (Qt::Key_A + (r.mnemonic - 'a'))) : QKeySequence());
        }
        for (const Row & r : kChecks)
        {
            QVERIFY2(h.check(r.name) != nullptr, r.name);
            QCOMPARE(h.check(r.name)->text(), QString(r.caption));
        }
        for (const Row & r : kRadios)
        {
            QVERIFY2(h.radio(r.name) != nullptr, r.name);
            QCOMPARE(h.radio(r.name)->text(), QString(r.caption));
        }
        for (const Row & r : kLabels)
        {
            QLabel * l = h.w<QLabel>(r.name);
            QVERIFY2(l != nullptr, r.name);
            QCOMPARE(l->text(), QString(r.caption));
        }
        QCOMPARE(h.w<QGroupBox>("putGetGroup")->title(), QString("Put/Get Options"));
        QCOMPARE(h.w<QGroupBox>("propsGroup")->title(), QString("User Props"));
        QCOMPARE(h.w<QGroupBox>("bindGroup")->title(), QString("Cluster Open"));
        QCOMPARE(h.w<QGroupBox>("closeGroup")->title(), QString("Close Options"));
    }

    void theWindowsToolTipsAreKept()
    {
        Harness h;
        QCOMPARE(h.combo("qmCombo")->toolTip(), QString("Name of the queue manager to connect to"));
        QCOMPARE(h.button("readQButton")->toolTip(), QString("Read the next message from the specified queue"));
        QCOMPARE(h.button("endBrowseButton")->toolTip(), QString("End the currently active browse operation (Ctrl+M)"));
        QCOMPARE(h.button("clearAllButton")->toolTip(), QString("Clear the current data buffer and all other fields (MQMD, etc) (Alt+Y)"));
        QCOMPARE(h.check("getByMsgIdCheck")->toolTip(), QString("Get by Message Id (Set on MQMD page)"));
        QCOMPARE(h.radio("closePurgeRadio")->toolTip(), QString("If temporary dynamic queue purge messages and delete queue after close"));
        QCOMPARE(h.edit("fileCodePageEdit")->toolTip(), QString("Code page to assume when reading a file"));
    }

    void phaseOneItemsAreDisabledAndSayNotYetPorted()
    {
        Harness h;
        for (QWidget * w : { (QWidget *)h.button("saveQButton"), (QWidget *)h.button("loadQButton"), (QWidget *)h.button("moveQButton"),
                             (QWidget *)h.button("displayQButton"), (QWidget *)h.radio("propsYesRadio") })
        {
            QVERIFY2(!w->isEnabled(), qPrintable(w->objectName()));
            QCOMPARE(w->toolTip(), QString("Not yet ported"));
        }
        // and they stay disabled whatever the document does
        h.doc.setConnected(true);
        BrowseState idle;
        h.doc.setBrowse(idle);
        QVERIFY(!h.button("saveQButton")->isEnabled());
        QVERIFY(!h.radio("propsYesRadio")->isEnabled());
        QVERIFY(h.radio("propsNoneRadio")->isEnabled());
    }

    void readOnlyFieldsAreReadOnly()
    {
        Harness h;
        for (const char * name : { "fileNameEdit", "fileSizeEdit", "copyBookEdit", "queueDepthEdit", "queueTypeEdit" })
        {
            QVERIFY2(h.edit(name)->isReadOnly(), name);
            QCOMPARE(h.edit(name)->focusPolicy(), Qt::ClickFocus);		// NOT WS_TABSTOP
        }
        QVERIFY(h.w<QPlainTextEdit>("statusEdit")->isReadOnly());
        for (const char * name : { "qmCombo", "queueCombo" })
        {
            QVERIFY2(h.combo(name)->isEditable(), name);
        }
        QVERIFY(!h.edit("remoteQmEdit")->isReadOnly());
        QVERIFY(!h.edit("selectorEdit")->isReadOnly());
        QVERIFY(!h.edit("fileCodePageEdit")->isReadOnly());
    }

    void defaultsAreThoseOfTheWindowsConstructor()
    {
        Harness h(false);
        for (const Row & r : kChecks)
        {
            QCOMPARE(h.check(r.name)->isChecked(), QString(r.name) == "newMsgIdCheck");
        }
        QVERIFY(h.radio("propsAsQueueRadio")->isChecked());
        QVERIFY(h.radio("bindAsQueueRadio")->isChecked());
        QVERIFY(h.radio("closeNoneRadio")->isChecked());
        QCOMPARE(h.edit("fileCodePageEdit")->text(), QString("437"));
        QCOMPARE(h.combo("qmCombo")->currentText(), QString());
        QCOMPARE(h.combo("queueCombo")->currentText(), QString());
        QCOMPARE(h.edit("remoteQmEdit")->text(), QString());
        QCOMPARE(h.edit("selectorEdit")->text(), QString());
        QCOMPARE(h.edit("fileSizeEdit")->text(), QString("0"));
        QCOMPARE(h.edit("queueDepthEdit")->text(), QString());
        QCOMPARE(h.w<QSpinBox>("waitSpin")->value(), 0);
        QVERIFY(h.radio("bindingsRadio")->isChecked());
        const MainOptions o = h.tab->options();
        QVERIFY(o.put.newMsgId);
        QVERIFY(o.write.includeMqmd);
        QCOMPARE(o.get.waitMs, (MQLONG)0);
        QCOMPARE((int)o.connect.mode, (int)rfh::Mode::Bindings);
    }

    void editLimitsAreTheWindowsLimits()
    {
        Harness h(false);
        QCOMPARE(h.combo("qmCombo")->lineEdit()->maxLength(), 48);				// LimitText(MQ_Q_NAME_LENGTH), bindings
        h.radio("clientRadio")->setChecked(true);
        QCOMPARE(h.combo("qmCombo")->lineEdit()->maxLength(), 255);				// a client string can be 255
        h.radio("bindingsRadio")->setChecked(true);
        QCOMPARE(h.combo("qmCombo")->lineEdit()->maxLength(), 48);
        QCOMPARE(h.combo("queueCombo")->lineEdit()->maxLength(), 255);			// DDV_MaxChars(m_q_name, 255)
        QCOMPARE(h.edit("remoteQmEdit")->maxLength(), 48);						// DDV_MaxChars(m_remote_qm, 48)
        QCOMPARE(h.edit("fileCodePageEdit")->maxLength(), 5);					// DDV_MaxChars(m_file_codepage, 5)
        h.edit("fileCodePageEdit")->clear();
        QTest::keyClicks(h.edit("fileCodePageEdit"), "12a34567");
        QCOMPARE(h.edit("fileCodePageEdit")->text(), QString("12345"));		// ES_NUMBER, five characters
    }

    void tabOrderFollowsTheTemplate()
    {
        Harness h(false);
        // rfhutil.rc:297-380 in template order, then the new controls, Exit last as in the template; statusEdit is reachable
        // by keyboard (deviation: NOT WS_TABSTOP on Windows)
        const QStringList expected = {
            "qmCombo", "queueCombo", "remoteQmEdit", "selectorEdit", "fileCodePageEdit", "waitSpin", "cancelButton",
            "newMsgIdCheck", "getByMsgIdCheck", "getByCorrelIdCheck", "getByGroupIdCheck", "setIdentityCheck", "setAllCheck",
            "newCorrelIdCheck", "logicalOrderCheck", "completeMsgCheck", "allAvailCheck", "convertCheck", "alternateUserCheck",
            "propsAsQueueRadio", "propsNoneRadio", "propsYesRadio", "propsRfh2Radio", "propsCompatRadio",
            "bindAsQueueRadio", "bindOnOpenRadio", "bindNotFixedRadio", "bindGroupRadio",
            "closeNoneRadio", "closeDeleteRadio", "closePurgeRadio",
            "readQButton", "writeQButton", "browseQButton", "startBrowseButton", "browseNextButton", "browsePrevButton",
            "endBrowseButton", "closeQButton", "saveQButton", "loadQButton", "moveQButton", "purgeQButton", "displayQButton",
            "openFileButton", "saveFileButton", "clearDataButton", "clearAllButton", "loadNamesButton", "setConnIdButton",
            "bindingsRadio", "clientRadio", "channelEdit", "connNameEdit", "ccdtEdit", "exitButton", "statusEdit" };
        QStringList chain;
        QWidget * start = h.combo("qmCombo");
        QWidget * w = start;
        do
        {
            if (w->parentWidget() == h.tab.get() || w->parentWidget() == nullptr || qobject_cast<QGroupBox *>(w->parentWidget()))
            {
                if (!w->objectName().isEmpty() && w->focusPolicy() & Qt::TabFocus)
                {
                    chain << w->objectName();
                }
            }
            w = w->nextInFocusChain();
        } while (w != start);
        QCOMPARE(chain, expected);
    }

    void everyInteractiveControlHasAnAccessibleName()
    {
        Harness h(false);
        int checked = 0;
        for (QWidget * w : h.tab->findChildren<QWidget *>())
        {
            const bool interactive = qobject_cast<QAbstractButton *>(w) || qobject_cast<QComboBox *>(w) || qobject_cast<QSpinBox *>(w)
                || qobject_cast<QPlainTextEdit *>(w)
                || (qobject_cast<QLineEdit *>(w) && !qobject_cast<QComboBox *>(w->parentWidget()) && !qobject_cast<QSpinBox *>(w->parentWidget()));
            if (interactive && !w->objectName().startsWith("qt_"))
            {
                QVERIFY2(!w->accessibleName().isEmpty(), qPrintable(w->objectName()));
                checked++;
            }
        }
        QVERIFY(checked >= 55);
        QCOMPARE(h.button("readQButton")->accessibleName(), QString("Read Q"));
    }

    // ---- the options ----------------------------------------------------------------------------------------------------

    void optionsRoundTripEveryControl()
    {
        Harness h(false);
        h.check("newMsgIdCheck")->setChecked(false);
        h.check("newCorrelIdCheck")->setChecked(true);
        h.check("getByMsgIdCheck")->setChecked(true);
        h.check("getByCorrelIdCheck")->setChecked(true);
        h.check("getByGroupIdCheck")->setChecked(true);
        h.check("logicalOrderCheck")->setChecked(true);
        h.check("completeMsgCheck")->setChecked(true);
        h.check("allAvailCheck")->setChecked(true);
        h.check("convertCheck")->setChecked(true);
        h.edit("selectorEdit")->setText("color = 'red'");
        h.edit("fileCodePageEdit")->setText("1252");
        h.w<QSpinBox>("waitSpin")->setValue(7);
        h.radio("propsRfh2Radio")->setChecked(true);
        h.radio("closeDeleteRadio")->setChecked(true);
        h.radio("bindNotFixedRadio")->setChecked(true);
        MainOptions o = h.tab->options();
        QVERIFY(!o.put.newMsgId);
        QVERIFY(o.put.newCorrelId);
        QVERIFY(o.put.logicalOrder);
        QCOMPARE((int)o.put.bind, (int)rfh::BindOption::NotFixed);
        QVERIFY(o.get.matchMsgId && o.get.matchCorrelId && o.get.matchGroupId);
        QVERIFY(o.get.logicalOrder && o.get.completeMsg && o.get.allAvailable && o.get.convert);
        QCOMPARE(o.get.selector, std::string("color = 'red'"));
        QCOMPARE(o.get.waitMs, (MQLONG)7000);
        QCOMPARE((int)o.get.properties, (int)rfh::PropertiesMode::Rfh2);
        QVERIFY(o.get.inputShared);
        QCOMPARE((int)o.closeOption, (int)rfh::CloseOption::Delete);
        QCOMPARE(o.fileCodePage, 1252);
        h.edit("fileCodePageEdit")->clear();
        QCOMPARE(h.tab->options().fileCodePage, 0);		// empty = decide from the data
    }

    void eachClusterOpenRadioSelectsItsBindOption()
    {
        Harness h(false);
        const std::pair<const char *, rfh::BindOption> table[] = {
            { "bindAsQueueRadio", rfh::BindOption::AsQueueDefinition }, { "bindOnOpenRadio", rfh::BindOption::OnOpen },
            { "bindNotFixedRadio", rfh::BindOption::NotFixed }, { "bindGroupRadio", rfh::BindOption::OnGroup } };
        for (const auto & row : table)
        {
            h.radio(row.first)->setChecked(true);
            QCOMPARE((int)h.tab->options().put.bind, (int)row.second);
        }
    }

    void userPropsAndCloseRadiosMapToTheCoreValues()
    {
        Harness h(false);
        h.radio("propsNoneRadio")->setChecked(true);
        QCOMPARE((int)h.tab->options().get.properties, (int)rfh::PropertiesMode::None);
        h.radio("propsCompatRadio")->setChecked(true);
        QCOMPARE((int)h.tab->options().get.properties, (int)rfh::PropertiesMode::Compat);
        h.radio("propsAsQueueRadio")->setChecked(true);
        QCOMPARE((int)h.tab->options().get.properties, (int)rfh::PropertiesMode::AsQueue);
        h.radio("closePurgeRadio")->setChecked(true);
        QCOMPARE((int)h.tab->options().closeOption, (int)rfh::CloseOption::Purge);
    }

    void aBindingsConnectionCarriesNoClientValues()
    {
        Harness h(false);
        h.edit("channelEdit")->setText("SYSTEM.DEF.SVRCONN");
        h.edit("connNameEdit")->setText("host(1414)");
        h.edit("ccdtEdit")->setText("/tmp/ccdt.json");
        MainOptions o = h.tab->options();
        QVERIFY(o.connect.channel.empty() && o.connect.connName.empty() && o.connect.ccdtUrl.empty());
        h.radio("clientRadio")->setChecked(true);
        o = h.tab->options();
        QCOMPARE((int)o.connect.mode, (int)rfh::Mode::Client);
        QCOMPARE(o.connect.channel, std::string("SYSTEM.DEF.SVRCONN"));
        QCOMPARE(o.connect.connName, std::string("host(1414)"));
        QCOMPARE(o.connect.ccdtUrl, std::string("/tmp/ccdt.json"));
        QVERIFY(h.edit("channelEdit")->isEnabled());
        h.radio("bindingsRadio")->setChecked(true);
        QVERIFY(!h.edit("channelEdit")->isEnabled());
    }

    // ---- enabled states ---------------------------------------------------------------------------------------------------

    void expectMatchesEnables(Harness & h)
    {
        const Enables e = h.doc.enables();
        const std::pair<const char *, bool> table[] = {
            { "openFileButton", e.openFile }, { "saveFileButton", e.saveFile }, { "clearDataButton", e.clearData },
            { "clearAllButton", e.clearAll }, { "readQButton", e.readQ }, { "purgeQButton", e.purgeQ },
            { "writeQButton", e.writeQ }, { "browseQButton", e.browseQ }, { "startBrowseButton", e.startBrowse },
            { "browseNextButton", e.browseNext }, { "browsePrevButton", e.browsePrev }, { "endBrowseButton", e.endBrowse },
            { "closeQButton", e.closeQ }, { "loadNamesButton", e.loadNames }, { "setConnIdButton", e.setConnId },
            { "exitButton", e.exit }, { "cancelButton", e.cancel }, { "displayQButton", e.displayQ }, { "loadQButton", e.loadQ },
            { "saveQButton", e.saveQ }, { "moveQButton", e.moveQ }, { "logicalOrderCheck", e.getOptions },
            { "completeMsgCheck", e.getOptions }, { "allAvailCheck", e.getOptions }, { "convertCheck", e.getOptions },
            { "propsAsQueueRadio", e.propsRadios }, { "propsNoneRadio", e.propsRadios }, { "propsRfh2Radio", e.propsRadios },
            { "propsCompatRadio", e.propsRadios } };
        for (const auto & row : table)
        {
            QWidget * w = h.tab->findChild<QWidget *>(row.first);
            QVERIFY2(w != nullptr, row.first);
            QVERIFY2(w->isEnabled() == row.second, row.first);
        }
        QCOMPARE(h.edit("selectorEdit")->isReadOnly(), !e.selectorEditable);
        QVERIFY(!h.radio("propsYesRadio")->isEnabled());
        // the Put/Get boxes that Windows never switches
        for (const char * name : { "newMsgIdCheck", "getByMsgIdCheck", "getByCorrelIdCheck", "getByGroupIdCheck", "setIdentityCheck",
                                   "setAllCheck", "newCorrelIdCheck", "alternateUserCheck" })
        {
            QVERIFY2(h.check(name)->isEnabled(), name);
        }
        for (const char * name : { "bindAsQueueRadio", "bindGroupRadio", "closeNoneRadio", "closePurgeRadio" })
        {
            QVERIFY2(h.radio(name)->isEnabled(), name);
        }
    }

    void enabledStatesAreExactlyWhatEnablesForSays()
    {
        Harness h(false);
        // idle, not connected (General.cpp:583-612 and 615)
        expectMatchesEnables(h);
        QVERIFY(h.button("readQButton")->isEnabled());
        QVERIFY(!h.button("closeQButton")->isEnabled());
        QVERIFY(!h.button("browseNextButton")->isEnabled());
        QVERIFY(!h.button("cancelButton")->isEnabled());
        QVERIFY(h.button("exitButton")->isEnabled());

        // connected: Close Q follows the connection
        h.doc.setConnected(true);
        expectMatchesEnables(h);
        QVERIFY(h.button("closeQButton")->isEnabled());

        // first message of a browse: Next and End, not Prev (General.cpp:546-582)
        BrowseState b;
        b.active = true;
        b.position = 1;
        h.doc.setBrowse(b);
        expectMatchesEnables(h);
        QVERIFY(!h.button("readQButton")->isEnabled());
        QVERIFY(!h.button("openFileButton")->isEnabled());
        QVERIFY(!h.button("loadNamesButton")->isEnabled());
        QVERIFY(!h.button("closeQButton")->isEnabled());
        QVERIFY(h.button("browseNextButton")->isEnabled());
        QVERIFY(h.button("endBrowseButton")->isEnabled());
        QVERIFY(!h.button("browsePrevButton")->isEnabled());
        QVERIFY(!h.check("convertCheck")->isEnabled());
        QVERIFY(!h.radio("propsNoneRadio")->isEnabled());
        QVERIFY(h.button("saveFileButton")->isEnabled());
        QVERIFY(h.check("newMsgIdCheck")->isEnabled());

        // second message: Prev too
        b.position = 2;
        h.doc.setBrowse(b);
        expectMatchesEnables(h);
        QVERIFY(h.button("browsePrevButton")->isEnabled());

        // a job runs: the starters go off, Cancel only when the job can be cancelled
        h.doc.setBrowse(BrowseState());
        ShellState s = h.doc.shell();
        s.busy = true;
        h.doc.setShell(s);
        expectMatchesEnables(h);
        QVERIFY(!h.button("readQButton")->isEnabled());
        QVERIFY(!h.button("cancelButton")->isEnabled());
        s.cancellable = true;
        h.doc.setShell(s);
        expectMatchesEnables(h);
        QVERIFY(h.button("cancelButton")->isEnabled());

        // no properties support: the selector is read only
        s.busy = s.cancellable = false;
        s.propertiesSupported = false;
        h.doc.setShell(s);
        expectMatchesEnables(h);
        QVERIFY(h.edit("selectorEdit")->isReadOnly());
    }

    void theDocumentValuesAreShown()
    {
        Harness h(false);
        rfh::Message m;
        m.body.assign(1234, 'x');
        h.doc.setMessage(std::move(m), "Message Data (1234)", true);
        h.doc.setFilePath("/tmp/some dir/msg.dat");
        rfh::QueueInfo info;
        info.type = MQQT_ALIAS;
        info.depth = 17;
        h.doc.setQueueInfo(info);
        QCOMPARE(h.edit("fileSizeEdit")->text(), QString("1234"));
        QCOMPARE(h.edit("fileNameEdit")->text(), QString("/tmp/some dir/msg.dat"));
        QCOMPARE(h.edit("queueDepthEdit")->text(), QString("17"));
        QCOMPARE(h.edit("queueTypeEdit")->text(), QString("Alias"));
        rfh::format::RenderOptions render = h.doc.render();
        render.copybookPath = "/tmp/book.cpy";
        h.doc.setRender(render);
        QCOMPARE(h.edit("copyBookEdit")->text(), QString("/tmp/book.cpy"));
        info.depth = -1;				// unknown: UpdatePageData shows nothing for a negative depth
        info.type = MQQT_MODEL;
        h.doc.setQueueInfo(info);
        QCOMPARE(h.edit("queueDepthEdit")->text(), QString());
        QCOMPARE(h.edit("queueTypeEdit")->text(), QString("Model"));
        h.log.add("first");
        h.log.add("second");
        QVERIFY(h.w<QPlainTextEdit>("statusEdit")->toPlainText().contains("second\n"));
        QVERIFY(h.w<QPlainTextEdit>("statusEdit")->toPlainText().indexOf("second") < h.w<QPlainTextEdit>("statusEdit")->toPlainText().indexOf("first"));
        QVERIFY(!h.w<QPlainTextEdit>("statusEdit")->toPlainText().contains("\r"));
    }

    // ---- the buttons call Operations with the field values -------------------------------------------------------------------

    void readQReadsTheQueueNamedInTheFieldsAtClickTime()
    {
        Watchdog dog(60);
        Harness h(false);
        fake.queue(kQueue).messages.push_back(fakeMessage("hello"));
        h.type(kQm, kQueue);
        QVERIFY(h.doc.queueName().isEmpty());				// nothing is copied before a click
        QVERIFY(h.run([&]() { h.button("readQButton")->click(); }));
        QCOMPARE(h.finished.last().at(0).toString(), QString("readQ"));
        QCOMPARE(h.doc.queueManager(), QString(kQm));
        QCOMPARE(h.doc.queueName(), QString(kQueue));
        QCOMPARE(std::string(h.doc.message().body.begin(), h.doc.message().body.end()), std::string("hello"));
        QVERIFY(h.entries().value(0).startsWith(QString("Msg read from %1 length=5").arg(kQueue)));
        QCOMPARE(h.edit("queueDepthEdit")->text(), QString("0"));
        QCOMPARE(h.edit("queueTypeEdit")->text(), QString("Local"));
        QCOMPARE(h.edit("fileSizeEdit")->text(), QString("5"));
        QVERIFY(h.w<QPlainTextEdit>("statusEdit")->toPlainText().contains("Msg read from"));

        // a changed field is not seen until the next click
        h.combo("queueCombo")->setEditText("OTHER.QUEUE");
        QCOMPARE(h.doc.queueName(), QString(kQueue));
        QVERIFY(h.run([&]() { h.button("readQButton")->click(); }));
        QCOMPARE(h.doc.queueName(), QString("OTHER.QUEUE"));
    }

    void remoteQueueManagerAndContextBoxesReachTheDocument()
    {
        Harness h(false);
        h.edit("remoteQmEdit")->setText("REMOTE.QM");
        QVERIFY(h.doc.remoteQueueManager().isEmpty());
        h.check("setIdentityCheck")->setChecked(true);
        h.check("setAllCheck")->setChecked(true);
        h.check("alternateUserCheck")->setChecked(true);
        QVERIFY(h.doc.shell().setIdentity && h.doc.shell().setAll && h.doc.shell().alternateUser);		// at once: the MQMD tab needs them
        QVERIFY(h.doc.enables().mqmdPutApplEditable);
        h.tab->syncDocument();
        QCOMPARE(h.doc.remoteQueueManager(), QString("REMOTE.QM"));
    }

    void setIdentityContextClearsAfterAReadLikeWindows()
    {
        Watchdog dog(60);
        Harness h(false);
        fake.queue(kQueue).messages.push_back(fakeMessage("x"));
        h.type(kQm, kQueue);
        h.check("setIdentityCheck")->setChecked(true);
        QVERIFY(h.run([&]() { h.button("readQButton")->click(); }));
        QVERIFY(!h.check("setIdentityCheck")->isChecked());		// m_set_UserID = FALSE (General.cpp:315)
    }

    void writeQPutsTheDocumentDataToTheQueueOfTheField()
    {
        Watchdog dog(60);
        Harness h(false);
        rfh::Message m;
        m.body = { 'd', 'a', 't', 'a' };
        h.doc.setMessage(std::move(m), "Message Data (4)", true);
        h.type(kQm, kQueue);
        QVERIFY(h.run([&]() { h.button("writeQButton")->click(); }));
        QCOMPARE(h.finished.last().at(0).toString(), QString("writeQ"));
        QCOMPARE((int)fake.putLog.size(), 1);
        QCOMPARE(fake.putLog[0].queue, std::string(kQueue));
        QCOMPARE(fake.putLog[0].message.data, std::vector<unsigned char>({ 'd', 'a', 't', 'a' }));
        QVERIFY(h.entries().value(0).startsWith("Message sent to"));
    }

    void newMsgIdOffReachesThePutOptions()
    {
        Watchdog dog(60);
        Harness h(false);
        rfh::Message m;
        m.body = { 'x' };
        h.doc.setMessage(std::move(m), "Message Data (1)", true);
        h.type(kQm, kQueue);
        h.check("newMsgIdCheck")->setChecked(false);
        QVERIFY(h.run([&]() { h.button("writeQButton")->click(); }));
        QCOMPARE(fake.lastPmoOptions & MQPMO_NEW_MSG_ID, (MQLONG)0);
        h.check("newMsgIdCheck")->setChecked(true);
        QVERIFY(h.run([&]() { h.button("writeQButton")->click(); }));
        QVERIFY((fake.lastPmoOptions & MQPMO_NEW_MSG_ID) != 0);
    }

    void browseQLeavesTheMessageOnTheQueue()
    {
        Watchdog dog(60);
        Harness h(false);
        fake.queue(kQueue).messages.push_back(fakeMessage("keep"));
        h.type(kQm, kQueue);
        QVERIFY(h.run([&]() { h.button("browseQButton")->click(); }));
        QCOMPARE(h.finished.last().at(0).toString(), QString("browseQ"));
        QCOMPARE((int)fake.queue(kQueue).messages.size(), 1);
        QVERIFY(h.entries().value(0).startsWith("Msg browse from"));
    }

    void theBrowseButtonsWalkAQueueAndEndIt()
    {
        Watchdog dog(60);
        fake.browseCursors = true;
        Harness h(false);
        for (const char * body : { "one", "two", "three" })
        {
            fake.queue(kQueue).messages.push_back(fakeMessage(body));
        }
        h.type(kQm, kQueue);
        QVERIFY(h.run([&]() { h.button("startBrowseButton")->click(); }));
        QVERIFY(h.doc.browse().active);
        QVERIFY(!h.button("readQButton")->isEnabled());
        QVERIFY(h.button("browseNextButton")->isEnabled());
        QVERIFY(!h.button("browsePrevButton")->isEnabled());
        QVERIFY(h.run([&]() { h.button("browseNextButton")->click(); }));
        QCOMPARE(h.finished.last().at(0).toString(), QString("browseNext"));
        QCOMPARE(std::string(h.doc.message().body.begin(), h.doc.message().body.end()), std::string("two"));
        QVERIFY(h.button("browsePrevButton")->isEnabled());
        QVERIFY(h.run([&]() { h.button("browsePrevButton")->click(); }));
        QCOMPARE(h.finished.last().at(0).toString(), QString("browsePrev"));
        QCOMPARE(std::string(h.doc.message().body.begin(), h.doc.message().body.end()), std::string("one"));
        QVERIFY(h.run([&]() { h.button("endBrowseButton")->click(); }));
        QCOMPARE(h.finished.last().at(0).toString(), QString("endBrowse"));
        QVERIFY(!h.doc.browse().active);
        QVERIFY(h.button("readQButton")->isEnabled());
        QVERIFY(!h.button("browseNextButton")->isEnabled());
        QVERIFY(h.entries().value(0).contains("Browse operation ended"));
    }

    void closeQClosesTheQueueAndDisconnects()
    {
        Watchdog dog(60);
        Harness h(false);
        fake.queue(kQueue).messages.push_back(fakeMessage("x"));
        h.type(kQm, kQueue);
        QVERIFY(h.run([&]() { h.button("readQButton")->click(); }));
        QVERIFY(h.button("closeQButton")->isEnabled());
        const int closes = fake.closeCalls;
        QVERIFY(h.run([&]() { h.button("closeQButton")->click(); }));
        QCOMPARE(h.finished.last().at(0).toString(), QString("closeQ"));
        QVERIFY(fake.closeCalls > closes);
        QVERIFY(!h.doc.connected());
        QVERIFY(!h.button("closeQButton")->isEnabled());
    }

    void purgeAsksAndOnlyYesPurges()
    {
        Watchdog dog(60);
        Harness h(false);
        for (int i = 0; i < 3; i++)
        {
            fake.queue(kQueue).messages.push_back(fakeMessage("m"));
        }
        h.type(kQm, kQueue);
        QString asked;
        auto answer = [&](QMessageBox::StandardButton which)
        {
            QTimer::singleShot(100, [&asked, which]()
            {
                if (QMessageBox * box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                {
                    asked = box->text();
                    QCOMPARE(box->defaultButton(), box->button(QMessageBox::No));
                    box->button(which)->click();
                }
            });
        };
        answer(QMessageBox::No);
        QVERIFY(h.run([&]() { h.button("purgeQButton")->click(); }));
        QCOMPARE(asked, QString("Purge all messages from queue %1?").arg(kQueue));
        QCOMPARE((int)fake.queue(kQueue).messages.size(), 3);
        answer(QMessageBox::Yes);
        QVERIFY(h.run([&]() { h.button("purgeQButton")->click(); }));
        h.settle();
        QCOMPARE((int)fake.queue(kQueue).messages.size(), 0);
        QVERIFY(h.entries().value(0).contains("3 messages purged from"));
    }

    void openAndSaveFileUseTheChooserAndTheLastDirectories()
    {
        Watchdog dog(60);
        Harness h;
        QTemporaryDir files;
        const QString in = files.filePath("in.txt");
        QFile f(in);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("file body");
        f.close();
        h.chooser.openAnswer = in;
        QVERIFY(h.run([&]() { h.button("openFileButton")->click(); }));
        QCOMPARE(h.finished.last().at(0).toString(), QString("openFile"));
        QCOMPARE(h.doc.filePath(), in);
        QCOMPARE(h.edit("fileNameEdit")->text(), in);
        QCOMPARE(h.edit("fileSizeEdit")->text(), QString("9"));
        QCOMPARE(h.settings.lastDirectory("open"), files.path());

        const QString out = files.filePath("out.bin");
        h.chooser.saveAnswer = out;
        QVERIFY(h.run([&]() { h.button("saveFileButton")->click(); }));
        QVERIFY(QFileInfo::exists(out));
        h.chooser.openDirectory.clear();
        h.chooser.openAnswer.clear();				// a cancelled dialog starts nothing
        const int flows = h.finished.count();
        h.button("openFileButton")->click();
        QCOMPARE(h.chooser.openDirectory, files.path());		// starts in the last directory
        QCOMPARE(h.finished.count(), flows);
    }

    void clearDataAndClearAllCallTheirFlows()
    {
        Watchdog dog(60);
        Harness h(false);
        rfh::Message m;
        m.body = { 'a', 'b' };
        h.doc.setMessage(std::move(m), "Message Data (2)", true);
        QVERIFY(h.run([&]() { h.button("clearDataButton")->click(); }));
        QVERIFY(h.doc.message().body.empty());
        h.doc.setFilePath("/tmp/x");
        QVERIFY(h.run([&]() { h.button("clearAllButton")->click(); }));
        QVERIFY(h.doc.filePath().isEmpty());
    }

    void exitButtonAsksTheWindowToClose()
    {
        Harness h(false);
        QSignalSpy exit(h.tab.get(), &MainTab::exitRequested);
        h.button("exitButton")->click();
        QCOMPARE(exit.count(), 1);
    }

    // ---- Load Names and the drop-downs -------------------------------------------------------------------------------------------

    void loadNamesFillsTheQueueComboAndHidesSystemQueues()
    {
        Watchdog dog(60);
        Harness h(false);
        pcf_test::installCommandServer(fake, { { MQQT_LOCAL, { "SYSTEM.DEFAULT.LOCAL.QUEUE", "APP.Q2", "APP.Q1" } },
                                               { MQQT_ALIAS, { "APP.ALIAS" } } });
        h.type(kQm, "");
        QVERIFY(h.run([&]() { h.button("loadNamesButton")->click(); }));
        QCOMPARE(h.finished.last().at(0).toString(), QString("loadNames"));
        QStringList items;
        for (int i = 0; i < h.combo("queueCombo")->count(); i++) items << h.combo("queueCombo")->itemText(i);
        QCOMPARE(items, QStringList() << "APP.ALIAS" << "APP.Q1" << "APP.Q2");
        h.tab->setShowSystemQueues(true);
        items.clear();
        for (int i = 0; i < h.combo("queueCombo")->count(); i++) items << h.combo("queueCombo")->itemText(i);
        QCOMPARE(items, QStringList() << "APP.ALIAS" << "APP.Q1" << "APP.Q2" << "SYSTEM.DEFAULT.LOCAL.QUEUE");
        h.tab->setShowSystemQueues(false);
        QCOMPARE(h.combo("queueCombo")->count(), 3);
    }

    void theTypedQueueNameSurvivesARefillOfTheList()
    {
        Harness h(false);
        h.type(kQm, "TYPED.BY.HAND");
        h.tab->refreshQueues();
        h.tab->refreshQueueManagers();
        QCOMPARE(h.combo("queueCombo")->currentText(), QString("TYPED.BY.HAND"));
        QCOMPARE(h.combo("qmCombo")->currentText(), QString(kQm));
    }

    void theQueueManagerComboHoldsTheLocalOnesAndTheRecentOnes()
    {
        Harness h(true, [](AppSettings & s) { s.addQueueManager("RECENT.Z"); s.addQueueManager("LOCAL.A"); });
        QStringList items;
        for (int i = 0; i < h.combo("qmCombo")->count(); i++) items << h.combo("qmCombo")->itemText(i);
        QCOMPARE(items, QStringList() << "LOCAL.A" << "LOCAL.B" << "RECENT.Z");		// sorted, each once
    }

    void returnInTheQueueNameStartsNothing()
    {
        Watchdog dog(60);
        Harness h(false);
        h.type(kQm, kQueue);
        h.combo("queueCombo")->setFocus();
        QTest::keyClick(h.combo("queueCombo"), Qt::Key_Return);
        QTest::keyClick(h.combo("qmCombo"), Qt::Key_Return);
        QCOMPARE(h.finished.count(), 0);				// Windows only closes the drop-down (General.cpp:1443-1484)
        QVERIFY(!h.controller.busy());
        QCOMPARE(h.combo("queueCombo")->currentText(), QString(kQueue));
    }

    // ---- settings ----------------------------------------------------------------------------------------------------------------

    void lastQueueManagerAndQueueAreRestoredFromTheSettings()
    {
        Harness h(true, [](AppSettings & s)
        {
            s.addQueueManager("OLD.QM");
            s.addQueue("OLD.QM", "OLD.Q1");
            s.addQueue("OLD.QM", "OLD.Q2");
            s.addQueueManager("LAST.QM");
            s.addQueue("LAST.QM", "FIRST.Q");
            s.addQueue("LAST.QM", "LAST.Q");
            rfh::ConnectOptions c;
            c.mode = rfh::Mode::Client;
            c.channel = "CHAN.1";
            c.connName = "host(1415)";
            s.setConnection(c);
        });
        QCOMPARE(h.combo("qmCombo")->currentText(), QString("LAST.QM"));
        QCOMPARE(h.combo("queueCombo")->currentText(), QString("LAST.Q"));		// the most recent of that queue manager
        QVERIFY(h.radio("clientRadio")->isChecked());
        QCOMPARE(h.edit("channelEdit")->text(), QString("CHAN.1"));
        QCOMPARE(h.edit("connNameEdit")->text(), QString("host(1415)"));
        QCOMPARE(h.doc.queueManager(), QString("LAST.QM"));
        QCOMPARE(h.combo("queueCombo")->itemText(0), QString("FIRST.Q"));
    }

    void settingsAreWrittenOnlyAfterSuccess()
    {
        Watchdog dog(60);
        Harness h;
        h.type("FAIL.QM", "FAIL.Q");
        fake.connxRc = MQRC_Q_MGR_NOT_AVAILABLE;
        QVERIFY(h.run([&]() { h.button("readQButton")->click(); }));
        QVERIFY(h.settings.recentQueueManagers().isEmpty());
        QVERIFY(h.settings.recentQueues("FAIL.QM").isEmpty());

        fake.connxRc = MQRC_NONE;
        fake.queue(kQueue).messages.push_back(fakeMessage("ok"));
        h.type(kQm, kQueue);
        QVERIFY(h.run([&]() { h.button("readQButton")->click(); }));
        QCOMPARE(h.settings.recentQueueManagers(), QStringList() << kQm);
        QCOMPARE(h.settings.recentQueues(kQm), QStringList() << kQueue);

        // typing is not saving
        h.type("TYPED.ONLY", "TYPED.Q");
        QCOMPARE(h.settings.recentQueueManagers(), QStringList() << kQm);
    }

    // ---- Set Conn Id ---------------------------------------------------------------------------------------------------------------

    void setConnIdOpensTheDialogAndKeepsTheValuesInMemory()
    {
        Watchdog dog(60);
        Harness h;
        QTimer::singleShot(100, [&]()
        {
            ConnUserDialog * d = qobject_cast<ConnUserDialog *>(QApplication::activeModalWidget());
            if (d == nullptr) return;
            d->findChild<QLineEdit *>("userId")->setText("mqadmin");
            d->findChild<QLineEdit *>("password")->setText("s3cr3t-PASSWORD");
            d->findChild<QPushButton *>("ok")->click();
        });
        h.button("setConnIdButton")->click();
        MainOptions o = h.tab->options();
        QCOMPARE(o.connect.userId, std::string("mqadmin"));
        QCOMPARE(o.connect.password, std::string("s3cr3t-PASSWORD"));
        o.connect.wipePassword();

        // a connect with them: the fake sees no problem, and the password is nowhere afterwards
        fake.queue(kQueue).messages.push_back(fakeMessage("x"));
        h.type(kQm, kQueue);
        QVERIFY(h.run([&]() { h.button("readQButton")->click(); }));
        QVERIFY(!h.log.text().contains("PASSWORD"));
        QFile conf(h.settings.filePath());
        QVERIFY(conf.open(QIODevice::ReadOnly));
        const QByteArray text = conf.readAll();
        QVERIFY(!text.contains("PASSWORD"));
        QVERIFY(!text.toLower().contains("password"));
        
        // no widget of the tab itself holds a password
        for (QLineEdit * e : h.tab->findChildren<QLineEdit *>())
        {
            QVERIFY2(!e->text().contains("PASSWORD"), qPrintable(e->objectName()));
            QVERIFY(e->echoMode() == QLineEdit::Normal);
        }
    }

    void cancelledSetConnIdKeepsTheOldValues()
    {
        Watchdog dog(60);
        Harness h;
        auto drive = [&](const QString & user, const char * buttonName)
        {
            QTimer::singleShot(100, [&h, user, buttonName]()
            {
                ConnUserDialog * d = qobject_cast<ConnUserDialog *>(QApplication::activeModalWidget());
                if (d == nullptr) return;
                d->findChild<QLineEdit *>("userId")->setText(user);
                d->findChild<QPushButton *>(buttonName)->click();
            });
        };
        drive("first", "ok");
        h.button("setConnIdButton")->click();
        drive("second", "cancel");
        h.button("setConnIdButton")->click();
        MainOptions o = h.tab->options();
        QCOMPARE(o.connect.userId, std::string("first"));
    }

    // ---- busy and cancel ----------------------------------------------------------------------------------------------------------

    void cancelIsEnabledOnlyWhileAGetWaitsAndStopsIt()
    {
        Watchdog dog(60);
        Harness h(false);
        h.type(kQm, kQueue);
        h.w<QSpinBox>("waitSpin")->setValue(60);
        QVERIFY(!h.button("cancelButton")->isEnabled());
        bool enabledWhileWaiting = false, readDisabledWhileWaiting = false;
        QTimer::singleShot(300, [&]()
        {
            enabledWhileWaiting = h.button("cancelButton")->isEnabled();
            readDisabledWhileWaiting = !h.button("readQButton")->isEnabled() && !h.button("writeQButton")->isEnabled();
            h.button("cancelButton")->click();
        });
        QVERIFY(h.run([&]() { h.button("readQButton")->click(); }));
        QVERIFY(enabledWhileWaiting);
        QVERIFY(readDisabledWhileWaiting);
        QVERIFY(!h.button("cancelButton")->isEnabled());
        QVERIFY(h.button("readQButton")->isEnabled());
        QVERIFY(h.entries().value(0).contains("cancelled"));
    }

    void noWaitMeansReadQDoesNotCancelOrWait()
    {
        Watchdog dog(60);
        Harness h(false);
        h.type(kQm, kQueue);
        QVERIFY(h.run([&]() { h.button("readQButton")->click(); }));
        QVERIFY(h.entries().value(0).contains("No messages"));		// 2033 at once
        QVERIFY(!h.button("cancelButton")->isEnabled());
    }

    // ---- keys ---------------------------------------------------------------------------------------------------------------------------

    void shortcutsPressTheirButtonsOnlyWhileTheMainTabIsShown()
    {
        Watchdog dog(60);
        ModalGuard guard;
        Harness h(false);
        QTabWidget host;
        host.addTab(h.tab.get(), "Main");
        QWidget other;
        host.addTab(&other, "Other");
        host.resize(1000, 700);
        host.show();
        QVERIFY(QTest::qWaitForWindowActive(&host));

        struct Key { Qt::Key key; Qt::KeyboardModifier mod; const char * button; };
        const Key keys[] = {
            { Qt::Key_Q, Qt::ControlModifier, "readQButton" }, { Qt::Key_W, Qt::ControlModifier, "writeQButton" },
            { Qt::Key_B, Qt::ControlModifier, "browseQButton" }, { Qt::Key_T, Qt::ControlModifier, "startBrowseButton" },
            { Qt::Key_N, Qt::ControlModifier, "browseNextButton" }, { Qt::Key_R, Qt::ControlModifier, "browsePrevButton" },
            { Qt::Key_M, Qt::ControlModifier, "endBrowseButton" }, { Qt::Key_V, Qt::AltModifier, "endBrowseButton" },
            { Qt::Key_K, Qt::ControlModifier, "closeQButton" }, { Qt::Key_E, Qt::ControlModifier, "purgeQButton" },
            { Qt::Key_D, Qt::ControlModifier, "displayQButton" }, { Qt::Key_L, Qt::ControlModifier, "loadQButton" },
            { Qt::Key_Y, Qt::ControlModifier, "saveQButton" }, { Qt::Key_H, Qt::ControlModifier, "loadNamesButton" },
            { Qt::Key_I, Qt::ControlModifier, "setConnIdButton" }, { Qt::Key_J, Qt::ControlModifier, "clearAllButton" },
            { Qt::Key_X, Qt::AltModifier, "clearDataButton" }, { Qt::Key_Y, Qt::AltModifier, "clearAllButton" },
            { Qt::Key_O, Qt::ControlModifier, "openFileButton" }, { Qt::Key_S, Qt::ControlModifier, "saveFileButton" } };

        // states: idle; connected (Close Q); browsing at message 2 (Next, Prev, End)
        const int states = 3;
        for (int state = 0; state < states; state++)
        {
            h.doc.setBrowse(BrowseState());
            h.doc.setConnected(state == 1);
            if (state == 2)
            {
                h.doc.setConnected(true);
                BrowseState b;
                b.active = true;
                b.position = 2;
                h.doc.setBrowse(b);
            }
            host.setCurrentIndex(0);
            for (const Key & k : keys)
            {
                QPushButton * b = h.button(k.button);
                const bool enabled = b->isEnabled();
                QSignalSpy clicked(b, &QAbstractButton::clicked);
                QTest::keyClick(&host, k.key, k.mod);
                h.settle();
                host.activateWindow();
                QVERIFY(QTest::qWaitForWindowActive(&host));
                QVERIFY2(clicked.count() == (enabled ? 1 : 0), qPrintable(QString("count %4 state %1 key %2 button %3").arg(state).arg(QKeySequence(k.key | k.mod).toString()).arg(k.button).arg(clicked.count())));
                if (state == 0 && QString(k.button) != "closeQButton" && QString(k.button) != "browseNextButton")
                {
                    const QString name = k.button;
                    const bool phase1 = name == "displayQButton" || name == "loadQButton" || name == "saveQButton"
                        || name == "browsePrevButton" || name == "endBrowseButton";
                    QCOMPARE(enabled, !phase1);
                }
            }
        }
        // other tab current: the same keys do nothing
        host.setCurrentIndex(1);
        h.doc.setBrowse(BrowseState());
        h.doc.setConnected(false);
        QSignalSpy readClicked(h.button("readQButton"), &QAbstractButton::clicked);
        QSignalSpy openClicked(h.button("openFileButton"), &QAbstractButton::clicked);
        QTest::keyClick(&host, Qt::Key_Q, Qt::ControlModifier);
        QTest::keyClick(&host, Qt::Key_O, Qt::ControlModifier);
        QCOMPARE(readClicked.count(), 0);
        QCOMPARE(openClicked.count(), 0);
        QVERIFY(guard.unexpected >= 1);		// the Set Conn Id dialog that Ctrl+I opened was closed by the guard
        host.removeTab(0);
        h.tab->setParent(nullptr);
    }

    // ---- looking at it ----------------------------------------------------------------------------------------------------------------

    void screenshots()
    {
        const QByteArray dirName = qgetenv("RFH_SHOT_DIR");
        if (dirName.isEmpty())
        {
            QSKIP("set RFH_SHOT_DIR to write PNG files of the tab");
        }
        Watchdog dog(120);
        fake.browseCursors = true;
        for (int i = 0; i < 6; i++)
        {
            fake.queue(kQueue).messages.push_back(fakeMessage("message " + std::to_string(i)));
        }
        const double scales[] = { 1.0, 1.5 };
        for (double scale : scales)
        {
            Harness h;
            QFont f = QApplication::font();
            const double base = f.pointSizeF() > 0 ? f.pointSizeF() : 10;
            f.setPointSizeF(base * scale);
            h.tab->setFont(f);
            h.tab->resize(scale > 1.0 ? 1500 : 1100, scale > 1.0 ? 960 : 700);
            h.tab->show();
            h.type(kQm, kQueue);
            const QString suffix = scale > 1.0 ? "-150" : "";
            QVERIFY(h.tab->grab().save(QString::fromLocal8Bit(dirName) + "/main-idle" + suffix + ".png"));
            QVERIFY(h.run([&]() { h.button("readQButton")->click(); }));
            QVERIFY(h.tab->grab().save(QString::fromLocal8Bit(dirName) + "/main-connected" + suffix + ".png"));
            QVERIFY(h.run([&]() { h.button("startBrowseButton")->click(); }));
            QVERIFY(h.run([&]() { h.button("browseNextButton")->click(); }));
            QVERIFY(h.doc.browse().active);
            QVERIFY(h.tab->grab().save(QString::fromLocal8Bit(dirName) + "/main-browsing" + suffix + ".png"));
            h.tab->hide();
        }
    }
};

QTEST_MAIN(MainTabTests)
#include "MainTabTests.moc"
