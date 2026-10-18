#include "AppSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <sys/stat.h>

#include <filesystem>

namespace rfhqt
{
	namespace
	{
		const int MaxGeometryBytes = 4096;
		const char * const DirectoryKinds[] = { "open", "save", "copybook" };

		bool knownKind(const QString & kind)
		{
			for (const char * k : DirectoryKinds)
			{
				if (kind == QLatin1String(k))
				{
					return true;
				}
			}
			return false;
		}

		// A CCDT URL may carry "user:password@host" (the core says so in describeConnect).
		bool urlCarriesCredential(const QString & text)
		{
			const QUrl url(text);
			return !url.password().isEmpty() || (url.userInfo().contains(QLatin1Char(':')));
		}
	}

	QString AppSettings::defaultFilePath()
	{
		const QString xdg = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
		const QString base = (!xdg.isEmpty() && QDir::isAbsolutePath(xdg)) ? xdg : QDir::homePath() + "/.config";
		return base + "/rfhutil/rfhutil.conf";
	}

	AppSettings::AppSettings(const QString & filePath)
		: path(filePath.isEmpty() ? defaultFilePath() : filePath)
	{
		if (QFileInfo::exists(path))
		{
			QFile probe(path);
			if (!probe.open(QIODevice::ReadOnly))
			{
				unreadable = true;
				warn("The settings file " + path + " cannot be read (" + probe.errorString() + "); defaults are used and changes are not saved");
			}
		}
		store.reset(new QSettings(path, QSettings::IniFormat));
		if (!unreadable && store->status() == QSettings::FormatError)
		{
			corrupt = true;
			warn("The settings file " + path + " is not valid; defaults are used and the file is kept as " + path + ".corrupt when settings are next saved");
		}
	}

	AppSettings::~AppSettings() = default;

	void AppSettings::warn(const QString & text) const
	{
		if (!pending.contains(text))
		{
			pending.append(text);
		}
	}

	QStringList AppSettings::takeWarnings()
	{
		QStringList out = pending;
		pending.clear();
		return out;
	}

	bool AppSettings::isSecretKey(const QString & key)
	{
		const QString last = key.mid(key.lastIndexOf(QLatin1Char('/')) + 1).toLower();
		return last.contains("pass") || last.contains("pwd") || last.contains("secret")
			|| last.contains("token") || last.endsWith("pw");
	}

	// Makes the file writable for the next change; false (with a warning) when it must not be written.
	bool AppSettings::prepareWrite()
	{
		if (unreadable)
		{
			warn("The settings were not saved: " + path + " cannot be read, so it is not replaced");
			return false;
		}
		if (corrupt)
		{
			const QString kept = path + ".corrupt";
			QFile::remove(kept);
			store.reset();
			if (!QFile::rename(path, kept))
			{
				store.reset(new QSettings(path, QSettings::IniFormat));
				warn("The settings were not saved: the invalid file " + path + " could not be moved to " + kept);
				return false;
			}
			store.reset(new QSettings(path, QSettings::IniFormat));
			corrupt = false;
			warn("The invalid settings file was kept as " + kept);
		}

		// The directory is the user's own: owner only when this creates it.
		const QString dir = QFileInfo(path).absolutePath();
		std::error_code ec;
		if (!std::filesystem::is_directory(dir.toStdString(), ec))
		{
			std::filesystem::create_directories(dir.toStdString(), ec);
			if (!ec)
			{
				::chmod(QFile::encodeName(dir).constData(), 0700);
			}
		}
		return true;
	}

	bool AppSettings::commit()
	{
		store->sync();
		if (store->status() != QSettings::NoError)
		{
			const QString why = (store->status() == QSettings::AccessError) ? "the file or its directory is not writable" : "the file is not valid";
			warn("The settings were not saved to " + path + ": " + why);
			// QSettings keeps the failure status for good: start again from what is on disk
			store.reset(new QSettings(path, QSettings::IniFormat));
			return false;
		}
		::chmod(QFile::encodeName(path).constData(), 0600);
		return true;
	}

	QVariant AppSettings::value(const QString & key, const QVariant & defaultValue) const
	{
		return store->value(key, defaultValue);
	}

	bool AppSettings::setValue(const QString & key, const QVariant & newValue)
	{
		if (isSecretKey(key))
		{
			warn("The setting " + key + " looks like a password and is never stored");
			return false;
		}
		if (!prepareWrite())
		{
			return false;
		}
		store->setValue(key, newValue);
		return commit();
	}

