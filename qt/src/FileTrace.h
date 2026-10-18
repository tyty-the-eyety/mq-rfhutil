// FileTrace.h - the trace file (Windows CRfhutilApp::startTrace / logTraceEntry / dumpTraceData,
// rfhutil.cpp 1817-2003 and 2384-2528), installed with rfhSetTrace().
//
// What it writes, as Windows does:
//   - an entry is "yy-mm-dd:HH.MM.SS " + text + LF in local time, written and flushed (one write(2)) per line;
//   - dumpTraceData writes a label line "<label> at <8 hex digits>" and then hex lines without a time stamp
//     (offset, characters in groups of 8, hex in groups of 4 bytes); without verboseTrace a dump stops after
//     8 KB and says so;
//   - enabling appends to an existing file and starts with a blank line, "RFHUtil started" and "verboseTrace=n".
// What it never writes: passwords. The core's trace text names the password only as "password=set" or
// "password=not set"; FileTrace writes what it is given and adds nothing of its own. A host that traces its own
// text must keep secrets out of it.
//
// Differences from Windows, all on purpose:
//   - Windows opens the file named by the environment variable RFHUTIL_TRACE_FILE and has no default; here the
//     default path is $XDG_STATE_HOME/rfhutil/trace.log (else ~/.local/state/rfhutil/trace.log), and
//     fromEnvironment() keeps the Windows variables RFHUTIL_TRACE_FILE and RFHUTIL_VERBOSE_TRACE. The value
//     "stdout" (Windows) is not supported.
//   - The file is created with mode 0600 and the directories this creates with 0700: traces hold queue names and
//     user ids. A file that already exists and belongs to the user is tightened to 0600. A symbolic link or
//     anything that is not a regular file at the path is refused.
//   - Windows never rotates and never limits the file. Here the file is limited to maxBytes (default 10 MiB):
//     before a line that would pass the limit the file is renamed to "<path>.1" (replacing the older one) and a
//     new file is started with the header again, so at most about 2 * maxBytes stay on disk.
//   - An error (cannot create, cannot write, rotation failed) never fails an operation: the trace disables
//     itself, lastError() says why, and the caller (the UI) shows it. setEnabled() returns false for the
//     errors it meets itself.
//
// Threading: the MQ session traces from its worker thread while the GUI thread may enable or disable the trace,
// so every member may be called from any thread; a mutex serialises the writes (a dump is written without
// other lines in between). Install it with rfhSetTrace() before the worker starts and uninstall it after the
// worker has stopped (rfhSetTrace is not synchronised).
#pragma once

#include <QDateTime>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include "rfh_trace.h"

namespace rfhqt
{
	class FileTrace : public RfhTrace
	{
	public:
		static constexpr qint64 DefaultMaxBytes = qint64(10) * 1024 * 1024;

		// Starts disabled: nothing is created or written until setEnabled(true).
		explicit FileTrace(const QString & path = QString(), qint64 maxBytes = DefaultMaxBytes);
		~FileTrace() override;

		FileTrace(const FileTrace &) = delete;
		FileTrace & operator=(const FileTrace &) = delete;

		// $XDG_STATE_HOME/rfhutil/trace.log when XDG_STATE_HOME is an absolute path, else ~/.local/state/rfhutil/trace.log.
		static QString defaultPath();

		// Windows startTrace: RFHUTIL_TRACE_FILE names the file and enables the trace; RFHUTIL_VERBOSE_TRACE turns
		// on verbose trace unless its first character is N, n or 0. Returns nullptr when the trace is not
		// requested (variable unset or empty; error stays empty) or cannot be enabled (error says why).
		static std::unique_ptr<FileTrace> fromEnvironment(QString * error);

		QString path() const { return filePath; }

		// Opens (true) or closes (false) the file. Returns false, with lastError() set, when it cannot be opened
		// or its header cannot be written; the trace then stays disabled.
		bool setEnabled(bool enable);

		// The reason the last open or write failed; empty after a successful setEnabled(true).
		QString lastError() const;

		// For tests: the time source of the entry stamps.
		void setClock(std::function<QDateTime()> clock);

		BOOL isTraceEnabled() override { return enabled.load() ? TRUE : FALSE; }
		void logTraceEntry(const char * traceInfo) override;
		void dumpTraceData(const char * label, const unsigned char * data, unsigned int length) override;

	private:
		bool openLocked();
		void closeLocked();
		bool writeHeaderLocked();
		bool writeLineLocked(const QByteArray & line);
		bool writeRawLocked(const QByteArray & bytes);
		bool rotateLocked();
		void failLocked(const QString & reason);
		QByteArray stampedLocked(const char * text) const;

		const QString filePath;
		const qint64 maxBytes;

		mutable std::mutex mutex;
		std::atomic<bool> enabled{ false };
		int fd = -1;
		qint64 size = 0;
		bool entriesInFile = false;
		QString error;
		std::function<QDateTime()> now;
	};
}
