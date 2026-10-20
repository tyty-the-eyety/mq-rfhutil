// SPDX-License-Identifier: Apache-2.0
// rfh_parsed_headers.h - structured fields for the MQ headers that can follow the MQMD:
// MQDLH (dead letter), MQCIH (CICS), MQIIH (IMS), MQRFH (RFH v1), MQRFH2 (RFH v2).
//
// parse() populates these structs from raw header bytes; build() serialises them back.
// The callers chain these headers in the order they appear in the message:
//   DLQ → CICS → IMS → RFH1 → RFH2
// Only the headers that are present in the raw data are populated (included = true).
//
// RFH2 sub-folders:
//   mcd  — message content description (domain, set, type, format)
//   jms  — JMS bridge routing metadata (structured fields)
//   usr  — user-defined XML (stored and built verbatim)
//   ps   — pub/sub command (stored and built verbatim)
//   pscr — pub/sub create response (stored and built verbatim)
//   other — unrecognised folders (stored as raw length-prefixed XML blocks)
//
// Encoding constants (NUMERIC_PC / NUMERIC_HOST, FLOAT_PC / FLOAT_UNIX / FLOAT_390)
// are from rfhconst.h (mqsubs.h). Callers that only read the parsed fields do not need
// to include rfhconst.h.
#pragma once

#include <cmqc.h>
#include <cmqpsc.h>

#include <string>
#include <vector>

namespace rfh
{
	// ---- DLQ header (MQDLH) -----------------------------------------------

	struct DlqFields
	{
		bool included = false;
		int ccsid = 0;
		int encoding = 0;		// NUMERIC_PC or NUMERIC_HOST
		int pdEncoding = 0;
		int floatEncoding = 0;	// FLOAT_PC / FLOAT_UNIX / FLOAT_390
		std::string format;		// 8 chars, blank-padded, trailing blanks trimmed for display
		int reason = 0;
		std::string destQueueName;			// MQCHAR48
		std::string destQueueManagerName;	// MQCHAR48
		int putApplType = 0;
		std::string putApplName;	// MQCHAR28
		std::string putDate;		// MQCHAR8
		std::string putTime;		// MQCHAR8
	};

	// Parse MQDLH from `data[0..length)`.  Returns the number of bytes consumed
	// (0 if the header is not present or is invalid).
	size_t parseDlq(const unsigned char * data, size_t length, int ccsid, int encoding,
	                DlqFields & out, std::vector<std::string> & warnings);

	// Build a MQDLH and append its bytes to `out`.  No-op when !f.included.
	void buildDlq(const DlqFields & f, std::vector<unsigned char> & out);

	// ---- CICS header (MQCIH) -----------------------------------------------

	struct CicsFields
	{
		bool included = false;
		int version = 2;		// MQCIH_VERSION_2
		int ccsid = 0;
		int encoding = 0;
		int pdEncoding = 0;
		int floatEncoding = 0;
		std::string format;		// 8 chars, blank-padded
		int flags = 0;
		int returnCode = 0;
		int compCode = 0;
		int reason = 0;
		int uowControl = 0;
		int getWaitInterval = -1;
		int linkType = 0;
		int outputDataLength = -1;
		int facilityKeepTime = 0;
		int adsDescriptor = 0;
		int conversationalTask = 0;
		int taskEndStatus = 0;
		std::string facility;		// MQBYTE8 as hex string
		std::string function;		// MQCHAR4
		std::string abendCode;		// MQCHAR4
		std::string authenticator;	// MQCHAR8
		std::string replyToFormat;	// MQCHAR8
		std::string remoteSysId;	// MQCHAR4
		std::string remoteTransId;	// MQCHAR4
		std::string transactionId;	// MQCHAR4
		std::string facilityLike;	// MQCHAR4
		std::string attentionId;	// MQCHAR4
		std::string startCode;		// MQCHAR4
		std::string cancelCode;		// MQCHAR4
		std::string nextTransactionId;	// MQCHAR4
		int cursorPosition = 0;
		int errorOffset = 0;
		int inputItem = 0;
	};

	size_t parseCics(const unsigned char * data, size_t length, int ccsid, int encoding,
	                 CicsFields & out, std::vector<std::string> & warnings);

	void buildCics(const CicsFields & f, std::vector<unsigned char> & out);

	// ---- IMS header (MQIIH) -----------------------------------------------

	struct ImsFields
	{
		bool included = false;
		int ccsid = 0;
		int encoding = 0;
		int pdEncoding = 0;
		int floatEncoding = 0;
		std::string format;		// 8 chars
		int flags = 0;
		std::string lTerm;			// MQCHAR8
		std::string mapName;		// MQCHAR8
		std::string replyToFormat;	// MQCHAR8
		std::string authenticator;	// MQCHAR8
		int commitMode = 0;
		int transState = 0;
		int securityScope = 0;
		bool passExpiration = false;
		bool addLength = false;
		bool noReplyFormat = false;
	};

