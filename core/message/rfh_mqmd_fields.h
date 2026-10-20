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
#include <time.h>

#include "rfh_header_chain.h"
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
		int floatEncoding = -1;				// -1 = no selection (Windows default); FLOAT_PC / FLOAT_UNIX / FLOAT_390 / FLOAT_TNS
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

	// What MQMDPAGE::setMessageMQMD takes besides the page: the Set Iden Context and
	// Set All Context check boxes (m_setUserID, m_set_all of DataArea).
	struct PutContext
	{
		bool setIdentity = false;		// Set Iden Context
		bool setAll = false;			// Set All Context
		// Set when the OS user name could be looked up (GetUserNameEx or GetUserName
		// succeeded); empty means the lookup failed. setMessageMQMD never copies the name
		// itself: see toPutMqmd. It has no observable effect when the MQMD starts from
		// MQMD2_DEFAULT, whose UserIdentifier is already zeros; it matters only for a
		// pre-populated MQMD, where a non-empty value zeroes UserIdentifier. Windows has the
		// lookup result from the OS; on Linux "lookup succeeded" is the caller's choice
		// (empty = lookup failed).
		std::string osUser;
		// Not read: setMessageMQMD formats the current time but never uses it, so a blank
		// Put Date/Time is written as binary zeros. Kept so callers need not change if
		// that Windows defect is ever fixed.
		const struct tm * now = nullptr;
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
		// number. For a version 1 MQMD, GroupId is still copied from mqmd, as on Windows;
		// pass the bytes that follow the 324-byte MQMD (or zeros) to get Windows' result.
		// Returns false, leaving fields unchanged, for any other version.
		bool fromMqmd(const MQMD2 & mqmd, MqmdFields & fields);

		// The port of MQMDPAGE::processMessageMQMD (MQMDPAGE.cpp:1530-1686) after clearMQMD
		// (:1688-1785): the MQMD tab's values for an MQMD that MQ returned from a get or
		// browse. `received` is in host byte order, as MQ returns it, so nothing is swapped
		// or translated. Text fields are read at their MQ size and never past it, whether or
		// not they hold a blank or NUL. The float encoding is left unchanged, as on Windows.
		// Never fails: any Version is accepted (see rule 1).
		//
		// What of Windows' DataArea::processMessage (DataArea.cpp:7064-7144) is not ported:
		// the brief and the inventory expected an ASCII/EBCDIC/endian pre-translation of the
		// MQMD there; there is none. processMessage calls the page at once, and the get path
		// (processMQGet, DataArea.cpp:7851) hands the MQMD from MQGET straight on. The
		// translation lives in processMQMD (file path) and translateMQMD (mqsubs.cpp:410,
		// used by WritePubs), neither of which a get uses; MQ returns native-order ASCII-
		// character fields, so fromGetMqmd needs none. What processMessage does after the
		// page is document state, not tab fields, and stays with the caller: numeric format
		// from MQENC_INTEGER_REVERSED (fromMqmd's caller does the same), the CCSID sanity
		// check (a value outside 0..99999 is tried byte-reversed, else "Invalid code page in
		// MQMD - %d" and 0; the tab keeps showing the raw CCSID), the character format from
		// the CCSID, and parsing the headers. Unlike processMQMD, a get never sets the
		// document's packed decimal format (it stays NUMERIC_PC after clearMQMD).
		//
		// Differences from fromMqmd (the file path, MQMDPAGE::processMQMD :1092-1380), each
		// pinned by a test:
		// 1. Version: 2 or more is read as version 2, anything else (0, 1, negative) as
		//    version 1 (:1655); fromMqmd accepts only 1 and 2 and returns false otherwise.
		// 2. Group Id: copied only for version 2 (:1658); fromMqmd copies it for version 1
		//    too (bytes after the MQMD). Here a version 1 Group Id stays zero.
		// 3. Format: all 8 bytes up to the first NUL, trailing blanks kept (:1641-1643);
		//    fromMqmd ends the text at the first byte at or below a blank.
		// 4. Reply To Queue and Queue Manager: up to the first NUL, trailing blanks kept
		//    (:1646-1652); fromMqmd removes trailing blanks and controls.
		// 5. Put Application Name: up to the first NUL, trailing blanks kept (:1636-1638);
		//    fromMqmd trims it.
		// 6. Put Application Type: setPutApplType (:1046) shows an unnamed positive type as
		//    its number and any other unnamed type as "-1 Other/Unknown"; fromMqmd shows "".
		// 7. Put Date/Time: formatDateTime (comsubs.cpp:636) builds one 22 character string
		//    and the first NUL anywhere ends all of it, so zero date and time give "";
		//    fromMqmd cuts the date and the time separately and gives " ".
		// Equal to fromMqmd: report options, message type, expiry, feedback, encodings, CCSID,
		// priority, persistence, Msg Id, Correl Id, backout count, User Id, accounting token,
		// Appl Identity and Origin (trimmed), and for version 2 the sequence number, offset,
		// flags and original length. A version 1 MQMD leaves those as clearMQMD does.
		void fromGetMqmd(const MQMD2 & received, MqmdFields & fields);

		// Sets the MQMD tab's version 2 fields from an MQMDE, as MQMDPAGE::processMQMDE does
		void applyMdExtension(const MdExtensionFields & e, MqmdFields & fields);

		// Resets fields as MQMDPAGE::clearMQMD does; only the float encoding is left
		// unchanged.
		void clear(MqmdFields & fields);

		// Fills mqmd as MQMDPAGE::setMessageMQMD does for a put (toMqmd is the file-write
		// path and stays as it is; the rules differ). mqmd must hold MQMD2_DEFAULT, whose
		// values stay wherever a rule does not write. Rules, with the Windows quirks kept:
		// - Format is blank filled and cut to 8; ReplyToQ and ReplyToQMgr are copied
		//   without filling (the default content stays after a short text), cut to the
		//   field size where Windows would overrun.
		// - Encoding and Persistence are always set; CCSID, Priority, Expiry, Feedback,
		//   Original Length and Seq No only when their text is not empty.
		// - MsgId, CorrelId and GroupId are copied only when not all zero. MsgFlags are
		//   set only when a flag box is checked (Offset then too, when its text is not
		//   empty); a GroupId with no flag box checked sets MQMF_LAST_MSG_IN_GROUP.
		// - Report is set only when not zero; MsgType only for the types toMqmd accepts.
		// - With Set Iden or Set All: a User Id text is copied zero padded to 12; when it
		//   is empty and osUser is not, UserIdentifier becomes zeros (Windows passes the OS
		//   user to setUserId, which reads the empty page field instead). The accounting
		//   token is converted from hex (at most 48 bytes, 32 stored, rest zeros).
		// - With Set All only: Appl Identity, Appl Origin (at most 4) and Put Appl Name (at
		//   most 28) are zero padded; Put Appl Type is its leading number (0 when empty);
		//   Put Date/Time come from the date text "YYYY/MM/DD HH:MM:SS.hh", zeros when empty.
		// StrucId, Version and BackoutCount are never written.
		//
		// Known platform divergence: the numeric fields use atoi, and for text that overflows
		// an int the result is undefined; glibc on Linux LP64 wraps the value where MSVC
		// saturates. That is not valid input, so the byte-identical rule does not cover it.
		void toPutMqmd(const MqmdFields & fields, const PutContext & context, MQMD2 & mqmd);

		// What the MQMD tab takes from the MQMD returned by a put, as
		// MQMDPAGE::updateMQMDafterPut does: Seq No, Offset (the returned offset plus
		// msgLen while a segment sequence continues, else "0"), MsgId, and GroupId (zeroed
		// unless a group or segment box is checked). msgLen is the byte count passed to MQPUT
		// (headers plus data; Windows passes msgSize, DataArea.cpp:6457). returned is the same
		// MQMD that MQPUT updated. Windows calls this after a warning but not after a failed
		// put. It must run on the same MqmdFields that built the put, because it reads the
		// group and segment boxes. The offset sum wraps modulo 2^32 like MSVC's 32 bit signed
		// addition (a negative returned Offset is simply added to).
		void applyAfterPut(const MQMD2 & returned, MQLONG msgLen, MqmdFields & fields);

		// "8 Datagram" for 8, "" for types the MQMD tab does not name (including
		// application-defined types), as MQMDPAGE::setMsgType does.
		std::string msgTypeText(MQLONG msgType);
	}
}
