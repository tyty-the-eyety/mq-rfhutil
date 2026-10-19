// GuiSmokeTests.cpp - the whole window against a real queue manager (design section 6, item 4; Plan 3 Task 28).
//
// The window is built on the real MQI (bindings) with a settings file in a temporary directory and driven the way a
// user drives it: buttons, fields, radios. Every test creates its own queue RFHUTIL.TEST.GUI.<n> on the queue manager
// (MQ_BINDINGS_QMGR, default TEST_QM) with runmqsc and deletes it in cleanup(), which Qt Test also runs after a failure.
// Nothing else is created or deleted. All tests skip, with the reason, when the MQ library cannot be loaded or the
// queue manager cannot be reached; RFHUTIL_REQUIRE_MQ=1 turns that skip into a failure, as in tests/core/MqSessionTests.cpp.
//
// Client mode runs only when MQ_CHANNEL is set (MQ_HOST, MQ_PORT, MQ_QMGR, MQ_USER, MQ_PASSWORD, MQ_CLIENT_QUEUE as in
// the core test); it neither creates nor deletes a queue.

#include <QtTest>
#include <QtWidgets>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chrono>
#include <memory>
#include <regex>
#include <string>

#include "AppDocument.h"
#include "AppSettings.h"
#include "DataTab.h"
#include "MainTab.h"
#include "MainWindow.h"
#include "MqmdTab.h"
#include "Operations.h"
#include "ProcessSetup.h"
#include "StatusLog.h"
#include "rfh_message_file.h"
#include "rfh_mq_loader.h"
#include "rfh_mq_session.h"

using namespace rfhqt;

namespace
{
	std::string envOr(const char * name, const char * def)
	{
		const char * v = getenv(name);
		return (v != nullptr && v[0] != 0) ? v : def;
	}

	std::string bindingsQm() { return envOr("MQ_BINDINGS_QMGR", "TEST_QM"); }
	bool requireMq() { return envOr("RFHUTIL_REQUIRE_MQ", "") == "1"; }

	// The same runmqsc helper as the core test: the commands go to standard input, only validated names reach the shell.
	bool runMqsc(const std::string & commands)
	{
		const std::string qm = bindingsQm();
		const std::string bin = envOr("MQ_INSTALLATION_PATH", "/opt/mqm") + "/bin/runmqsc";
		if (!std::regex_match(qm, std::regex("[A-Za-z0-9._%/]{1,48}")) || !std::regex_match(bin, std::regex("[A-Za-z0-9._/+-]+")))
		{
			return false;
		}
		FILE * pipe = popen((bin + " " + qm + " >/dev/null 2>&1").c_str(), "w");
		if (pipe == nullptr)
		{
			return false;
		}
		const bool written = fputs((commands + "\n").c_str(), pipe) >= 0;
		return pclose(pipe) == 0 && written;
	}

	// RFHUTIL_TRACE_FILE is cleared for the life of the object, as in MainWindowTests.cpp.
	struct TraceEnv
	{
		TraceEnv() { qunsetenv("RFHUTIL_TRACE_FILE"); }
		~TraceEnv() { qunsetenv("RFHUTIL_TRACE_FILE"); }
	};

	class StubChooser : public FileChooser
	{
	public:
		QString openPath(QWidget *, const QString &) override { return openAnswer; }
		QString savePath(QWidget *, const QString &) override { return saveAnswer; }
		QString openAnswer, saveAnswer;
	};

	// The depth of a queue as an independent session sees it.
	int depthOf(const std::string & queue)
	{
		rfh::MqSession session;
		rfh::ConnectOptions connect;
		connect.mode = rfh::Mode::Bindings;
		connect.qmName = bindingsQm();
		if (!session.connect(connect).ok())
		{
			return -2;
		}
		rfh::QueueInfo info;
		if (!session.queueInfo(queue, info).ok())
		{
			return -3;
		}
		return info.depth;
	}

	// A window on the real MQ library with its own settings file.
	struct Harness
	{
		Harness()
		{
			MainWindow::Config config;
			config.settingsPath = dir.filePath("rfhutil.conf");
			window.reset(new MainWindow(config));
			window->mainTab()->setFileChooser(&chooser);
			window->resize(1000, 700);
			window->show();
		}

		~Harness()
		{
			// the window (and its session) goes first; the queue is deleted after that by cleanup()
			window.reset();
		}

		template <typename T>
		T * main(const char * name) const { return window->mainTab()->findChild<T *>(name); }
		template <typename T>
		T * mqmd(const char * name) const { return window->mqmdTab()->findChild<T *>(name); }
		template <typename T>
		T * data(const char * name) const { return window->dataTab()->findChild<T *>(name); }

