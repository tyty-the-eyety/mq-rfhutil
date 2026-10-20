// AppDocument.h - the document state the Main, Data and MQMD tabs share, and the enable rules.
//
// What it replaces: the part of Windows DataArea that the three tabs read (fileData/fileSize/fileName/fileSource,
// browseActive/browsePrevActive, the connection and queue state) and the enable and disable logic spread over
// General::UpdatePageData (General.cpp:504-620), the CRfhutilView::OnUpdate* menu handlers (rfhutilView.cpp:1059-1210,
// 1634-1900, 2214-2450, 3216-3260), MSGDATA::updateMsgdata (MSGDATA.cpp:388-410) and MQMDPAGE::OnSetActive
// (MQMDPAGE.cpp:410-446). AppDocument::enablesFor() is all of that as one pure function of EnableInputs; the table
// in AppDocumentTests.cpp cites the Windows line for every row.
//
// Ownership. The document owns the single copy of the message bytes: setMessage() takes them by move and message()
// returns a const reference. Signals carry no payload; a tab reads through the const accessors after the signal.
// The document never talks to MQ (SessionController does; Operations mirrors the outcomes in with the setters
// and applyJobResult()). It has no password field and no setter takes one.
//
// Two MQMD representations, on purpose. message().mqmd is the descriptor that MQ or a file last supplied (what a
// put sends or a file write stores). mqmd() is the MQMD tab's text/radio state, as Windows keeps it on the tab
// (core: MqmdFields). Editing the tab does not touch message().mqmd; toMqmd/fromMqmd convert at the boundaries.
//
// Differences from the interface sketched in the plan (reported with the task): clearData() keeps the headers
// and the file name, as Windows does; the enable rules take more inputs than (browse, connected) because the
// Windows handlers also look at the tab, the queue, the display view, the data size and open Find dialogs.
//
// Threading: GUI thread only.
#pragma once

#include <QObject>
#include <QString>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "rfh_format.h"
#include "rfh_message.h"
#include "rfh_mq_session.h"
#include "rfh_mqmd_fields.h"
#include "SessionController.h"

namespace rfhqt
{
	// The tabs in the order of the Windows program (rfhutilView.cpp:599-659); the values equal MainWindow::Tab.
	enum class Tab : int
	{
		Main, Data, Mqmd, Ps, UsrProp, Rfh, PubSub, Pscr, Jms, Usr, Other, Cics, Ims, Dlq, Conn,
		Count
	};

	// Windows browseActive and browsePrevActive. The core knows the 1-based position of the last browsed
	// message (MqSession::browsePosition(); 0 = none or unknown), which is what decides Browse Prev:
	// Windows turns browsePrevActive off exactly when the first message is shown (DataArea.cpp:9172-9184).
	struct BrowseState
	{
		bool active = false;
		long position = 0;

		bool previousAllowed() const { return active && position > 1; }
		bool operator==(const BrowseState & o) const { return active == o.active && position == o.position; }
		bool operator!=(const BrowseState & o) const { return !(*this == o); }
	};

	// What the shell around the document decides (not document data): the visible tab, the Data tab's display,
	// open Find dialogs, a running job, and the Main tab check boxes that the MQMD tab's enables depend on.
	struct ShellState
	{
		Tab tab = Tab::Main;
		rfh::format::View view = rfh::format::View::Character;	// Data tab display (CRfhutilView::m_data.m_data_format)
		bool findDialogOpen = false;		// Find or Find Hex open (findDialog / hexFindDialog != NULL)
		bool busy = false;					// SessionController::busy()
		bool cancellable = false;			// the running job polls the cancel flag (a waiting get, a purge, a browse previous walk)
		bool setIdentity = false;			// Set Iden Context box (pDoc->m_setUserID)
		bool setAll = false;				// Set All Context box (pDoc->m_set_all)
		bool alternateUser = false;			// Alternate User Id box (pDoc->m_alt_userid)
		bool propertiesSupported = true;	// the queue manager library supports properties and selectors (MQ V7 or later)
		bool clipboardText = false;			// the clipboard holds text (IsClipboardFormatAvailable(CF_TEXT))

		bool operator==(const ShellState & o) const
		{
			return tab == o.tab && view == o.view && findDialogOpen == o.findDialogOpen && busy == o.busy
				&& cancellable == o.cancellable && setIdentity == o.setIdentity && setAll == o.setAll
				&& alternateUser == o.alternateUser && propertiesSupported == o.propertiesSupported
				&& clipboardText == o.clipboardText;
		}
		bool operator!=(const ShellState & o) const { return !(*this == o); }
	};

