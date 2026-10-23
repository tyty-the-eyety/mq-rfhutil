// Operations.h - what the Main tab buttons and the MQ menu do (the flows of General.cpp and DataArea.cpp).
//
// Operations ties the document (AppDocument), the worker (SessionController), the status box (StatusLog), the
// settings (AppSettings) and the core together. It has no widgets and shows no dialog: a confirmation goes through
// the Prompter, so a test can answer it and Task 27 can show the real box.
//
// Where the inputs come from. The Main tab keeps AppDocument's queueManager(), queueName() and remoteQueueManager()
// current (Windows UpdateData(TRUE), and the update handlers that call getQnames()), and its check boxes in
// AppDocument::shell() (Set Iden Context, Set All Context, Alternate User Id). The MQMD tab keeps mqmd() current.
// The Main tab's other choices come in with setOptions(). Operations reads all of that when a flow starts and
// captures it for the job, so a change made while a job runs does not touch it.
//
// One flow = one call here = one job on the worker (the connection check, the MQ calls and the queue information
// are one job, as they are one Windows call chain) = one status entry and exactly one flowFinished() signal, also
// when the flow is refused. Results come back on the GUI thread; only there are the document and the log changed.
//
// Refusals. While a job runs a flow is refused with the status "Another operation is still running" (NEW: Windows
// has no worker). A flow that AppDocument::enables() disables (Read Q while browsing, Close Q without a connection)
// does nothing and says nothing, as the Windows command UI handlers disable the control.
//
// Passwords. The stored MainOptions::connect is handed to a connect and wiped as soon as it is consumed, whatever
// the outcome, so a reconnect needs the Set Conn Id values again (the caller calls setOptions() again). The password
// is never written to the status log, the trace or the settings.
//
// Threading: GUI thread only.
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <string>

#include "AppDocument.h"
#include "SessionController.h"
#include "StatusLog.h"
#include "rfh_message_file.h"
#include "rfh_mq_session.h"
#include "rfh_queue_file.h"

namespace rfhqt
{
	class AppSettings;

	// The Main tab's check boxes and radios that are not in AppDocument (inventory 3.3).
	struct MainOptions
	{
		rfh::ConnectOptions connect;					// the Set Conn Id values; qmName is taken from the document at connect time
		rfh::PutOptions put;							// newMsgId, newCorrelId, bind (all four), logicalOrder. remoteQm, the contexts, syncpoint and alternateUser are derived
		rfh::GetOptions get;							// matchMsgId/matchCorrelId/matchGroupId (applied only for a non-zero id on the MQMD tab), convert, waitMs, selector, properties, logicalOrder, completeMsg, allAvailable
		rfh::CloseOption closeOption = rfh::CloseOption::None;
		int fileCodePage = 0;							// Main tab File Code Page; 0 = decide (the MQMD tab, then the system). Replaces read.fileCcsid
		rfh::fileio::ReadOptions read;					// Read menu; fileCcsid is ignored (see fileCodePage)
		rfh::fileio::WriteOptions write;				// Write menu

		MainOptions() { write.includeMqmd = true; }		// Write > Include MQMD is on by default (DataArea.cpp:373)
	};

	// Asks the user. The only question Windows asks is the purge confirmation (General.cpp:900, MB_YESNO).
	class Prompter
	{
	public:
		virtual ~Prompter() = default;
		virtual bool confirm(const QString & text) = 0;
	};

	class Operations : public QObject
	{
		Q_OBJECT

	public:
		Operations(AppDocument & document, SessionController & controller, StatusLog & statusLog, QObject * parent = nullptr);
		~Operations() override;

		// The options are copied. The password in options.connect is wiped from the stored copy when a connect
		// has used it (see the note on passwords).
		void setOptions(const MainOptions & options);
		void setPrompter(Prompter * prompter);			// not owned; without one a purge is not started
		void setSettings(AppSettings * settings);		// not owned; optional

		// ---- connection and queue (MQ menu, Close Q, queue information) ------------------------------------------
		void connectQm();				// MQ > MQConn: explicitConnect, "Connected to <qm>" (DataArea.cpp:11776)
		void disconnectQm();			// MQ > MQDisc: explicitCloseQ when a queue is open, then explicitDiscQM (VIEW 2477)
		void closeQueueMenu();			// MQ > MQClose: explicitCloseQ, "Queue closed" (DataArea.cpp:12469)
		void closeQ();					// Close Q button: closeQ(close option), then discQM (General.cpp:1617)
		void refreshQueueInfo();		// getQueueDepth: connect on demand, queue type, depth and maximum length
		void loadNames();				// Load Names button: OnLoadQueueNames (General.cpp:1642) -> getQueueNames (DataArea.cpp:25198)

		// The queue names the last Load Names found for a queue manager (the name as typed), sorted, each once; empty
		// before the first load. Windows keeps the same per-queue-manager cache for the Queue Name drop down.
		QStringList queueNames(const QString & queueManager) const;

