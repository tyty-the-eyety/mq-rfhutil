#include "CicsTab.h"
#include "ui_CicsTab.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>

#include "rfhconst.h"

namespace rfhqt
{
	static void populateNumericCombo(QComboBox * combo)
	{
		combo->addItem(QStringLiteral("PC (reversed)"), NUMERIC_PC);
		combo->addItem(QStringLiteral("Host (normal)"), NUMERIC_HOST);
	}

	static void populateFloatCombo(QComboBox * combo)
	{
		combo->addItem(QStringLiteral("PC (IEEE normal)"),     FLOAT_PC);
		combo->addItem(QStringLiteral("Unix (IEEE reversed)"), FLOAT_UNIX);
		combo->addItem(QStringLiteral("390 (S390)"),           FLOAT_390);
		combo->addItem(QStringLiteral("TNS"),                  FLOAT_TNS);
	}

	static void setCombo(QComboBox * combo, int value)
	{
		const int idx = combo->findData(value);
		if (idx >= 0) combo->setCurrentIndex(idx);
	}

	static int comboValue(const QComboBox * combo)
	{
		return combo->currentData().toInt();
	}

	CicsTab::CicsTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::CicsTab)
	{
		ui->setupUi(this);
		populateCombos();
		wire();
	}

	CicsTab::~CicsTab() = default;

	void CicsTab::populateCombos()
	{
		populateNumericCombo(ui->encodingCombo);
		populateNumericCombo(ui->pdEncodingCombo);
		populateFloatCombo(ui->floatEncodingCombo);
	}

	void CicsTab::wire()
	{
		connect(ui->includedCheck, &QCheckBox::toggled, this, &CicsTab::editedByUser);
		const auto editSlot = [this]() { editedByUser(); };
		connect(ui->versionCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->ccsidEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->encodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->pdEncodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->floatEncodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->formatEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->flagsEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->returnCodeEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->compCodeEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->reasonEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->uowControlEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->getWaitEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->linkTypeEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->outputDataLenEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->facilityKeepTimeEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->adsDescriptorEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->convTaskEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->taskEndStatusEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->facilityEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->functionEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->abendCodeEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->authenticatorEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->replyToFmtEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->remoteSysIdEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->remoteTransIdEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->transactionIdEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->facilityLikeEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->attentionIdEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->startCodeEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->cancelCodeEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->nextTransIdEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->cursorPosEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->errorOffsetEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->inputItemEdit, &QLineEdit::textEdited, this, editSlot);
	}

	void CicsTab::editedByUser()
	{
		emit fieldsEdited();
	}

	void CicsTab::setFields(const rfh::CicsFields & f)
	{
		ui->includedCheck->setChecked(f.included);
		ui->versionCombo->setCurrentIndex(f.version == 1 ? 0 : 1);
		ui->ccsidEdit->setText(QString::number(f.ccsid));
		setCombo(ui->encodingCombo, f.encoding);
		setCombo(ui->pdEncodingCombo, f.pdEncoding);
		setCombo(ui->floatEncodingCombo, f.floatEncoding);
		ui->formatEdit->setText(QString::fromStdString(f.format));
		ui->flagsEdit->setText(QString::number(f.flags));
		ui->returnCodeEdit->setText(QString::number(f.returnCode));
		ui->compCodeEdit->setText(QString::number(f.compCode));
		ui->reasonEdit->setText(QString::number(f.reason));
		ui->uowControlEdit->setText(QString::number(f.uowControl));
		ui->getWaitEdit->setText(QString::number(f.getWaitInterval));
		ui->linkTypeEdit->setText(QString::number(f.linkType));
		ui->outputDataLenEdit->setText(QString::number(f.outputDataLength));
		ui->facilityKeepTimeEdit->setText(QString::number(f.facilityKeepTime));
		ui->adsDescriptorEdit->setText(QString::number(f.adsDescriptor));
		ui->convTaskEdit->setText(QString::number(f.conversationalTask));
		ui->taskEndStatusEdit->setText(QString::number(f.taskEndStatus));
		ui->facilityEdit->setText(QString::fromStdString(f.facility));
		ui->functionEdit->setText(QString::fromStdString(f.function));
		ui->abendCodeEdit->setText(QString::fromStdString(f.abendCode));
		ui->authenticatorEdit->setText(QString::fromStdString(f.authenticator));
		ui->replyToFmtEdit->setText(QString::fromStdString(f.replyToFormat));
		ui->remoteSysIdEdit->setText(QString::fromStdString(f.remoteSysId));
		ui->remoteTransIdEdit->setText(QString::fromStdString(f.remoteTransId));
		ui->transactionIdEdit->setText(QString::fromStdString(f.transactionId));
		ui->facilityLikeEdit->setText(QString::fromStdString(f.facilityLike));
		ui->attentionIdEdit->setText(QString::fromStdString(f.attentionId));
		ui->startCodeEdit->setText(QString::fromStdString(f.startCode));
		ui->cancelCodeEdit->setText(QString::fromStdString(f.cancelCode));
		ui->nextTransIdEdit->setText(QString::fromStdString(f.nextTransactionId));
		ui->cursorPosEdit->setText(QString::number(f.cursorPosition));
		ui->errorOffsetEdit->setText(QString::number(f.errorOffset));
		ui->inputItemEdit->setText(QString::number(f.inputItem));
	}

	rfh::CicsFields CicsTab::fields() const
	{
		rfh::CicsFields f;
		f.included = ui->includedCheck->isChecked();
		f.version = ui->versionCombo->currentIndex() == 0 ? 1 : 2;
		f.ccsid = ui->ccsidEdit->text().toInt();
		f.encoding = comboValue(ui->encodingCombo);
		f.pdEncoding = comboValue(ui->pdEncodingCombo);
		f.floatEncoding = comboValue(ui->floatEncodingCombo);
		f.format = ui->formatEdit->text().toStdString();
		f.flags = ui->flagsEdit->text().toInt();
		f.returnCode = ui->returnCodeEdit->text().toInt();
		f.compCode = ui->compCodeEdit->text().toInt();
		f.reason = ui->reasonEdit->text().toInt();
		f.uowControl = ui->uowControlEdit->text().toInt();
		f.getWaitInterval = ui->getWaitEdit->text().toInt();
		f.linkType = ui->linkTypeEdit->text().toInt();
		f.outputDataLength = ui->outputDataLenEdit->text().toInt();
		f.facilityKeepTime = ui->facilityKeepTimeEdit->text().toInt();
		f.adsDescriptor = ui->adsDescriptorEdit->text().toInt();
		f.conversationalTask = ui->convTaskEdit->text().toInt();
		f.taskEndStatus = ui->taskEndStatusEdit->text().toInt();
		f.facility = ui->facilityEdit->text().toStdString();
		f.function = ui->functionEdit->text().toStdString();
		f.abendCode = ui->abendCodeEdit->text().toStdString();
		f.authenticator = ui->authenticatorEdit->text().toStdString();
		f.replyToFormat = ui->replyToFmtEdit->text().toStdString();
		f.remoteSysId = ui->remoteSysIdEdit->text().toStdString();
		f.remoteTransId = ui->remoteTransIdEdit->text().toStdString();
		f.transactionId = ui->transactionIdEdit->text().toStdString();
		f.facilityLike = ui->facilityLikeEdit->text().toStdString();
		f.attentionId = ui->attentionIdEdit->text().toStdString();
		f.startCode = ui->startCodeEdit->text().toStdString();
		f.cancelCode = ui->cancelCodeEdit->text().toStdString();
		f.nextTransactionId = ui->nextTransIdEdit->text().toStdString();
		f.cursorPosition = ui->cursorPosEdit->text().toInt();
		f.errorOffset = ui->errorOffsetEdit->text().toInt();
		f.inputItem = ui->inputItemEdit->text().toInt();
		return f;
	}
}
