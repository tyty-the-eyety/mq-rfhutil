// MainTab.h - the Main tab (Windows IDD_MAIN, class General, General.cpp; layout in rfhutil.rc:297).
//
// The tab owns no MQ logic. It turns the widgets into MainOptions and AppDocument values, calls Operations, and shows
// what AppDocument and StatusLog hold. Three rules keep that honest:
//
//   * Read at click time. The queue manager, queue name, remote queue manager and the options are copied into the
//     document and into Operations when a button is clicked (General.cpp does UpdateData(TRUE) and copies the Put/Get
//     options into DataArea at the top of every handler; OnKillActive 656-687 does the same when the page is left),
//     never earlier, so nothing is cached between clicks. syncDocument() is that step, for a window that wants it on a
//     tab change. The three check boxes the MQMD tab depends on (Set Iden Context, Set All Context, Alternate User Id)
//     reach the shell state when they are toggled, because the MQMD tab enables its fields from them.
//   * Enabled states come only from AppDocument::enablesFor (through doc.enables()); the tab adds none of its own. The
//     controls that are not ported in Phase 1 (Save Q, Load Q, Move Q, Display Q and the User Props "Yes" radio) are
//     never enabled and say "Not yet ported" in their tool tips.
//   * Passwords stay in memory. The Set Conn Id values (ConnUserDialog) are kept in this object and handed to Operations
//     with every setOptions(); nothing here writes them to the settings, the log or the trace.
//
// Settings. The queue manager and queue name of the last run come from AppSettings (the first recent entries, as
// GetLastUsedQNames does) and the Connection box from AppSettings::connection(). Nothing is written by the tab: Operations
// writes the queue manager, the queue and the connection after a flow that worked (and nothing after a failure).
//
// Keys. Return in either combo does nothing beyond the combo's own handling: Windows only closes the drop-down and takes
// the highlighted item (General::PreTranslateMessage 1443-1484), and the dialog has no default button. The Ctrl+ and
// Alt+ accelerators of inventory 7.1 for the Main tab are window shortcuts that act only while this tab is visible and
// only on an enabled button.
//
// Threading: GUI thread only.
#pragma once

#include <QWidget>

#include <functional>
#include <memory>

#include "Operations.h"
#include "rfh_message_file.h"
#include "rfh_mq_session.h"

class QButtonGroup;
class QShortcut;

namespace Ui
{
	class MainTab;
}

namespace rfhqt
{
	class AppDocument;
	class AppSettings;
	class StatusLog;

	// The file dialogs, behind an interface so a test can answer them.
	class FileChooser
	{
	public:
		virtual ~FileChooser() = default;
		// Windows OnFileOpen: filter "All (*.*)|XML (*.XML)|Text (*.TXT)", starting in the last directory. Empty = cancelled.
		virtual QString openPath(QWidget * parent, const QString & directory) = 0;
		// Windows OnWriteFile: no filter, asks before it replaces a file. Empty = cancelled.
		virtual QString savePath(QWidget * parent, const QString & directory) = 0;
	};

	class MainTab : public QWidget
	{
		Q_OBJECT

	public:
		// Windows IDC_QM: MQ_Q_NAME_LENGTH in a bindings build, 255 (channel/host/port) in a client build.
		static constexpr int MaxQueueManagerBindings = 48;
		static constexpr int MaxQueueManagerClient = 255;
		static constexpr int MaxQueueName = 255;		// DDV_MaxChars(m_q_name, 255), General.cpp:177
		static constexpr int MaxRemoteQueueManager = 48;	// DDV_MaxChars(m_remote_qm, 48), General.cpp:184
		static constexpr int MaxFileCodePage = 5;			// DDV_MaxChars(m_file_codepage, 5), General.cpp:203

		MainTab(AppDocument & document, Operations & operations, StatusLog & statusLog, AppSettings * settings = nullptr,
		        QWidget * parent = nullptr);
		~MainTab() override;

		// The widgets as MainOptions (with the Set Conn Id values and the file options). The caller wipes
		// result.connect when it is done with the copy.
		MainOptions options() const;

		// Copies the queue manager, queue name, remote queue manager and the three context check boxes into the
		// document and the options into Operations. Every button does this first.
		void syncDocument();

		// The Read and Write menu choices belong to the window; the tab passes them on with its own.
		void setFileOptions(const rfh::fileio::ReadOptions & read, const rfh::fileio::WriteOptions & write);

		// View > System Queues: hides or shows the SYSTEM.* names that Load Names found.
		void setShowSystemQueues(bool show);
		bool showSystemQueues() const { return systemQueues; }

		// The queue managers offered by the drop-down besides the recent ones (default: the mqs.ini of this machine).
		void setQueueManagerLister(std::function<QStringList()> lister);
		void refreshQueueManagers();
		void refreshQueues();

		void setFileChooser(FileChooser * chooser);		// not owned; null restores the Qt dialogs

		// Replaces the queue manager, queue and remote queue manager texts (start-up values, as app->initQMname).
		void setNames(const QString & queueManager, const QString & queue, const QString & remoteQueueManager);

		// The commands of the buttons that open a dialog, for a menu or an accelerator. Each does nothing when the
		// button is disabled.
		void openFileDialog();
		void saveFileDialog();
		void setConnectionDialog();

		// Update the TLS and heartbeat fields of `connection` from AppSettings when the Conn tab applies
		// changes. Does not touch the mode, channel, connName, ccdtUrl or password fields.
		void refreshConnectionTls();

	signals:
		void exitRequested();		// the Exit button (OnExitButton 1029); the window closes itself

	private slots:
		void refresh();

	private:
		class PurgePrompter;

		rfh::ConnectOptions currentConnect() const;
		void applyConnect(const rfh::ConnectOptions & connect);
		void wire();
		void addShortcuts();
		void applyLimitsForMode();
		void updateShellFlags();
		void fillQueueManagers();
		void fillQueues();
		void setBusyCursor(bool busy);
		template <typename Fn>
		void onClick(QWidget * button, Fn flow);

		AppDocument & doc;
		Operations & ops;
		StatusLog & log;
		AppSettings * settings;
		std::unique_ptr<Ui::MainTab> ui;
		std::unique_ptr<PurgePrompter> prompter;
		std::unique_ptr<FileChooser> defaultChooser;
		FileChooser * chooser;
		QButtonGroup * propsGroup;
		QButtonGroup * bindGroup;
		QButtonGroup * closeGroup;
		std::function<QStringList()> lister;
		rfh::ConnectOptions connection;			// Set Conn Id values; the Connection box overlays mode, channel, name and CCDT
		rfh::fileio::ReadOptions readOptions;
		rfh::fileio::WriteOptions writeOptions;
		bool systemQueues = false;
		bool cursorSet = false;
		bool refreshing = false;
	};
}
