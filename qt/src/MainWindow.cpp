#include "MainWindow.h"

#include "AppDocument.h"
#include "AppSettings.h"
#include "CicsTab.h"
#include "ConnTab.h"
#include "DataTab.h"
#include "DlqTab.h"
#include "FileTrace.h"
#include "ImsTab.h"
#include "JmsTab.h"
#include "MainTab.h"
#include "MqmdTab.h"
#include "OtherTab.h"
#include "Operations.h"
#include "PscrTab.h"
#include "PsTab.h"
#include "PubSubTab.h"
#include "RfhTab.h"
#include "SessionController.h"
#include "StatusLog.h"
#include "UsrPropTab.h"
#include "UsrTab.h"
#include "rfh_trace.h"

#include <QCoreApplication>
#include <QCloseEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTabWidget>

namespace rfhqt
{
	// The trace belongs to the process while the window lives: installed before the worker thread starts (the first
	// member to be built) and removed after it has stopped (the last to be destroyed).
	class MainWindow::TraceScope
	{
	public:
		TraceScope()
		{
			trace = FileTrace::fromEnvironment(&error);
			if (trace)
			{
				rfhSetTrace(trace.get());
			}
		}

		~TraceScope()
		{
			if (trace)
			{
				rfhSetTrace(nullptr);
			}
		}

		TraceScope(const TraceScope &) = delete;
		TraceScope & operator=(const TraceScope &) = delete;

		QString error;
		std::unique_ptr<FileTrace> trace;
	};

	const QStringList & MainWindow::tabCaptions()
	{
		static const QStringList captions = {
			"Main", "Data", "MQMD", "PS", "Usr Prop", "RFH", "PubSub", "pscr",
			"jms", "usr", "other", "CICS", "IMS", "DLQ", "Conn"
		};
		return captions;
	}

	bool MainWindow::isPhase1Tab(int index)
	{
		// one owner: the enable rules (AppDocument::enablesFor)
		static_assert((int)rfhqt::Tab::Conn == TabConn && (int)rfhqt::Tab::Count == TabCount && (int)rfhqt::Tab::Mqmd == TabMqmd && (int)rfhqt::Tab::Data == TabData, "MainWindow::Tab follows rfhqt::Tab");
		if (index < 0 || index >= TabCount)
		{
			return false;
		}
		return AppDocument::enablesFor(EnableInputs()).tabEnabled((rfhqt::Tab)index);
	}

	int MainWindow::steppedTab(int current, bool forward, const std::function<bool(int)> & usable)
	{
		// Windows steps over the tabs before Conn only (rfhutilView.cpp: if (PAGE_DLQ == sel) sel = 0)
		const int steps = TabConn;
		const int direction = forward ? 1 : -1;
		const int base = (current >= steps) ? (forward ? -1 : steps) : current;
		for (int k = 1; k <= steps; k++)
		{
			const int index = (((base + direction * k) % steps) + steps) % steps;
			if (usable(index))
			{
				return index;
			}
		}
		return current;
	}

	MainWindow::MainWindow(QWidget * parent)
		: MainWindow(Config(), parent)
	{
	}

