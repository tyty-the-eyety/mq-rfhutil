#include "MqmdTab.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QKeySequence>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShortcut>

#include <string.h>

#include "AppDocument.h"
#include "IdEdit.h"
#include "ui_MqmdTab.h"

// mqsubs.cpp: the application type texts of the Appl Type list (MQMDPAGE::OnSetfocusApplType)
const char * getApplTypeDisplay(int putType);

namespace rfhqt
{
	namespace
	{
		using rfh::id_display::Field;
		using rfh::id_display::Mode;

		// OnSetfocusApplType (MQMDPAGE.cpp:995): the types the list offers, in that order
		const int kApplTypes[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 19, 20, 21, 22, 25, 26, 28, 29, 30, 31, 32, 33, 34, 35 };

		// OnSetfocusMsgtype (MQMDPAGE.cpp:777); the Windows combo has CBS_SORT, so the list is in text order
		const char * const kMsgTypes[] = { "1 Request", "112 FR_MQE", "113 MQE", "2 Reply", "4 Report", "8 Datagram" };

		// OnInitDialog (MQMDPAGE.cpp:249-267): the 8 character MQ constants
		const char * const kFormats[] = {
			MQFMT_STRING, MQFMT_DEAD_LETTER_HEADER, MQFMT_CICS, MQFMT_IMS, MQFMT_IMS_VAR_STRING, MQFMT_PCF, MQFMT_ADMIN,
			MQFMT_COMMAND_1, MQFMT_COMMAND_2, MQFMT_DIST_HEADER, MQFMT_EMBEDDED_PCF, MQFMT_EVENT, MQFMT_REF_MSG_HEADER,
			MQFMT_RF_HEADER_1, MQFMT_RF_HEADER_2, MQFMT_TRIGGER, MQFMT_WORK_INFO_HEADER, MQFMT_XMIT_Q_HEADER,
			MQFMT_CHANNEL_COMPLETED };

		QString text(const std::string & s)
		{
			return QString::fromLatin1(s.data(), (qsizetype)s.size());
		}

		std::string text(const QString & s)
		{
			return s.toLatin1().toStdString();
		}

		void fillCombo(QComboBox * combo, const QStringList & items, int maxLength)
		{
			combo->addItems(items);
			combo->setCurrentIndex(-1);
			combo->setEditText(QString());		// a new Windows page starts with empty boxes
			combo->lineEdit()->setMaxLength(maxLength);
		}

		// -1 (nothing selected) unchecks every button; an id without a button does the same
		void check(QButtonGroup * group, int id)
		{
			if (QAbstractButton * button = group->button(id))
			{
				button->setChecked(true);
				return;
			}
			group->setExclusive(false);
			for (QAbstractButton * b : group->buttons())
			{
				b->setChecked(false);
			}
			group->setExclusive(true);
		}

		void addButtons(QButtonGroup * group, const QList<QAbstractButton *> & buttons, const QList<int> & ids)
		{
			for (int i = 0; i < buttons.size(); i++)
			{
				group->addButton(buttons[i], ids[i]);
			}
		}

		void setText(QLineEdit * edit, const std::string & value)
		{
			const QString t = text(value);
			if (edit->text() != t)
			{
				edit->setText(t);
			}
		}

		void setComboText(QComboBox * combo, const std::string & value)
		{
			const QString t = text(value);
			if (combo->currentText() != t || combo->currentIndex() >= 0)
			{
				combo->setCurrentIndex(-1);
				combo->setEditText(t);
			}
		}
	}

