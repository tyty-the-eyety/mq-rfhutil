#include "AppDocument.h"

#include <QFileInfo>

namespace rfhqt
{
	const char * const AppDocument::EmptySource = "Message Data";

	namespace
	{
		const char * const kTabNames[] = {
			"Main", "Data", "Mqmd", "Ps", "UsrProp", "Rfh", "PubSub", "Pscr", "Jms", "Usr", "Other", "Cics", "Ims", "Dlq", "Conn"
		};
		static_assert(sizeof(kTabNames) / sizeof(kTabNames[0]) == (size_t)Tab::Count, "one name per tab");

		bool isCharacterHexOrBoth(rfh::format::View v)
		{
			using rfh::format::View;
			return v == View::Character || v == View::Hex || v == View::Both;
		}

		// OnUpdateSearchFind (rfhutilView.cpp:1737) also allows XML, PARSED, JSON and FIX, but not COBOL.
		bool isSearchableView(rfh::format::View v)
		{
			using rfh::format::View;
			return isCharacterHexOrBoth(v) || v == View::Xml || v == View::Parsed || v == View::Json || v == View::Fixed;
		}

		bool sameQueueFacts(const rfh::QueueInfo & a, const rfh::QueueInfo & b)
		{
			return a.type == b.type && a.depth == b.depth && a.maxMsgLength == b.maxMsgLength && a.resolvedName == b.resolvedName;
		}
	}

	std::vector<std::pair<std::string, bool>> Enables::fields() const
	{
		std::vector<std::pair<std::string, bool>> all;
#define RFH_FIELD(name) all.emplace_back(#name, name)
		RFH_FIELD(openFile); RFH_FIELD(saveFile); RFH_FIELD(clearData); RFH_FIELD(clearAll);
		RFH_FIELD(readQ); RFH_FIELD(purgeQ); RFH_FIELD(writeQ); RFH_FIELD(browseQ); RFH_FIELD(startBrowse);
		RFH_FIELD(browseNext); RFH_FIELD(browsePrev); RFH_FIELD(endBrowse); RFH_FIELD(closeQ); RFH_FIELD(loadNames);
		RFH_FIELD(displayQ); RFH_FIELD(loadQ); RFH_FIELD(saveQ); RFH_FIELD(moveQ);
		RFH_FIELD(setConnId); RFH_FIELD(exit); RFH_FIELD(cancel);
		RFH_FIELD(getOptions); RFH_FIELD(propsRadios); RFH_FIELD(selectorEditable);
		RFH_FIELD(dataBrowseNext); RFH_FIELD(dataBrowsePrev); RFH_FIELD(dataAllowEdit); RFH_FIELD(dataWriteQ); RFH_FIELD(dataBom);
		RFH_FIELD(menuOpen); RFH_FIELD(menuSave); RFH_FIELD(menuPrint);
		RFH_FIELD(menuCut); RFH_FIELD(menuPaste); RFH_FIELD(menuCopy); RFH_FIELD(menuSelectAll); RFH_FIELD(menuTop); RFH_FIELD(menuBottom);
		RFH_FIELD(menuFind); RFH_FIELD(menuFindHex); RFH_FIELD(menuGoto);
		RFH_FIELD(menuMqConn); RFH_FIELD(menuMqDisc); RFH_FIELD(menuMqOpenGet); RFH_FIELD(menuMqOpenPut);
		RFH_FIELD(menuMqOpenBrowse); RFH_FIELD(menuMqClose); RFH_FIELD(menuMqEndBrowse);
		RFH_FIELD(mqmdMsgIdEditable); RFH_FIELD(mqmdUserIdEditable); RFH_FIELD(mqmdPutApplEditable);
		RFH_FIELD(tabChangeBlocked);
#undef RFH_FIELD
		for (size_t i = 0; i < tabs.size(); i++)
		{
			all.emplace_back(std::string("tab:") + kTabNames[i], tabs[i]);
		}
		return all;
	}

