// rfh_mqmd_fields.h - the fields of the MQMD tab and their conversion to and
// from an MQMD.
//
// MqmdFields holds what the MQMD tab shows and edits: text for numeric fields
// (so an empty field means "leave the MQMD default"), option values for radio
// groups, flags for check boxes, and the three identifiers as raw bytes.
// mqmd_fields::toMqmd is the port of MQMDPAGE::buildMQMD and fromMqmd is the
// field-setting part of MQMDPAGE::processMQMD; both keep the Windows results,
// including their quirks, so files and messages built from the same fields are
// byte-identical on both platforms.
#pragma once

#include <cmqc.h>

#include <string>

#include "rfhconst.h"

namespace rfh
{
	// The defaults are the state MQMDPAGE::clearMQMD leaves.
	struct MqmdFields
	{
		// text fields, exactly as shown on the MQMD tab
		std::string format;			// up to 8 characters, e.g. "MQSTR"
		std::string ccsid;			// e.g. "1208"
		std::string msgType;		// e.g. "8 Datagram"; only the leading number is used
		std::string expiry;
		std::string feedback;
		std::string priority;
		std::string backoutCount = "0";
		std::string replyToQ;
		std::string replyToQMgr;
		std::string userId;
		std::string accountingToken;	// hex, two characters per byte
		std::string applIdentity;
		std::string applOrigin;
		std::string putApplName;
		std::string putApplType;	// e.g. "6 Unix"; only the leading number is used
		std::string dateTime;		// "YYYY/MM/DD HH:MM:SS.hh"
		std::string seqNo = "1";
		std::string offset;
		std::string originalLength = "-1";	// MQOL_UNDEFINED

		// radio groups; the report choices are REPORT_NONE / REPORT_YES / REPORT_DATA /
		// REPORT_FULL, and -1 means "none selected" and builds no report bits
		int persistence = MQPERSIST_NO;		// MQPERSIST_NO / MQPERSIST_YES / MQPERSIST_AS_Q
		int encoding = NUMERIC_PC;			// NUMERIC_PC / NUMERIC_HOST
		int pdEncoding = NUMERIC_PC;
		int floatEncoding = FLOAT_PC;		// FLOAT_PC / FLOAT_UNIX / FLOAT_390 / FLOAT_TNS
		int reportException = -1;
		int reportExpiration = -1;
		int reportCoa = -1;
		int reportCod = -1;

		// check boxes
		bool reportPan = false;
		bool reportNan = false;
		bool reportActivity = false;
		bool reportDiscard = false;
		bool reportPassMsgId = false;
		bool reportPassCorrelId = false;
		bool reportPassDiscardAndExpiry = false;
		bool groupYes = false;
		bool groupLast = false;
		bool segmentYes = false;
		bool segmentLast = false;
		bool segmentAllowed = false;

		// identifiers, as raw bytes
		MQBYTE24 msgId = { 0 };
		MQBYTE24 correlId = { 0 };
		MQBYTE24 groupId = { 0 };
	};

	namespace mqmd_fields
	{
		// Fills mqmd from fields, as MQMDPAGE::buildMQMD does. mqmd must already hold
		// the values to keep for empty text fields (normally MQMD2_DEFAULT). Fields
		// longer than their MQMD field are truncated; Windows overruns into the next
		// MQMD field instead (Format, ReplyToQ, ReplyToQMgr). StrucId and Version are not
		// set, and Format is copied without blank filling, so start from MQMD2_DEFAULT.
		void toMqmd(const MqmdFields & fields, MQMD2 & mqmd);

		// Sets fields from a native-encoding MQMD of version 1 or 2, as
		// MQMDPAGE::processMQMD does after its byte-order and EBCDIC translation.
		// The float encoding is left unchanged, as on Windows. The document's display
		// state (numeric, packed decimal and character format) is not set here; callers
		// derive it from the MQMD. msgType and putApplType are read by their leading
		// number. Returns false, leaving fields unchanged, for any other version.
		bool fromMqmd(const MQMD2 & mqmd, MqmdFields & fields);

		// Resets fields as MQMDPAGE::clearMQMD does; only the float encoding is left
		// unchanged.
		void clear(MqmdFields & fields);

		// "8 Datagram" for 8, "" for types the MQMD tab does not name (including
		// application-defined types), as MQMDPAGE::setMsgType does.
		std::string msgTypeText(MQLONG msgType);
	}
}
