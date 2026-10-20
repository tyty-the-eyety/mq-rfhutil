// MainWindowTests.cpp - the application window: its tabs, menus, shortcuts, shared objects and the process setup.
//
// The window is built on the fake MQI (tests/core/fake_mq.h) with a settings file in a temporary directory, so no test
// touches the real configuration or a real queue manager. The expectations come from the inventory (section 1.2 tabs,
// 3.6 file options, 7.1 accelerators, 7.2 menu bar) and the Task 27 brief.

#include <QtTest>
#include <QtWidgets>

#include <locale.h>

#include <functional>
#include <memory>

#include "AppDocument.h"
#include "AppSettings.h"
#include "DataTab.h"
#include "MainTab.h"
#include "MainWindow.h"
#include "MqmdTab.h"
#include "Operations.h"
#include "ProcessSetup.h"
#include "StatusLog.h"
#include "fake_mq.h"
#include "rfh_trace.h"

using namespace rfhqt;

namespace
{
	rfh_test::FakeMq & fake = rfh_test::fakeMq();
	const char * const kQueue = "RFHUTIL.TEST.WIN";

	class StubChooser : public FileChooser
	{
	public:
		QString openPath(QWidget *, const QString &) override { openCalls++; return openAnswer; }
		QString savePath(QWidget *, const QString &) override { saveCalls++; return saveAnswer; }
		int openCalls = 0, saveCalls = 0;
		QString openAnswer, saveAnswer;
	};

	// RFHUTIL_TRACE_FILE for the life of the object: cleared on entry, so a window never depends on the developer's
	// environment, and cleared on exit, so a failing test cannot leak it into the next one.
	struct TraceEnv
	{
		explicit TraceEnv(const QString & value)
		{
			qunsetenv("RFHUTIL_TRACE_FILE");
			if (!value.isEmpty())
			{
				qputenv("RFHUTIL_TRACE_FILE", value.toLocal8Bit());
			}
		}
		~TraceEnv() { qunsetenv("RFHUTIL_TRACE_FILE"); }
	};

	// A window on the fake MQI with its own settings file.
	struct Harness
	{
		explicit Harness(const std::function<void(MainWindow::Config &)> & tweak = nullptr, const std::function<void(const QString &)> & prepare = nullptr,
		                 const QString & traceFile = QString())
			: env(traceFile), api(rfh_test::fakeApi())
		{
			fake.reset();
			fake.queue(kQueue);
			if (prepare)
			{
				prepare(settingsPath());
			}
			build(tweak);
		}

		void build(const std::function<void(MainWindow::Config &)> & tweak = nullptr)
		{
			MainWindow::Config config;
			config.settingsPath = settingsPath();
			config.api = &api;
			if (tweak)
			{
				tweak(config);
			}
			window.reset(new MainWindow(config));
			window->mainTab()->setFileChooser(&chooser);
		}

		QString settingsPath() const { return dir.filePath("rfhutil.conf"); }

		void rebuild()
		{
			window.reset();
			build();
		}

		QAction * action(const char * name) const
		{
			QAction * found = window->findChild<QAction *>(name);
			if (found == nullptr)
			{
				printf("no action named %s\n", name);
			}
			return found;
		}

		QPushButton * button(const char * name) const { return window->mainTab()->findChild<QPushButton *>(name); }

		bool showActive()
		{
			window->resize(900, 600);
			window->show();
			return QTest::qWaitForWindowActive(window.get());
		}

		void key(Qt::Key k, Qt::KeyboardModifiers m) { QTest::keyClick(window.get(), k, m); }

		void settle()
		{
			QTRY_VERIFY_WITH_TIMEOUT(!window->document().shell().busy, 5000);
			QCoreApplication::processEvents();
		}

		TraceEnv env;		// first: cleared last, after the window is gone
		QTemporaryDir dir;
		rfh::MqApi api;
		StubChooser chooser;
		std::unique_ptr<MainWindow> window;
	};

	QString plain(QString text)
	{
		const int tab = text.indexOf('\t');
		if (tab >= 0)
		{
			text.truncate(tab);
		}
		return text.remove('&');
	}

	QStringList itemTexts(QMenu * menu)
	{
		QStringList texts;
		for (QAction * a : menu->actions())
		{
			texts << (a->isSeparator() ? QString("-") : plain(a->text()));
		}
		return texts;
	}

	int letterOf(const QKeySequence & mnemonic)
	{
		return mnemonic.isEmpty() ? 0 : (int)(mnemonic[0].toCombined() & ~Qt::KeyboardModifierMask);
	}

