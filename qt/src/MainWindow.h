// MainWindow.h - the application window: the shared objects, the three working tabs, the menus and the window keys.
//
// The window owns every shared object once (trace, settings, status log, document, session controller, operations) and
// gives the document, operations and log to the tabs, so no tab owns state another one needs:
//
//   * Document. Tabs write the document whenever the user changes something; Operations reads the document. The window
//     adds the one thing no tab knows, which tab is visible (AppDocument::shell().tab, the input of the menu enable rules),
//     and copies the Main tab into the document when it is left (Windows OnKillActive).
//   * Menus. Every item follows AppDocument::enables(), the same rules as the buttons. The captions advertise their keys
//     ("Open\tCtrl+O") but the window registers none of them: Ctrl+O and Ctrl+S belong to the pages (the Main tab owns its
//     own; the window adds them to the Data and MQMD pages so each page has exactly one, and a window-wide shortcut would
//     be ambiguous with the Main tab's), Ctrl+F, Ctrl+Z and Ctrl+G to the Data tab.
//   * Mnemonics. The menu bar has none but Alt+M for MQ: every other letter a menu name would use (F, E, S, R, W, V, H) is a
//     mnemonic of a control on a tab (FIX, Ebcdic, Save File and Simp Chinese, Browse Prev, Write Q, End Browse, Thai), and
//     two widgets claiming one letter make the key ambiguous, so it would do nothing. Alt+N and Alt+P step through the
//     tabs, so no control may use N or P (Browse Next has lost its N).
//   * Tabs. Only the Main, Data and MQMD tabs work in Phase 1; the others are shown disabled. A change is refused while a
//     Find dialog is open (rfhutilView.cpp:749-780).
//
// Threading: GUI thread only.
#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

#include "rfh_mq_loader.h"
#include "rfh_result.h"

class QAction;
class QMenu;
class QTabWidget;

namespace rfhqt
{
	class AppDocument;
	class AppSettings;
	class DataTab;
	class MainTab;
	class MqmdTab;
	class Operations;
	class SessionController;
	class StatusLog;

	class MainWindow : public QMainWindow
	{
		Q_OBJECT

	public:
		// The tabs, in the order of the Windows program (rfhutilView.cpp).
		enum Tab
		{
			TabMain, TabData, TabMqmd, TabPs, TabUsrProp, TabRfh, TabPubSub, TabPscr,
			TabJms, TabUsr, TabOther, TabCics, TabIms, TabDlq, TabConn,
			TabCount
		};

		// What a test (or another host) may replace. The defaults are the real program.
		struct Config
		{
			QString settingsPath;					// empty = AppSettings::defaultFilePath()
			const rfh::MqApi * api = nullptr;		// null = the real MQ library, loaded on first use; not owned, copied
			std::function<rfh::Result()> loadMq;	// how to learn whether MQ can be loaded; empty = MqLoader::load, or success when api is given
		};

		explicit MainWindow(QWidget * parent = nullptr);
		explicit MainWindow(const Config & config, QWidget * parent = nullptr);
		~MainWindow() override;

		QTabWidget * tabs() const { return tabWidget; }
		MainTab * mainTab() const { return mainPage; }
		DataTab * dataTab() const { return dataPage; }
		MqmdTab * mqmdTab() const { return mqmdPage; }
		AppDocument & document() const { return *doc; }
		AppSettings & settings() const { return *appSettings; }
		StatusLog & statusLog() const { return *log; }
		Operations & operations() const { return *ops; }

		// The tab captions, exactly as Windows shows them (some are lower case).
		static const QStringList & tabCaptions();

		// True for the tabs that work in Phase 1.
		static bool isPhase1Tab(int index);

		// Alt+N / Alt+P (rfhutilView.cpp:2856-2965): the next (or previous) tab for which usable() is true, wrapping from DLQ
		// to Main and never stopping on Conn. When no tab qualifies the current one is returned.
		static int steppedTab(int current, bool forward, const std::function<bool(int)> & usable);

	protected:
		void closeEvent(QCloseEvent * event) override;

	private:
		class TraceScope;

		void buildMenus();
		QAction * addAction(QMenu * menu, const char * name, const QString & text, const std::function<void()> & run);
		void wireTabs();
		void wireShortcuts();
		void applyMenuEnables();
		void fileOptionsChanged();
		void tabChanged(int index);
		void step(bool forward);

		// Destruction runs bottom to top: the operations and the controller (and its worker thread) go before the trace
		// is uninstalled, which is last.
		std::unique_ptr<TraceScope> trace;
		std::unique_ptr<AppSettings> appSettings;
		std::unique_ptr<StatusLog> log;
		std::unique_ptr<AppDocument> doc;
		std::unique_ptr<SessionController> controller;
		std::unique_ptr<Operations> ops;

		QTabWidget * tabWidget;
		MainTab * mainPage = nullptr;
		DataTab * dataPage = nullptr;
		MqmdTab * mqmdPage = nullptr;
		int shownTab = 0;
		QAction * readRemoveCrLf = nullptr;
		QAction * readUnixText = nullptr;
		QAction * readIgnoreHeaders = nullptr;
		QAction * writeNoHeaders = nullptr;
		QAction * writeIncludeMqmd = nullptr;
		QAction * viewSystemQueues = nullptr;
		QAction * fileOpen = nullptr;
		QAction * fileSave = nullptr;
		QAction * editTop = nullptr;
		QAction * editBottom = nullptr;
		QAction * searchFind = nullptr;
		QAction * searchFindHex = nullptr;
		QAction * searchGoto = nullptr;
		QAction * mqConnect = nullptr;
		QAction * mqDisconnect = nullptr;
		QAction * mqClose = nullptr;
		QAction * mqEndBrowse = nullptr;
	};
}
