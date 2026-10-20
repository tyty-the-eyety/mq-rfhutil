#include "ConnTab.h"
#include "ui_ConnTab.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSpinBox>

#include "AppSettings.h"

namespace rfhqt
{
	ConnTab::ConnTab(AppSettings & settings, QWidget * parent)
		: QWidget(parent), appSettings(settings), ui(new Ui::ConnTab)
	{
		ui->setupUi(this);
		wire();
		load();
	}

	ConnTab::~ConnTab() = default;

	void ConnTab::wire()
	{
		connect(ui->buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked,
		        this, &ConnTab::apply);
		connect(ui->buttonBox->button(QDialogButtonBox::Reset), &QPushButton::clicked,
		        this, &ConnTab::reset);
	}

	void ConnTab::load()
	{
		const rfh::ConnectOptions opts = appSettings.connection();
		ui->heartbeatSpin->setValue(opts.heartbeat);
		ui->sslEnabledCheck->setChecked(opts.sslEnabled);
		ui->cipherSpecEdit->setText(QString::fromStdString(opts.cipherSpec));
		ui->keyRepositoryEdit->setText(QString::fromStdString(opts.keyRepository));
		ui->sslPeerNameEdit->setText(QString::fromStdString(opts.sslPeerName));
		ui->sslResetCountSpin->setValue(opts.sslResetCount);
	}

	void ConnTab::apply()
	{
		rfh::ConnectOptions opts = appSettings.connection();
		opts.heartbeat = ui->heartbeatSpin->value();
		opts.sslEnabled = ui->sslEnabledCheck->isChecked();
		opts.cipherSpec = ui->cipherSpecEdit->text().toStdString();
		opts.keyRepository = ui->keyRepositoryEdit->text().toStdString();
		opts.sslPeerName = ui->sslPeerNameEdit->text().toStdString();
		opts.sslResetCount = ui->sslResetCountSpin->value();
		appSettings.setConnection(opts);
		emit settingsApplied();
	}

	void ConnTab::reset()
	{
		load();
	}
}