	// The Alt+letter keys that a widget text stands for, and the Alt+letter QShortcuts under root.
	void collectMnemonics(QWidget * root, QSet<int> & into)
	{
		for (QWidget * w : root->findChildren<QWidget *>())
		{
			QString text;
			if (QAbstractButton * b = qobject_cast<QAbstractButton *>(w))
			{
				text = b->text();
			}
			else if (QGroupBox * g = qobject_cast<QGroupBox *>(w))
			{
				text = g->title();
			}
			else if (QLabel * l = qobject_cast<QLabel *>(w))
			{
				if (l->buddy() != nullptr)
				{
					text = l->text();
				}
			}
			if (letterOf(QKeySequence::mnemonic(text)) != 0)
			{
				into.insert(letterOf(QKeySequence::mnemonic(text)));
			}
		}
		for (QShortcut * s : root->findChildren<QShortcut *>())
		{
			if (s->key().count() == 1 && ((int)s->key()[0].toCombined() & (int)Qt::KeyboardModifierMask) == (int)Qt::AltModifier)
			{
				into.insert(letterOf(s->key()));
			}
		}
	}
}

class MainWindowTests : public QObject
{
	Q_OBJECT

private slots:
	// ---- the tabs ----------------------------------------------------------------------------------------------

	void tabsFollowTheWindowsOrderAndCaptions()
	{
		Harness h;
		const QStringList expected = {
			"Main", "Data", "MQMD", "PS", "Usr Prop", "RFH", "PubSub", "pscr",
			"jms", "usr", "other", "CICS", "IMS", "DLQ", "Conn" };
		QCOMPARE(h.window->tabs()->count(), expected.size());
		for (int i = 0; i < expected.size(); i++)
		{
			QCOMPARE(h.window->tabs()->tabText(i), expected[i]);
		}
	}

	void onlyMainDataAndMqmdAreEnabled()
	{
		Harness h;
		for (int i = 0; i < h.window->tabs()->count(); i++)
		{
			const bool phase1 = (i == MainWindow::TabMain) || (i == MainWindow::TabData) || (i == MainWindow::TabMqmd);
			QCOMPARE(h.window->tabs()->isTabEnabled(i), phase1);
		}
	}

	void disabledTabsSayTheyAreNotPortedYet()
	{
		Harness h;
		QCOMPARE(h.window->tabs()->tabToolTip(MainWindow::TabRfh), QString("Not yet ported"));
		QCOMPARE(h.window->tabs()->tabToolTip(MainWindow::TabConn), QString("Not yet ported"));
		QVERIFY(h.window->tabs()->tabToolTip(MainWindow::TabMain).isEmpty());
	}

	void theThreePagesAreTheRealTabs()
	{
		Harness h;
		QCOMPARE(h.window->tabs()->widget(MainWindow::TabMain), static_cast<QWidget *>(h.window->mainTab()));
		QCOMPARE(h.window->tabs()->widget(MainWindow::TabData), static_cast<QWidget *>(h.window->dataTab()));
		QCOMPARE(h.window->tabs()->widget(MainWindow::TabMqmd), static_cast<QWidget *>(h.window->mqmdTab()));
	}

	void numbersAndCharactersUseTheCLocaleAfterSetup()
	{
		setlocale(LC_NUMERIC, "");
		rfhqt::useCLocaleForNumbersAndCharacters();
		QCOMPARE(QString(setlocale(LC_NUMERIC, nullptr)), QString("C"));
		QCOMPARE(QString(setlocale(LC_CTYPE, nullptr)), QString("C"));
	}

	// ---- tab changes and the document -----------------------------------------------------------------------------

	void aTabChangeWritesTheTabIntoTheDocument()
	{
		Harness h;
		QCOMPARE(h.window->document().shell().tab, Tab::Main);
		h.window->tabs()->setCurrentIndex(MainWindow::TabData);
		QCOMPARE(h.window->document().shell().tab, Tab::Data);
		h.window->tabs()->setCurrentIndex(MainWindow::TabMqmd);
		QCOMPARE(h.window->document().shell().tab, Tab::Mqmd);
		h.window->tabs()->setCurrentIndex(MainWindow::TabMain);
		QCOMPARE(h.window->document().shell().tab, Tab::Main);
	}

	void leavingTheMainTabCopiesItsNamesIntoTheDocument()
	{
		Harness h;
		h.window->mainTab()->setNames("QM.X", "SOME.QUEUE", "REMOTE.QM");
		h.window->tabs()->setCurrentIndex(MainWindow::TabData);
		QCOMPARE(h.window->document().queueManager(), QString("QM.X"));
		QCOMPARE(h.window->document().queueName(), QString("SOME.QUEUE"));
		QCOMPARE(h.window->document().remoteQueueManager(), QString("REMOTE.QM"));
	}

