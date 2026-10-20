// StatusLog.h - the Main tab's status box (Windows IDC_ERRMSG, DataArea::updateMsgText, DataArea.cpp 10301-10352).
//
// Every operation adds its text; the box shows the newest entry first, each stamped with the local time
// "HH.MM.SS" and a blank. Several messages of one operation are joined with CR LF and stamped once. The log is
// kept as one string with CR LF line ends, exactly as Windows keeps m_msg_text, and is cut like Windows cuts it:
// when the text would pass MaxChars (8191, MAX_ERR_MSG_LEN) it is cut to that length and then back to the end of
// a line, so the oldest entries drop out whole. A new entry with no line break inside the limit is kept truncated
// rather than wiped. Trailing CR and LF are removed. Clear Data and Clear All do not
// clear it (inv 3.2); only clear() does.
//
// Threading: GUI thread only. add() and clear() are not synchronised; the worker thread never touches the
// log (results come back as signals and the GUI thread adds the text).
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTime>

namespace rfhqt
{
	class StatusLog : public QObject
	{
		Q_OBJECT

	public:
		// Windows MAX_ERR_MSG_LEN. Windows counts ANSI bytes; this counts characters, which is the same for
		// the ASCII texts the log holds.
		static constexpr int MaxChars = 8191;

		explicit StatusLog(QObject * parent = nullptr);

		// Adds `text` as the newest entry, stamped with `now`. Empty text changes nothing (Windows tests
		// m_error_msg.GetLength() > 0). `now` is injectable for tests.
		void add(const QString & text, const QTime & now = QTime::currentTime());

		// Several messages of one operation: join()ed, stamped once.
		void add(const QStringList & messages, const QTime & now = QTime::currentTime());

		// The messages joined with CR LF (how operations build m_error_msg).
		static QString join(const QStringList & messages);

		// The log with CR LF line ends, as Windows holds it.
		QString text() const { return log; }

		// The same text with LF line ends, for the clipboard and for text widgets.
		QString textForClipboard() const;

		// Empties the log. Sends changed() only when there was something to remove.
		void clear();

	signals:
		void changed();

	private:
		QString log;
	};
}
