// rfh_header_chain.cpp - see rfh_header_chain.h

#include "rfh_header_chain.h"

#include <string.h>

#include <algorithm>

#include <cmqxc.h>

#include "rfh_platform.h"
#include "rfhconst.h"
#include "comsubs.h"
#include "mqsubs.h"

namespace rfh
{
	namespace
	{
		// header names as parseMsgHeaders compares them: formats with trailing blanks removed
		const char * const kDeadLetter = "MQDEAD";
		const char * const kCics = "MQCICS";
		const char * const kIms = "MQIMS";
		const char * const kRfh1 = "MQHRF";
		const char * const kRfh2 = "MQHRF2";
		const char * const kMdExtension = "MQHMDE";

		const size_t kCicsProgramName = 8;		// CIH_MAX_PROG_NAME

		MQLONG readLong(const unsigned char * p, bool reverse)
		{
			MQLONG v;
			memcpy(&v, p, sizeof(v));
			return reverse ? reverseBytes4(v) : v;
		}

		// StrucId and Format are translated from EBCDIC when the incoming data is EBCDIC
		std::string chars(const unsigned char * p, size_t length, bool ebcdic)
		{
			char temp[16] = { 0 };
			if (ebcdic)
			{
				EbcdicToAscii(p, (unsigned int)length, (unsigned char *)temp);
			}
			else
			{
				memcpy(temp, p, length);
			}
			temp[length] = 0;
			return temp;
		}

		std::string trimmedFormat(const unsigned char * p, bool ebcdic)
		{
			std::string format = chars(p, MQ_FORMAT_LENGTH, ebcdic);
			char temp[MQ_FORMAT_LENGTH + 1];
			memcpy(temp, format.c_str(), format.size() + 1);
			Rtrim(temp);
			return temp;
		}

		struct State
		{
			std::string nextFormat;
			int ccsid;
			int charFormat;
			int encoding;
		};

		// Offsets shared by the CIH, IIH, RFH and RFH2 structures.
		const size_t kStrucLength = 8, kEncoding = 12, kCcsid = 16, kFormat = 20;

		// CDlq::parseDLQheader + CDlq::setNextFormat
		size_t deadLetter(const unsigned char * p, size_t n, State & s)
		{
			bool ebcdic = (CHAR_EBCDIC == getCcsidType(s.ccsid));
			bool reverse = (s.encoding != NUMERIC_PC);

			if ((n >= sizeof(MQDLH)) &&
			    (chars(p, 4, ebcdic) == std::string(MQDLH_STRUC_ID, 4)) &&
			    (readLong(p + 4, reverse) == MQDLH_VERSION_1))
			{
				MQLONG encoding = readLong(p + 108, reverse);
				s.ccsid = readLong(p + 112, reverse);
				s.nextFormat = trimmedFormat(p + 116, ebcdic);
				s.charFormat = getCcsidType(s.ccsid);
				s.encoding = getIntEncode(encoding);
				return sizeof(MQDLH);
			}

			// the cleared DLQ tab: no format, no code page, encoding -1
			s.nextFormat.clear();
			s.ccsid = 0;
			s.charFormat = getCcsidType(0);
			s.encoding = -1;
			return 0;
		}

		bool inRange(MQLONG v)
		{
			return (v < 100000) && (v > -10000);
		}

