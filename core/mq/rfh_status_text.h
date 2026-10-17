// rfh_status_text.h - the status line wording of the Windows program for a failed MQ call.
//
// Ported from DataArea::setErrorMsg (RFHUtil/DataArea.cpp:9315-9889), which has 102 case labels
// keyed by reason code. The texts are byte for byte those of Windows, so the Qt application shows
// "2033 No messages in queue" instead of the technical text of Result::fromMq.
//
// Operation words the Windows callers pass (65 call sites): "Open", "OpenBase", "Close", "Get",
// "Get (Browse)", "Get (QBrowse)", "GetNext", "BrowsePrevGet", "BrowsePrevGetMsg", "Get under cursor",
// "Getting message", "Put", "Connect", "Disc", "begin", "commit", "rollback", "MQCRTMH", "MQDLTMH",
// "MQSETMP", "MQINQMP", "MQGETCAPT", "MQSub", "MQSubRQ", "OpenSubQ", "CloseSub", "CloseSubQ",
// "PubOpen", "PubXMQSetMp", "MQCLOSE", "MQCMIT". Two call sites pass a variable, so any text is accepted: getType at
// DataArea.cpp:7925 (the callers of processMQGet pass "Get", "StartBr", "BrowseNext" and "PSGet") and
// operation at 14476 (processSaveMsgsCC; the callers pass "GetSaveMsgs", "Get2SaveMsgs", "Get3SaveMsgs"
// and "Get under cursor"). The word is used only by the reason codes 2035, 2046, 2057, 2058, 2059 and
// by the default text. Windows formats into a 512 byte work area: an operation word that does not fit
// cuts the text to 511 characters here (snprintf), where Windows would overrun its buffer.
//
// Differences from Windows, on purpose:
// - Windows appends the text to m_error_msg (appendError: CRLF between messages); statusText
//   returns the one text and the caller decides how to join.
// - Windows also changes state in three cases: 2009 (connectionLostCleanup), 2033 (m_q_depth = 0)
//   and 6104 (closeQ). A text function does not do that; the session and the operations layer do.
// - Windows writes the text to the trace file; the caller does that.
// - Windows passes cc and rc as two arguments; here they come from the Result. The cc is used only
//   by the default text ("*Error cc=%d rc=%d ...").
// - Result::warnings and Result::text are ignored when rc != MQRC_NONE; the text comes from the reason
//   code alone.
// - A Result without a reason code (rc MQRC_NONE) is not a Windows case: a failure keeps its own
//   text, anything else gives an empty string.
#pragma once

#include <string>

#include "rfh_result.h"

namespace rfh
{
	struct StatusContext
	{
		const char * operation = "";	// the Windows operation word, see above
		MQLONG maxMsgLenQm = 0;			// QMgr max for 2010, 2030 and 2031 (0 = unknown)
		MQLONG maxMsgLenQueue = 0;		// Queue max for the same texts
	};

	// The text Windows shows for a failed call. A plain success gives an empty string.
	std::string statusText(const Result & result, const StatusContext & context);
}
