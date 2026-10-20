#include "PscrTab.h"
#include "ui_PscrTab.h"

#include <QCheckBox>
#include <QPlainTextEdit>

namespace rfhqt
{
	PscrTab::PscrTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::PscrTab)
	{
		ui->setupUi(this);
		wire();
	}

	PscrTab::~PscrTab() = default;

	void PscrTab::wire()
	{
		connect(ui->includedCheck, &QCheckBox::toggled, this, [this]() { emit fieldsEdited(); });
		connect(ui->xmlEdit, &QPlainTextEdit::textChanged, this, [this]() { emit fieldsEdited(); });
	}

	void PscrTab::setFields(const std::string & xml, bool included)
	{
		ui->includedCheck->setChecked(included);
		ui->xmlEdit->setPlainText(QString::fromStdString(xml));
	}

	std::string PscrTab::xml() const
	{
		return ui->xmlEdit->toPlainText().toStdString();
	}

	bool PscrTab::isIncluded() const
	{
		return ui->includedCheck->isChecked();
	}
}