	bool AppSettings::readBool(const QString & key, bool defaultValue) const
	{
		if (!store->contains(key))
		{
			return defaultValue;
		}
		const QString text = store->value(key).toString().trimmed().toLower();
		if (text == "true" || text == "1")
		{
			return true;
		}
		if (text == "false" || text == "0")
		{
			return false;
		}
		warn("The setting " + key + " is \"" + text + "\", not true or false; the default is used");
		return defaultValue;
	}

	int AppSettings::readInt(const QString & key, int defaultValue, int minimum, int maximum) const
	{
		if (!store->contains(key))
		{
			return defaultValue;
		}
		bool ok = false;
		const qlonglong n = store->value(key).toString().trimmed().toLongLong(&ok);
		if (!ok || n < minimum || n > maximum)
		{
			warn("The setting " + key + " is \"" + store->value(key).toString() + "\", not a number from "
				+ QString::number(minimum) + " to " + QString::number(maximum) + "; the default " + QString::number(defaultValue) + " is used");
			return defaultValue;
		}
		return (int)n;
	}

	// ---- recent queue managers --------------------------------------------------------------------------

	QStringList AppSettings::recentQueueManagers() const
	{
		QStringList names;
		const int count = qMin(store->beginReadArray("RecentQueueManagers"), MaxRecent);
		for (int i = 0; i < count; i++)
		{
			store->setArrayIndex(i);
			const QString name = store->value("name").toString();
			if (!name.isEmpty() && !names.contains(name))
			{
				names.append(name);
			}
		}
		store->endArray();
		return names;
	}

	void AppSettings::addQueueManager(const QString & qm)
	{
		if (qm.isEmpty() || !prepareWrite())
		{
			return;
		}
		QStringList names = recentQueueManagers();
		names.removeAll(qm);
		names.prepend(qm);
		while (names.size() > MaxRecent)
		{
			names.removeLast();
		}
		store->remove("RecentQueueManagers");
		store->beginWriteArray("RecentQueueManagers", names.size());
		for (int i = 0; i < names.size(); i++)
		{
			store->setArrayIndex(i);
			store->setValue("name", names[i]);
		}
		store->endArray();
		commit();
	}

	QList<AppSettings::QueueEntry> AppSettings::readQueues() const
	{
		QList<QueueEntry> entries;
		const int count = qMin(store->beginReadArray("RecentQueues"), MaxRecent * MaxRecent);
		for (int i = 0; i < count; i++)
		{
			store->setArrayIndex(i);
			QueueEntry e;
			e.qm = store->value("qm").toString();
			e.queue = store->value("queue").toString();
			if (!e.qm.isEmpty() && !e.queue.isEmpty())
			{
				entries.append(e);
			}
		}
		store->endArray();
		return entries;
	}

	void AppSettings::writeQueues(const QList<QueueEntry> & entries)
	{
		store->remove("RecentQueues");
		store->beginWriteArray("RecentQueues", entries.size());
		for (int i = 0; i < entries.size(); i++)
		{
			store->setArrayIndex(i);
			store->setValue("qm", entries[i].qm);
			store->setValue("queue", entries[i].queue);
		}
		store->endArray();
	}

	QStringList AppSettings::recentQueues(const QString & qm) const
	{
		QStringList queues;
		for (const QueueEntry & e : readQueues())
		{
			if (e.qm == qm && !queues.contains(e.queue) && queues.size() < MaxRecent)
			{
				queues.append(e.queue);
			}
		}
		return queues;
	}

	void AppSettings::addQueue(const QString & qm, const QString & queue)
	{
		if (qm.isEmpty() || queue.isEmpty() || !prepareWrite())
		{
			return;
		}
		QList<QueueEntry> entries = readQueues();
		for (int i = entries.size() - 1; i >= 0; i--)
		{
			if (entries[i].qm == qm && entries[i].queue == queue)
			{
				entries.removeAt(i);
			}
		}
		entries.prepend(QueueEntry{ qm, queue });

		// at most MaxRecent per queue manager, and lists for at most MaxRecent queue managers
		QList<QueueEntry> kept;
		QStringList managers;
		QHash<QString, int> perManager;
		for (const QueueEntry & e : entries)
		{
			if (!managers.contains(e.qm))
			{
				if (managers.size() >= MaxRecent)
				{
					continue;
				}
				managers.append(e.qm);
			}
			if (perManager[e.qm] >= MaxRecent)
			{
				continue;
			}
			perManager[e.qm]++;
			kept.append(e);
		}
		writeQueues(kept);
		commit();
	}

