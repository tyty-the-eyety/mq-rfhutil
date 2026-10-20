#include "UsrTab.h"
#include "ui_UsrTab.h"

#include <QCheckBox>
#include <QPlainTextEdit>

namespace rfhqt
{
	UsrTab::UsrTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::UsrTab)
	{
		ui->setupUi(this);
		wire();
	}

	UsrTab::~UsrTab() = default;

	void UsrTab::wire()
	{
		connect(ui->includedCheck, &QCheckBox::toggled, this, [this]() { emit fieldsEdited(); });
		connect(ui->xmlEdit, &QPlainTextEdit::textChanged, this, [this]() { emit fieldsEdited(); });
	}

	void UsrTab::setFields(const std::string & xml, bool included)
	{
		ui->includedCheck->setChecked(included);
		ui->xmlEdit->setPlainText(QString::fromStdString(xml));
	}

	std::string UsrTab::xml() const
	{
		return ui->xmlEdit->toPlainText().toStdString();
	}

	bool UsrTab::isIncluded() const
	{
		return ui->includedCheck->isChecked();
	}
}