	// Everything enablesFor() looks at.
	struct EnableInputs : ShellState
	{
		bool connected = false;		// DataArea::isConnectionActive()
		bool queueOpen = false;		// DataArea::isQueueOpen()
		BrowseState browse;
		bool dataPresent = false;	// DataArea::fileSize > 0 (the message body; headers do not count)
	};

	// true = enabled. Names follow the Windows control or menu item.
	struct Enables
	{
		// Main tab buttons
		bool openFile = false, saveFile = false, clearData = false, clearAll = false;
		bool readQ = false, purgeQ = false, writeQ = false, browseQ = false, startBrowse = false;
		bool browseNext = false, browsePrev = false, endBrowse = false, closeQ = false, loadNames = false;
		bool displayQ = false, loadQ = false, saveQ = false, moveQ = false;	// not ported in Phase 1: always off
		bool setConnId = false, exit = false;
		bool cancel = false;				// new in the Qt program: stops the running job
		// Main tab Put/Get option controls that Windows switches with the browse state
		bool getOptions = false;			// Logical Order, Complete Msg, All Avail, Convert
		bool propsRadios = false;			// the five User Props radios
		bool selectorEditable = false;		// the Selector edit (read-only without MQ V7)
		// Data tab
		bool dataBrowseNext = false, dataBrowsePrev = false;
		bool dataAllowEdit = false, dataWriteQ = false;	// out of scope in Phase 1: always off
		bool dataBom = false;				// Windows disables it at start and never enables it
		// Menus
		bool menuOpen = false, menuSave = false, menuPrint = false;
		bool menuCut = false, menuPaste = false, menuCopy = false, menuSelectAll = false, menuTop = false, menuBottom = false;
		bool menuFind = false, menuFindHex = false, menuGoto = false;
		bool menuMqConn = false, menuMqDisc = false, menuMqOpenGet = false, menuMqOpenPut = false;
		bool menuMqOpenBrowse = false, menuMqClose = false, menuMqEndBrowse = false;
		// MQMD tab edit fields
		bool mqmdMsgIdEditable = false;
		bool mqmdUserIdEditable = false;	// User Id and Accounting Token
		bool mqmdPutApplEditable = false;	// Appl Identity, Appl Origin, Put Appl Name, Put Appl Type, Put Date/Time
		// Tabs: only Main, Data and MQMD in Phase 1 (the others show "Not yet ported")
		std::array<bool, (size_t)Tab::Count> tabs{};
		bool tabChangeBlocked = false;		// a Find dialog is open: the tab control ignores a change

		bool tabEnabled(Tab t) const { return tabs[(size_t)t]; }

		// Every flag with its name, in declaration order (the tab flags as "tab:Main" and so on).
		std::vector<std::pair<std::string, bool>> fields() const;
	};

	// Actions that discard or replace data, for the confirm-before-discard rule.
	enum class DiscardAction { OpenFile, ClearData, ClearAll, ReadQ, BrowseQ, StartBrowse, PurgeQ, CloseQ, Exit };

	class AppDocument : public QObject
	{
		Q_OBJECT

	public:
		// The text shown when a document is empty (Windows resetDisplayData: fileSource = "Message Data").
		static const char * const EmptySource;

		explicit AppDocument(QObject * parent = nullptr);

		// ---- the pure rules -------------------------------------------------------------------------------
		static Enables enablesFor(const EnableInputs & in);

		// Windows asks before discarding only for Purge Q (General.cpp:900, MB_YESNO). Open File, Clear Data,
		// Clear All, Exit and every read replace data silently: there is no modified-data prompt.
		static bool confirmsBeforeDiscard(DiscardAction action);
		// "Purge all messages from queue <q>?\n<depth> message(s)[ · Selector: <sel>]"
		static QString purgeConfirmation(const QString & queueName, MQLONG depth, const std::string & selector);

		// ---- state, read-only access ------------------------------------------------------------------------
		const rfh::Message & message() const { return msg; }
		const rfh::MqmdFields & mqmd() const { return fields; }
		const std::string & dataSource() const { return source; }				// "Message Data", "Message Data (N) from <path>", ...
		const rfh::format::RenderOptions & render() const { return renderOptions; }	// formats, user's display choices, copy book path
		size_t dataSize() const { return msg.body.size(); }						// Windows fileSize: the body only
		bool hasData() const { return !msg.body.empty(); }
		const QString & filePath() const { return path; }						// full path of the loaded or saved file; empty = none
		QString fileName() const;												// the last path component
		bool hasFile() const { return !path.isEmpty(); }
		bool dirty() const { return isDirty; }

