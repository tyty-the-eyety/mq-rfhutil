// rfh_message.h - one MQ message: its descriptor and its bytes.
//
// Phase 1 keeps the MQ headers that follow the MQMD (RFH2, DLQ, CICS, ...) inside
// body, exactly as MQGET returns them. Header parsing comes with the file format
// extraction, which adds a header chain without changing body's meaning.
#pragma once

#include <cmqc.h>

#include <vector>

namespace rfh
{
	struct Message
	{
		MQMD2 mqmd = { MQMD2_DEFAULT };
		std::vector<unsigned char> body;

		Message() { mqmd.Version = MQMD_VERSION_2; }
	};
}