	size_t parseIms(const unsigned char * data, size_t length, int ccsid, int encoding,
	                ImsFields & out, std::vector<std::string> & warnings);

	void buildIms(const ImsFields & f, std::vector<unsigned char> & out);

	// ---- RFH v1 header (MQRFH) --------------------------------------------

	struct Rfh1Fields
	{
		bool included = false;
		int ccsid = 0;			// CodedCharSetId of the data after MQRFH
		int encoding = 0;
		int pdEncoding = 0;
		int floatEncoding = 0;
		std::string format;		// 8 chars
		int flags = 0;

		// Name-value variable part (OPT_APP_GRP, OPT_MSG_TYPE, pubsub, pscr fields)
		std::string appGroup;	// OPT_APP_GRP value
		std::string msgType;	// OPT_MSG_TYPE value
		bool hasMcd = false;
		bool hasPubsub = false;
		bool hasPscr = false;
		std::string pscrOther;	// unrecognised fields (raw text)
	};

	size_t parseRfh1(const unsigned char * data, size_t length, int ccsid, int encoding,
	                 Rfh1Fields & out, std::vector<std::string> & warnings);

	void buildRfh1(const Rfh1Fields & f, std::vector<unsigned char> & out);

	// ---- RFH v2 sub-folders -----------------------------------------------

	struct Rfh2McdFields
	{
		std::string msgDomain;	// <Msd>
		std::string msgSet;		// <Set>
		std::string msgType;	// <Type>
		std::string msgFmt;		// <Fmt>
	};

	struct Rfh2JmsFields
	{
		std::string dst;			// <Dst>
		std::string rto;			// <Rto>
		std::string expiration;		// <Exp>
		std::string correlId;		// <Cid>
		std::string groupId;		// <Gid>
		std::string sequence;		// <Seq> (numeric as string)
		std::string timestamp;		// <Tms>
		std::string priority;		// <Pri> (numeric)
		std::string deliveryMode;	// <Dlv> (numeric)
		std::string userDef;		// unrecognised elements
	};

	// ---- RFH v2 header (MQRFH2) -------------------------------------------

	struct Rfh2Fields
	{
		bool included = false;
		int ccsid = 0;				// CodedCharSetId (encoding of data after RFH2)
		int nameValueCcsid = 1208;	// NameValueCCSID (encoding of the folder XML)
		int encoding = 0;
		int pdEncoding = 0;
		int floatEncoding = 0;
		std::string format;			// 8 chars, format of data after RFH2
		int flags = 0;

		// Sub-folders: the `has*` flags mirror Windows' m_incl_* booleans
		bool hasMcd = false;
		Rfh2McdFields mcd;

		bool hasJms = false;
		Rfh2JmsFields jms;

		bool hasUsr = false;
		std::string usrXml;		// raw UTF-8 XML content of the <usr> folder

		bool hasPs = false;
		std::string psXml;		// raw UTF-8 XML content of the <psc> folder

		bool hasPscr = false;
		std::string pscrXml;	// raw UTF-8 XML content of the <pscr> folder

		bool hasOther = false;
		std::string otherXml;	// concatenated XML of unrecognised folders (no length prefix)
	};

	size_t parseRfh2(const unsigned char * data, size_t length, int ccsid, int encoding,
	                 Rfh2Fields & out, std::vector<std::string> & warnings);

	void buildRfh2(const Rfh2Fields & f, std::vector<unsigned char> & out);

	// ---- Aggregate --------------------------------------------------------

	struct ParsedHeaders
	{
		DlqFields  dlq;
		CicsFields cics;
		ImsFields  ims;
		Rfh1Fields rfh1;
		Rfh2Fields rfh2;
		std::vector<std::string> warnings;

		bool hasAny() const { return dlq.included || cics.included || ims.included || rfh1.included || rfh2.included; }
	};

	// Populate `out` by walking the header chain embedded in `data`.
	// `initialCcsid` and `initialEncoding` (NUMERIC_PC or NUMERIC_HOST) come from
	// the MQMD (or detectLeadingHeader for file-read paths without an MQMD).
	void parseHeaders(const unsigned char * data, size_t dataLength,
	                  int initialCcsid, int initialEncoding, ParsedHeaders & out);

	// Build all included headers and return the concatenated bytes.
	// Returns an empty vector when !headers.hasAny().
	std::vector<unsigned char> buildHeaders(const ParsedHeaders & headers);
}
