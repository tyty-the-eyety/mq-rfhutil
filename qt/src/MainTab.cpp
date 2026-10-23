#include "MainTab.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCompleter>
#include <QComboBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>

#include "AppDocument.h"
#include "AppSettings.h"
#include "ConnUserDialog.h"
#include "PopupComboBox.h"
#include "StatusLog.h"
#include "rfh_queue_managers.h"
#include "ui_MainTab.h"

namespace rfhqt
{
	namespace
	{
		const char * const kNotYetPorted = "Not yet ported";

		// Windows OnFileOpen's filter (General.cpp:421): All (*.*)|XML (*.XML)|Text (*.TXT)
		const char * const kOpenFilter = "All (*.*);;XML (*.XML);;Text (*.TXT)";

		class QtFileChooser : public FileChooser
		{
		public:
			QString openPath(QWidget * parent, const QString & directory) override
			{
				return QFileDialog::getOpenFileName(parent, QStringLiteral("Open"), directory, QString::fromLatin1(kOpenFilter));
			}
			QString savePath(QWidget * parent, const QString & directory) override
			{
				return QFileDialog::getSaveFileName(parent, QStringLiteral("Save"), directory);
			}
		};

		// setQueueType (DataArea.cpp:10354): the text of the queue types; any other type shows nothing
		QString queueTypeText(MQLONG type)
		{
			switch (type)
			{
			case MQQT_LOCAL: return QStringLiteral("Local");
			case MQQT_MODEL: return QStringLiteral("Model");
			case MQQT_ALIAS: return QStringLiteral("Alias");
			case MQQT_REMOTE: return QStringLiteral("Remote");
			case MQQT_CLUSTER: return QStringLiteral("Cluster");
			default: return QString();
			}
		}

		void setTextIfChanged(QLineEdit * edit, const QString & text)
		{
			if (edit->text() != text)
			{
				edit->setText(text);
			}
		}

		QStringList sortedUnique(QStringList names)
		{
			names.removeAll(QString());
			names.removeDuplicates();
			names.sort();
			return names;
		}

		QStringList localQueueManagers()
		{
			rfh::mq::LocalQueueManagers found;
			QStringList names;
			if (rfh::mq::listLocalQueueManagers(std::string(), found).ok())
			{
				for (const rfh::mq::LocalQueueManager & m : found.managers)
				{
					names << QString::fromStdString(m.name);
				}
			}
			return names;
		}
	}

	// OnPurgeQ's question (General.cpp:900): MB_YESNO | MB_ICONQUESTION. Windows lets Yes be the default button; here No is,
	// so that a stray Return does not empty a queue.
	class MainTab::PurgePrompter : public Prompter
	{
	public:
		explicit PurgePrompter(QWidget * owner) : owner(owner) {}
		bool confirm(const QString & text) override
		{
			QMessageBox box(QMessageBox::Question, QStringLiteral("RfhUtil"), text, QMessageBox::Yes | QMessageBox::No, owner);
			box.setObjectName(QStringLiteral("purgeConfirmation"));
			box.setDefaultButton(QMessageBox::No);
			return box.exec() == QMessageBox::Yes;
		}

	private:
		QWidget * owner;
	};

