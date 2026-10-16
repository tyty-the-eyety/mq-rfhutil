// rfh_header_chain.h - finds the MQ headers (DLQ, CICS, IMS, RFH1, RFH2, MQMDE)
// at the front of message data and where the body starts.
//
// walkHeaders is the port of the chain logic in DataArea::parseMsgHeaders and the
// length and next-format rules of the header parsers (CDlq::parseDLQheader,
// CCICS::parseCICSheader, CIms::parseIMSheader, RFH::parseRFH, RFH::parseRFH2).
// It records where each header is and what follows it; it does not decode header
// contents. Deliberate differences from Windows:
//  - An MQMDE is read from its real offset (Windows uses (MQMDE *)data + offset,
//    which scales the offset by sizeof(MQMDE)), and its integers are byte-swapped
//    only for host encoding (Windows always swaps them). Both reported upstream.
//  - After an MQMDE the next encoding is getIntEncode(MDE.Encoding); Windows keeps
//    the raw MQENC value, so later DLQ, CICS and IMS headers always swap.
//  - A failed parse of a header type that already parsed earlier in the chain uses
//    the cleared state; Windows reuses the earlier parse's state and can loop
//    forever or read past the buffer.
//  - A header whose length runs past the data ends the chain (Windows' behaviour
//    is undefined there).
//  - An MQMDE under 72 bytes ends the chain and leaves ccsid, charFormat and
//    encoding unchanged.
// Warnings accumulate, whereas Windows' RFH message replaces earlier ones.
//
// detectLeadingHeader and looksLikeEbcdic are the ports of
// DataArea::checkDataForHeader and DataArea::checkForEBCDIC, used when a file has
// no MQMD to say what the data is.
#pragma once

#include <cmqc.h>

#include <stddef.h>

#include <string>
#include <vector>

namespace rfh
{
	enum class HeaderType { DeadLetter, Cics, Ims, Rfh1, Rfh2, MdExtension };

	struct HeaderInfo
	{
		HeaderType type;
		size_t offset;			// from the start of the data
		size_t length;
	};

	// The MQMD fields an MQMDE carries, already in native byte order.
	struct MdExtensionFields
	{
		MQBYTE24 groupId;
		MQLONG msgSeqNumber;
		MQLONG offset;
		MQLONG msgFlags;
		MQLONG originalLength;
	};

	struct HeaderChain
	{
		std::vector<HeaderInfo> headers;
		size_t bodyOffset = 0;		// total length of the headers
		std::string bodyFormat;		// format of the data after the last header, trailing blanks removed
		int bodyCcsid = 0;
		int charFormat = 0;			// CHAR_* for the body
		int encoding = 0;			// NUMERIC_PC or NUMERIC_HOST for the body; -1 after an invalid DLQ
									// (consumers derive pd/float as Windows does: PC/FLOAT_PC when
									// NUMERIC_PC, else HOST/FLOAT_390)
		bool hasMdExtension = false;
		MdExtensionFields mdExtension = {};
		std::vector<std::string> warnings;
	};

	// Walks the headers starting with the given format (normally MQMD.Format), the
	// data's ccsid and character format, and its integer encoding (NUMERIC_PC or
	// NUMERIC_HOST). An invalid header ends the chain, as on Windows: the body
	// then starts at that header and its format is empty.
	HeaderChain walkHeaders(const unsigned char * data, size_t length, const std::string & format,
	                        int ccsid, int charFormat, int encoding);

	// What a file without an MQMD starts with, judged as DataArea::checkDataForHeader
	// does: a DLQ, CICS, IMS, RFH1 or RFH2 header in ASCII or EBCDIC, either byte order.
	struct LeadingHeader
	{
		bool found = false;
		std::string format;			// e.g. MQFMT_RF_HEADER_2, blank padded
		int ccsid = 0;				// ccsid the MQMD tab would show
		int charFormat = 0;			// CHAR_ASCII or CHAR_EBCDIC
		int encoding = 0;			// NUMERIC_PC or NUMERIC_HOST
		int pdEncoding = 0;
		int floatEncoding = 0;		// FLOAT_PC, FLOAT_UNIX or FLOAT_390
	};
	LeadingHeader detectLeadingHeader(const unsigned char * data, size_t length);

	// True when the first 1024 bytes look more like EBCDIC text than ASCII, as
	// DataArea::checkForEBCDIC judges it. Needs at least 64 bytes.
	bool looksLikeEbcdic(const unsigned char * data, size_t length);
}