	MainWindow::MainWindow(const Config & config, QWidget * parent)
		: QMainWindow(parent),
		  trace(new TraceScope),
		  appSettings(new AppSettings(config.settingsPath)),
		  log(new StatusLog),
		  doc(new AppDocument),
		  controller(config.api != nullptr ? new SessionController(*config.api) : new SessionController),
		  ops(new Operations(*doc, *controller, *log)),
		  tabWidget(new QTabWidget(this))
	{
		const QString ver = QCoreApplication::applicationVersion();
		setWindowTitle(ver.isEmpty() ? QStringLiteral("RfhUtil (Linux)")
		                             : QStringLiteral("RfhUtil ") + ver + QStringLiteral(" (Linux)"));
		setCentralWidget(tabWidget);

		mainPage    = new MainTab(*doc, *ops, *log, appSettings.get());
		dataPage    = new DataTab(*doc, *ops, appSettings.get());
		mqmdPage    = new MqmdTab(*doc);
		psPage      = new PsTab(*doc);
		usrPropPage = new UsrPropTab(*doc);
		rfhPage     = new RfhTab(*doc);
		pubSubPage  = new PubSubTab(*doc);
		pscrPage    = new PscrTab(*doc);
		jmsPage     = new JmsTab(*doc);
		usrPage     = new UsrTab(*doc);
		otherPage   = new OtherTab(*doc);
		cicsPage    = new CicsTab(*doc);
		imsPage     = new ImsTab(*doc);
		dlqPage     = new DlqTab(*doc);
		connPage    = new ConnTab(*appSettings);

		const QStringList & captions = tabCaptions();
		for (int i = 0; i < TabCount; i++)
		{
			QWidget * page = (i == TabMain)    ? static_cast<QWidget *>(mainPage)
				: (i == TabData)    ? static_cast<QWidget *>(dataPage)
				: (i == TabMqmd)    ? static_cast<QWidget *>(mqmdPage)
				: (i == TabPs)      ? static_cast<QWidget *>(psPage)
				: (i == TabUsrProp) ? static_cast<QWidget *>(usrPropPage)
				: (i == TabRfh)     ? static_cast<QWidget *>(rfhPage)
				: (i == TabPubSub)  ? static_cast<QWidget *>(pubSubPage)
				: (i == TabPscr)    ? static_cast<QWidget *>(pscrPage)
				: (i == TabJms)     ? static_cast<QWidget *>(jmsPage)
				: (i == TabUsr)     ? static_cast<QWidget *>(usrPage)
				: (i == TabOther)   ? static_cast<QWidget *>(otherPage)
				: (i == TabCics)    ? static_cast<QWidget *>(cicsPage)
				: (i == TabIms)     ? static_cast<QWidget *>(imsPage)
				: (i == TabDlq)     ? static_cast<QWidget *>(dlqPage)
				: (i == TabConn)    ? static_cast<QWidget *>(connPage)
				: nullptr;
			tabWidget->addTab(page, captions[i]);
			if (!isPhase1Tab(i))
			{
				tabWidget->setTabEnabled(i, false);
				tabWidget->setTabToolTip(i, "Enabled when data is present");
			}
		}

		buildMenus();
		wireTabs();
		wireShortcuts();

		// what the process learned while starting, oldest first (the log shows the newest entry on top)
		const rfh::Result mq = config.loadMq ? config.loadMq() : (config.api != nullptr) ? rfh::Result::success() : [] { rfh::MqApi api; return rfh::MqLoader::load(api); }();
		if (!mq.ok())
		{
			// General.cpp:1192-1205 (bindings build); the reason is added to the Windows text
			log->add(QStringList() << "*****MQ not installed" << QString::fromStdString(mq.text));
		}
		if (!trace->error.isEmpty())
		{
			log->add(QStringLiteral("The trace file could not be started: ") + trace->error);
		}
		log->add(appSettings->takeWarnings());

		const QByteArray geometry = appSettings->windowGeometry();
		if (!geometry.isEmpty())
		{
			restoreGeometry(geometry);
		}
		applyMenuEnables();
	}

	MainWindow::~MainWindow()
	{
		// the tabs hold references to the objects below: they go first
		delete tabWidget;
		tabWidget = nullptr;
	}

	void MainWindow::closeEvent(QCloseEvent * event)
	{
		appSettings->setWindowGeometry(saveGeometry());
		QMainWindow::closeEvent(event);
	}

	QAction * MainWindow::addAction(QMenu * menu, const char * name, const QString & text, const std::function<void()> & run)
	{
		QAction * action = menu->addAction(text);
		action->setObjectName(QString::fromLatin1(name));
		if (run)
		{
			connect(action, &QAction::triggered, this, [run]() { run(); });
		}
		return action;
	}