		// CCICS::parseCICSheader + CCICS::setNextFormat (which only sets the format)
		size_t cics(const unsigned char * p, size_t n, State & s, std::vector<std::string> & warnings)
		{
			bool ebcdic = (CHAR_EBCDIC == getCcsidType(s.ccsid));
			bool reverse = (s.encoding != NUMERIC_PC);

			if (n >= MQCIH_LENGTH_1)
			{
				MQLONG version = readLong(p + 4, reverse);
				if ((chars(p, 4, ebcdic) == std::string(MQCIH_STRUC_ID, 4)) &&
				    ((version == MQCIH_VERSION_1) || (version == MQCIH_VERSION_2)))
				{
					size_t length = (version == MQCIH_VERSION_1) ? MQCIH_LENGTH_1 : MQCIH_LENGTH_2;

					// The header itself must be there in full.
					if (length > n)
					{
						warnings.push_back("header length runs past the end of the data");
						s.nextFormat.clear();
						s.ccsid = 0;
						return 0;
					}

					// LinkType sits at the same offset in both versions; with MQCLT_PROGRAM the
					// 8-byte program name follows the header. Windows checks the space for it
					// against the version 1 length even for version 2. When the header fits but
					// the name does not, the header is kept at its base length: Windows
					// recognises it and reads the name past the data.
					MQLONG linkType = readLong(p + 52, reverse);
					// n >= MQCIH_LENGTH_1 + kCicsProgramName mirrors Windows' check; implied by the next condition
					if ((MQCLT_PROGRAM == linkType) && (n >= MQCIH_LENGTH_1 + kCicsProgramName) &&
					    (length + kCicsProgramName <= n))
					{
						length += kCicsProgramName;
					}

					// Windows reports out-of-range CCSID, and for version 2 cursor position
					// (164) and error offset (168), as one error; version 1 zeroes the latter.
					bool err = false;
					MQLONG ccsid = readLong(p + kCcsid, reverse);
					s.ccsid = inRange(ccsid) ? ccsid : 0;
					err = !inRange(ccsid);
					if (version == MQCIH_VERSION_2)
					{
						err = err || !inRange(readLong(p + 164, reverse)) || !inRange(readLong(p + 168, reverse));
					}
					if (err)
					{
						warnings.push_back("*** Errors parsing CICS header\r\n");
					}
					s.nextFormat = trimmedFormat(p + kFormat, ebcdic);
					return length;
				}
			}

			s.nextFormat.clear();
			s.ccsid = 0;
			return 0;
		}

		// CIms::parseIMSheader + CIms::setNextFormat (which only sets the format)
		size_t ims(const unsigned char * p, size_t n, State & s)
		{
			bool ebcdic = (CHAR_EBCDIC == getCcsidType(s.ccsid));
			bool reverse = (s.encoding != NUMERIC_PC);

			if ((n >= MQIIH_LENGTH_1) &&
			    (chars(p, 4, ebcdic) == std::string(MQIIH_STRUC_ID, 4)) &&
			    (readLong(p + 4, reverse) == MQIIH_VERSION_1))
			{
				s.ccsid = readLong(p + kCcsid, reverse);
				s.nextFormat = trimmedFormat(p + kFormat, ebcdic);
				return MQIIH_LENGTH_1;
			}

			s.nextFormat.clear();
			s.ccsid = 0;
			return 0;
		}

		// RFH::parseRFH / RFH::parseRFH2 + setNextFormatRfh1 / setNextFormatRfh2.
		// Neither checks the version; RFH1 needs a length above the fixed part,
		// RFH2 accepts exactly the fixed part.
		size_t rfh(const unsigned char * p, size_t n, State & s, bool v2, std::vector<std::string> & warnings)
		{
			bool ebcdic = (CHAR_EBCDIC == getCcsidType(s.ccsid));
			bool reverse = (NUMERIC_HOST == s.encoding);
			size_t fixed = v2 ? MQRFH_STRUC_LENGTH_FIXED_2 : MQRFH_STRUC_LENGTH_FIXED;

			// RFH2 reports a truncated header; RFH1 returns silently, as Windows does
			if (v2 && (n >= 4) && (n < fixed) && (chars(p, 4, ebcdic) == std::string(MQRFH_STRUC_ID, 4)))
			{
				warnings.push_back("***Invalid RFH Length field*** ");
			}

			if ((n >= fixed) && (chars(p, 4, ebcdic) == std::string(MQRFH_STRUC_ID, 4)))
			{
				MQLONG length = readLong(p + kStrucLength, reverse);
				bool lengthOk = v2 ? ((length >= (MQLONG)fixed) && ((size_t)length <= n))
				                   : ((length > (MQLONG)fixed) && ((size_t)length <= n));
				if (lengthOk)
				{
					MQLONG encoding = readLong(p + kEncoding, reverse);
					s.ccsid = readLong(p + kCcsid, reverse);
					s.nextFormat = trimmedFormat(p + kFormat, ebcdic);
					s.charFormat = getCcsidType(s.ccsid);
					s.encoding = getIntEncode(encoding);
					return (size_t)length;
				}

				warnings.push_back("***Invalid RFH Length field*** ");
			}

			// the cleared RFH tab
			s.nextFormat.clear();
			s.ccsid = 0;
			s.charFormat = getCcsidType(0);
			s.encoding = NUMERIC_PC;
			return 0;
		}