		QString dataText() const { return data<QPlainTextEdit>("dataEdit")->toPlainText(); }
		QString status() const { return window->statusLog().text(); }

		void setTarget(const std::string & queue)
		{
			window->mainTab()->setNames(QString::fromStdString(bindingsQm()), QString::fromStdString(queue), QString());
		}

		// Clicks a Main tab button and waits for the flow it starts to finish.
		void press(const char * button)
		{
			QSignalSpy finished(&window->operations(), &Operations::flowFinished);
			QPushButton * b = main<QPushButton>(button);
			QVERIFY2(b != nullptr, button);
			QVERIFY2(b->isEnabled(), button);
			b->click();
			QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 20000);
			QTRY_VERIFY_WITH_TIMEOUT(!window->document().shell().busy, 5000);
			QCoreApplication::processEvents();
		}

		// Loads text through Open File (the Main tab owns the file name and the file options).
		void openText(const QString & text, const QString & name = QStringLiteral("in.txt"))
		{
			QFile f(dir.filePath(name));
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write(text.toLatin1());
			f.close();
			chooser.openAnswer = f.fileName();
			press("openFileButton");
			QVERIFY2(window->document().message().body.size() == (size_t)text.size(), qPrintable(status()));
		}

		void showTab(int tab)
		{
			window->tabs()->setCurrentIndex(tab);
			QCoreApplication::processEvents();
		}

		// Sets the MQMD tab the way a user does: Format and Persistence.
		void setMqmd(const QString & format, const char * persistRadio)
		{
			showTab(MainWindow::TabMqmd);
			QComboBox * c = mqmd<QComboBox>("formatCombo");
			c->setEditText(format);
			mqmd<QRadioButton>(persistRadio)->click();
			showTab(MainWindow::TabMain);
		}

		// Open File + Format/Persistence + Write Q, for a message that is easy to recognise.
		void put(const QString & text, const char * persistRadio = "persistYesRadio")
		{
			openText(text);
			setMqmd(QStringLiteral("MQSTR"), persistRadio);
			press("writeQButton");
		}

		TraceEnv env;
		QTemporaryDir dir;
		StubChooser chooser;
		std::unique_ptr<MainWindow> window;
	};

	QString zeros() { return QString(48, QLatin1Char('0')); }

	// Answers the next modal message box (the purge confirmation) with Yes or No.
	struct BoxAnswerer
	{
		explicit BoxAnswerer(QMessageBox::StandardButton answer) : answer(answer)
		{
			timer.setInterval(30);
			QObject::connect(&timer, &QTimer::timeout, [this]() {
				if (QMessageBox * box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
				{
					text = box->text();
					box->button(this->answer)->click();
					answered++;
				}
			});
			timer.start();
		}
		QMessageBox::StandardButton answer;
		QTimer timer;
		QString text;
		int answered = 0;
	};
}

class GuiSmokeTests : public QObject
{
	Q_OBJECT

private:
	std::string queue;			// the queue of the running test, empty when none
	int counter = 0;
	bool ready = false;
	QString why;

	void createQueue(const std::string & name)
	{
		QVERIFY2(runMqsc("DEFINE QLOCAL(" + name + ") DEFSOPT(SHARED) SHARE REPLACE"), "DEFINE QLOCAL failed");
		queue = name;
	}

private slots:
	void initTestCase()
	{
		signal(SIGPIPE, SIG_IGN);		// a runmqsc that exits early must not kill the tests
		rfhqt::useCLocaleForNumbersAndCharacters();
		rfh::MqApi api;
		const rfh::Result loaded = rfh::MqLoader::load(api);
		if (!loaded.ok())
		{
			why = QString::fromStdString("no MQ library on this machine: " + loaded.text);
		}
		else if (!runMqsc("DISPLAY QMSTATUS"))
		{
			why = QString::fromStdString("queue manager " + bindingsQm() + " is not available (runmqsc failed)");
		}
		ready = why.isEmpty();
		if (!ready)
		{
			if (requireMq())
			{
				QFAIL(qPrintable(why));
			}
			printf("GUI smoke tests skipped: %s\n", qPrintable(why));
		}
	}

	void init()
	{
		if (!ready)
		{
			QSKIP(qPrintable("GUI smoke tests need a queue manager: " + why));
		}
		queue.clear();
	}

