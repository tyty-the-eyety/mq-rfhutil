#include "UsrPropTab.h"
#include "ui_UsrPropTab.h"

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QPushButton>

namespace rfhqt
{
	UsrPropTab::UsrPropTab(AppDocument & document, QWidget * parent)
		: QWidget(parent), doc(document), ui(new Ui::UsrPropTab)
	{
		ui->setupUi(this);
		ui->propTable->horizontalHeader()->setStretchLastSection(true);
		wire();
	}

	UsrPropTab::~UsrPropTab() = default;

	void UsrPropTab::wire()
	{
		connect(ui->addButton, &QPushButton::clicked, this, &UsrPropTab::addRow);
		connect(ui->removeButton, &QPushButton::clicked, this, &UsrPropTab::removeRow);
		connect(ui->propTable, &QTableWidget::itemChanged, this, &UsrPropTab::tableEdited);
	}

	void UsrPropTab::addRow()
	{
		const int row = ui->propTable->rowCount();
		ui->propTable->insertRow(row);
		ui->propTable->setItem(row, 0, new QTableWidgetItem(QString()));
		ui->propTable->setItem(row, 1, new QTableWidgetItem(QStringLiteral("string")));
		ui->propTable->setItem(row, 2, new QTableWidgetItem(QString()));
		ui->propTable->editItem(ui->propTable->item(row, 0));
		emit fieldsEdited();
	}

	void UsrPropTab::removeRow()
	{
		const int row = ui->propTable->currentRow();
		if (row >= 0)
		{
			ui->propTable->removeRow(row);
			emit fieldsEdited();
		}
	}

	void UsrPropTab::tableEdited()
	{
		emit fieldsEdited();
	}

	void UsrPropTab::setProperties(const std::vector<UserProperty> & props)
	{
		QSignalBlocker blocker(ui->propTable);
		ui->propTable->setRowCount(0);
		for (const UserProperty & p : props)
		{
			const int row = ui->propTable->rowCount();
			ui->propTable->insertRow(row);
			ui->propTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(p.name)));
			ui->propTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(p.type)));
			ui->propTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(p.value)));
		}
	}

	std::vector<UserProperty> UsrPropTab::properties() const
	{
		std::vector<UserProperty> result;
		for (int row = 0; row < ui->propTable->rowCount(); ++row)
		{
			auto cell = [&](int col) -> std::string {
				const QTableWidgetItem * item = ui->propTable->item(row, col);
				return item ? item->text().toStdString() : std::string{};
			};
			UserProperty p;
			p.name = cell(0);
			p.type = cell(1);
			p.value = cell(2);
			result.push_back(p);
		}
		return result;
	}
}
