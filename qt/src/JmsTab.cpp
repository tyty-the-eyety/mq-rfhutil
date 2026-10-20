#include "JmsTab.h"
#include "ui_JmsTab.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>

namespace rfhqt
{
	JmsTab::JmsTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::JmsTab)
	{
		ui->setupUi(this);
		wire();
	}

	JmsTab::~JmsTab() = default;

	void JmsTab::wire()
	{
		const auto editSlot = [this]() { emit fieldsEdited(); };
		connect(ui->includedCheck, &QCheckBox::toggled, this, editSlot);
		connect(ui->dstEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->rtoEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->expEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->cidEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->gidEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->seqEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->tmsEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->priEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->dlvEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->userDefEdit, &QPlainTextEdit::textChanged, this, editSlot);
	}

	void JmsTab::setFields(const rfh::Rfh2JmsFields & f, bool included)
	{
		ui->includedCheck->setChecked(included);
		ui->dstEdit->setText(QString::fromStdString(f.dst));
		ui->rtoEdit->setText(QString::fromStdString(f.rto));
		ui->expEdit->setText(QString::fromStdString(f.expiration));
		ui->cidEdit->setText(QString::fromStdString(f.correlId));
		ui->gidEdit->setText(QString::fromStdString(f.groupId));
		ui->seqEdit->setText(QString::fromStdString(f.sequence));
		ui->tmsEdit->setText(QString::fromStdString(f.timestamp));
		ui->priEdit->setText(QString::fromStdString(f.priority));
		ui->dlvEdit->setText(QString::fromStdString(f.deliveryMode));
		ui->userDefEdit->setPlainText(QString::fromStdString(f.userDef));
	}

	rfh::Rfh2JmsFields JmsTab::fields() const
	{
		rfh::Rfh2JmsFields f;
		f.dst = ui->dstEdit->text().toStdString();
		f.rto = ui->rtoEdit->text().toStdString();
		f.expiration = ui->expEdit->text().toStdString();
		f.correlId = ui->cidEdit->text().toStdString();
		f.groupId = ui->gidEdit->text().toStdString();
		f.sequence = ui->seqEdit->text().toStdString();
		f.timestamp = ui->tmsEdit->text().toStdString();
		f.priority = ui->priEdit->text().toStdString();
		f.deliveryMode = ui->dlvEdit->text().toStdString();
		f.userDef = ui->userDefEdit->toPlainText().toStdString();
		return f;
	}

	bool JmsTab::isIncluded() const
	{
		return ui->includedCheck->isChecked();
	}
}