	MainTab::MainTab(AppDocument & document, Operations & operations, StatusLog & statusLog, AppSettings * appSettings, QWidget * parent)
		: QWidget(parent), doc(document), ops(operations), log(statusLog), settings(appSettings), ui(new Ui::MainTab),
		  prompter(new PurgePrompter(this)), defaultChooser(new QtFileChooser), chooser(defaultChooser.get()),
		  propsGroup(new QButtonGroup(this)), bindGroup(new QButtonGroup(this)), closeGroup(new QButtonGroup(this)),
		  lister(localQueueManagers), readOptions(MainOptions().read), writeOptions(MainOptions().write)
	{
		ui->setupUi(this);
		setObjectName(QStringLiteral("mainTab"));

		// the radio groups carry the Windows values (DDX_Radio: the zero based position in the template)
		const QList<QAbstractButton *> props = { ui->propsAsQueueRadio, ui->propsNoneRadio, ui->propsYesRadio, ui->propsRfh2Radio, ui->propsCompatRadio };
		for (int i = 0; i < props.size(); i++) propsGroup->addButton(props[i], i);
		const QList<QAbstractButton *> binds = { ui->bindAsQueueRadio, ui->bindOnOpenRadio, ui->bindNotFixedRadio, ui->bindGroupRadio };
		for (int i = 0; i < binds.size(); i++) bindGroup->addButton(binds[i], i);
		const QList<QAbstractButton *> closes = { ui->closeNoneRadio, ui->closeDeleteRadio, ui->closePurgeRadio };
		for (int i = 0; i < closes.size(); i++) closeGroup->addButton(closes[i], i);

		// what Phase 1 does not do: the controls stay disabled and say why
		for (QWidget * w : { (QWidget *)ui->saveQButton, (QWidget *)ui->loadQButton, (QWidget *)ui->moveQButton,
		                     (QWidget *)ui->displayQButton, (QWidget *)ui->propsYesRadio })
		{
			w->setToolTip(QString::fromLatin1(kNotYetPorted));
			w->setEnabled(false);
		}

		// edit rules: digits only for the code page; the read-only fields are reached with the mouse, as on Windows
		ui->fileCodePageEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]*")), ui->fileCodePageEdit));
		ui->queueCombo->lineEdit()->setMaxLength(MaxQueueName);
		for (QWidget * w : { (QWidget *)ui->fileNameEdit, (QWidget *)ui->fileSizeEdit, (QWidget *)ui->copyBookEdit,
		                     (QWidget *)ui->queueDepthEdit, (QWidget *)ui->queueTypeEdit })
		{
			w->setFocusPolicy(Qt::ClickFocus);
		}
		ui->statusEdit->setTabChangesFocus(true);		// the status box is the one read-only field a keyboard user can reach
		ui->statusEdit->setMinimumHeight(80);
		ui->waitSpin->setSuffix(QStringLiteral(" s"));
		for (QComboBox * combo : { (QComboBox *)ui->qmCombo, (QComboBox *)ui->queueCombo })
		{
			combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
			combo->setMinimumContentsLength(24);
			combo->completer()->setCaseSensitivity(Qt::CaseSensitive);		// MQ names are case sensitive
		}

		ops.setPrompter(prompter.get());
		if (settings != nullptr)
		{
			ops.setSettings(settings);
			systemQueues = settings->showSystemQueues();
			applyConnect(settings->connection());
			const QStringList managers = settings->recentQueueManagers();
			const QString qm = managers.value(0);
			const QString queue = settings->recentQueues(qm).value(0);
			ui->qmCombo->setEditText(qm);
			ui->queueCombo->setEditText(queue);
		}
		applyLimitsForMode();
		fillQueueManagers();
		fillQueues();