	void anOpenFindDialogRefusesATabChange()
	{
		Harness h;
		h.window->tabs()->setCurrentIndex(MainWindow::TabData);
		ShellState shell = h.window->document().shell();
		shell.findDialogOpen = true;
		h.window->document().setShell(shell);
		h.window->tabs()->setCurrentIndex(MainWindow::TabMqmd);
		QCOMPARE(h.window->tabs()->currentIndex(), (int)MainWindow::TabData);
		QCOMPARE(h.window->document().shell().tab, Tab::Data);
		shell.findDialogOpen = false;
		h.window->document().setShell(shell);
		h.window->tabs()->setCurrentIndex(MainWindow::TabMqmd);
		QCOMPARE(h.window->tabs()->currentIndex(), (int)MainWindow::TabMqmd);
	}

	// ---- Alt+N and Alt+P ---------------------------------------------------------------------------------------------

	void steppingWrapsFromDlqToMainAndSkipsConn()
	{
		const auto all = [](int) { return true; };
		QCOMPARE(MainWindow::steppedTab(MainWindow::TabMain, true, all), (int)MainWindow::TabData);
		QCOMPARE(MainWindow::steppedTab(MainWindow::TabDlq, true, all), (int)MainWindow::TabMain);
		QCOMPARE(MainWindow::steppedTab(MainWindow::TabMain, false, all), (int)MainWindow::TabDlq);
		QCOMPARE(MainWindow::steppedTab(MainWindow::TabData, false, all), (int)MainWindow::TabMain);
		// a tab that is not usable is passed over; Conn is never a target
		const auto phase1 = [](int i) { return MainWindow::isPhase1Tab(i); };
		QCOMPARE(MainWindow::steppedTab(MainWindow::TabMqmd, true, phase1), (int)MainWindow::TabMain);
		QCOMPARE(MainWindow::steppedTab(MainWindow::TabMain, false, phase1), (int)MainWindow::TabMqmd);
		const auto onlyConn = [](int i) { return i == MainWindow::TabConn; };
		QCOMPARE(MainWindow::steppedTab(MainWindow::TabMain, true, onlyConn), (int)MainWindow::TabMain);
	}

	void altNAndAltPStepThroughTheTabs()
	{
		Harness h;
		QVERIFY(h.showActive());
		h.key(Qt::Key_N, Qt::AltModifier);
		QCOMPARE(h.window->tabs()->currentIndex(), (int)MainWindow::TabData);
		QCOMPARE(h.window->document().shell().tab, Tab::Data);
		h.key(Qt::Key_N, Qt::AltModifier);
		QCOMPARE(h.window->tabs()->currentIndex(), (int)MainWindow::TabMqmd);
		h.key(Qt::Key_N, Qt::AltModifier);
		QCOMPARE(h.window->tabs()->currentIndex(), (int)MainWindow::TabMain);		// the later tabs are disabled in Phase 1
		h.key(Qt::Key_P, Qt::AltModifier);
		QCOMPARE(h.window->tabs()->currentIndex(), (int)MainWindow::TabMqmd);
		h.key(Qt::Key_P, Qt::AltModifier);
		QCOMPARE(h.window->tabs()->currentIndex(), (int)MainWindow::TabData);
	}

	void altNFromDlqWrapsToMain()
	{
		Harness h;
		QVERIFY(h.showActive());
		h.window->tabs()->setTabEnabled(MainWindow::TabDlq, true);
		h.window->tabs()->setCurrentIndex(MainWindow::TabDlq);
		h.key(Qt::Key_N, Qt::AltModifier);
		QCOMPARE(h.window->tabs()->currentIndex(), (int)MainWindow::TabMain);
	}

	void altNAndAltPAreNotMnemonicsOfAnyControl()
	{
		Harness h;
		for (QWidget * page : { (QWidget *)h.window->mainTab(), (QWidget *)h.window->dataTab(), (QWidget *)h.window->mqmdTab() })
		{
			QSet<int> own;
			collectMnemonics(page, own);
			QVERIFY2(!own.contains(Qt::Key_N), "a control claims Alt+N, which steps to the next tab");
			QVERIFY2(!own.contains(Qt::Key_P), "a control claims Alt+P, which steps to the previous tab");
		}
	}

	// ---- the menus -----------------------------------------------------------------------------------------------------