	// ---- plain values ------------------------------------------------------------------------------------

	QString AppSettings::lastDirectory(const QString & kind) const
	{
		return knownKind(kind) ? store->value("Directories/" + kind).toString() : QString();
	}

	void AppSettings::setLastDirectory(const QString & kind, const QString & dir)
	{
		if (!knownKind(kind))
		{
			warn("The directory kind \"" + kind + "\" is not open, save or copybook; the directory is not remembered");
			return;
		}
		setValue("Directories/" + kind, dir);
	}

	QByteArray AppSettings::windowGeometry() const
	{
		const QByteArray geometry = store->value("Window/geometry").toByteArray();
		if (geometry.size() > MaxGeometryBytes)
		{
			warn("The setting Window/geometry is " + QString::number(geometry.size()) + " bytes, more than "
				+ QString::number(MaxGeometryBytes) + "; it is ignored");
			return QByteArray();
		}
		return geometry;
	}

	void AppSettings::setWindowGeometry(const QByteArray & geometry)
	{
		setValue("Window/geometry", geometry);
	}

	int AppSettings::displayCodePage() const
	{
		return readInt("Display/codePage", DefaultCodePage, 1, 65535);
	}

	void AppSettings::setDisplayCodePage(int codePage)
	{
		setValue("Display/codePage", codePage);
	}

	bool AppSettings::showSystemQueues() const
	{
		return readBool("Display/showSystemQueues", false);
	}

	void AppSettings::setShowSystemQueues(bool show)
	{
		setValue("Display/showSystemQueues", show);
	}

	// ---- connection --------------------------------------------------------------------------------------

	rfh::ConnectOptions AppSettings::connection() const
	{
		rfh::ConnectOptions o;
		const QString mode = store->value("Connection/mode").toString();
		if (store->contains("Connection/mode"))
		{
			if (mode == "client")
			{
				o.mode = rfh::Mode::Client;
			}
			else if (mode != "bindings")
			{
				warn("The setting Connection/mode is \"" + mode + "\", not bindings or client; bindings is used");
			}
		}
		auto text = [this](const char * name) { return store->value(QString("Connection/") + name).toString().toStdString(); };
		o.qmName = text("qmName");
		o.channel = text("channel");
		o.connName = text("connName");
		o.ccdtUrl = text("ccdtUrl");
		o.userId = text("userId");
		o.cipherSpec = text("cipherSpec");
		o.keyRepository = text("keyRepository");
		o.sslPeerName = text("sslPeerName");
		o.certificateLabel = text("certificateLabel");
		o.securityExit = text("securityExit");
		o.localAddress = text("localAddress");
		o.heartbeat = readInt("Connection/heartbeat", (int)o.heartbeat, 0, 999999);
		o.keepAlive = readInt("Connection/keepAlive", (int)o.keepAlive, -1, 1);
		o.sslEnabled = readBool("Connection/sslEnabled", false);
		o.sslClientValidation = readBool("Connection/sslClientValidation", false);
		o.fipsRequired = readBool("Connection/fipsRequired", false);
		o.sslResetCount = readInt("Connection/sslResetCount", 0, 0, 999999999);
		return o;
	}

	void AppSettings::setConnection(const rfh::ConnectOptions & o)
	{
		if (!prepareWrite())
		{
			return;
		}
		auto put = [this](const char * name, const QVariant & v) { store->setValue(QString("Connection/") + name, v); };
		auto str = [](const std::string & s) { return QString::fromStdString(s); };
		put("mode", o.mode == rfh::Mode::Client ? "client" : "bindings");
		put("qmName", str(o.qmName));
		put("channel", str(o.channel));
		put("connName", str(o.connName));
		put("ccdtUrl", urlCarriesCredential(str(o.ccdtUrl)) ? QString() : str(o.ccdtUrl));
		put("heartbeat", (int)o.heartbeat);
		put("keepAlive", (int)o.keepAlive);
		put("userId", str(o.userId));
		put("sslEnabled", o.sslEnabled);
		put("cipherSpec", str(o.cipherSpec));
		put("keyRepository", str(o.keyRepository));
		put("sslPeerName", str(o.sslPeerName));
		put("sslClientValidation", o.sslClientValidation);
		put("fipsRequired", o.fipsRequired);
		put("sslResetCount", o.sslResetCount);
		put("certificateLabel", str(o.certificateLabel));
		put("securityExit", str(o.securityExit));
		put("localAddress", str(o.localAddress));
		// o.password and o.securityData are not stored, by design
		commit();
	}
}
