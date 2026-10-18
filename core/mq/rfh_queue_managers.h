// rfh_queue_managers.h - the queue managers defined on this machine, read from mqs.ini.
//
// Windows has no counterpart. Its bindings build lists only the queue managers in an in-memory
// name cache (DataArea::getFirstQMname / getNextQMname, DataArea.cpp:24199-), its client build
// lists MQSERVER, channel table entries and the saved last-used names (DataArea::loadQMComboBox,
// DataArea.cpp:25392-25611). The registry code (SearchForMQ71, DataArea.cpp:24620-) enumerates MQ
// installations, not queue managers, and the key PubSub.cpp:38 names for queue managers is never
// used. So the format below is IBM's documented mqs.ini, not a port.
//
// mqs.ini format relied on (IBM MQ "Configuration files" documentation, and the file shipped with
// MQ 9.4 on this machine):
// - Stanzas: a line "Name:" starts one; the "key=value" lines after it belong to it.
// - "QueueManager:" has one stanza per queue manager: Name, Prefix, Directory, DataPath (and others
//   this code does not read, such as InstallationName).
// - "DefaultQueueManager:" holds Name= of the default queue manager.
// - Comment lines start with "#". Blank lines are ignored. Keys and values have no quotes.
// - The directory that holds mqs.ini is the MQ data path: the environment variable MQ_DATA_PATH
//   (set by setmqenv; the string is in libmqmcs_r.so) or /var/mqm.
// Not documented, so chosen here: stanza and key words are matched without regard to case; the
// first value of a repeated key wins; a name is compared exactly (MQ names are case sensitive).
//
// What it does not do: it does not check that a queue manager still exists on disk, is running,
// or belongs to the installation whose library is loaded, and it does not work out which
// directory the queue manager's data is in (Prefix, Directory and DataPath are kept as written).
//
// Bounds: at most kMaxIniBytes of the file are read. A larger file is reported as truncated with
// a warning, and a final line cut by the limit is dropped, so no name is ever a prefix of the
// real one. Warnings are capped at kMaxWarnings.
//
// On Windows the function returns a failure: the Windows build keeps this information in the
// registry and has no mqs.ini.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "rfh_result.h"

namespace rfh
{
	namespace mq
	{
		// 1 MiB. A real file is a few KB (the one shipped with MQ 9.4 is 2.6 KB, with one 5-line
		// stanza per queue manager), so 1 MiB holds thousands of queue managers; the limit exists
		// only so a wrong file (a log, a core dump) cannot be read whole.
		const std::size_t kMaxIniBytes = 1024 * 1024;

		// Warnings kept in Result.warnings; the last one says how many were left out.
		const std::size_t kMaxWarnings = 50;

		// MQ_Q_MGR_NAME_LENGTH
		const std::size_t kMaxQueueManagerNameLength = 48;

		// One QueueManager: stanza. Strings are the bytes of the file (a path may be any bytes on
		// Linux), so they are not guaranteed to be UTF-8; notes say when they are not.
		struct LocalQueueManager
		{
			std::string name;
			std::optional<std::string> prefix;			// Prefix=, absent when the stanza has none
			std::optional<std::string> directory;		// Directory=
			std::optional<std::string> dataPath;		// DataPath=
			bool isDefault = false;						// name equals the DefaultQueueManager: Name
			int line = 0;								// line of the "QueueManager:" header, 1 based
			std::vector<std::string> notes;				// absent fields, a name MQ cannot accept, bytes that are not UTF-8
		};

		struct LocalQueueManagers
		{
			std::string iniPath;						// the file looked at
			bool fileFound = false;						// false: no mqs.ini there (not an error)
			bool truncated = false;						// the file was larger than kMaxIniBytes
			std::vector<LocalQueueManager> managers;	// file order, each name once
			std::optional<std::string> defaultName;		// Name= of DefaultQueueManager:, absent if there is none
		};

		// MQ_DATA_PATH when set and not empty, otherwise "/var/mqm".
		std::string defaultMqDataPath();

		// Reads <mqDataPath>/mqs.ini; an empty mqDataPath means defaultMqDataPath().
		// - No such file: success, fileFound false, empty list, a warning with the path (a client-only
		//   machine has no mqs.ini).
		// - The file cannot be read (permission, a directory, an I/O error) or this is Windows: failure
		//   whose text names the path and the reason; out has iniPath and nothing else.
		// - Anything wrong inside the file never fails the call: it is a warning (with the line number)
		//   and the usable parts are kept. Stanzas without a Name are skipped, repeated names are
		//   listed once, over-long names are kept with a note.
		Result listLocalQueueManagers(const std::string & mqDataPath, LocalQueueManagers & out);
	}
}
