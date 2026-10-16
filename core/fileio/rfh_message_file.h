// rfh_message_file.h - RFHUtil message files.
//
// A message file is the raw message image: optionally an MQMD (364 bytes, version
// 2, native byte order), then the MQ headers, then the body. read is the port of
// DataArea::ReadFileData and write the port of DataArea::WriteFile, without the
// file dialogs; both keep the Windows results and status text.
//
// Differences from Windows RFHUtil:
// - an MQMD in big-endian or EBCDIC form is normalised with translateMQMD, which
//   keeps MsgId, CorrelId and GroupId as they are (Windows' processMQMD
//   byte-reverses them; reported upstream);
// - headers are written back exactly as read, because the header tabs that would
//   rebuild them are not ported yet;
// - the "keep headers when reading" (save RFH) option is not supported yet;
// - the group/segment read branch is not supported: Windows keeps the tab's float
//   encoding when a group or segment is active;
// - the AccountingToken of a big-endian MQMD is kept as it is, like the ids
//   (Windows reverses it);
// - the MQMDE handling differs; see rfh_header_chain.h;
// - write failures (fwrite or fclose) are reported; Windows ignores them;
// - header warnings are returned in Result.warnings. Windows prefixes them to the
//   status text, so a UI that wants the same text joins warnings, then text;
// - a header type that appears twice in the chain is summed in the status text,
//   whereas Windows reports the length of the last one.
#pragma once

#include <string>

#include "rfh_header_chain.h"
#include "rfh_message.h"
#include "rfh_mqmd_fields.h"
#include "rfh_result.h"

namespace rfh
{
	namespace fileio
	{
		struct ReadOptions
		{
			int fileCcsid = 0;			// the "file code page" setting; 0 decides from the data
			bool ignoreHeaders = false;	// do not look for MQ headers in the data
			bool removeCrLf = false;	// remove carriage returns and line feeds from the body
			bool unixLineEnds = false;	// turn bare line feeds in the body into CR LF
			int ansiCcsid = 0;			// last-resort code page; 0 uses rfh_get_acp()
		};

		struct ReadResult
		{
			Message message;			// message.mqmd is the MQMD the MQMD tab now holds (as toMqmd would build it); hadMqmd says whether the file had one
			bool hadMqmd = false;
			HeaderChain chain;			// headers found, relative to the data after the MQMD
			int fileCcsid = 0;			// code page of the data (DataArea::fileCcsid)
			int fileIntFormat = 0;		// integer encoding of the data (DataArea::fileIntFormat), e.g. from a byte order mark
			int charFormat = 0;			// CHAR_* for displaying the body
			int numericFormat = 0;		// NUMERIC_PC or NUMERIC_HOST for displaying the body; -1 is possible after an invalid DLQ, as on Windows
			int pdFormat = 0;
			int floatFormat = 0;
			std::string dataSource;		// "Message Data (N) from <path>", as Windows formats fileSource
		};

		// Reads path. fields is the MQMD tab: its code page and encoding are the
		// defaults, and it is cleared and refilled from the file as on Windows.
		// The Result text is the Windows status message, e.g. "12 bytes read from file x".
		Result read(const std::string & path, const ReadOptions & options, MqmdFields & fields, ReadResult & out);

		struct WriteOptions
		{
			bool includeMqmd = false;	// write message.mqmd first (as version 2, 364 bytes)
			bool dataOnly = false;		// leave out the headers
		};

		// Writes message to path. The Result text is the Windows status message,
		// e.g. "400 bytes (data 0 mqmd 364 rfh2 36) written to file x". The header
		// breakdown follows message.mqmd.Format, because the format chain decides what
		// the headers are.
		Result write(const std::string & path, const Message & message, const WriteOptions & options);
	}
}
