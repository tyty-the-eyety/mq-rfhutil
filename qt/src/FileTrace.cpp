#include "FileTrace.h"

#include <QDir>
#include <QFileInfo>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>

namespace rfhqt
{
	namespace
	{
		// rfhutil.cpp MAX_TRACE_BYTES_PER_LINE
		const int BytesPerLine = 32;
		const unsigned int NonVerboseDumpLimit = 8 * 1024;
		const char HexNumbers[] = "0123456789ABCDEF";		// comsubs.h HEX_NUMBERS

		QString describe(const QString & what, int err)
		{
			return what + ": " + QString::fromLocal8Bit(strerror(err));
		}
	}

	FileTrace::FileTrace(const QString & path, qint64 limit)
		: filePath(path.isEmpty() ? defaultPath() : path)
		, maxBytes(limit)
		, now([]() { return QDateTime::currentDateTime(); })
	{
	}

	FileTrace::~FileTrace()
	{
		setEnabled(false);
	}

	QString FileTrace::defaultPath()
	{
		const QString xdg = QString::fromLocal8Bit(qgetenv("XDG_STATE_HOME"));
		const QString base = (!xdg.isEmpty() && QDir::isAbsolutePath(xdg)) ? xdg : QDir::homePath() + "/.local/state";
		return base + "/rfhutil/trace.log";
	}

	std::unique_ptr<FileTrace> FileTrace::fromEnvironment(QString * error)
	{
		if (error != nullptr)
		{
			error->clear();
		}
		const QByteArray name = qgetenv("RFHUTIL_TRACE_FILE");
		if (name.isEmpty())
		{
			return nullptr;
		}
		if (name == "stdout")
		{
			if (error != nullptr)
			{
				*error = "RFHUTIL_TRACE_FILE=stdout is not supported; name a file";
			}
			return nullptr;
		}

		std::unique_ptr<FileTrace> trace(new FileTrace(QString::fromLocal8Bit(name)));

		// rfhutil.cpp 2506-2513
		const QByteArray verbose = qgetenv("RFHUTIL_VERBOSE_TRACE");
		if (!verbose.isEmpty() && verbose[0] != 'N' && verbose[0] != 'n' && verbose[0] != '0')
		{
			trace->verboseTrace = TRUE;
		}

		if (!trace->setEnabled(true))
		{
			if (error != nullptr)
			{
				*error = trace->lastError();
			}
			return nullptr;
		}
		return trace;
	}