		// ---- queue messages (Read Q, Write Q, Browse Q and the browse session) --------------------------------------
		void readQ();					// getMessage(READQ): destructive get, DataArea.cpp:7947
		void writeQ();					// putMessage: the MQMD of the tab, the data of the document, DataArea.cpp:6166
		void browseQ();					// getMessage(BROWSEQ): one browse, then closeQ
		void startBrowse();				// startBrowse -> readFirstMessage (DataArea.cpp:8346, 8180)
		void browseNext();				// browseNext (DataArea.cpp:8459)
		void browsePrev();				// browsePrev (DataArea.cpp:8895)
		void endBrowse();				// endBrowse: depth, close, disconnect, "Browse operation ended" (DataArea.cpp:8425)

		// ---- files, clearing and purging (Main tab) -------------------------------------------------------------------
		void openFile(const QString & path);	// ReadFileData through fileio::read, on the worker (DataArea.cpp:994)
		void saveFile(const QString & path);	// WriteFile through fileio::write, with the MQMD of the tab (DataArea.cpp:1778)
		void clearData();						// clearFileData: the body only (DataArea.cpp:10597)
		void clearAll();						// DeleteContents: headers, MQMD, file name, depth (DataArea.cpp:1996)
		void purgeQ();							// OnPurgeQ: depth, confirmation, purgeQueue (General.cpp:866)

		// ---- save/load queue messages to/from files (Main tab) --------------------------------------------------------
		void saveQ(QWidget * parent);	// SaveQ button: shows SaveQDialog, calls rfh::saveQueue (General.cpp:2519)
		void loadQ(QWidget * parent);	// LoadQ button: shows LoadQDialog, calls rfh::loadQueue (General.cpp:2507)

		// Stops the running job that polls the cancel flag (a waiting get, a purge, a Browse Prev walk).
		void cancelGet();

	signals:
		void statusChanged();							// the log or the document changed because a flow ended
		void flowFinished(const QString & flow);		// exactly once per flow call, also when it was refused
		void queueNamesChanged(const QString & queueManager);	// Load Names stored a list (also an empty one)
		void titleChanged(const QString & queue);		// the window title is the name of the last queue used (setWindowTitle)

	private:
		// What a job needs, captured on the GUI thread when the flow starts (the lambda that runs on the worker
		// must not touch the document or the options).
		struct Env
		{
			std::string qm;							// the queue manager name as typed; checkConnection compares it with connectedQm
			std::string connectedQm;				// the name the connection was made for
			std::string queue;
			std::string remoteQm;
			bool pendingUnit = false;				// a put under syncpoint is waiting for its commit
			bool checks = true;						// the flow runs checkConnection (so the connection is for qm afterwards)
			rfh::CloseOption closeOption = rfh::CloseOption::None;
			std::shared_ptr<rfh::ConnectOptions> connect;	// a copy for a connect the flow may need; the job wipes it
		};

		struct Running
		{
			QString name;
			QString qm;							// the queue manager name the flow checked the connection for
			bool checked = false;				// the flow ran checkConnection
			bool cancellable = false;
			std::function<void(const JobResult &)> done;
		};

		enum class GetKind { Read, BrowseQ, Start, Next, Prev };

		bool admit(const QString & flow, bool enabled);
		void finishFlow(const QString & flow);
		void addStatus(const QStringList & lines);
		Env capture(bool mayConnect) const;
		rfh::GetOptions buildGet(rfh::GetMode mode, bool useMatch, MQLONG hostEncoding, bool wait) const;
		void queueGet(GetKind kind);
		void startPurge(const QString & queue);
		void acceptMessage(const rfh::Message & message, const std::string & source, QStringList & warnings);
		void clearAfterGet(bool clearFileName);
		QString depthText() const;
		QStringList describeBrowseFailure(const JobResult & result, const char * secondWord) const;
		bool submitFlow(const QString & flow, const Env & env, bool cancellable, SessionController::Job job, std::function<void(const JobResult &)> done);
		void onFinished(const QString & name, const JobResult & result);
		void absorb(const QString & name, const Running & flow, const JobResult & result);
		QStringList describeStep(const JobStep & step, bool closeFailed) const;
		QStringList describe(const JobResult & result) const;
		static QStringList noteLines(const JobResult & result);
		void applyEffects(const JobResult & result);
		void logResolvedName(const JobResult & result);
		void rememberQueue(const QString & queue);
		void trace(const QString & text) const;
		void mirrorBusy(bool busy);

		AppDocument & doc;
		SessionController & controller;
		StatusLog & log;
		AppSettings * settings = nullptr;
		Prompter * prompter = nullptr;
		MainOptions options;

		QString connectedQm;			// the queue manager name the connection was made for (Windows currentQM)
		bool pendingUnit = false;		// a put under syncpoint has not been committed or backed out yet
		QString resolvedLogged;			// the last "Queue name resolved to" that was logged, to say it once
		bool prompting = false;			// a confirmation is open (the purge's): flows are refused until it is answered
		bool userCancelled = false;		// cancelGet() was called for the running job
		Running running;
		QHash<QString, QStringList> loadedNames;	// queueNames()
	};
}