	// The rules, with the Windows source of each. "idle" = no job runs (NEW); a job starter is also off while busy.
	Enables AppDocument::enablesFor(const EnableInputs & in)
	{
		Enables e;
		const bool browsing = in.browse.active;
		const bool idle = !in.busy;
		const bool prevAllowed = in.browse.previousAllowed();
		const bool onMain = in.tab == Tab::Main;
		const bool onData = in.tab == Tab::Data;

		// Main tab buttons: General::UpdatePageData (General.cpp:546-612). Browsing turns off everything that
		// opens, reads, writes, clears or replaces; Browse Next and End Browse come on, Prev only after message 1.
		e.openFile = !browsing && idle;
		e.saveFile = true;								// never changed
		e.clearData = !browsing && idle;
		e.clearAll = !browsing && idle;
		e.readQ = !browsing && idle;
		e.purgeQ = !browsing && idle;
		e.writeQ = !browsing && idle;
		e.browseQ = !browsing && idle;
		e.startBrowse = !browsing && idle;
		e.browseNext = browsing && idle;
		e.browsePrev = prevAllowed && idle;
		e.endBrowse = browsing && idle;
		e.closeQ = !browsing && in.connected && idle;	// the connection, not the open queue (General.cpp:615)
		e.loadNames = !browsing && idle;
		// Display Q, Load Q, Save Q and Move Q follow Load Names in Windows (General.cpp:560-562, 567, 600-602, 607);
		// they are not ported in Phase 1 (inventory 3.2), so they stay off.
		e.setConnId = true;								// never changed
		e.exit = true;									// never changed
		e.cancel = in.busy && in.cancellable;

		// Option controls that follow the browse state (General.cpp:563-572, 603-612). They do not start jobs.
		e.getOptions = !browsing;
		e.propsRadios = !browsing;
		e.selectorEditable = in.propertiesSupported;	// SetReadOnly(TRUE) without properties (General.cpp:1302)

		// Data tab (MSGDATA.cpp:388-410): same variables as the Main tab. Allow Edit and Write Q are out of
		// scope in Phase 1. The BOM box is disabled at start and no code enables it (MSGDATA.cpp:870).
		e.dataBrowseNext = browsing && idle;
		e.dataBrowsePrev = prevAllowed && idle;

		// Menus: rfhutilView.cpp
		e.menuOpen = !browsing && !in.findDialogOpen && idle;	// OnUpdateOpen 1670 (idle: NEW)
		e.menuSave = !in.findDialogOpen;						// OnUpdateFileWrite 1634
		e.menuPrint = onData && in.dataPresent;					// OnUpdateFilePrint 1059; the other pages are not ported
		e.menuCut = !onData;									// OnUpdateEditCut 1110
		e.menuPaste = !onData && in.clipboardText;				// OnUpdateEditPaste 1120
		e.menuCopy = true;										// 1151
		e.menuSelectAll = true;									// 1144
		e.menuTop = onData;										// 3216
		e.menuBottom = onData;									// 3249
		const bool searchOk = onData && !in.findDialogOpen && in.dataPresent;
		e.menuFind = searchOk && isSearchableView(in.view);			// OnUpdateSearchFind 1737
		e.menuFindHex = searchOk && isCharacterHexOrBoth(in.view);	// OnUpdateSearchFindHex 1786
		e.menuGoto = searchOk && isCharacterHexOrBoth(in.view);		// OnUpdateSearchGoto 1873
		e.menuMqConn = onMain && !in.connected && !browsing && idle;	// OnUpdateMqMqconn 2296
		e.menuMqDisc = in.connected && !browsing && idle;				// OnUpdateMqMqdisc 2315
		const bool canOpen = onMain && in.connected && !browsing && !in.queueOpen && idle;
		e.menuMqOpenGet = canOpen;										// OnUpdateMqMqopenGet 2335
		e.menuMqOpenPut = canOpen;										// ...Put 2362
		e.menuMqOpenBrowse = canOpen;									// ...Browse 2389
		e.menuMqClose = in.queueOpen && !browsing && idle;				// OnUpdateMqMqclose 2416
		e.menuMqEndBrowse = (onMain || onData) && browsing && idle;		// OnUpdateMqEndbr 2435

		// MQMD tab (MQMDPAGE.cpp:410-446): the Message Id is always editable; the user id and token open with
		// any of the three context options, the rest of the put application fields only with Set All.
		e.mqmdMsgIdEditable = true;
		e.mqmdUserIdEditable = in.setIdentity || in.setAll || in.alternateUser;
		e.mqmdPutApplEditable = in.setAll;

		// Tabs: Main, Data and MQMD work in Phase 1 (NEW). Windows ignores a tab change while a Find dialog is open
		// (rfhutilView.cpp:788).
		e.tabs[(size_t)Tab::Main] = true;
		e.tabs[(size_t)Tab::Data] = true;
		e.tabs[(size_t)Tab::Mqmd] = true;
		e.tabChangeBlocked = in.findDialogOpen;
		return e;
	}

