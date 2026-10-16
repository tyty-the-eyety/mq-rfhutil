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
// - the "keep headers when reading" (save RFH) option is not supported yet.
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
			Message message;			// mqmd is meaningful only when hadMqmd
			bool hadMqmd = false;
			HeaderChain chain;			// headers found, relative to the data after the MQMD
			int fileCcsid = 0;			// code page of the data (DataArea::fileCcsid)
			int fileIntFormat = 0;		// integer encoding of the data (DataArea::fileIntFormat), e.g. from a byte order mark
			int charFormat = 0;			// CHAR_* for displaying the body
			int numericFormat = 0;		// NUMERIC_PC or NUMERIC_HOST for displaying the body
			int pdFormat = 0;
			int floatFormat = 0;
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
		// e.g. "400 bytes (data 0 mqmd 364 rfh2 36) written to file x".
		Result write(const std::string & path, const Message & message, const WriteOptions & options);
	}
}
