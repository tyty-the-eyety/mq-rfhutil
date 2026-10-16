// rfh_message.h - one MQ message: its descriptor, its MQ headers and its body.
//
// headers holds the MQ headers that follow the MQMD (DLQ, CICS, IMS, RFH1, RFH2,
// MQMDE) as raw bytes, exactly as they arrived or were read from a file; body is
// the data after them. On the wire and in files the two are simply concatenated.
// Until the header tabs are ported, headers are carried unchanged rather than
// rebuilt from tab fields as Windows RFHUtil does.
#pragma once

#include <cmqc.h>

#include <vector>

namespace rfh
{
	struct Message
	{
		MQMD2 mqmd = { MQMD2_DEFAULT };
		std::vector<unsigned char> headers;
		std::vector<unsigned char> body;

		Message() { mqmd.Version = MQMD_VERSION_2; }
	};
}