	QString FileTrace::lastError() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return error;
	}

	void FileTrace::setClock(std::function<QDateTime()> clock)
	{
		std::lock_guard<std::mutex> lock(mutex);
		now = clock ? clock : []() { return QDateTime::currentDateTime(); };
	}

	bool FileTrace::setEnabled(bool enable)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!enable)
		{
			closeLocked();
			return true;
		}
		if (enabled.load())
		{
			return true;
		}
		error.clear();
		if (!openLocked())
		{
			return false;
		}
		return writeHeaderLocked();
	}

	void FileTrace::closeLocked()
	{
		enabled.store(false);
		if (fd >= 0)
		{
			::close(fd);
			fd = -1;
		}
		size = 0;
		entriesInFile = false;
	}

	void FileTrace::failLocked(const QString & reason)
	{
		error = reason;
		closeLocked();
	}

	// Opens the file for appending, creating it (and the directories) owner only. Never follows a symbolic link.
	bool FileTrace::openLocked()
	{
		const QString dir = QFileInfo(filePath).absolutePath();
		std::error_code ec;
		const bool dirExisted = std::filesystem::is_directory(dir.toStdString(), ec);
		if (!dirExisted)
		{
			std::filesystem::create_directories(dir.toStdString(), ec);
			if (ec)
			{
				error = "Cannot create the trace directory " + dir + ": " + QString::fromStdString(ec.message());
				return false;
			}
			::chmod(QFile::encodeName(dir).constData(), 0700);
		}

		const QByteArray name = QFile::encodeName(filePath);
		fd = ::open(name.constData(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
		if (fd < 0)
		{
			error = describe("Cannot open the trace file " + filePath, errno);
			return false;
		}

		struct stat st;
		if (::fstat(fd, &st) != 0)
		{
			const int err = errno;
			::close(fd);
			fd = -1;
			error = describe("Cannot examine the trace file " + filePath, err);
			return false;
		}
		if (!S_ISREG(st.st_mode))
		{
			::close(fd);
			fd = -1;
			error = "The trace file " + filePath + " is not a regular file";
			return false;
		}
		if (((st.st_mode & 077) != 0) && (st.st_uid == geteuid()))
		{
			::fchmod(fd, 0600);			// traces hold queue names and user ids
		}
		size = st.st_size;
		entriesInFile = false;
		enabled.store(true);
		return true;
	}

	QByteArray FileTrace::stampedLocked(const char * text) const
	{
		return now().toString(QStringLiteral("yy-MM-dd:HH.mm.ss")).toLatin1() + ' ' + (text != nullptr ? text : "") + '\n';
	}

	// rfhutil.cpp 2418-2436 and 2521-2525: a blank line, the start line and the verbose setting.
	bool FileTrace::writeHeaderLocked()
	{
		QByteArray header = "\n";
		header += stampedLocked("RFHUtil started");
		header += stampedLocked(verboseTrace ? "verboseTrace=1" : "verboseTrace=0");
		return writeRawLocked(header);
	}

	bool FileTrace::writeRawLocked(const QByteArray & bytes)
	{
		qint64 done = 0;
		while (done < bytes.size())
		{
			const ssize_t n = ::write(fd, bytes.constData() + done, (size_t)(bytes.size() - done));
			if (n < 0)
			{
				if (errno == EINTR)
				{
					continue;
				}
				const int err = errno;
				failLocked(describe("Cannot write the trace file " + filePath + "; the trace is switched off", err));
				return false;
			}
			done += n;
			size += n;
		}
		return true;
	}

	bool FileTrace::rotateLocked()
	{
		const QByteArray name = QFile::encodeName(filePath);
		const QByteArray older = QFile::encodeName(filePath + ".1");
		::close(fd);
		fd = -1;
		if (::rename(name.constData(), older.constData()) != 0)
		{
			failLocked(describe("Cannot rotate the trace file " + filePath + "; the trace is switched off", errno));
			return false;
		}
		enabled.store(false);
		if (!openLocked())
		{
			closeLocked();
			return false;
		}
		return writeHeaderLocked();
	}

	// Writes one whole line (or a hex line). A line that would pass the limit starts a new file first, unless the
	// file holds nothing but its header.
	bool FileTrace::writeLineLocked(const QByteArray & line)
	{
		if (entriesInFile && (size + line.size() > maxBytes))
		{
			if (!rotateLocked())
			{
				return false;
			}
		}
		if (!writeRawLocked(line))
		{
			return false;
		}
		entriesInFile = true;
		return true;
	}

	void FileTrace::logTraceEntry(const char * traceInfo)
	{
		if (!enabled.load())
		{
			return;
		}
		std::lock_guard<std::mutex> lock(mutex);
		if (fd < 0)
		{
			return;
		}
		writeLineLocked(stampedLocked(traceInfo));
	}

	// rfhutil.cpp 1855-2003
	void FileTrace::dumpTraceData(const char * label, const unsigned char * data, unsigned int length)
	{
		if (!enabled.load())
		{
			return;
		}
		std::lock_guard<std::mutex> lock(mutex);
		if (fd < 0)
		{
			return;
		}

		char traceLine[256];
		if (label != nullptr)
		{
			snprintf(traceLine, sizeof(traceLine), "%.192s at %8.8X", label, (unsigned int)(uintptr_t)data);
		}
		else
		{
			snprintf(traceLine, sizeof(traceLine), "memory at %8.8X", (unsigned int)(uintptr_t)data);
		}
		if (!writeLineLocked(stampedLocked(traceLine)))
		{
			return;
		}

		if ((data == nullptr) || (length == 0))
		{
			writeLineLocked(stampedLocked("data is NULL or zero length"));
			return;
		}

		if (!verboseTrace && (length > NonVerboseDumpLimit))
		{
			snprintf(traceLine, sizeof(traceLine), "Dumping out first 8K bytes of %d", (int)length);
			if (!writeLineLocked(stampedLocked(traceLine)))
			{
				return;
			}
			length = NonVerboseDumpLimit;
		}

		unsigned int offset = 0;
		const unsigned char * ptr = data;
		unsigned int remaining = length;
		while (remaining > 0)
		{
			const unsigned int count = (remaining > (unsigned int)BytesPerLine) ? (unsigned int)BytesPerLine : remaining;

			// 4 digit offset, then 37 blanks; the characters start in column 5, in groups of 8 with a blank
			// between them; the hex starts in column 41
			snprintf(traceLine, sizeof(traceLine), "%4.4X", offset);
			offset += BytesPerLine;
			memset(traceLine + 4, ' ', 37);

			unsigned int j = 5;
			unsigned int k = 0;
			for (unsigned int i = 0; i < count; i++)
			{
				traceLine[j++] = (ptr[i] >= ' ' && ptr[i] < 127) ? (char)ptr[i] : '.';
				if (++k == 8)
				{
					k = 0;
					j++;
				}
			}

			j = 41;
			const unsigned char * hexptr = ptr;
			for (unsigned int g = 0; g < (count >> 2); g++)
			{
				traceLine[j++] = ' ';
				for (int b = 0; b < 4; b++)
				{
					traceLine[j++] = HexNumbers[hexptr[b] >> 4];
					traceLine[j++] = HexNumbers[hexptr[b] & 0x0F];
				}
				hexptr += 4;
			}
			const unsigned int rest = count % 4;
			if (rest > 0)
			{
				traceLine[j++] = ' ';
				for (unsigned int b = 0; b < rest; b++)
				{
					traceLine[j++] = HexNumbers[hexptr[b] >> 4];
					traceLine[j++] = HexNumbers[hexptr[b] & 0x0F];
				}
			}
			traceLine[j] = 0;

			// no time stamp on the hex lines
			if (!writeLineLocked(QByteArray(traceLine) + '\n'))
			{
				return;
			}
			ptr += count;
			remaining -= count;
		}
	}
}
