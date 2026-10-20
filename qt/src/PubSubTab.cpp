#include "PubSubTab.h"
#include "ui_PubSubTab.h"

#include <QCheckBox>
#include <QPlainTextEdit>

namespace rfhqt
{
	PubSubTab::PubSubTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::PubSubTab)
	{
		ui->setupUi(this);
		wire();
	}

	PubSubTab::~PubSubTab() = default;

	void PubSubTab::wire()
	{
		connect(ui->includedCheck, &QCheckBox::toggled, this, [this]() { emit fieldsEdited(); });
		connect(ui->xmlEdit, &QPlainTextEdit::textChanged, this, [this]() { emit fieldsEdited(); });
	}

	void PubSubTab::setFields(const std::string & xml, bool included)
	{
		ui->includedCheck->setChecked(included);
		ui->xmlEdit->setPlainText(QString::fromStdString(xml));
	}

	std::string PubSubTab::xml() const
	{
		return ui->xmlEdit->toPlainText().toStdString();
	}

	bool PubSubTab::isIncluded() const
	{
		return ui->includedCheck->isChecked();
	}
}
