#include "DlqTab.h"
#include "ui_DlqTab.h"

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

	DlqTab::DlqTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::DlqTab)
	{
		ui->setupUi(this);
		populateCombos();
		wire();
	}

	DlqTab::~DlqTab() = default;

	void DlqTab::populateCombos()
	{
		populateNumericCombo(ui->encodingCombo);
		populateNumericCombo(ui->pdEncodingCombo);
		populateFloatCombo(ui->floatEncodingCombo);
	}

	void DlqTab::wire()
	{
		connect(ui->includedCheck, &QCheckBox::toggled, this, &DlqTab::editedByUser);
		const auto editSlot = [this]() { editedByUser(); };
		connect(ui->reasonEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->destQueueEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->destQmEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->putApplTypeEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->putApplNameEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->ccsidEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->encodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->pdEncodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->floatEncodingCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, editSlot);
		connect(ui->formatEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->putDateEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->putTimeEdit, &QLineEdit::textEdited, this, editSlot);
	}

	void DlqTab::editedByUser()
	{
		emit fieldsEdited();
	}

	void DlqTab::setFields(const rfh::DlqFields & f)
	{
		ui->includedCheck->setChecked(f.included);
		ui->reasonEdit->setText(QString::number(f.reason));
		ui->destQueueEdit->setText(QString::fromStdString(f.destQueueName));
		ui->destQmEdit->setText(QString::fromStdString(f.destQueueManagerName));
		ui->putApplTypeEdit->setText(QString::number(f.putApplType));
		ui->putApplNameEdit->setText(QString::fromStdString(f.putApplName));
		ui->ccsidEdit->setText(QString::number(f.ccsid));
		setCombo(ui->encodingCombo, f.encoding);
		setCombo(ui->pdEncodingCombo, f.pdEncoding);
		setCombo(ui->floatEncodingCombo, f.floatEncoding);
		ui->formatEdit->setText(QString::fromStdString(f.format));
		ui->putDateEdit->setText(QString::fromStdString(f.putDate));
		ui->putTimeEdit->setText(QString::fromStdString(f.putTime));
	}

	rfh::DlqFields DlqTab::fields() const
	{
		rfh::DlqFields f;
		f.included = ui->includedCheck->isChecked();
		f.reason = ui->reasonEdit->text().toInt();
		f.destQueueName = ui->destQueueEdit->text().toStdString();
		f.destQueueManagerName = ui->destQmEdit->text().toStdString();
		f.putApplType = ui->putApplTypeEdit->text().toInt();
		f.putApplName = ui->putApplNameEdit->text().toStdString();
		f.ccsid = ui->ccsidEdit->text().toInt();
		f.encoding = comboValue(ui->encodingCombo);
		f.pdEncoding = comboValue(ui->pdEncodingCombo);
		f.floatEncoding = comboValue(ui->floatEncodingCombo);
		f.format = ui->formatEdit->text().toStdString();
		f.putDate = ui->putDateEdit->text().toStdString();
		f.putTime = ui->putTimeEdit->text().toStdString();
		return f;
	}
}