	// runs after every test, also a failed one
	void cleanup()
	{
		if (!queue.empty())
		{
			if (!runMqsc("DELETE QLOCAL(" + queue + ") PURGE"))
			{
				printf("WARNING: could not delete %s\n", queue.c_str());
			}
			queue.clear();
		}
	}

	void writeThenReadShowsTheMessageOnTheDataAndMqmdTabs()
	{
		createQueue("RFHUTIL.TEST.GUI.WRITEREAD");
		Harness h;
		h.setTarget(queue);

		h.put(QStringLiteral("Hello GUI smoke"));
		QVERIFY2(!h.status().contains("fail", Qt::CaseInsensitive), qPrintable(h.status()));
		QCOMPARE(depthOf(queue), 1);

		h.press("clearDataButton");
		QCOMPARE(h.window->document().message().body.size(), size_t(0));
		h.press("clearAllButton");

		h.press("readQButton");
		QVERIFY2(!h.status().contains("fail", Qt::CaseInsensitive), qPrintable(h.status()));
		QCOMPARE(depthOf(queue), 0);

		h.showTab(MainWindow::TabData);
		QVERIFY2(h.dataText().contains("Hello GUI smoke"), qPrintable(h.dataText()));
		h.data<QRadioButton>("viewHexRadio")->click();
		QCoreApplication::processEvents();
		// "Hello GUI" is 48 65 6C 6C 6F 20 47 55 49; the hex view groups four bytes per word
		QVERIFY2(h.dataText().contains("48656C6C 6F204755 49"), qPrintable(h.dataText()));
		h.data<QRadioButton>("viewCharacterRadio")->click();

		h.showTab(MainWindow::TabMqmd);
		QCOMPARE(h.mqmd<QComboBox>("formatCombo")->currentText().trimmed(), QStringLiteral("MQSTR"));
		QVERIFY(h.mqmd<QRadioButton>("persistYesRadio")->isChecked());
		const QString msgId = h.mqmd<QLineEdit>("msgIdEdit")->text();
		QVERIFY2(!msgId.isEmpty() && msgId != zeros(), qPrintable(msgId));
		QCOMPARE(h.window->document().message().body.size(), size_t(15));
	}

	void browseWalksFirstNextAndPrevious()
	{
		createQueue("RFHUTIL.TEST.GUI.BROWSE");
		Harness h;
		h.setTarget(queue);
		h.put(QStringLiteral("first message"));
		h.put(QStringLiteral("second message"));
		h.put(QStringLiteral("third message"));
		QCOMPARE(depthOf(queue), 3);
		h.press("clearAllButton");

		const auto bodyIs = [&h](const char * text) {
			const auto & body = h.window->document().message().body;
			return std::string(body.begin(), body.end()) == text;
		};
		const auto shown = [&h](const char * text) {
			h.showTab(MainWindow::TabData);
			const bool found = h.dataText().contains(text);
			h.showTab(MainWindow::TabMain);
			return found;
		};

		// before: the browse buttons that need a browse are off
		QVERIFY(h.main<QPushButton>("startBrowseButton")->isEnabled());
		QVERIFY(!h.main<QPushButton>("browseNextButton")->isEnabled());
		QVERIFY(!h.main<QPushButton>("endBrowseButton")->isEnabled());

		h.press("startBrowseButton");
		QVERIFY2(bodyIs("first message"), qPrintable(h.status()));
		QVERIFY(shown("first message"));
		QVERIFY(!h.main<QPushButton>("startBrowseButton")->isEnabled());
		QVERIFY(h.main<QPushButton>("browseNextButton")->isEnabled());
		QVERIFY(!h.main<QPushButton>("browsePrevButton")->isEnabled());		// Windows turns Prev off at the first message
		QVERIFY(h.main<QPushButton>("endBrowseButton")->isEnabled());
		QVERIFY(!h.main<QPushButton>("readQButton")->isEnabled());		// no destructive read while a browse is open

		h.press("browseNextButton");
		QVERIFY2(bodyIs("second message"), qPrintable(h.status()));
		QVERIFY(shown("second message"));
		QVERIFY(h.main<QPushButton>("browsePrevButton")->isEnabled());

		h.press("browseNextButton");
		QVERIFY2(bodyIs("third message"), qPrintable(h.status()));
		QVERIFY(shown("third message"));

		h.press("browsePrevButton");
		QVERIFY2(bodyIs("second message"), qPrintable(h.status()));
		QVERIFY(shown("second message"));
		QVERIFY(h.main<QPushButton>("endBrowseButton")->isEnabled());

		h.press("endBrowseButton");
		QVERIFY(h.status().contains("Browse operation ended"));
		QVERIFY(h.main<QPushButton>("startBrowseButton")->isEnabled());
		QVERIFY(h.main<QPushButton>("readQButton")->isEnabled());
		QVERIFY(!h.main<QPushButton>("browseNextButton")->isEnabled());
		QCOMPARE(depthOf(queue), 3);			// browsing removed nothing
	}