	// Menu bar IDR_MAINFRAME (inventory 7.2). The Ids, Cluster Queues, Theme and Help items are not ported. The captions keep
	// the Windows mnemonics for the items (they act only while their menu is open); the menu names keep only Alt+M, see the
	// header.
	void MainWindow::buildMenus()
	{
		const auto menu = [this](const char * name, const QString & title)
		{
			QMenu * m = menuBar()->addMenu(title);
			m->setObjectName(QString::fromLatin1(name));
			return m;
		};

		QMenu * file = menu("fileMenu", "File");
		fileOpen = addAction(file, "fileOpen", "&Open\tCtrl+O", [this]() { mainPage->openFileDialog(); });
		fileSave = addAction(file, "fileSave", "&Save\tCtrl+S", [this]() { mainPage->saveFileDialog(); });
		file->addSeparator();
		addAction(file, "fileExit", "E&xit", [this]() { close(); });

		QMenu * edit = menu("editMenu", "Edit");
		editTop = addAction(edit, "editTop", "Top", [this]() { dataPage->scrollTop(); });
		editBottom = addAction(edit, "editBottom", "Bottom", [this]() { dataPage->scrollBottom(); });

		QMenu * search = menu("searchMenu", "Search");
		searchFind = addAction(search, "searchFind", "&Find\tCtrl+F", [this]() { dataPage->showFind(); });
		searchFindHex = addAction(search, "searchFindHex", "Find He&x", [this]() { dataPage->showFindHex(); });
		searchGoto = addAction(search, "searchGoto", "&Go to\tCtrl+G", [this]() { dataPage->showGoto(); });

		const auto option = [this](QMenu * m, const char * name, const QString & text, bool checked)
		{
			QAction * a = addAction(m, name, text, nullptr);
			a->setCheckable(true);
			a->setChecked(checked);
			connect(a, &QAction::toggled, this, [this]() { fileOptionsChanged(); });
			return a;
		};

		// inventory 3.6; Ignore MQMD is a dead option in Windows and is left out
		QMenu * read = menu("readMenu", "Read");
		readRemoveCrLf = option(read, "readRemoveCrLf", "Remo&ve CrLf", false);
		readUnixText = option(read, "readUnixText", "&Unix Text", false);
		readIgnoreHeaders = option(read, "readIgnoreHeaders", "&Ignore Headers", false);
		QAction * saveHeaders = option(read, "readSaveHeaders", "Save Headers", false);
		saveHeaders->setEnabled(false);
		saveHeaders->setToolTip("Not yet ported");

		QMenu * write = menu("writeMenu", "Write");
		writeNoHeaders = option(write, "writeNoHeaders", "No &Headers", false);
		writeIncludeMqmd = option(write, "writeIncludeMqmd", "Include MQMD", true);

		QMenu * view = menu("viewMenu", "View");
		viewSystemQueues = addAction(view, "viewSystemQueues", "System Queues", nullptr);
		viewSystemQueues->setCheckable(true);
		viewSystemQueues->setChecked(appSettings->showSystemQueues());
		connect(viewSystemQueues, &QAction::toggled, this, [this](bool show)
		{
			appSettings->setShowSystemQueues(show);
			mainPage->setShowSystemQueues(show);
		});
		mainPage->setShowSystemQueues(viewSystemQueues->isChecked());

		// rfhutilView.cpp:2462-2600: the explicit commands; the three MQOpen items wait for a public open in the core
		QMenu * mq = menu("mqMenu", "&MQ");
		mqConnect = addAction(mq, "mqConnect", "MQ&Conn", [this]()
		{
			mainPage->syncDocument();
			ops->connectQm();
		});
		mqDisconnect = addAction(mq, "mqDisconnect", "MQ&Disc", [this]()
		{
			mainPage->syncDocument();
			ops->disconnectQm();
		});
		mq->addSeparator();
		mqClose = addAction(mq, "mqClose", "MQClos&e", [this]()
		{
			mainPage->syncDocument();
			ops->closeQueueMenu();
		});
		mq->addSeparator();
		mqEndBrowse = addAction(mq, "mqEndBrowse", "End Browse", [this]()
		{
			mainPage->syncDocument();
			ops->endBrowse();
		});

		fileOptionsChanged();
	}

