#include "OtherTab.h"
#include "ui_OtherTab.h"

#include <QPlainTextEdit>

namespace rfhqt
{
	OtherTab::OtherTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::OtherTab)
	{
		ui->setupUi(this);
	}

	OtherTab::~OtherTab() = default;

	void OtherTab::setXml(const std::string & xml)
	{
		ui->xmlEdit->setPlainText(QString::fromStdString(xml));
	}
}