	void saveFileRoundTrips()
	{
		createQueue("RFHUTIL.TEST.GUI.SAVE");
		Harness h;
		h.setTarget(queue);
		h.put(QStringLiteral("round trip body"));
		h.press("clearAllButton");
		h.press("readQButton");

		const QString path = h.dir.filePath("saved.msg");
		h.chooser.saveAnswer = path;
		h.press("saveFileButton");
		QVERIFY2(QFile::exists(path), qPrintable(h.status()));

		rfh::MqmdFields fields;
		rfh::fileio::ReadResult read;
		const rfh::Result r = rfh::fileio::read(path.toStdString(), rfh::fileio::ReadOptions(), fields, read);
		QVERIFY2(r.ok(), r.text.c_str());
		const auto & body = h.window->document().message().body;
		QVERIFY(read.hadMqmd);
		QCOMPARE(QByteArray(reinterpret_cast<const char *>(read.message.body.data()), (int)read.message.body.size()),
		         QByteArray(reinterpret_cast<const char *>(body.data()), (int)body.size()));
		QCOMPARE(QString::fromStdString(fields.format).trimmed(), QStringLiteral("MQSTR"));
		QCOMPARE(fields.persistence, (int)MQPERSIST_YES);
		const rfh::MqmdFields & tab = h.window->document().mqmd();
		QCOMPARE(fields.ccsid, tab.ccsid);
		QCOMPARE(fields.msgType, tab.msgType);
		QCOMPARE(memcmp(fields.msgId, tab.msgId, sizeof(fields.msgId)), 0);
	}

	void aMissingQueueShowsTheWindowsText()
	{
		// the name is in the test namespace but is never created
		Harness h;
		h.setTarget("RFHUTIL.TEST.GUI.NOSUCH");
		h.press("readQButton");
		QVERIFY2(h.status().contains("2085 No such queue"), qPrintable(h.status()));
	}

	void getByMsgIdFindsTheRightMessage()
	{
		createQueue("RFHUTIL.TEST.GUI.MSGID");
		Harness h;
		h.setTarget(queue);

		rfh::MqmdFields wanted;
		for (const char * text : { "alpha", "bravo", "charlie" })
		{
			h.put(QString::fromLatin1(text));
			if (strcmp(text, "bravo") == 0)
			{
				wanted = h.window->document().mqmd();		// the id MQ gave the put is shown on the MQMD tab
			}
		}
		QCOMPARE(depthOf(queue), 3);
		bool nonZero = false;
		for (unsigned char b : wanted.msgId) nonZero = nonZero || b != 0;
		QVERIFY(nonZero);

		h.press("clearAllButton");
		rfh::MqmdFields fields = h.window->document().mqmd();
		memcpy(fields.msgId, wanted.msgId, sizeof(fields.msgId));
		h.window->document().setMqmd(fields);
		h.main<QCheckBox>("getByMsgIdCheck")->setChecked(true);

		h.press("readQButton");
		const auto & body = h.window->document().message().body;
		QCOMPARE(QString::fromLatin1(reinterpret_cast<const char *>(body.data()), (int)body.size()), QStringLiteral("bravo"));
		QCOMPARE(depthOf(queue), 2);
	}

	void purgeEmptiesTheQueue()
	{
		createQueue("RFHUTIL.TEST.GUI.PURGE");
		Harness h;
		h.setTarget(queue);
		h.put(QStringLiteral("one"));
		h.put(QStringLiteral("two"));
		QCOMPARE(depthOf(queue), 2);

		BoxAnswerer yes(QMessageBox::Yes);
		h.press("purgeQButton");
		QTRY_VERIFY_WITH_TIMEOUT(yes.answered >= 1, 5000);
		QVERIFY2(!yes.text.isEmpty(), "the confirmation had no text");
		QTRY_COMPARE_WITH_TIMEOUT(depthOf(queue), 0, 10000);
		QVERIFY2(!h.status().contains("fail", Qt::CaseInsensitive), qPrintable(h.status()));
	}