	MqmdTab::MqmdTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::MqmdTab), persistGroup(new QButtonGroup(this)),
		  encodingGroup(new QButtonGroup(this)), pdEncodingGroup(new QButtonGroup(this)), floatGroup(new QButtonGroup(this)),
		  idDisplayGroup(new QButtonGroup(this))
	{
		ui->setupUi(this);
		setObjectName(QStringLiteral("mqmdTab"));

		// radio groups carry the core's option values
		persistGroup->setObjectName(QStringLiteral("persistGroup"));
		encodingGroup->setObjectName(QStringLiteral("encodingGroup"));
		pdEncodingGroup->setObjectName(QStringLiteral("pdEncodingGroup"));
		floatGroup->setObjectName(QStringLiteral("floatGroup"));
		idDisplayGroup->setObjectName(QStringLiteral("idDisplayGroup"));
		addButtons(persistGroup, { ui->persistNoRadio, ui->persistYesRadio, ui->persistAsQueueRadio },
		           { MQPERSIST_NO, MQPERSIST_YES, MQPERSIST_AS_Q });
		addButtons(encodingGroup, { ui->encPcRadio, ui->encHostRadio }, { NUMERIC_PC, NUMERIC_HOST });
		addButtons(pdEncodingGroup, { ui->pdPcRadio, ui->pdHostRadio }, { NUMERIC_PC, NUMERIC_HOST });
		addButtons(floatGroup, { ui->floatPcRadio, ui->floatUnixRadio, ui->float390Radio, ui->floatTnsRadio },
		           { FLOAT_PC, FLOAT_UNIX, FLOAT_390, FLOAT_TNS });
		addButtons(idDisplayGroup, { ui->dispAsciiRadio, ui->dispEbcdicRadio, ui->dispHexRadio },
		           { (int)Mode::Ascii, (int)Mode::Ebcdic, (int)Mode::Hex });
		const QList<QList<QAbstractButton *>> reports = {
			{ ui->reportExceptNoneRadio, ui->reportExceptYesRadio, ui->reportExceptDataRadio, ui->reportExceptFullRadio },
			{ ui->reportExpireNoneRadio, ui->reportExpireYesRadio, ui->reportExpireDataRadio, ui->reportExpireFullRadio },
			{ ui->reportCoaNoneRadio, ui->reportCoaYesRadio, ui->reportCoaDataRadio, ui->reportCoaFullRadio },
			{ ui->reportCodNoneRadio, ui->reportCodYesRadio, ui->reportCodDataRadio, ui->reportCodFullRadio } };
		const char * const reportNames[] = { "reportExceptGroup", "reportExpireGroup", "reportCoaGroup", "reportCodGroup" };
		for (int i = 0; i < 4; i++)
		{
			reportGroups[i] = new QButtonGroup(this);
			reportGroups[i]->setObjectName(QString::fromLatin1(reportNames[i]));
			addButtons(reportGroups[i], reports[i], { REPORT_NONE, REPORT_YES, REPORT_DATA, REPORT_FULL });
		}

		// lists
		QStringList formats;
		for (const char * f : kFormats)
		{
			formats << QString::fromLatin1(f, 8);
		}
		fillCombo(ui->formatCombo, formats, 8);
		QStringList msgTypes;
		for (const char * t : kMsgTypes)
		{
			msgTypes << QString::fromLatin1(t);
		}
		fillCombo(ui->msgTypeCombo, msgTypes, 11);		// LimitText(11), MQMDPAGE.cpp:792
		QStringList applTypes;
		for (int t : kApplTypes)
		{
			applTypes << QString::fromLatin1(getApplTypeDisplay(t));
		}
		fillCombo(ui->applTypeCombo, applTypes, 64);

		// ES_NUMBER: Code Page, Expiry and Offset take digits only
		for (QLineEdit * e : { ui->ccsidEdit, ui->expiryEdit, ui->offsetEdit })
		{
			e->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]*")), e));
		}

		// the ids: overtype, hex only until the display changes
		for (IdEdit * e : { ui->msgIdEdit, ui->correlIdEdit, ui->groupIdEdit })
		{
			e->setOvertype(true);
		}
		ui->dispHexRadio->setChecked(true);

		new QShortcut(QKeySequence(Qt::ALT | Qt::Key_J), this, [this]() { resetIds(); });
		new QShortcut(QKeySequence(Qt::ALT | Qt::Key_C), this, [this]() { copyMsgId(); });

		setFields(doc.mqmd());
		applyEnables();
		wire();
	}

	MqmdTab::~MqmdTab() = default;

	// ---- ids ----------------------------------------------------------------------------------------------------------

	unsigned char * MqmdTab::idBytes(Field field) const
	{
		switch (field)
		{
		case Field::MsgId: return cur.msgId;
		case Field::CorrelId: return cur.correlId;
		default: return cur.groupId;
		}
	}

	IdEdit * MqmdTab::idEdit(Field field) const
	{
		switch (field)
		{
		case Field::MsgId: return ui->msgIdEdit;
		case Field::CorrelId: return ui->correlIdEdit;
		default: return ui->groupIdEdit;
		}
	}

	void MqmdTab::showIds()
	{
		const bool wasLoading = loading;
		loading = true;
		for (Field f : { Field::MsgId, Field::CorrelId, Field::GroupId })
		{
			IdEdit * e = idEdit(f);
			e->setMaxLength((int)rfh::id_display::maxLength(mode));		// before the text, which may be shorter or longer
			e->setHexOnly(Mode::Hex == mode);
			const QString shown = QString::fromLatin1(rfh::id_display::toText(idBytes(f), mode).c_str());
			if (e->text() != shown)
			{
				e->setText(shown);
			}
		}
		loading = wasLoading;
	}

	bool MqmdTab::storeId(Field field)
	{
		unsigned char * id = idBytes(field);
		const std::string typed = text(idEdit(field)->text());
		const std::string previous = rfh::id_display::toText(id, mode);
		if (typed == previous)
		{
			return false;		// untouched: keep the bytes, including the ones the box shows as blanks
		}
		MQBYTE24 updated;
		memcpy(updated, id, sizeof(updated));
		std::string error;
		if (!rfh::id_display::applyEdit(field, updated, typed, previous, mode, error))
		{
			emit statusMessage(QString::fromLatin1(error.c_str()));
			return false;
		}
		const bool changed = memcmp(updated, id, sizeof(updated)) != 0;
		memcpy(id, updated, sizeof(updated));
		return changed;
	}

	bool MqmdTab::storeIds()
	{
		bool changed = false;
		for (Field f : { Field::MsgId, Field::CorrelId, Field::GroupId })
		{
			changed = storeId(f) || changed;
		}
		return changed;
	}

	void MqmdTab::showMode(Mode newMode)
	{
		// edits made in the old mode are stored in the old mode, then the boxes are redrawn from the bytes (updateIdFields)
		const bool changed = storeIds();
		mode = newMode;
		showIds();
		if (changed)
		{
			editedByUser();
		}
	}

	void MqmdTab::copyMsgId()
	{
		storeId(Field::MsgId);
		memcpy(cur.correlId, cur.msgId, sizeof(cur.correlId));
		showIds();
		editedByUser();
	}

	void MqmdTab::resetIds()
	{
		memset(cur.msgId, 0, sizeof(cur.msgId));
		memset(cur.correlId, 0, sizeof(cur.correlId));
		memset(cur.groupId, 0, sizeof(cur.groupId));
		showIds();
		editedByUser();
	}

	// ---- fields <-> widgets --------------------------------------------------------------------------------------------

	rfh::MqmdFields MqmdTab::fields() const
	{
		const_cast<MqmdTab *>(this)->storeIds();
		rfh::MqmdFields f = cur;		// the ids, and the values of groups that show none
		f.format = text(ui->formatCombo->currentText());
		f.ccsid = text(ui->ccsidEdit->text());
		f.msgType = text(ui->msgTypeCombo->currentText());
		f.expiry = text(ui->expiryEdit->text());
		f.feedback = text(ui->feedbackEdit->text());
		f.priority = text(ui->priorityEdit->text());
		f.backoutCount = text(ui->backoutEdit->text());
		f.replyToQ = text(ui->replyQueueEdit->text());
		f.replyToQMgr = text(ui->replyQmEdit->text());
		f.userId = text(ui->userIdEdit->text());
		f.accountingToken = text(ui->accountTokenEdit->text());
		f.applIdentity = text(ui->applIdentityEdit->text());
		f.applOrigin = text(ui->applOriginEdit->text());
		f.putApplName = text(ui->applNameEdit->text());
		f.putApplType = text(ui->applTypeCombo->currentText());
		f.dateTime = text(ui->dateTimeEdit->text());
		f.seqNo = text(ui->seqNoEdit->text());
		f.offset = text(ui->offsetEdit->text());
		f.originalLength = text(ui->origLenEdit->text());

		auto pick = [](const QButtonGroup * g, int fallback) { return g->checkedId() >= 0 ? g->checkedId() : fallback; };
		f.persistence = pick(persistGroup, cur.persistence);
		f.encoding = pick(encodingGroup, cur.encoding);
		f.pdEncoding = pick(pdEncodingGroup, cur.pdEncoding);
		f.floatEncoding = pick(floatGroup, cur.floatEncoding);
		f.reportException = reportGroups[0]->checkedId();
		f.reportExpiration = reportGroups[1]->checkedId();
		f.reportCoa = reportGroups[2]->checkedId();
		f.reportCod = reportGroups[3]->checkedId();

		f.reportPan = ui->panCheck->isChecked();
		f.reportNan = ui->nanCheck->isChecked();
		f.reportActivity = ui->activityCheck->isChecked();
		f.reportDiscard = ui->discardCheck->isChecked();
		f.reportPassMsgId = ui->passMsgIdCheck->isChecked();
		f.reportPassCorrelId = ui->passCorrelCheck->isChecked();
		f.reportPassDiscardAndExpiry = ui->expiryDiscardCheck->isChecked();
		f.groupYes = ui->groupYesCheck->isChecked();
		f.groupLast = ui->groupLastCheck->isChecked();
		f.segmentYes = ui->segmentYesCheck->isChecked();
		f.segmentLast = ui->segmentLastCheck->isChecked();
		f.segmentAllowed = ui->segmentAllowedCheck->isChecked();
		return f;
	}

	void MqmdTab::setFields(const rfh::MqmdFields & f)
	{
		const bool wasLoading = loading;
		loading = true;
		cur = f;
		setComboText(ui->formatCombo, f.format);
		setText(ui->ccsidEdit, f.ccsid);
		setComboText(ui->msgTypeCombo, f.msgType);
		setText(ui->expiryEdit, f.expiry);
		setText(ui->feedbackEdit, f.feedback);
		setText(ui->priorityEdit, f.priority);
		setText(ui->backoutEdit, f.backoutCount);
		setText(ui->replyQueueEdit, f.replyToQ);
		setText(ui->replyQmEdit, f.replyToQMgr);
		setText(ui->userIdEdit, f.userId);
		setText(ui->accountTokenEdit, f.accountingToken);
		setText(ui->applIdentityEdit, f.applIdentity);
		setText(ui->applOriginEdit, f.applOrigin);
		setText(ui->applNameEdit, f.putApplName);
		setComboText(ui->applTypeCombo, f.putApplType);
		setText(ui->dateTimeEdit, f.dateTime);
		setText(ui->seqNoEdit, f.seqNo);
		setText(ui->offsetEdit, f.offset);
		setText(ui->origLenEdit, f.originalLength);

		check(persistGroup, f.persistence);
		check(encodingGroup, f.encoding);
		check(pdEncodingGroup, f.pdEncoding);
		check(floatGroup, f.floatEncoding);
		check(reportGroups[0], f.reportException);
		check(reportGroups[1], f.reportExpiration);
		check(reportGroups[2], f.reportCoa);
		check(reportGroups[3], f.reportCod);

		ui->panCheck->setChecked(f.reportPan);
		ui->nanCheck->setChecked(f.reportNan);
		ui->activityCheck->setChecked(f.reportActivity);
		ui->discardCheck->setChecked(f.reportDiscard);
		ui->passMsgIdCheck->setChecked(f.reportPassMsgId);
		ui->passCorrelCheck->setChecked(f.reportPassCorrelId);
		ui->expiryDiscardCheck->setChecked(f.reportPassDiscardAndExpiry);
		ui->groupYesCheck->setChecked(f.groupYes);
		ui->groupLastCheck->setChecked(f.groupLast);
		ui->segmentYesCheck->setChecked(f.segmentYes);
		ui->segmentLastCheck->setChecked(f.segmentLast);
		ui->segmentAllowedCheck->setChecked(f.segmentAllowed);

		showIds();
		loading = wasLoading;
	}

	void MqmdTab::focusFirst()
	{
		ui->formatCombo->setFocus(Qt::OtherFocusReason);
	}

	// ---- reaction ------------------------------------------------------------------------------------------------------

	void MqmdTab::editedByUser()
	{
		if (loading)
		{
			return;
		}
		publishing = true;
		doc.setMqmd(fields());
		publishing = false;
		emit fieldsEdited();
	}

	void MqmdTab::applyEnables()
	{
		// MQMDPAGE::OnSetActive (MQMDPAGE.cpp:396-455), through the document's rules
		const Enables e = doc.enables();
		ui->backoutEdit->setReadOnly(true);
		ui->msgIdEdit->setReadOnly(!e.mqmdMsgIdEditable);
		ui->userIdEdit->setReadOnly(!e.mqmdUserIdEditable);
		ui->accountTokenEdit->setReadOnly(!e.mqmdUserIdEditable);
		for (QLineEdit * edit : { ui->applIdentityEdit, ui->applOriginEdit, ui->applNameEdit, ui->dateTimeEdit })
		{
			edit->setReadOnly(!e.mqmdPutApplEditable);
		}
		ui->applTypeCombo->setEnabled(e.mqmdPutApplEditable);
	}

	void MqmdTab::wire()
	{
		const auto edited = [this]() { editedByUser(); };

		for (QLineEdit * e : { ui->ccsidEdit, ui->expiryEdit, ui->feedbackEdit, ui->priorityEdit, ui->replyQueueEdit,
		                       ui->replyQmEdit, ui->userIdEdit, ui->accountTokenEdit, ui->applIdentityEdit,
		                       ui->applOriginEdit, ui->applNameEdit, ui->dateTimeEdit, ui->seqNoEdit, ui->offsetEdit,
		                       ui->origLenEdit })
		{
			connect(e, &QLineEdit::textEdited, this, edited);
		}
		for (QComboBox * c : { ui->formatCombo, ui->msgTypeCombo, ui->applTypeCombo })
		{
			connect(c, &QComboBox::editTextChanged, this, edited);
		}
		for (QCheckBox * c : { ui->panCheck, ui->nanCheck, ui->activityCheck, ui->discardCheck, ui->passMsgIdCheck,
		                       ui->passCorrelCheck, ui->expiryDiscardCheck, ui->groupYesCheck, ui->groupLastCheck,
		                       ui->segmentYesCheck, ui->segmentLastCheck, ui->segmentAllowedCheck })
		{
			connect(c, &QCheckBox::toggled, this, edited);
		}
		for (QButtonGroup * g : { persistGroup, encodingGroup, pdEncodingGroup, floatGroup, reportGroups[0], reportGroups[1],
		                          reportGroups[2], reportGroups[3] })
		{
			connect(g, &QButtonGroup::idToggled, this, [this](int, bool checked) { if (checked) editedByUser(); });
		}

		// the ids: the Correl Id and Group Id boxes are stored on every change, the Message Id box when it is left
		for (Field f : { Field::CorrelId, Field::GroupId })
		{
			connect(idEdit(f), &QLineEdit::textEdited, this, [this, f]() { if (!loading && storeId(f)) editedByUser(); });
		}
		connect(ui->msgIdEdit, &QLineEdit::editingFinished, this, [this]() { if (!loading && storeId(Field::MsgId)) editedByUser(); });
		for (IdEdit * e : { ui->msgIdEdit, ui->correlIdEdit, ui->groupIdEdit })
		{
			connect(e, &IdEdit::keyRejected, this, [this]() {
				emit statusMessage(QStringLiteral("Only the digits 0-9 and the letters A-F can be typed in a hex field"));
			});
		}
		connect(idDisplayGroup, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
			if (checked && !loading)
			{
				showMode((Mode)id);
			}
		});
		connect(ui->resetIdsButton, &QAbstractButton::clicked, this, [this]() { resetIds(); });
		connect(ui->copyMsgIdButton, &QAbstractButton::clicked, this, [this]() { copyMsgId(); });

		connect(&doc, &AppDocument::mqmdChanged, this, [this]() {
			if (!publishing)
			{
				setFields(doc.mqmd());
			}
		});
		connect(&doc, &AppDocument::stateChanged, this, [this]() { applyEnables(); });
	}
}