	void theMenuBarHasTheWindowsMenusAndItems()
	{
		Harness h;
		QStringList titles;
		for (QAction * a : h.window->menuBar()->actions())
		{
			titles << plain(a->text());
		}
		QCOMPARE(titles, (QStringList{ "File", "Edit", "Search", "Read", "Write", "View", "MQ" }));
		const auto menu = [&h](const char * name) { return h.window->findChild<QMenu *>(name); };
		QCOMPARE(itemTexts(menu("fileMenu")), (QStringList{ "Open", "Save", "-", "Exit" }));
		QCOMPARE(itemTexts(menu("editMenu")), (QStringList{ "Top", "Bottom" }));
		QCOMPARE(itemTexts(menu("searchMenu")), (QStringList{ "Find", "Find Hex", "Go to" }));
		QCOMPARE(itemTexts(menu("readMenu")), (QStringList{ "Remove CrLf", "Unix Text", "Ignore Headers", "Save Headers" }));
		QCOMPARE(itemTexts(menu("writeMenu")), (QStringList{ "No Headers", "Include MQMD" }));
		QCOMPARE(itemTexts(menu("viewMenu")), (QStringList{ "System Queues" }));
		QCOMPARE(itemTexts(menu("mqMenu")), (QStringList{ "MQConn", "MQDisc", "-", "MQClose", "-", "End Browse" }));
		// the keys the captions advertise
		QCOMPARE(h.action("fileOpen")->text(), QString("&Open\tCtrl+O"));
		QCOMPARE(h.action("fileSave")->text(), QString("&Save\tCtrl+S"));
		QCOMPARE(h.action("searchFind")->text(), QString("&Find\tCtrl+F"));
		QCOMPARE(h.action("searchGoto")->text(), QString("&Go to\tCtrl+G"));
		QCOMPARE(h.action("searchFindHex")->text(), QString("Find He&x"));
	}

	void noMenuBarMnemonicIsTakenByAControlOrWindowShortcut()
	{
		Harness h;
		QSet<int> menus;
		for (QAction * a : h.window->menuBar()->actions())
		{
			if (letterOf(QKeySequence::mnemonic(a->text())) != 0)
			{
				menus.insert(letterOf(QKeySequence::mnemonic(a->text())));
			}
		}
		QSet<int> used;
		for (QWidget * page : { (QWidget *)h.window->mainTab(), (QWidget *)h.window->dataTab(), (QWidget *)h.window->mqmdTab() })
		{
			collectMnemonics(page, used);
		}
		collectMnemonics(h.window.get(), used);
		for (int key : menus)
		{
			QVERIFY2(!used.contains(key), qPrintable(QString("menu mnemonic Alt+%1 clashes").arg(QChar(key))));
		}
		// the check proves nothing if it collected nothing
		QVERIFY(used.size() > 10);
		QVERIFY(used.contains(Qt::Key_F));		// the FIX radio: the reason File has no Alt+F
		QVERIFY(used.contains(Qt::Key_N));		// the window's own Alt+N
	}

	void readAndWriteOptionsStartWithTheWindowsDefaults()
	{
		Harness h;
		for (const char * name : { "readRemoveCrLf", "readUnixText", "readIgnoreHeaders", "readSaveHeaders", "writeNoHeaders" })
		{
			QVERIFY2(h.action(name)->isCheckable(), name);
			QVERIFY2(!h.action(name)->isChecked(), name);
		}
		QVERIFY(h.action("writeIncludeMqmd")->isCheckable());
		QVERIFY(h.action("writeIncludeMqmd")->isChecked());
		QVERIFY(!h.action("readSaveHeaders")->isEnabled());		// not supported by the core: shown disabled
		for (const char * name : { "readRemoveCrLf", "readUnixText", "readIgnoreHeaders", "writeNoHeaders", "writeIncludeMqmd" })
		{
			QVERIFY2(h.action(name)->isEnabled(), name);
		}
	}

	void uncheckingIncludeMqmdChangesTheNextSave()
	{
		Harness h;
		const QString path = h.dir.filePath("out.bin");
		rfh::Message m;
		m.body = { 'a', 'b', 'c' };
		h.window->document().setMessage(std::move(m));
		h.chooser.saveAnswer = path;

		QSignalSpy finished(&h.window->operations(), &Operations::flowFinished);
		h.button("saveFileButton")->click();
		QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 5000);
		h.settle();
		QCOMPARE(QFileInfo(path).size(), qint64(364 + 3));