	void purgeAnsweredNoLeavesTheMessages()
	{
		createQueue("RFHUTIL.TEST.GUI.PURGENO");
		Harness h;
		h.setTarget(queue);
		h.put(QStringLiteral("keep me"));

		BoxAnswerer no(QMessageBox::No);
		h.press("purgeQButton");
		QTRY_VERIFY_WITH_TIMEOUT(no.answered >= 1, 5000);
		QCOMPARE(depthOf(queue), 1);
	}

	void aWaitingGetCanBeCancelled()
	{
		createQueue("RFHUTIL.TEST.GUI.WAIT");
		Harness h;
		h.setTarget(queue);
		h.main<QSpinBox>("waitSpin")->setValue(30);

		QSignalSpy finished(&h.window->operations(), &Operations::flowFinished);
		QElapsedTimer clock;
		clock.start();
		h.main<QPushButton>("readQButton")->click();
		QTest::qWait(1000);
		QVERIFY2(finished.count() == 0, "the get returned before it was cancelled");
		QVERIFY(h.main<QPushButton>("cancelButton")->isEnabled());
		h.main<QPushButton>("cancelButton")->click();
		const qint64 cancelledAt = clock.elapsed();
		QTRY_VERIFY_WITH_TIMEOUT(finished.count() >= 1, 3000);
		printf("cancel took %lld ms after the click\n", (long long)(clock.elapsed() - cancelledAt));
		QVERIFY2(clock.elapsed() - cancelledAt < 3000, "the cancel took too long");
		QTRY_VERIFY_WITH_TIMEOUT(!h.window->document().shell().busy, 3000);
		QVERIFY(!h.main<QPushButton>("cancelButton")->isEnabled());
	}

	void loadNamesListsTheTestQueues()
	{
		createQueue("RFHUTIL.TEST.GUI.NAMES");
		Harness h;
		h.setTarget(queue);
		h.press("loadNamesButton");
		const QStringList names = h.window->operations().queueNames(QString::fromStdString(bindingsQm()));
		if (names.isEmpty() && h.status().contains("command server", Qt::CaseInsensitive))
		{
			QSKIP(qPrintable("the PCF command server of " + QString::fromStdString(bindingsQm()) + " does not answer (strmqcsv): " + h.status()));
		}
		QVERIFY2(names.contains(QString::fromStdString(queue)), qPrintable(names.join(',') + " / " + h.status()));
		QVERIFY(names.contains(QStringLiteral("RFHUTIL.TEST.GUI.NAMES")));
	}

	void clientModeConnectsOverTheChannel()
	{
		if (envOr("MQ_CHANNEL", "").empty())
		{
			QSKIP("set MQ_CHANNEL (and MQ_HOST, MQ_PORT, MQ_QMGR, MQ_USER, MQ_PASSWORD, MQ_CLIENT_QUEUE) to test client mode");
		}
		Harness h;
		const QString clientQueue = QString::fromStdString(envOr("MQ_CLIENT_QUEUE", "DEV.QUEUE.1"));
		h.window->mainTab()->setNames(QString::fromStdString(envOr("MQ_QMGR", "QM1")), clientQueue, QString());
		h.main<QRadioButton>("clientRadio")->click();
		h.main<QLineEdit>("channelEdit")->setText(QString::fromStdString(envOr("MQ_CHANNEL", "")));
		h.main<QLineEdit>("connNameEdit")->setText(QString::fromStdString(envOr("MQ_HOST", "localhost") + "(" + envOr("MQ_PORT", "1414") + ")"));

		QTimer answer;
		answer.setInterval(30);
		QObject::connect(&answer, &QTimer::timeout, [&]() {
			if (QDialog * d = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
			{
				if (d->objectName() == "connUserDialog")
				{
					d->findChild<QLineEdit *>("userId")->setText(QString::fromStdString(envOr("MQ_USER", "app")));
					d->findChild<QLineEdit *>("password")->setText(QString::fromStdString(envOr("MQ_PASSWORD", "passw0rd")));
					d->findChild<QPushButton *>("ok")->click();
				}
			}
		});
		answer.start();
		h.main<QPushButton>("setConnIdButton")->click();
		answer.stop();

		h.put(QStringLiteral("over the channel"));
		QVERIFY2(!h.status().contains("fail", Qt::CaseInsensitive), qPrintable(h.status()));
		h.press("clearAllButton");
		h.press("readQButton");
		const auto & body = h.window->document().message().body;
		QCOMPARE(QString::fromLatin1(reinterpret_cast<const char *>(body.data()), (int)body.size()), QStringLiteral("over the channel"));
	}
};

QTEST_MAIN(GuiSmokeTests)
#include "GuiSmokeTests.moc"
