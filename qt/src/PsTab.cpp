#include "PsTab.h"
#include "ui_PsTab.h"

#include <QLineEdit>

namespace rfhqt
{
	PsTab::PsTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::PsTab)
	{
		ui->setupUi(this);
		wire();
	}

	PsTab::~PsTab() = default;

	void PsTab::wire()
	{
		const auto editSlot = [this]() { emit fieldsEdited(); };
		connect(ui->topicEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->subNameEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->subQmEdit, &QLineEdit::textEdited, this, editSlot);
		connect(ui->pubQueueEdit, &QLineEdit::textEdited, this, editSlot);
	}

	void PsTab::setFields(const PsSettings & s)
	{
		ui->topicEdit->setText(QString::fromStdString(s.topic));
		ui->subNameEdit->setText(QString::fromStdString(s.subName));
		ui->subQmEdit->setText(QString::fromStdString(s.subQm));
		ui->pubQueueEdit->setText(QString::fromStdString(s.pubQueue));
	}

	PsSettings PsTab::fields() const
	{
		PsSettings s;
		s.topic = ui->topicEdit->text().toStdString();
		s.subName = ui->subNameEdit->text().toStdString();
		s.subQm = ui->subQmEdit->text().toStdString();
		s.pubQueue = ui->pubQueueEdit->text().toStdString();
		return s;
	}
}
