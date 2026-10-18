// ConnUserDialog.h - the Set Conn Id dialog (Windows IDD_USERID, "Set Connection Parameters", ConnUser.cpp, rc:1040).
//
// It edits the connection values that are not the queue manager name: user id and password, and for a client
// connection the TLS values, the security exit and the local address. The values live in a rfh::ConnectOptions
// that the Main tab keeps in memory only: this dialog has no "Save password" box (Windows stores the password with
// DPAPI when it is on; the owner decided that passwords are never stored), and it never writes the password anywhere.
// A QLineEdit keeps its text as a QString, which cannot be overwritten in place; the dialog empties the edit when it
// closes and values() hands the password out only to its caller.
//
// Differences from Windows, on purpose: Local Address is read back (General::OnSetConnUser 2347-2412 copies every
// dialog value except m_local_address, so a local address typed in the dialog is lost on Windows); the first cipher
// item is empty instead of one blank; there is no Save password box.
//
// A bindings connection ignores the TLS group, the security exit and the local address (ConnectOptions), so the
// dialog hides them in bindings mode as the Windows server build does (ConnUser.cpp OnInitDialog, #ifndef MQCLIENT).
//
// Threading: GUI thread only.
#pragma once

#include <QDialog>

#include "rfh_mq_session.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace rfhqt
{
	class ConnUserDialog : public QDialog
	{
		Q_OBJECT

	public:
		// Windows DDV_MaxChars of ConnUser.cpp:62-79.
		static constexpr int MaxUserId = 255;
		static constexpr int MaxPassword = 1024;
		static constexpr int MaxSecurityExit = 128;
		static constexpr int MaxSecurityData = 32;
		static constexpr int MaxLocalAddress = 48;
		static constexpr int MaxKeyRepository = 254;
		static constexpr int MaxCipher = 32;
		static constexpr int MaxPeerName = 1024;
		static constexpr int MaxResetCountDigits = 9;		// ConnectOptions::sslResetCount is 0 to 999999999

		// The MQ cipher specs of ConnUser.cpp OnDropdownConnSslCipher, in its order, after the empty entry.
		static const QStringList & cipherSpecs();

		// `current` supplies the values to show; everything of it that the dialog does not edit is kept by values().
		ConnUserDialog(const rfh::ConnectOptions & current, QWidget * parent = nullptr);
		~ConnUserDialog() override;

		// `current` with the dialog's values applied. The caller wipes the password of the copy it gets.
		rfh::ConnectOptions values() const;

		// Windows OnConnReset: user id, password, TLS values, security exit and local address are emptied.
		void reset();

		// The key repository chosen with Browse: the ".kdb" extension is removed (OnConnBrowse).
		static QString keyRepositoryFromFile(const QString & fileName);

	private slots:
		void browse();

	private:
		void load(const rfh::ConnectOptions & options);
		void setClientFieldsVisible(bool visible);

		rfh::ConnectOptions base;
		QLineEdit * userId;
		QLineEdit * password;
		QLineEdit * securityExit;
		QLineEdit * securityData;
		QLineEdit * localAddress;
		QLineEdit * keyRepository;
		QPushButton * browseButton;
		QCheckBox * useSsl;
		QCheckBox * validateClient;
		QCheckBox * fipsRequired;
		QComboBox * cipher;
		QLineEdit * resetCount;
		QLineEdit * peerName;
		QPushButton * okButton;
		QPushButton * cancelButton;
		QPushButton * resetButton;
		QList<QWidget *> clientOnly;
	};
}