		wire();
		addShortcuts();
		syncDocument();
		refresh();
	}

	MainTab::~MainTab()
	{
		setBusyCursor(false);
		connection.wipePassword();
		ops.setPrompter(nullptr);
	}

	// ---- reading the widgets ------------------------------------------------------------------------------------------

	rfh::ConnectOptions MainTab::currentConnect() const
	{
		rfh::ConnectOptions c = connection;
		const bool client = ui->clientRadio->isChecked();
		c.mode = client ? rfh::Mode::Client : rfh::Mode::Bindings;
		c.channel = ui->channelEdit->text().toStdString();
		c.connName = ui->connNameEdit->text().toStdString();
		c.ccdtUrl = ui->ccdtEdit->text().toStdString();
		if (!client)
		{
			// a bindings connection ignores the client values and warns about the ones it finds set; they stay in the
			// dialog for the next client connection
			c.channel.clear();
			c.connName.clear();
			c.ccdtUrl.clear();
			c.sslEnabled = false;
			c.cipherSpec.clear();
			c.keyRepository.clear();
			c.sslPeerName.clear();
			c.sslClientValidation = false;
			c.fipsRequired = false;
			c.sslResetCount = 0;
			c.securityExit.clear();
			c.securityData.clear();
			c.localAddress.clear();
		}
		return c;
	}

	void MainTab::refreshConnectionTls()
	{
		if (settings == nullptr)
			return;
		const rfh::ConnectOptions saved = settings->connection();
		// Update only the ConnTab-owned fields; leave mode, channel, connName, ccdtUrl,
		// userId, password and the Set-Conn-Id-owned fields (securityExit, localAddress, etc.) alone.
		connection.heartbeat     = saved.heartbeat;
		connection.sslEnabled    = saved.sslEnabled;
		connection.cipherSpec    = saved.cipherSpec;
		connection.keyRepository = saved.keyRepository;
		connection.sslPeerName   = saved.sslPeerName;
		connection.sslResetCount = saved.sslResetCount;
	}

	void MainTab::applyConnect(const rfh::ConnectOptions & saved)
	{
		connection.wipePassword();
		connection = saved;
		connection.wipePassword();		// a settings object has none; be sure
		const QSignalBlocker blockBindings(ui->bindingsRadio);
		const QSignalBlocker blockClient(ui->clientRadio);
		ui->bindingsRadio->setChecked(saved.mode != rfh::Mode::Client);
		ui->clientRadio->setChecked(saved.mode == rfh::Mode::Client);
		ui->channelEdit->setText(QString::fromStdString(saved.channel));
		ui->connNameEdit->setText(QString::fromStdString(saved.connName));
		ui->ccdtEdit->setText(QString::fromStdString(saved.ccdtUrl));
	}

	MainOptions MainTab::options() const
	{
		MainOptions o;
		o.connect = currentConnect();

		o.put.newMsgId = ui->newMsgIdCheck->isChecked();			// MQPMO_NEW_MSG_ID
		o.put.newCorrelId = ui->newCorrelIdCheck->isChecked();		// MQPMO_NEW_CORREL_ID
		o.put.logicalOrder = ui->logicalOrderCheck->isChecked();
		switch (bindGroup->checkedId())
		{
		case 1: o.put.bind = rfh::BindOption::OnOpen; break;
		case 2: o.put.bind = rfh::BindOption::NotFixed; break;
		case 3: o.put.bind = rfh::BindOption::OnGroup; break;		// no handler on Windows (IDC_BIND_GROUP); wired here
		default: o.put.bind = rfh::BindOption::AsQueueDefinition; break;
		}

		o.get.matchMsgId = ui->getByMsgIdCheck->isChecked();
		o.get.matchCorrelId = ui->getByCorrelIdCheck->isChecked();
		o.get.matchGroupId = ui->getByGroupIdCheck->isChecked();
		o.get.convert = ui->convertCheck->isChecked();
		o.get.logicalOrder = ui->logicalOrderCheck->isChecked();
		o.get.completeMsg = ui->completeMsgCheck->isChecked();
		o.get.allAvailable = ui->allAvailCheck->isChecked();
		o.get.selector = ui->selectorEdit->text().toStdString();
		o.get.waitMs = ui->waitSpin->value() * 1000;
		switch (propsGroup->checkedId())
		{
		case 1: o.get.properties = rfh::PropertiesMode::None; break;
		case 3: o.get.properties = rfh::PropertiesMode::Rfh2; break;
		case 4: o.get.properties = rfh::PropertiesMode::Compat; break;
		default: o.get.properties = rfh::PropertiesMode::AsQueue; break;	// "Yes" (2) is not ported and cannot be chosen
		}

		switch (closeGroup->checkedId())
		{
		case 1: o.closeOption = rfh::CloseOption::Delete; break;
		case 2: o.closeOption = rfh::CloseOption::Purge; break;
		default: o.closeOption = rfh::CloseOption::None; break;
		}

		o.fileCodePage = ui->fileCodePageEdit->text().toInt();		// atoi: empty is 0, "decide from the data"
		o.read = readOptions;
		o.write = writeOptions;
		return o;
	}

	void MainTab::setFileOptions(const rfh::fileio::ReadOptions & read, const rfh::fileio::WriteOptions & write)
	{
		readOptions = read;
		writeOptions = write;
	}

	void MainTab::updateShellFlags()
	{
		ShellState shell = doc.shell();
		shell.setIdentity = ui->setIdentityCheck->isChecked();
		shell.setAll = ui->setAllCheck->isChecked();
		shell.alternateUser = ui->alternateUserCheck->isChecked();
		doc.setShell(shell);
	}

	void MainTab::syncDocument()
	{
		doc.setQueueManager(ui->qmCombo->currentText());
		doc.setQueueName(ui->queueCombo->currentText());
		doc.setRemoteQueueManager(ui->remoteQmEdit->text());
		updateShellFlags();
		MainOptions o = options();
		ops.setOptions(o);
		o.connect.wipePassword();
	}

	void MainTab::setNames(const QString & queueManager, const QString & queue, const QString & remoteQueueManager)
	{
		ui->qmCombo->setEditText(queueManager);
		ui->queueCombo->setEditText(queue);
		ui->remoteQmEdit->setText(remoteQueueManager);
		fillQueues();
	}

	// ---- the buttons ---------------------------------------------------------------------------------------------------

	template <typename Fn>
	void MainTab::onClick(QWidget * button, Fn flow)
	{
		connect(qobject_cast<QAbstractButton *>(button), &QAbstractButton::clicked, this, [this, flow]()
		{
			syncDocument();
			flow();
		});
	}

	void MainTab::wire()
	{
		onClick(ui->readQButton, [this]() { ops.readQ(); });
		onClick(ui->writeQButton, [this]() { ops.writeQ(); });
		onClick(ui->browseQButton, [this]() { ops.browseQ(); });
		onClick(ui->startBrowseButton, [this]() { ops.startBrowse(); });
		onClick(ui->browseNextButton, [this]() { ops.browseNext(); });
		onClick(ui->browsePrevButton, [this]() { ops.browsePrev(); });
		onClick(ui->endBrowseButton, [this]() { ops.endBrowse(); });
		onClick(ui->closeQButton, [this]() { ops.closeQ(); });
		onClick(ui->purgeQButton, [this]() { ops.purgeQ(); });
		connect(ui->saveQButton, &QAbstractButton::clicked, this, [this]() { ops.saveQ(this); });
		connect(ui->loadQButton, &QAbstractButton::clicked, this, [this]() { ops.loadQ(this); });
		onClick(ui->clearDataButton, [this]() { ops.clearData(); });
		onClick(ui->clearAllButton, [this]() { ops.clearAll(); });
		onClick(ui->loadNamesButton, [this]() { ops.loadNames(); });
		connect(ui->openFileButton, &QAbstractButton::clicked, this, &MainTab::openFileDialog);
		connect(ui->saveFileButton, &QAbstractButton::clicked, this, &MainTab::saveFileDialog);
		connect(ui->setConnIdButton, &QAbstractButton::clicked, this, &MainTab::setConnectionDialog);
		connect(ui->cancelButton, &QAbstractButton::clicked, this, [this]() { ops.cancelGet(); });
		connect(ui->exitButton, &QAbstractButton::clicked, this, &MainTab::exitRequested);

		// the context boxes feed the MQMD tab's enables at once
		for (QCheckBox * box : { ui->setIdentityCheck, ui->setAllCheck, ui->alternateUserCheck })
		{
			connect(box, &QCheckBox::toggled, this, &MainTab::updateShellFlags);
		}
		connect(ui->clientRadio, &QRadioButton::toggled, this, &MainTab::applyLimitsForMode);

		connect(ui->qmCombo, &PopupComboBox::aboutToShowPopup, this, &MainTab::refreshQueueManagers);
		connect(ui->queueCombo, &PopupComboBox::aboutToShowPopup, this, &MainTab::refreshQueues);
		connect(ui->qmCombo, QOverload<int>::of(&QComboBox::activated), this, &MainTab::refreshQueues);
		connect(&ops, &Operations::queueNamesChanged, this, &MainTab::refreshQueues);

		connect(&doc, &AppDocument::stateChanged, this, &MainTab::refresh);
		connect(&log, &StatusLog::changed, this, &MainTab::refresh);
	}

	void MainTab::openFileDialog()
	{
		if (!doc.enables().openFile)
		{
			return;
		}
		syncDocument();
		const QString directory = (settings != nullptr) ? settings->lastDirectory(QStringLiteral("open")) : QString();
		const QString path = chooser->openPath(this, directory);
		if (!path.isEmpty())
		{
			ops.openFile(path);
		}
	}

	void MainTab::saveFileDialog()
	{
		if (!doc.enables().saveFile)
		{
			return;
		}
		syncDocument();
		const QString directory = (settings != nullptr) ? settings->lastDirectory(QStringLiteral("save")) : QString();
		const QString path = chooser->savePath(this, directory);
		if (!path.isEmpty())
		{
			ops.saveFile(path);
		}
	}

	// OnSetConnUser (General.cpp:2347): the values go to the document only when the dialog ends with OK.
	void MainTab::setConnectionDialog()
	{
		if (!doc.enables().setConnId)
		{
			return;
		}
		ConnUserDialog dialog(currentConnect(), this);
		if (dialog.exec() == QDialog::Accepted)
		{
			connection.wipePassword();
			connection = dialog.values();
		}
		syncDocument();
	}

	void MainTab::setFileChooser(FileChooser * newChooser)
	{
		chooser = (newChooser != nullptr) ? newChooser : defaultChooser.get();
	}

	// Inventory 7.1: Ctrl+ keys of the Main tab, and the three Alt+ keys that have no mnemonic. They are window shortcuts, so
	// they work wherever the focus is while this tab is the visible one, and a disabled button ignores click().
	void MainTab::addShortcuts()
	{
		struct Entry { QKeySequence keys; QAbstractButton * button; };
		const Entry table[] = {
			{ Qt::CTRL | Qt::Key_Q, ui->readQButton },
			{ Qt::CTRL | Qt::Key_W, ui->writeQButton },
			{ Qt::CTRL | Qt::Key_B, ui->browseQButton },
			{ Qt::CTRL | Qt::Key_T, ui->startBrowseButton },
			{ Qt::CTRL | Qt::Key_N, ui->browseNextButton },
			{ Qt::CTRL | Qt::Key_R, ui->browsePrevButton },
			{ Qt::CTRL | Qt::Key_M, ui->endBrowseButton },
			{ Qt::ALT | Qt::Key_V, ui->endBrowseButton },
			{ Qt::CTRL | Qt::Key_K, ui->closeQButton },
			{ Qt::CTRL | Qt::Key_E, ui->purgeQButton },
			{ Qt::CTRL | Qt::Key_D, ui->displayQButton },
			{ Qt::CTRL | Qt::Key_L, ui->loadQButton },
			{ Qt::CTRL | Qt::Key_Y, ui->saveQButton },
			{ Qt::CTRL | Qt::Key_H, ui->loadNamesButton },
			{ Qt::CTRL | Qt::Key_I, ui->setConnIdButton },
			{ Qt::CTRL | Qt::Key_J, ui->clearAllButton },
			{ Qt::ALT | Qt::Key_X, ui->clearDataButton },
			{ Qt::ALT | Qt::Key_Y, ui->clearAllButton },		// OnCopybook calls ClearAll on the Main tab
			{ Qt::CTRL | Qt::Key_O, ui->openFileButton },
			{ Qt::CTRL | Qt::Key_S, ui->saveFileButton },
		};
		for (const Entry & e : table)
		{
			QShortcut * shortcut = new QShortcut(e.keys, this);
			shortcut->setContext(Qt::WindowShortcut);
			QAbstractButton * button = e.button;
			connect(shortcut, &QShortcut::activated, this, [button]() { button->click(); });
		}
	}

	// ---- the drop-downs --------------------------------------------------------------------------------------------------

	void MainTab::applyLimitsForMode()
	{
		const bool client = ui->clientRadio->isChecked();
		ui->qmCombo->lineEdit()->setMaxLength(client ? MaxQueueManagerClient : MaxQueueManagerBindings);
		ui->channelEdit->setEnabled(client);
		ui->connNameEdit->setEnabled(client);
		ui->ccdtEdit->setEnabled(client);
	}

	void MainTab::setQueueManagerLister(std::function<QStringList()> newLister)
	{
		lister = std::move(newLister);
		fillQueueManagers();
	}

	void MainTab::refreshQueueManagers()
	{
		fillQueueManagers();
	}

	void MainTab::refreshQueues()
	{
		fillQueues();
	}

	void MainTab::fillQueueManagers()
	{
		QStringList names = lister ? lister() : QStringList();
		if (settings != nullptr)
		{
			names += settings->recentQueueManagers();
		}
		names = sortedUnique(names);
		const QString keep = ui->qmCombo->currentText();
		const QSignalBlocker block(ui->qmCombo);
		ui->qmCombo->clear();
		ui->qmCombo->addItems(names);
		ui->qmCombo->setCurrentIndex(-1);
		ui->qmCombo->setEditText(keep);
	}

	// OnDropdownQName: the names Load Names found for the queue manager (SYSTEM.* ones only on request) and the queues used
	// before, which the user chose to type and so are always offered.
	void MainTab::fillQueues()
	{
		const QString qm = ui->qmCombo->currentText();
		QStringList names;
		if (settings != nullptr)
		{
			names += settings->recentQueues(qm);
		}
		for (const QString & name : ops.queueNames(qm))
		{
			if (systemQueues || !name.startsWith(QStringLiteral("SYSTEM.")))
			{
				names << name;
			}
		}
		names = sortedUnique(names);
		const QString keep = ui->queueCombo->currentText();
		const QSignalBlocker block(ui->queueCombo);
		ui->queueCombo->clear();
		ui->queueCombo->addItems(names);
		ui->queueCombo->setCurrentIndex(-1);
		ui->queueCombo->setEditText(keep);
	}

	void MainTab::setShowSystemQueues(bool show)
	{
		systemQueues = show;
		fillQueues();
	}

	// ---- showing the document ------------------------------------------------------------------------------------------------

	void MainTab::setBusyCursor(bool busy)
	{
		// BeginWaitCursor / EndWaitCursor around the long operations (inventory 3.1)
		if (busy && !cursorSet)
		{
			QGuiApplication::setOverrideCursor(Qt::BusyCursor);
			cursorSet = true;
		}
		else if (!busy && cursorSet)
		{
			QGuiApplication::restoreOverrideCursor();
			cursorSet = false;
		}
	}

	void MainTab::refresh()
	{
		if (refreshing)
		{
			return;
		}
		refreshing = true;
		const Enables e = doc.enables();

		// General::UpdatePageData (General.cpp:504-620) and the new Cancel button; every state is enablesFor's
		ui->openFileButton->setEnabled(e.openFile);
		ui->saveFileButton->setEnabled(e.saveFile);
		ui->clearDataButton->setEnabled(e.clearData);
		ui->clearAllButton->setEnabled(e.clearAll);
		ui->readQButton->setEnabled(e.readQ);
		ui->purgeQButton->setEnabled(e.purgeQ);
		ui->writeQButton->setEnabled(e.writeQ);
		ui->browseQButton->setEnabled(e.browseQ);
		ui->startBrowseButton->setEnabled(e.startBrowse);
		ui->browseNextButton->setEnabled(e.browseNext);
		ui->browsePrevButton->setEnabled(e.browsePrev);
		ui->endBrowseButton->setEnabled(e.endBrowse);
		ui->closeQButton->setEnabled(e.closeQ);
		ui->loadNamesButton->setEnabled(e.loadNames);
		ui->setConnIdButton->setEnabled(e.setConnId);
		ui->exitButton->setEnabled(e.exit);
		ui->cancelButton->setEnabled(e.cancel);
		// not ported in Phase 1: enablesFor keeps them off; the "Yes" radio has no flag of its own
		ui->displayQButton->setEnabled(e.displayQ);
		ui->loadQButton->setEnabled(e.loadQ);
		ui->saveQButton->setEnabled(e.saveQ);
		ui->moveQButton->setEnabled(e.moveQ);
		ui->propsYesRadio->setEnabled(false);

		ui->logicalOrderCheck->setEnabled(e.getOptions);
		ui->completeMsgCheck->setEnabled(e.getOptions);
		ui->allAvailCheck->setEnabled(e.getOptions);
		ui->convertCheck->setEnabled(e.getOptions);
		for (QWidget * w : { (QWidget *)ui->propsAsQueueRadio, (QWidget *)ui->propsNoneRadio, (QWidget *)ui->propsRfh2Radio,
		                     (QWidget *)ui->propsCompatRadio })
		{
			w->setEnabled(e.propsRadios);
		}
		ui->selectorEdit->setReadOnly(!e.selectorEditable);

		// the values UpdatePageData copies from the document
		setTextIfChanged(ui->fileNameEdit, doc.filePath());
		setTextIfChanged(ui->fileSizeEdit, QString::number((qulonglong)doc.dataSize()));
		setTextIfChanged(ui->copyBookEdit, QString::fromStdString(doc.render().copybookPath));
		const long depth = doc.queueInfo().depth;
		setTextIfChanged(ui->queueDepthEdit, (depth >= 0) ? QString::number(depth) : QString());
		setTextIfChanged(ui->queueTypeEdit, queueTypeText(doc.queueInfo().type));
		const QString status = log.textForClipboard();
		if (ui->statusEdit->toPlainText() != status)
		{
			ui->statusEdit->setPlainText(status);
		}
		const ShellState & shell = doc.shell();
		for (const auto & pair : { std::pair<QCheckBox *, bool>(ui->setIdentityCheck, shell.setIdentity),
		                           std::pair<QCheckBox *, bool>(ui->setAllCheck, shell.setAll),
		                           std::pair<QCheckBox *, bool>(ui->alternateUserCheck, shell.alternateUser) })
		{
			if (pair.first->isChecked() != pair.second)
			{
				const QSignalBlocker block(pair.first);
				pair.first->setChecked(pair.second);
			}
		}
		setBusyCursor(shell.busy);
		refreshing = false;
	}
}