		bool connected() const { return isConnected; }
		bool queueOpen() const { return isQueueOpen; }
		const QString & queueManager() const { return qm; }
		const QString & queueName() const { return queue; }
		const QString & remoteQueueManager() const { return remoteQm; }
		const rfh::QueueInfo & queueInfo() const { return queueFacts; }
		const rfh::QueueManagerInfo & queueManagerInfo() const { return managerFacts; }
		const BrowseState & browse() const { return browseState; }
		const ShellState & shell() const { return shellState; }

		EnableInputs inputs() const;
		Enables enables() const { return enablesFor(inputs()); }

		// ---- changes. Each emits its own signal and then stateChanged(), once per call, and nothing when
		// the value did not change (the bulk replacements setMessage, setMqmd and setRender always emit). -------

		// Takes the message by move: the document owns the only copy. source is the Data tab caption text;
		// dirty says whether the data is unsaved (true for a message from a queue or an edit, false when it
		// was just read from a file).
		void setMessage(rfh::Message && message, std::string dataSource = EmptySource, bool dirty = true);
		// A message and the things that come with it (the MQMD tab's fields and the display formats), as one change:
		// one set of signals, so a tab that redraws on them draws once and never shows the new formats with the old
		// message or the reverse. Operations uses it for a get and for a file read.
		void acceptMessage(rfh::Message && message, std::string dataSource, bool dirty, const rfh::MqmdFields & mqmdFields,
		                   const rfh::format::RenderOptions & options);
		void setMqmd(const rfh::MqmdFields & mqmdFields);
		void setRender(const rfh::format::RenderOptions & options);
		void setFilePath(const QString & filePath);
		void markSaved(const QString & filePath);		// a file was written or read: sets the path, clears dirty

		void setConnected(bool connected);				// false also closes the queue and ends a browse
		void setQueueOpen(bool open);
		void setQueueManager(const QString & name);
		void setQueueName(const QString & name);
		void setRemoteQueueManager(const QString & name);
		void setQueueInfo(const rfh::QueueInfo & info);
		void setBrowse(const BrowseState & state);
		void setShell(const ShellState & state);

		// Mirrors what a finished job tells: the connection state, whether the session holds an open queue handle,
		// the queue manager facts after "connect", and the queue facts when the job asked for them. Nothing else
		// is taken (no message, no names).
		void applyJobResult(const QString & jobName, const JobResult & result);

		// Clear Data: Windows clearFileData -> resetDataArea (DataArea.cpp:2148). Empties the body, resets the
		// data code page and integer format, and the source text. Keeps the headers, the MQMD, the file name
		// and the copy book name, as Windows does.
		void clearData();

		// Clear All: DeleteContents (DataArea.cpp:1996) with ClearData (2175). Also empties the headers, the MQMD
		// (message and tab), the file name and the copy book, and sets the queue depth to 0 (m_q_depth = 0).
		void clearAll();

	signals:
		void messageChanged();		// body, headers, source or the formats that came with them
		void mqmdChanged();
		void fileChanged();			// file path
		void renderChanged();
		void connectionChanged();	// connected, queue open, queue manager facts
		void queueChanged();		// queue manager name, queue name, remote queue manager, queue facts
		void browseChanged();
		void dirtyChanged();
		void shellChanged();
		void stateChanged();		// once after any of the above

	private:
		enum Change : unsigned
		{
			CMessage = 1, CMqmd = 2, CFile = 4, CRender = 8, CConnection = 16, CQueue = 32, CBrowse = 64, CDirty = 128, CShell = 256
		};

		void setDirty(bool value, unsigned & changes);
		void dropConnection(unsigned & changes);
		void publish(unsigned changes);

		rfh::Message msg;
		rfh::MqmdFields fields;
		std::string source = EmptySource;
		rfh::format::RenderOptions renderOptions;
		QString path;
		bool isDirty = false;
		bool isConnected = false;
		bool isQueueOpen = false;
		QString qm, queue, remoteQm;
		rfh::QueueInfo queueFacts;
		rfh::QueueManagerInfo managerFacts;
		BrowseState browseState;
		ShellState shellState;
	};
}