		QFile::remove(path);
		h.action("writeIncludeMqmd")->trigger();
		QVERIFY(!h.action("writeIncludeMqmd")->isChecked());
		finished.clear();
		h.button("saveFileButton")->click();
		QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 5000);
		h.settle();
		QCOMPARE(QFileInfo(path).size(), qint64(3));
	}

	void theReadMenuReachesTheNextOpenFile()
	{
		Harness h;
		const QString path = h.dir.filePath("in.txt");
		{
			QFile f(path);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("a\r\nb\r\n");
		}
		h.chooser.openAnswer = path;
		QSignalSpy finished(&h.window->operations(), &Operations::flowFinished);
		h.button("openFileButton")->click();
		QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 5000);
		h.settle();
		QCOMPARE(h.window->document().message().body.size(), size_t(6));

		h.action("readRemoveCrLf")->trigger();
		QVERIFY(h.action("readRemoveCrLf")->isChecked());
		finished.clear();
		h.button("openFileButton")->click();
		QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 5000);
		h.settle();
		QCOMPARE(h.window->document().message().body.size(), size_t(2));
	}

	void systemQueuesFollowsTheSettingAndChangesIt()
	{
		Harness h;
		QVERIFY(!h.action("viewSystemQueues")->isChecked());
		QVERIFY(!h.window->mainTab()->showSystemQueues());
		h.action("viewSystemQueues")->trigger();
		QVERIFY(h.action("viewSystemQueues")->isChecked());
		QVERIFY(h.window->mainTab()->showSystemQueues());
		QVERIFY(h.window->settings().showSystemQueues());
		h.rebuild();
		QVERIFY(h.action("viewSystemQueues")->isChecked());
		QVERIFY(h.window->mainTab()->showSystemQueues());
	}

	void menuItemsFollowTheEnableRules()
	{
		Harness h;
		const auto check = [&h](const char * where)
		{
			const Enables e = h.window->document().enables();
			const auto verify = [&](const char * name, bool expected)
			{
				QVERIFY2(h.action(name)->isEnabled() == expected, qPrintable(QString("%1: %2").arg(where, name)));
			};
			verify("fileOpen", e.menuOpen);
			verify("fileSave", e.menuSave);
			verify("editTop", e.menuTop);
			verify("editBottom", e.menuBottom);
			verify("searchFind", e.menuFind);
			verify("searchFindHex", e.menuFindHex);
			verify("searchGoto", e.menuGoto);
			verify("mqConnect", e.menuMqConn);
			verify("mqDisconnect", e.menuMqDisc);
			verify("mqClose", e.menuMqClose);
			verify("mqEndBrowse", e.menuMqEndBrowse);
		};
		{
			rfh::Message m;
			m.body = { 'x' };
			h.window->document().setMessage(std::move(m));
		}
		check("start");
		QVERIFY(h.action("fileOpen")->isEnabled());
		QVERIFY(h.action("mqConnect")->isEnabled());
		QVERIFY(!h.action("mqDisconnect")->isEnabled());
		QVERIFY(!h.action("searchFind")->isEnabled());
		QVERIFY(!h.action("editTop")->isEnabled());

		h.window->tabs()->setCurrentIndex(MainWindow::TabData);
		check("data tab");
		QVERIFY(h.action("editTop")->isEnabled());
		QVERIFY(h.action("editBottom")->isEnabled());
		QVERIFY(h.action("searchFind")->isEnabled());
		QVERIFY(h.action("searchFindHex")->isEnabled());
		QVERIFY(h.action("searchGoto")->isEnabled());
		QVERIFY(!h.action("mqConnect")->isEnabled());		// Windows: only on the Main tab

		h.window->tabs()->setCurrentIndex(MainWindow::TabMain);
		BrowseState browse;
		browse.active = true;
		browse.position = 1;
		h.window->document().setConnected(true);
		h.window->document().setBrowse(browse);
		check("browsing");
		QVERIFY(!h.action("fileOpen")->isEnabled());		// File > Open is off while browsing
		QVERIFY(h.action("mqEndBrowse")->isEnabled());
		QVERIFY(!h.action("mqDisconnect")->isEnabled());
	}

	void theSearchMenuOpensTheDataTabDialogs()
	{
		Harness h;
		rfh::Message m;
		m.body = { 'x' };
		h.window->document().setMessage(std::move(m));
		h.window->tabs()->setCurrentIndex(MainWindow::TabData);
		QVERIFY(!h.window->document().shell().findDialogOpen);
		h.action("searchFind")->trigger();
		QVERIFY(h.window->document().shell().findDialogOpen);
		for (QDialog * d : h.window->dataTab()->findChildren<QDialog *>())
		{
			d->close();
		}
		QVERIFY(!h.window->document().shell().findDialogOpen);
		h.action("searchFindHex")->trigger();
		QVERIFY(h.window->document().shell().findDialogOpen);
		for (QDialog * d : h.window->dataTab()->findChildren<QDialog *>())
		{
			d->close();
		}
		QVERIFY(!h.window->document().shell().findDialogOpen);
		// Go To is modal: answer it from the event loop
		bool shown = false;
		QTimer::singleShot(100, [&shown]()
		{
			if (QWidget * modal = QApplication::activeModalWidget())
			{
				shown = true;
				static_cast<QDialog *>(modal)->reject();
			}
		});
		h.action("searchGoto")->trigger();
		QVERIFY(shown);
	}

	void theEditMenuScrollsTheDataBox()
	{
		Harness h;
		QVERIFY(h.showActive());
		rfh::Message m;
		for (int i = 0; i < 4000; i++)
		{
			const std::string line = "line " + std::to_string(i) + "\r\n";
			m.body.insert(m.body.end(), line.begin(), line.end());
		}
		h.window->document().setMessage(std::move(m));
		h.window->tabs()->setCurrentIndex(MainWindow::TabData);
		QPlainTextEdit * box = h.window->dataTab()->findChild<QPlainTextEdit *>("dataEdit");
		QVERIFY(box != nullptr);
		QVERIFY(box->document()->blockCount() > 1000);
		h.action("editBottom")->trigger();
		QVERIFY(box->textCursor().blockNumber() >= box->document()->blockCount() - 2);
		QVERIFY(box->verticalScrollBar()->value() > 0);
		h.action("editTop")->trigger();
		QCOMPARE(box->textCursor().position(), 0);
		QCOMPARE(box->verticalScrollBar()->value(), 0);
	}

	void ctrlOAndCtrlSWorkOnEveryPageExactlyOnce()
	{
		Harness h;
		QVERIFY(h.showActive());
		rfh::Message m;
		m.body = { 'x' };
		h.window->document().setMessage(std::move(m));
		for (int tab : { (int)MainWindow::TabMain, (int)MainWindow::TabData, (int)MainWindow::TabMqmd })
		{
			h.window->tabs()->setCurrentIndex(tab);
			h.chooser.openCalls = 0;
			h.chooser.saveCalls = 0;
			h.key(Qt::Key_O, Qt::ControlModifier);
			QVERIFY2(h.chooser.openCalls == 1, qPrintable(QString("Ctrl+O on tab %1: %2 calls").arg(tab).arg(h.chooser.openCalls)));
			h.key(Qt::Key_S, Qt::ControlModifier);
			QVERIFY2(h.chooser.saveCalls == 1, qPrintable(QString("Ctrl+S on tab %1: %2 calls").arg(tab).arg(h.chooser.saveCalls)));
		}
	}

	void theFileMenuRunsTheMainTabDialogs()
	{
		Harness h;
		h.action("fileOpen")->trigger();
		QCOMPARE(h.chooser.openCalls, 1);
		h.action("fileSave")->trigger();
		QCOMPARE(h.chooser.saveCalls, 1);
		// disabled while browsing: Windows OnUpdateOpen
		BrowseState browse;
		browse.active = true;
		browse.position = 1;
		h.window->document().setConnected(true);
		h.window->document().setBrowse(browse);
		h.action("fileOpen")->trigger();
		QCOMPARE(h.chooser.openCalls, 1);
	}

	void exitClosesTheWindow()
	{
		Harness h;
		QVERIFY(h.showActive());
		h.button("exitButton")->click();
		QVERIFY(!h.window->isVisible());
		h.window->show();
		h.action("fileExit")->trigger();
		QVERIFY(!h.window->isVisible());
	}

	// ---- the shared objects ----------------------------------------------------------------------------------------------

	void theTabsStatusMessagesReachTheLog()
	{
		Harness h;
		emit h.window->dataTab()->statusMessage("data tab says hello");
		QVERIFY(h.window->statusLog().text().contains("data tab says hello"));
		emit h.window->mqmdTab()->statusMessage("mqmd tab says hello");
		QVERIFY(h.window->statusLog().text().contains("mqmd tab says hello"));
	}

	void theTitleIsRfhUtilThenTheQueueAfterAReadQ()
	{
		Harness h;
		QVERIFY2(h.window->windowTitle().startsWith(QStringLiteral("RfhUtil")), qPrintable(h.window->windowTitle()));
		rfh_test::FakeMessage m;
		memcpy(m.md.Format, MQFMT_STRING, 8);
		m.data = { 'h', 'i' };
		fake.queue(kQueue).messages.push_back(m);
		h.window->mainTab()->setNames("", kQueue, "");
		QSignalSpy titles(&h.window->operations(), &Operations::titleChanged);
		h.button("readQButton")->click();
		QTRY_VERIFY_WITH_TIMEOUT(titles.count() >= 1, 5000);
		QCOMPARE(h.window->windowTitle(), QString(kQueue));
		h.settle();
		QCOMPARE(h.window->document().message().body.size(), size_t(2));
	}

	void connectFromTheMqMenuPrintsConnectedTo()
	{
		Harness h;
		h.window->mainTab()->setNames("QM.WIN", kQueue, "");
		QSignalSpy flows(&h.window->operations(), &Operations::flowFinished);
		h.action("mqConnect")->trigger();
		QTRY_VERIFY_WITH_TIMEOUT(flows.count() >= 1, 5000);
		h.settle();
		QVERIFY2(h.window->statusLog().text().contains("Connected to QM.WIN"), qPrintable(h.window->statusLog().text()));
		QVERIFY(h.window->document().connected());
		QVERIFY(h.action("mqDisconnect")->isEnabled());
		QVERIFY(!h.action("mqConnect")->isEnabled());
		flows.clear();
		h.action("mqDisconnect")->trigger();
		QTRY_VERIFY_WITH_TIMEOUT(flows.count() >= 1, 5000);
		h.settle();
		QVERIFY(!h.window->document().connected());
	}

	void writeQSendsTheMqmdTabAsEditedAtOnce()
	{
		Harness h;
		rfh::Message m;
		m.body = { 'o', 'k' };
		h.window->document().setMessage(std::move(m));
		h.window->tabs()->setCurrentIndex(MainWindow::TabMqmd);
		QComboBox * format = h.window->mqmdTab()->findChild<QComboBox *>("formatCombo");
		QVERIFY(format != nullptr);
		format->setEditText("MYFORMAT");
		h.window->tabs()->setCurrentIndex(MainWindow::TabMain);
		h.window->mainTab()->setNames("", kQueue, "");
		QSignalSpy flows(&h.window->operations(), &Operations::flowFinished);
		h.button("writeQButton")->click();
		QTRY_VERIFY_WITH_TIMEOUT(flows.count() >= 1, 5000);
		h.settle();
		const auto & queued = fake.queue(kQueue).messages;
		QCOMPARE(queued.size(), size_t(1));
		QCOMPARE(QString::fromLatin1((const char *)queued.front().md.Format, 8).trimmed(), QString("MYFORMAT"));
	}

	void noMqLibraryIsShownInTheLog()
	{
		Harness h([](MainWindow::Config & c) { c.loadMq = []() { return rfh::Result::failure("no libmqm"); }; });
		QVERIFY2(h.window->statusLog().text().contains("*****MQ not installed"), qPrintable(h.window->statusLog().text()));
		Harness ok;
		QVERIFY(!ok.window->statusLog().text().contains("MQ not installed"));
	}

	void settingsWarningsAreShownInTheLog()
	{
		Harness h(nullptr, [](const QString & path)
		{
			QFile f(path);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("[Display]\ncodePage=banana\n[[[ not ini\n");
		});
		QVERIFY2(!h.window->statusLog().text().isEmpty(), "the damaged settings file was not reported");
		QVERIFY(h.window->settings().warnings().isEmpty());		// taken, not left behind
	}

	void geometryPersistsAcrossTwoWindowLifetimes()
	{
		// The offscreen screen is smaller than the window's own minimum, so a size or position is clamped on restore;
		// the window state is not.
		Harness h;
		h.window->showMaximized();
		QVERIFY(QTest::qWaitForWindowExposed(h.window.get()));
		QVERIFY(h.window->isMaximized());
		h.window->close();
		QVERIFY(!h.window->settings().windowGeometry().isEmpty());
		h.rebuild();
		QVERIFY(h.window->windowState() & Qt::WindowMaximized);
		Harness fresh;
		QVERIFY(!(fresh.window->windowState() & Qt::WindowMaximized));
	}

	void theTraceIsInstalledForTheLifeOfTheWindow()
	{
		QTemporaryDir dir;
		const QString trace = dir.filePath("trace.log");
		RfhTrace * before = rfhGetTrace();
		{
			Harness h(nullptr, nullptr, trace);
			QVERIFY(rfhGetTrace()->isTraceEnabled());
			QVERIFY(rfhGetTrace() != before);
			QVERIFY(QFileInfo::exists(trace));
		}
		QVERIFY(!rfhGetTrace()->isTraceEnabled());
		QCOMPARE(rfhGetTrace(), before);
	}

	// The variable names the file, as in Windows (rfhutil.cpp startTrace); $XDG_STATE_HOME/rfhutil/trace.log is only the
	// default of FileTrace's own constructor. A value that is not a path is therefore a relative file name.
	void theTraceVariableIsALiteralPath()
	{
		QTemporaryDir dir;
		const QString cwd = QDir::currentPath();
		QVERIFY(QDir::setCurrent(dir.path()));
		{
			Harness h(nullptr, nullptr, "1");
			QVERIFY(QFileInfo::exists(dir.filePath("1")));
		}
		QDir::setCurrent(cwd);
	}

	void aFailedTraceIsReportedInTheLog()
	{
		QTemporaryDir dir;
		Harness h(nullptr, nullptr, dir.filePath("no/such/dir/../../../../../proc/nope/trace.log"));
		QVERIFY2(h.window->statusLog().text().contains("trace"), qPrintable(h.window->statusLog().text()));
	}

	void closingCommitsAPendingUnitViaDisconnect()
	{
		for (int how = 0; how < 3; how++)		// destroy, close, Exit button
		{
			Harness h;
			rfh::Message m;
			m.body = { 'o', 'k' };
			h.window->document().setMessage(std::move(m));
			rfh::MqmdFields f;
			f.segmentYes = true;		// a put of a segment runs under syncpoint and waits for its commit
			h.window->document().setMqmd(f);
			h.window->mainTab()->setNames("", kQueue, "");
			QSignalSpy flows(&h.window->operations(), &Operations::flowFinished);
			h.button("writeQButton")->click();
			QTRY_VERIFY_WITH_TIMEOUT(flows.count() >= 1, 5000);
			h.settle();
			QCOMPARE(fake.pendingPuts.size(), size_t(1));
			QCOMPARE(fake.queue(kQueue).messages.size(), size_t(0));
			const int backs = fake.backCalls;
			if (how == 1)
			{
				h.window->close();
			}
			else if (how == 2)
			{
				h.window->show();
				h.button("exitButton")->click();
			}
			h.window.reset();
			QCOMPARE(fake.backCalls, backs);								// no explicit rollback
			QCOMPARE(fake.pendingPuts.size(), size_t(0));
			QCOMPARE(fake.queue(kQueue).messages.size(), size_t(1));		// committed via MQDISC
		}
	}

	void changingTabsMovesTheFocusIntoTheDataAndMqmdPages()
	{
		Harness h;
		QVERIFY(h.showActive());
		h.window->tabs()->setCurrentIndex(MainWindow::TabData);
		QCOMPARE(QApplication::focusWidget(), static_cast<QWidget *>(h.window->dataTab()->findChild<QPlainTextEdit *>("dataEdit")));
		h.window->tabs()->setCurrentIndex(MainWindow::TabMqmd);
		QWidget * format = h.window->mqmdTab()->findChild<QComboBox *>("formatCombo");
		QCOMPARE(QApplication::focusWidget(), format);
		// Qt gives a page back the widget that had the focus last; the tab must still start at its first field
		h.window->mqmdTab()->findChild<QLineEdit *>("ccsidEdit")->setFocus();
		h.window->tabs()->setCurrentIndex(MainWindow::TabMain);
		h.window->tabs()->setCurrentIndex(MainWindow::TabMqmd);
		QCOMPARE(QApplication::focusWidget(), format);
	}

	// RFHUTIL_SCREENSHOT_DIR=<dir> saves a picture of each working tab (offscreen only; a manual check, not an assertion)
	void screenshotsOfTheThreeTabs()
	{
		const QByteArray dir = qgetenv("RFHUTIL_SCREENSHOT_DIR");
		if (dir.isEmpty())
		{
			QSKIP("RFHUTIL_SCREENSHOT_DIR is not set");
		}
		Harness h;
		rfh::Message m;
		m.body = { 'H', 'e', 'l', 'l', 'o', ' ', 'R', 'F', 'H' };
		h.window->document().setMessage(std::move(m));
		h.window->resize(1100, 800);
		h.window->show();
		QVERIFY(QTest::qWaitForWindowExposed(h.window.get()));
		for (int tab : { (int)MainWindow::TabMain, (int)MainWindow::TabData, (int)MainWindow::TabMqmd })
		{
			h.window->tabs()->setCurrentIndex(tab);
			QCoreApplication::processEvents();
			QVERIFY(h.window->grab().save(QString::fromLocal8Bit(dir) + "/window-" + MainWindow::tabCaptions()[tab] + ".png"));
		}
	}

	void withoutTheEnvironmentVariableThereIsNoTrace()
	{
		Harness h;
		QVERIFY(!rfhGetTrace()->isTraceEnabled());
	}
};

QTEST_MAIN(MainWindowTests)
#include "MainWindowTests.moc"
