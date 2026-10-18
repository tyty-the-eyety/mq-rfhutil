// AppSettings.h - the settings file of the Qt application (QSettings, INI format).
//
// What it replaces: the Windows registry values under RFHUtilLastUsed / RFHUtilcLastUsed (rfhutil.cpp
// SaveLastUsedQNames 1575-1619 and GetLastUsedQNames 1621-1720): the last queue manager, queue, user id, file
// path, TLS values, security exit and local address, and the last 30 client queue managers
// (RFHUTIL_LAST_CLIENT_MAX, writeQMgr2Registry 2108-2165). Queue lists per queue manager, the last directories,
// the window geometry and the display options are additions of this program.
//
// PASSWORDS ARE NEVER STORED (owner decision). Windows keeps one, DPAPI encrypted, when "Save password" is on
// (rfhutil.cpp 1589-1599); this program does not: no setter takes a password, setConnection() leaves out the
// password and the security exit user data (which may carry a secret) and a CCDT URL that contains a credential,
// and every write goes through one funnel that refuses a key whose name looks like a password (isSecretKey).
//
// Where: the file given to the constructor (tests pass a temporary file and never touch the real configuration),
// by default $XDG_CONFIG_HOME/rfhutil/rfhutil.conf, else ~/.config/rfhutil/rfhutil.conf.
//
// Reading is forgiving, writing is careful:
//   - A value that is missing, unparsable or out of range gives its documented default and one warning, which
//     warnings()/takeWarnings() hand to the UI. Nothing here throws or crashes on a bad file.
//   - Unknown keys and sections are kept: QSettings rewrites the whole file with everything it read.
//   - A file that is not valid INI is moved to "<file>.corrupt" before the first save, so nothing is lost, and
//     a fresh file is started. A file that cannot be read at all (permissions) is never overwritten: saves are
//     refused with a warning.
//   - Every setter writes at once. QSettings replaces the file atomically (QSaveFile: a temporary file in the
//     same directory, renamed over the original), so a failed write leaves the old file intact; the failure
//     is reported as a warning ("not saved"). The file is made owner only (0600).
//
// Defaults: no recent names, no directories, no geometry, display code page 1252 (DefaultCodePage), system
// queues hidden, and a default-constructed rfh::ConnectOptions for the connection.
//
// Threading: GUI thread only (QSettings is not shared with the worker).
#pragma once

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>

#include "rfh_mq_session.h"

namespace rfhqt
{
	class AppSettings
	{
	public:
		static constexpr int MaxRecent = 30;			// RFHUTIL_LAST_CLIENT_MAX; also the limit per queue manager
		static constexpr int DefaultCodePage = 1252;	// the Windows default ANSI code page (rfh_get_acp)

		// An empty path means defaultFilePath().
		explicit AppSettings(const QString & filePath = QString());
		~AppSettings();

		AppSettings(const AppSettings &) = delete;
		AppSettings & operator=(const AppSettings &) = delete;

		static QString defaultFilePath();
		QString filePath() const { return path; }

		// Most recent first, no duplicates (names are case sensitive, as MQ's are), at most MaxRecent.
		// An empty name is not remembered.
		QStringList recentQueueManagers() const;
		void addQueueManager(const QString & qm);

		// Per queue manager: most recent first, at most MaxRecent for each of at most MaxRecent queue managers.
		QStringList recentQueues(const QString & qm) const;
		void addQueue(const QString & qm, const QString & queue);

		// kind is "open", "save" or "copybook"; any other kind is refused with a warning.
		QString lastDirectory(const QString & kind) const;
		void setLastDirectory(const QString & kind, const QString & dir);

		// At most 4096 bytes (QMainWindow::saveGeometry is about 50); a larger stored value is ignored.
		QByteArray windowGeometry() const;
		void setWindowGeometry(const QByteArray & geometry);

		// 1 to 65535; anything else gives DefaultCodePage.
		int displayCodePage() const;
		void setDisplayCodePage(int codePage);

		bool showSystemQueues() const;
		void setShowSystemQueues(bool show);

		// The connection dialog values Windows remembers. connection() never has a password or security data;
		// out of range values (heartbeat 0-999999, sslResetCount 0-999999999, unknown mode) give the defaults.
		rfh::ConnectOptions connection() const;
		void setConnection(const rfh::ConnectOptions & options);

		// Generic access for settings of later tabs. setValue refuses a key that isSecretKey() recognises and
		// returns false; otherwise it returns whether the value was saved.
		QVariant value(const QString & key, const QVariant & defaultValue = QVariant()) const;
		bool setValue(const QString & key, const QVariant & value);

		// True when the last segment of the key names a password, secret or token (case insensitive): contains
		// "pass", "pwd", "secret" or "token", or ends in "pw".
		static bool isSecretKey(const QString & key);

		// The problems met so far, in order, each once. takeWarnings() also forgets them.
		QStringList warnings() const { return pending; }
		QStringList takeWarnings();

	private:
		void warn(const QString & text) const;
		bool prepareWrite();
		bool commit();
		bool readBool(const QString & key, bool defaultValue) const;
		int readInt(const QString & key, int defaultValue, int minimum, int maximum) const;

		struct QueueEntry
		{
			QString qm;
			QString queue;
		};
		QList<QueueEntry> readQueues() const;
		void writeQueues(const QList<QueueEntry> & entries);

		QString path;
		std::unique_ptr<QSettings> store;
		bool unreadable = false;			// the file exists and cannot be read: never overwrite it
		bool corrupt = false;				// the file is not valid INI: keep it as .corrupt before the first save
		mutable QStringList pending;
	};
}