	bool AppDocument::confirmsBeforeDiscard(DiscardAction action)
	{
		return action == DiscardAction::PurgeQ;
	}

	QString AppDocument::purgeConfirmation(const QString & queueName)
	{
		return QString("Purge all messages from queue %1?").arg(queueName);
	}

	AppDocument::AppDocument(QObject * parent)
		: QObject(parent)
	{
		rfh::mqmd_fields::clear(fields);
	}

	QString AppDocument::fileName() const
	{
		return path.isEmpty() ? QString() : QFileInfo(path).fileName();
	}

	EnableInputs AppDocument::inputs() const
	{
		EnableInputs in;
		static_cast<ShellState &>(in) = shellState;
		in.connected = isConnected;
		in.queueOpen = isQueueOpen;
		in.browse = browseState;
		in.dataPresent = hasData();
		return in;
	}

	void AppDocument::publish(unsigned changes)
	{
		if (changes == 0)
		{
			return;
		}
		if (changes & CMessage) emit messageChanged();
		if (changes & CMqmd) emit mqmdChanged();
		if (changes & CFile) emit fileChanged();
		if (changes & CRender) emit renderChanged();
		if (changes & CConnection) emit connectionChanged();
		if (changes & CQueue) emit queueChanged();
		if (changes & CBrowse) emit browseChanged();
		if (changes & CDirty) emit dirtyChanged();
		if (changes & CShell) emit shellChanged();
		emit stateChanged();
	}

	void AppDocument::setDirty(bool value, unsigned & changes)
	{
		if (isDirty != value)
		{
			isDirty = value;
			changes |= CDirty;
		}
	}

	void AppDocument::dropConnection(unsigned & changes)
	{
		// MQDISC closes the queue and ends a browse; the queue manager facts are unknown again (MqSession::queueManagerInfo)
		isQueueOpen = false;
		managerFacts = rfh::QueueManagerInfo();
		if (browseState != BrowseState())
		{
			browseState = BrowseState();
			changes |= CBrowse;
		}
	}

	void AppDocument::setMessage(rfh::Message && message, std::string dataSource, bool dirty)
	{
		unsigned changes = CMessage;
		msg = std::move(message);
		source = std::move(dataSource);
		setDirty(dirty, changes);
		publish(changes);
	}

	void AppDocument::acceptMessage(rfh::Message && message, std::string dataSource, bool dirty, const rfh::MqmdFields & mqmdFields,
	                                const rfh::format::RenderOptions & options)
	{
		unsigned changes = CMessage | CMqmd | CRender;
		msg = std::move(message);
		source = std::move(dataSource);
		fields = mqmdFields;
		renderOptions = options;
		setDirty(dirty, changes);
		publish(changes);
	}

	void AppDocument::setMqmd(const rfh::MqmdFields & mqmdFields)
	{
		fields = mqmdFields;
		publish(CMqmd);
	}

	void AppDocument::setRender(const rfh::format::RenderOptions & options)
	{
		renderOptions = options;
		publish(CRender);
	}