		// MQMDPAGE::processMQMDE, at the header's real offset and swapping only for host data
		size_t mdExtension(const unsigned char * p, size_t n, State & s, HeaderChain & chain)
		{
			if (n < MQMDE_LENGTH_2)
			{
				s.nextFormat.clear();
				return 0;
			}

			bool reverse = (NUMERIC_HOST == s.encoding);
			MQLONG encoding = readLong(p + kEncoding, reverse);
			s.ccsid = readLong(p + kCcsid, reverse);
			s.nextFormat = trimmedFormat(p + kFormat, false);
			s.charFormat = getCcsidType(s.ccsid);
			s.encoding = getIntEncode(encoding);

			chain.hasMdExtension = true;
			memcpy(chain.mdExtension.groupId, p + 32, sizeof(MQBYTE24));
			chain.mdExtension.msgSeqNumber = readLong(p + 56, reverse);
			chain.mdExtension.offset = readLong(p + 60, reverse);
			chain.mdExtension.msgFlags = readLong(p + 64, reverse);
			chain.mdExtension.originalLength = readLong(p + 68, reverse);
			return MQMDE_LENGTH_2;
		}
	}

	HeaderChain walkHeaders(const unsigned char * data, size_t length, const std::string & format,
	                        int ccsid, int charFormat, int encoding)
	{
		HeaderChain chain;

		char trimmed[MQ_FORMAT_LENGTH + 1] = { 0 };
		memcpy(trimmed, format.data(), (std::min)(format.size(), (size_t)MQ_FORMAT_LENGTH));
		Rtrim(trimmed);

		State s = { trimmed, ccsid, charFormat, encoding };
		size_t ofs = 0;

		// The same loop structure as parseMsgHeaders: each pass tries every header
		// type in a fixed order, and a header followed by one of its own kind ends
		// the chain.
		auto step = [&](const char * name, HeaderType type, size_t (*parse)(const unsigned char *, size_t, State &, HeaderChain &)) -> bool
		{
			if (s.nextFormat != name)
			{
				return false;
			}

			size_t len = parse(data + ofs, length - ofs, s, chain);
			if (len > length - ofs)
			{
				// Unreachable today, because every parser bounds its own length; kept as a
				// guard for future parsers. See the header comment for the difference from Windows.
				chain.warnings.push_back("header length runs past the end of the data");
				s.nextFormat.clear();
				return true;
			}
			if (len > 0)
			{
				chain.headers.push_back({ type, ofs, len });
				ofs += len;
			}
			return s.nextFormat != name;
		};

		bool found = true;
		while (found)
		{
			found = false;
			found |= step(kDeadLetter, HeaderType::DeadLetter,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain &) { return deadLetter(p, n, st); });
			found |= step(kCics, HeaderType::Cics,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain & c) { return cics(p, n, st, c.warnings); });
			found |= step(kIms, HeaderType::Ims,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain &) { return ims(p, n, st); });
			found |= step(kRfh1, HeaderType::Rfh1,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain & c) { return rfh(p, n, st, false, c.warnings); });
			found |= step(kRfh2, HeaderType::Rfh2,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain & c) { return rfh(p, n, st, true, c.warnings); });
			found |= step(kMdExtension, HeaderType::MdExtension,
			              [](const unsigned char * p, size_t n, State & st, HeaderChain & c) { return mdExtension(p, n, st, c); });
		}

		chain.bodyOffset = ofs;
		chain.bodyFormat = s.nextFormat;
		chain.bodyCcsid = s.ccsid;
		chain.charFormat = s.charFormat;
		chain.encoding = s.encoding;
		return chain;
	}

	HeaderChain walkHeaders(const unsigned char * data, size_t length, const MQMD2 & mqmd)
	{
		return walkHeaders(data, length, std::string(mqmd.Format, MQ_FORMAT_LENGTH), mqmd.CodedCharSetId,
		                   getCcsidType(mqmd.CodedCharSetId), getIntEncode(mqmd.Encoding));
	}

	LeadingHeader detectLeadingHeader(const unsigned char * data, size_t length)
	{
		LeadingHeader h;
		if (length < 8)
		{
			return h;
		}

		char id[5] = { 0 };
		char ebcdicId[5] = { 0 };
		memcpy(id, data, 4);
		EbcdicToAscii((const unsigned char *)id, 4, (unsigned char *)ebcdicId);
		MQLONG version;
		memcpy(&version, data + 4, sizeof(version));
		MQLONG versionReversed = reverseBytes4(version);

		// DataArea::assignMQMDEncoding and assignMQMDccsid
		auto assign = [&](const char * strucId, size_t encodingOffset, size_t ccsidOffset, MQLONG headerVersion)
		{
			MQLONG encodingField;
			MQLONG ccsidField;
			memcpy(&encodingField, data + encodingOffset, sizeof(encodingField));
			memcpy(&ccsidField, data + ccsidOffset, sizeof(ccsidField));

			if (version == headerVersion)
			{
				h.encoding = NUMERIC_PC;
				h.pdEncoding = NUMERIC_PC;
				h.floatEncoding = FLOAT_PC;
			}
			else
			{
				h.encoding = NUMERIC_HOST;
				h.pdEncoding = NUMERIC_HOST;
				h.floatEncoding = (reverseBytes4(encodingField) == DEF_UNIX_ENCODING) ? FLOAT_UNIX : FLOAT_390;
			}

			int ccsid = (h.encoding != NUMERIC_PC) ? reverseBytes4(ccsidField) : ccsidField;
			if (memcmp(id, strucId, 4) == 0)
			{
				h.charFormat = CHAR_ASCII;
				h.ccsid = (CHAR_ASCII == getCcsidType(ccsid)) ? ccsid : DEF_ASCII_CCSID;
			}
			else
			{
				h.charFormat = CHAR_EBCDIC;
				h.ccsid = (CHAR_EBCDIC == getCcsidType(ccsid)) ? ccsid : DEF_EBCDIC_CCSID;
			}
		};

		auto idIs = [&](const char * strucId) { return (memcmp(id, strucId, 4) == 0) || (memcmp(ebcdicId, strucId, 4) == 0); };
		auto versionIs = [&](MQLONG v) { return (version == v) || (versionReversed == v); };

		// Later matches win, as in checkDataForHeader.
		if ((length >= sizeof(MQDLH)) && idIs(MQDLH_STRUC_ID) && versionIs(MQDLH_VERSION_1))
		{
			h.found = true;
			h.format = MQFMT_DEAD_LETTER_HEADER;
			assign(MQDLH_STRUC_ID, 108, 112, MQDLH_VERSION_1);
		}
		if ((length >= MQCIH_LENGTH_1 + 8) && idIs(MQCIH_STRUC_ID) && (versionIs(MQCIH_VERSION_1) || versionIs(MQCIH_VERSION_2)))
		{
			h.found = true;
			h.format = MQFMT_CICS;
			assign(MQCIH_STRUC_ID, 12, 16, MQCIH_VERSION_1);
		}
		if ((length >= MQIIH_LENGTH_1 + 8) && idIs(MQIIH_STRUC_ID) && versionIs(MQIIH_VERSION_1))
		{
			h.found = true;
			h.format = MQFMT_IMS;
			assign(MQIIH_STRUC_ID, 12, 16, MQIIH_VERSION_1);
		}
		if ((length >= MQRFH_STRUC_LENGTH_FIXED) && idIs(MQRFH_STRUC_ID) && versionIs(MQRFH_VERSION_1))
		{
			h.found = true;
			h.format = MQFMT_RF_HEADER;
			assign(MQRFH_STRUC_ID, 12, 16, MQRFH_VERSION_1);
		}
		if ((length >= MQRFH_STRUC_LENGTH_FIXED_2) && idIs(MQRFH_STRUC_ID) && versionIs(MQRFH_VERSION_2))
		{
			h.found = true;
			h.format = MQFMT_RF_HEADER_2;
			assign(MQRFH_STRUC_ID, 12, 16, MQRFH_VERSION_2);
		}

		return h;
	}

	bool looksLikeEbcdic(const unsigned char * data, size_t length)
	{
		size_t maxChars = (std::min)(length, (size_t)1024);
		if (maxChars < 64)
		{
			return false;
		}

		int aChars = 0, eChars = 0, eSpaces = 0, aSpaces = 0;
		for (size_t i = 0; i < maxChars; i++)
		{
			unsigned char c = data[i];
			if (64 == c)
			{
				eSpaces++;
			}
			else
			{
				if (32 == c) aSpaces++;
				if ((c >= 'A') && (c <= 'z')) aChars++;
				if ((c >= '0') && (c <= '9')) aChars++;
				if ((c >= 240) && (c <= 250)) eChars++;
				if ((c >= 193) && (c <= 233)) eChars++;
				if ((c >= 129) && (c <= 169)) eChars++;
			}
		}

		return (eChars + eSpaces) > (aChars + aSpaces + 32);
	}
}