	// The menu items follow the same rules as the buttons (AppDocument::enablesFor).
	void MainWindow::applyMenuEnables()
	{
		const Enables e = doc->enables();
		fileOpen->setEnabled(e.menuOpen);
		fileSave->setEnabled(e.menuSave);
		editTop->setEnabled(e.menuTop);
		editBottom->setEnabled(e.menuBottom);
		searchFind->setEnabled(e.menuFind);
		searchFindHex->setEnabled(e.menuFindHex);
		searchGoto->setEnabled(e.menuGoto);
		mqConnect->setEnabled(e.menuMqConn);
		mqDisconnect->setEnabled(e.menuMqDisc);
		mqClose->setEnabled(e.menuMqClose);
		mqEndBrowse->setEnabled(e.menuMqEndBrowse);
	}

	void MainWindow::fileOptionsChanged()
	{
		if (mainPage == nullptr)
		{
			return;
		}
		rfh::fileio::ReadOptions read;
		read.removeCrLf = readRemoveCrLf->isChecked();
		read.unixLineEnds = readUnixText->isChecked();
		read.ignoreHeaders = readIgnoreHeaders->isChecked();
		rfh::fileio::WriteOptions write;
		write.dataOnly = writeNoHeaders->isChecked();
		write.includeMqmd = writeIncludeMqmd->isChecked();
		mainPage->setFileOptions(read, write);
	}

	void MainWindow::wireTabs()
	{
		connect(mainPage, &MainTab::exitRequested, this, &MainWindow::close);
		connect(dataPage, &DataTab::statusMessage, log.get(), [this](const QString & text) { log->add(text); });
		connect(mqmdPage, &MqmdTab::statusMessage, log.get(), [this](const QString & text) { log->add(text); });
		connect(ops.get(), &Operations::titleChanged, this, [this](const QString & queue) { setWindowTitle(queue); });
		connect(doc.get(), &AppDocument::stateChanged, this, &MainWindow::applyMenuEnables);
		connect(tabWidget, &QTabWidget::currentChanged, this, &MainWindow::tabChanged);
		shownTab = tabWidget->currentIndex();
	}

	void MainWindow::tabChanged(int index)
	{
		if (index == shownTab)
		{
			return;
		}
		if (doc->shell().findDialogOpen)
		{
			// the tab control ignores a change while a Find dialog is open
			QSignalBlocker blocker(tabWidget);
			tabWidget->setCurrentIndex(shownTab);
			return;
		}
		if (shownTab == TabMain)
		{
			mainPage->syncDocument();		// OnKillActive
		}
		shownTab = index;
		ShellState shell = doc->shell();
		shell.tab = (rfhqt::Tab)index;
		doc->setShell(shell);
		if (index == TabData)
		{
			dataPage->focusFirst();
		}
		else if (index == TabMqmd)
		{
			mqmdPage->focusFirst();
		}
	}

	void MainWindow::step(bool forward)
	{
		const int next = steppedTab(tabWidget->currentIndex(), forward, [this](int i) { return tabWidget->isTabEnabled(i); });
		tabWidget->setCurrentIndex(next);
	}

	void MainWindow::wireShortcuts()
	{
		const auto window = [this](const QKeySequence & keys, std::function<void()> run)
		{
			QShortcut * shortcut = new QShortcut(keys, this);
			shortcut->setContext(Qt::WindowShortcut);
			connect(shortcut, &QShortcut::activated, this, [run]() { run(); });
		};
		window(QKeySequence(Qt::ALT | Qt::Key_N), [this]() { step(true); });
		window(QKeySequence(Qt::ALT | Qt::Key_P), [this]() { step(false); });

		// Ctrl+O and Ctrl+S act on every page (inventory 7.1); the Main tab has its own
		for (QWidget * page : { static_cast<QWidget *>(dataPage), static_cast<QWidget *>(mqmdPage) })
		{
			QShortcut * open = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_O), page);
			connect(open, &QShortcut::activated, this, [this]() { mainPage->openFileDialog(); });
			QShortcut * save = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), page);
			connect(save, &QShortcut::activated, this, [this]() { mainPage->saveFileDialog(); });
		}
	}
}
