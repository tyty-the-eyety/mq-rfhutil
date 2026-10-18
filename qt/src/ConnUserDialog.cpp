#include "ConnUserDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace rfhqt
{
	const QStringList & ConnUserDialog::cipherSpecs()
	{
		static const QStringList specs = {
			"ANY_TLS13_OR_HIGHER", "ANY_TLS12_OR_HIGHER", "ANY_TLS13", "ANY_TLS12", "ANY",
			"TLS_AES_256_GCM_SHA384", "TLS_AES_128_GCM_SHA256", "TLS_CHACHA20_POLY1305_SHA256", "TLS_AES_128_CCM_SHA256",
			"TLS_AES_128_CCM_8_SHA256",
			"ECDHE_ECDSA_AES_256_GCM_SHA384", "ECDHE_RSA_AES_256_GCM_SHA384", "ECDHE_ECDSA_AES_128_GCM_SHA256",
			"ECDHE_RSA_AES_128_GCM_SHA256", "ECDHE_ECDSA_AES_256_CBC_SHA384", "ECDHE_RSA_AES_256_CBC_SHA384",
			"ECDHE_ECDSA_AES_128_CBC_SHA256", "ECDHE_RSA_AES_128_CBC_SHA256",
			"TLS_RSA_WITH_AES_256_GCM_SHA384", "TLS_RSA_WITH_AES_128_GCM_SHA256", "TLS_RSA_WITH_AES_256_CBC_SHA256",
			"TLS_RSA_WITH_AES_128_CBC_SHA256"
		};
		return specs;
	}

	QString ConnUserDialog::keyRepositoryFromFile(const QString & fileName)
	{
		// OnConnBrowse: a name that ends in ".kdb" loses it, the key repository being named without the extension
		static const QString extension = QStringLiteral(".kdb");
		if (fileName.size() > extension.size() && fileName.endsWith(extension))
		{
			return fileName.left(fileName.size() - extension.size());
		}
		return fileName;
	}

	namespace
	{
		QLineEdit * edit(const char * name, int maxLength, const QString & accessibleName, const QString & toolTip = QString())
		{
			QLineEdit * e = new QLineEdit;
			e->setObjectName(name);
			e->setMaxLength(maxLength);
			e->setAccessibleName(accessibleName);
			e->setToolTip(toolTip);
			return e;
		}
	}

	ConnUserDialog::ConnUserDialog(const rfh::ConnectOptions & current, QWidget * parent)
		: QDialog(parent), base(current)
	{
		setWindowTitle(QStringLiteral("Set Connection Parameters"));
		setObjectName(QStringLiteral("connUserDialog"));

		userId = edit("userId", MaxUserId, "User id", "User id for connection requests");
		password = edit("password", MaxPassword, "Password", "Password to be used for connection requests");
		password->setEchoMode(QLineEdit::Password);
		securityExit = edit("securityExit", MaxSecurityExit, "Security exit name", "Name of the security exit used when connecting to a queue manager");
		securityData = edit("securityData", MaxSecurityData, "Security exit data", "Data to be passed to the security exit named above");
		localAddress = edit("localAddress", MaxLocalAddress, "Local address", "Local address of adapter to use");
		keyRepository = edit("keyRepository", MaxKeyRepository, "Certificate store location", "Location of the SSL Certificate key file");
		resetCount = edit("resetCount", MaxResetCountDigits, "SSL reset count", "Number of bytes sent over channel before encryption key is changed.");
		resetCount->setValidator(new QIntValidator(0, 999999999, resetCount));
		peerName = edit("peerName", MaxPeerName, "SSL peer name");

		browseButton = new QPushButton(QStringLiteral("Br&owse"));
		browseButton->setObjectName(QStringLiteral("browse"));
		browseButton->setToolTip(QStringLiteral("Locate the SSL Certificate key file"));
		browseButton->setAccessibleName(QStringLiteral("Browse for the certificate store"));

		useSsl = new QCheckBox(QStringLiteral("&SSL"));
		useSsl->setObjectName(QStringLiteral("useSsl"));
		useSsl->setToolTip(QStringLiteral("Use SSL client connection rather than normal client connection"));
		useSsl->setAccessibleName(QStringLiteral("SSL"));
		validateClient = new QCheckBox(QStringLiteral("SS&L Client Validation"));
		validateClient->setObjectName(QStringLiteral("validateClient"));
		validateClient->setToolTip(QStringLiteral("Should client certificates be used to validate SSL clients?"));
		validateClient->setAccessibleName(QStringLiteral("SSL client validation"));
		fipsRequired = new QCheckBox(QStringLiteral("&FIPS Required"));
		fipsRequired->setObjectName(QStringLiteral("fipsRequired"));
		fipsRequired->setAccessibleName(QStringLiteral("FIPS required"));

		cipher = new QComboBox;
		cipher->setObjectName(QStringLiteral("cipher"));
		cipher->setEditable(true);
		cipher->setInsertPolicy(QComboBox::NoInsert);
		cipher->lineEdit()->setMaxLength(MaxCipher);
		cipher->addItem(QString());
		cipher->addItems(cipherSpecs());
		cipher->setToolTip(QStringLiteral("Cipher  Spec to use for data encyption for SSL client connection"));
		cipher->setAccessibleName(QStringLiteral("SSL cipher algorithm"));

		okButton = new QPushButton(QStringLiteral("OK"));
		okButton->setObjectName(QStringLiteral("ok"));
		okButton->setDefault(true);
		cancelButton = new QPushButton(QStringLiteral("Cancel"));
		cancelButton->setObjectName(QStringLiteral("cancel"));
		resetButton = new QPushButton(QStringLiteral("R&eset"));
		resetButton->setObjectName(QStringLiteral("reset"));
		resetButton->setToolTip(QStringLiteral("Delete the current user id and password used for connection requests"));
		resetButton->setAccessibleName(QStringLiteral("Reset"));

		auto label = [this](const QString & text, QWidget * buddy)
		{
			QLabel * l = new QLabel(text);
			l->setBuddy(buddy);
			return l;
		};
		QLabel * exitLabel = label(QStringLiteral("Security Exit Name"), securityExit);
		QLabel * dataLabel = label(QStringLiteral("Security Exit Data"), securityData);
		QLabel * addressLabel = label(QStringLiteral("Local Address"), localAddress);
		QLabel * storeLabel = label(QStringLiteral("Certificate Store Location"), keyRepository);
		QLabel * cipherLabel = label(QStringLiteral("SSL Cipher Algorithm"), cipher);
		QLabel * resetLabel = label(QStringLiteral("SSL Reset Count"), resetCount);
		QLabel * peerLabel = label(QStringLiteral("SSL Peer Name (server cert DN, optional)"), peerName);

		// the template order (rc:1043-1070): user id, password, exit, exit data, local address, store, the three boxes,
		// cipher, reset count, peer name, then the buttons
		QVBoxLayout * fields = new QVBoxLayout;
		fields->addWidget(label(QStringLiteral("User id"), userId));
		fields->addWidget(userId);
		fields->addWidget(label(QStringLiteral("Password"), password));
		fields->addWidget(password);
		fields->addWidget(exitLabel);
		fields->addWidget(securityExit);
		fields->addWidget(dataLabel);
		fields->addWidget(securityData);
		fields->addWidget(addressLabel);
		fields->addWidget(localAddress);
		fields->addWidget(storeLabel);
		QHBoxLayout * store = new QHBoxLayout;
		store->addWidget(keyRepository, 1);
		store->addWidget(browseButton);
		fields->addLayout(store);
		QHBoxLayout * boxes = new QHBoxLayout;
		boxes->addWidget(useSsl);
		boxes->addWidget(validateClient);
		boxes->addWidget(fipsRequired);
		boxes->addStretch(1);
		fields->addLayout(boxes);
		fields->addWidget(cipherLabel);
		fields->addWidget(cipher);
		fields->addWidget(resetLabel);
		fields->addWidget(resetCount);
		fields->addWidget(peerLabel);
		fields->addWidget(peerName);

		QVBoxLayout * buttons = new QVBoxLayout;
		buttons->addWidget(okButton);
		buttons->addWidget(cancelButton);
		buttons->addWidget(resetButton);
		buttons->addStretch(1);

		QHBoxLayout * root = new QHBoxLayout(this);
		root->addLayout(fields, 1);
		root->addLayout(buttons);

		clientOnly = { exitLabel, securityExit, dataLabel, securityData, addressLabel, localAddress, storeLabel, keyRepository,
		               browseButton, useSsl, validateClient, fipsRequired, cipherLabel, cipher, resetLabel, resetCount,
		               peerLabel, peerName };

		setTabOrder(userId, password);
		setTabOrder(password, securityExit);
		setTabOrder(securityExit, securityData);
		setTabOrder(securityData, localAddress);
		setTabOrder(localAddress, keyRepository);
		setTabOrder(keyRepository, useSsl);
		setTabOrder(useSsl, validateClient);
		setTabOrder(validateClient, fipsRequired);
		setTabOrder(fipsRequired, cipher);
		setTabOrder(cipher, resetCount);
		setTabOrder(resetCount, peerName);
		setTabOrder(peerName, okButton);
		setTabOrder(okButton, cancelButton);
		setTabOrder(cancelButton, resetButton);
		setTabOrder(resetButton, browseButton);

		connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
		connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
		connect(resetButton, &QPushButton::clicked, this, &ConnUserDialog::reset);
		connect(browseButton, &QPushButton::clicked, this, &ConnUserDialog::browse);

		// IDR_CONNUSER_ACCELERTOR (rc:224)
		connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), this), &QShortcut::activated, resetButton, &QPushButton::click);
		connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this), &QShortcut::activated, validateClient, &QCheckBox::toggle);
		connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_O), this), &QShortcut::activated, browseButton, &QPushButton::click);
		connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this), &QShortcut::activated, useSsl, &QCheckBox::toggle);

		load(current);
		setClientFieldsVisible(current.mode == rfh::Mode::Client);
		userId->setFocus();
	}

	ConnUserDialog::~ConnUserDialog()
	{
		password->clear();
		base.wipePassword();
	}

	void ConnUserDialog::setClientFieldsVisible(bool visible)
	{
		for (QWidget * w : clientOnly)
		{
			w->setVisible(visible);
		}
	}

	void ConnUserDialog::load(const rfh::ConnectOptions & o)
	{
		userId->setText(QString::fromStdString(o.userId));
		password->setText(QString::fromStdString(o.password));
		securityExit->setText(QString::fromStdString(o.securityExit));
		securityData->setText(QString::fromStdString(o.securityData));
		localAddress->setText(QString::fromStdString(o.localAddress));
		keyRepository->setText(QString::fromStdString(o.keyRepository));
		useSsl->setChecked(o.sslEnabled);
		validateClient->setChecked(o.sslClientValidation);
		fipsRequired->setChecked(o.fipsRequired);
		cipher->setCurrentIndex(-1);
		cipher->setEditText(QString::fromStdString(o.cipherSpec));
		resetCount->setText(QString::number(o.sslResetCount));
		peerName->setText(QString::fromStdString(o.sslPeerName));
	}

	rfh::ConnectOptions ConnUserDialog::values() const
	{
		rfh::ConnectOptions o = base;
		o.userId = userId->text().toStdString();
		o.password = password->text().toStdString();
		o.securityExit = securityExit->text().toStdString();
		o.securityData = securityData->text().toStdString();
		o.localAddress = localAddress->text().toStdString();
		o.keyRepository = keyRepository->text().toStdString();
		o.sslEnabled = useSsl->isChecked();
		o.sslClientValidation = validateClient->isChecked();
		o.fipsRequired = fipsRequired->isChecked();
		o.cipherSpec = cipher->currentText().trimmed().toStdString();
		o.sslResetCount = resetCount->text().toInt();		// atoi: empty or not a number is 0
		o.sslPeerName = peerName->text().toStdString();
		return o;
	}

	void ConnUserDialog::reset()
	{
		rfh::ConnectOptions cleared = base;
		cleared.wipePassword();
		cleared.userId.clear();
		cleared.securityExit.clear();
		cleared.securityData.clear();
		cleared.localAddress.clear();
		cleared.keyRepository.clear();
		cleared.sslEnabled = false;
		cleared.sslClientValidation = false;
		cleared.fipsRequired = false;
		cleared.cipherSpec.clear();
		cleared.sslResetCount = 0;
		cleared.sslPeerName.clear();
		load(cleared);
	}

	void ConnUserDialog::browse()
	{
		const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Certificate store"), QString());
		if (!file.isEmpty())
		{
			keyRepository->setText(keyRepositoryFromFile(file));
		}
	}
}