	void AppDocument::setFilePath(const QString & filePath)
	{
		if (path != filePath)
		{
			path = filePath;
			publish(CFile);
		}
	}

	void AppDocument::markSaved(const QString & filePath)
	{
		unsigned changes = 0;
		if (path != filePath)
		{
			path = filePath;
			changes |= CFile;
		}
		setDirty(false, changes);
		publish(changes);
	}

	void AppDocument::setConnected(bool connected)
	{
		if (isConnected == connected)
		{
			return;
		}
		unsigned changes = CConnection;
		isConnected = connected;
		if (!connected)
		{
			dropConnection(changes);
		}
		publish(changes);
	}

	void AppDocument::setQueueOpen(bool open)
	{
		if (isQueueOpen != open)
		{
			isQueueOpen = open;
			publish(CConnection);
		}
	}

	void AppDocument::setQueueManager(const QString & name)
	{
		if (qm != name)
		{
			qm = name;
			publish(CQueue);
		}
	}

	void AppDocument::setQueueName(const QString & name)
	{
		if (queue != name)
		{
			queue = name;
			publish(CQueue);
		}
	}

	void AppDocument::setRemoteQueueManager(const QString & name)
	{
		if (remoteQm != name)
		{
			remoteQm = name;
			publish(CQueue);
		}
	}

	void AppDocument::setQueueInfo(const rfh::QueueInfo & info)
	{
		if (!sameQueueFacts(queueFacts, info))
		{
			queueFacts = info;
			publish(CQueue);
		}
	}

	void AppDocument::setBrowse(const BrowseState & state)
	{
		if (browseState != state)
		{
			browseState = state;
			publish(CBrowse);
		}
	}

	void AppDocument::setShell(const ShellState & state)
	{
		if (shellState != state)
		{
			shellState = state;
			publish(CShell);
		}
	}

	void AppDocument::applyJobResult(const QString & jobName, const JobResult & result)
	{
		unsigned changes = 0;
		if (isConnected != result.connected)
		{
			isConnected = result.connected;
			changes |= CConnection;
			if (!result.connected)
			{
				dropConnection(changes);
			}
		}
		if (isQueueOpen != result.queueOpen)
		{
			isQueueOpen = result.queueOpen;			// MqSession::isQueueOpen(): the session owns the handle
			changes |= CConnection;
		}
		if (result.connected && jobName == "connect")
		{
			managerFacts = result.queueManager;
			changes |= CConnection;
		}
		if (result.hasQueueInfo && !sameQueueFacts(queueFacts, result.queue))
		{
			queueFacts = result.queue;
			changes |= CQueue;
		}
		publish(changes);
	}

	void AppDocument::clearData()
	{
		// resetDataArea: fileSize, fileCcsid, fileIntFormat, fileData and the displays (fileSource = "Message Data")
		unsigned changes = 0;
		if (!msg.body.empty() || source != EmptySource || renderOptions.ccsid != 0 || renderOptions.intFormat != NUMERIC_PC)
		{
			msg.body.clear();
			msg.body.shrink_to_fit();
			source = EmptySource;
			renderOptions.ccsid = 0;
			renderOptions.intFormat = NUMERIC_PC;
			changes |= CMessage | CRender;
		}
		setDirty(false, changes);
		publish(changes);
	}

	void AppDocument::clearAll()
	{
		// DeleteContents: copy book, ClearData (data, depth, file name), MQMD, header tabs
		unsigned changes = CMessage | CMqmd | CRender;
		msg = rfh::Message();
		source = EmptySource;
		rfh::mqmd_fields::clear(fields);
		renderOptions.ccsid = 0;
		renderOptions.intFormat = NUMERIC_PC;
		renderOptions.copybookPath.clear();
		if (!path.isEmpty())
		{
			path.clear();
			changes |= CFile;
		}
		if (queueFacts.depth != 0)
		{
			queueFacts.depth = 0;			// m_q_depth = 0 (DataArea::ClearData)
			changes |= CQueue;
		}
		setDirty(false, changes);
		publish(changes);
	}
}
