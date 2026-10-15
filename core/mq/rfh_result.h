// rfh_result.h - outcome of a core operation.
//
// Core operations never show UI or exit; they report what happened here and the
// host decides how to present it.
#pragma once

#include <cmqc.h>

#include <string>
#include <vector>

namespace rfh
{
	struct Result
	{
		MQLONG cc = MQCC_OK;		// MQ completion code; MQCC_FAILED for non-MQ failures too
		MQLONG rc = MQRC_NONE;		// MQ reason code; MQRC_NONE for non-MQ failures
		std::string text;			// human-readable outcome, empty on plain success
		std::vector<std::string> warnings;

		// MQCC_WARNING counts as ok. A caller inspecting a get must still look at rc:
		// for example MQRC_TRUNCATED_MSG_FAILED (2080) is a warning where no message
		// was removed. MqSession grows its buffer, so callers normally never see 2080.
		bool ok() const { return cc != MQCC_FAILED; }

		static Result success() { return Result(); }

		// A failure that did not come from an MQ call, e.g. a missing library.
		static Result failure(const std::string & text);

		// The outcome of an MQ call such as "MQGET" with its completion and reason codes.
		// text has the form "MQGET cc=2 rc=2033 MQRC_NO_MSG_AVAILABLE (No message available)".
		static Result fromMq(const char * operation, MQLONG cc, MQLONG rc);
	};

	// "MQRC_NO_MSG_AVAILABLE" for 2033, or "MQRC_UNKNOWN" when not a known reason code.
	const char * reasonName(MQLONG rc);
}
