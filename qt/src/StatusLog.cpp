#include "StatusLog.h"

#include <QThread>

namespace rfhqt
{
	StatusLog::StatusLog(QObject * parent)
		: QObject(parent)
	{
	}

	QString StatusLog::join(const QStringList & messages)
	{
		return messages.join(QStringLiteral("\r\n"));
	}

	void StatusLog::add(const QStringList & messages, const QTime & now)
	{
		add(join(messages), now);
	}

	void StatusLog::add(const QString & text, const QTime & now)
	{
		Q_ASSERT(thread() == QThread::currentThread());
		if (text.isEmpty())
		{
			return;
		}

		// "%H.%M.%S" + " " + text + CR LF + previous log (DataArea.cpp 10320)
		QString msg = now.toString(QStringLiteral("HH.mm.ss")) + QLatin1Char(' ') + text
			+ QStringLiteral("\r\n") + log;

		// Limit: cut to MaxChars, then back to the end of a line (DataArea.cpp 10323-10333). A new entry with
		// no line break inside the limit keeps the new entry truncated to MaxChars rather than wiping the log.
		int len = msg.size();
		if (len > MaxChars)
		{
			len = MaxChars;
			int trimmed = len;
			while ((trimmed > 0) && (msg[trimmed - 1] != QLatin1Char('\n')) && (msg[trimmed - 1] != QLatin1Char('\r')))
			{
				trimmed--;
			}
			if (trimmed > 0)
			{
				len = trimmed;
			}
			// else: no line break in the first MaxChars — keep len=MaxChars so the entry is not wiped
		}

		// Remove the trailing CR and LF (DataArea.cpp 10335-10339)
		while ((len > 0) && ((msg[len - 1] == QLatin1Char('\r')) || (msg[len - 1] == QLatin1Char('\n'))))
		{
			len--;
		}

		log = msg.left(len);
		emit changed();
	}

	QString StatusLog::textForClipboard() const
	{
		QString copy = log;
		copy.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
		return copy;
	}

	void StatusLog::clear()
	{
		Q_ASSERT(thread() == QThread::currentThread());
		if (log.isEmpty())
		{
			return;
		}
		log.clear();
		emit changed();
	}
}
