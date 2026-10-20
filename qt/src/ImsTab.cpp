#include "ImsTab.h"
#include "ui_ImsTab.h"

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

	ImsTab::ImsTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::ImsTab)
	{
		ui->setupUi(this);
		populateCombos();
		wire();
	}

	ImsTab::~ImsTab() = default;

	void ImsTab::populateCombos()
	{
		populateNumericCombo(ui->encodingCombo);
		populateNumericCombo(ui->pdEncodingCombo);
		populateFloatCombo(ui->floatEncodingCombo);
	}

	void ImsTab::wire()
	{
		connect(ui->includedCheck, &QCheckBox::toggled, this, &ImsTab::editedByUser);
		const auto editSlot = [this]() { editedByUser(); };
		connect(ui->ccsidEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->encodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->pdEncodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->floatEncodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->formatEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->flagsEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->lTermEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->mapNameEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->replyToFmtEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->authenticatorEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->commitModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->transStateCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->securityScopeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->passExpirationCheck, &QCheckBox::toggled, this, &ImsTab::editedByUser);
		connect(ui->addLengthCheck, &QCheckBox::toggled, this, &ImsTab::editedByUser);
		connect(ui->noReplyFmtCheck, &QCheckBox::toggled, this, &ImsTab::editedByUser);
	}

	void ImsTab::editedByUser()
	{
		emit fieldsEdited();
	}

	void ImsTab::setFields(const rfh::ImsFields & f)
	{
		ui->includedCheck->setChecked(f.included);
		ui->ccsidEdit->setText(QString::number(f.ccsid));
		setCombo(ui->encodingCombo, f.encoding);
		setCombo(ui->pdEncodingCombo, f.pdEncoding);
		setCombo(ui->floatEncodingCombo, f.floatEncoding);
		ui->formatEdit->setText(QString::fromStdString(f.format));
		ui->flagsEdit->setText(QString::number(f.flags));
		ui->lTermEdit->setText(QString::fromStdString(f.lTerm));
		ui->mapNameEdit->setText(QString::fromStdString(f.mapName));
		ui->replyToFmtEdit->setText(QString::fromStdString(f.replyToFormat));
		ui->authenticatorEdit->setText(QString::fromStdString(f.authenticator));
		ui->commitModeCombo->setCurrentIndex(f.commitMode);
		ui->transStateCombo->setCurrentIndex(f.transState);
		ui->securityScopeCombo->setCurrentIndex(f.securityScope);
		ui->passExpirationCheck->setChecked(f.passExpiration);
		ui->addLengthCheck->setChecked(f.addLength);
		ui->noReplyFmtCheck->setChecked(f.noReplyFormat);
	}

	rfh::ImsFields ImsTab::fields() const
	{
		rfh::ImsFields f;
		f.included = ui->includedCheck->isChecked();
		f.ccsid = ui->ccsidEdit->text().toInt();
		f.encoding = comboValue(ui->encodingCombo);
		f.pdEncoding = comboValue(ui->pdEncodingCombo);
		f.floatEncoding = comboValue(ui->floatEncodingCombo);
		f.format = ui->formatEdit->text().toStdString();
		f.flags = ui->flagsEdit->text().toInt();
		f.lTerm = ui->lTermEdit->text().toStdString();
		f.mapName = ui->mapNameEdit->text().toStdString();
		f.replyToFormat = ui->replyToFmtEdit->text().toStdString();
		f.authenticator = ui->authenticatorEdit->text().toStdString();
		f.commitMode = ui->commitModeCombo->currentIndex();
		f.transState = ui->transStateCombo->currentIndex();
		f.securityScope = ui->securityScopeCombo->currentIndex();
		f.passExpiration = ui->passExpirationCheck->isChecked();
		f.addLength = ui->addLengthCheck->isChecked();
		f.noReplyFormat = ui->noReplyFmtCheck->isChecked();
		return f;
	}
}
