// rfh_pcf_queues.h - the PCF messages that list queue names: the Inquire Queue request and the
// reading of one reply message. Pure byte handling; MqSession::listQueues does the MQ calls.
//
// Port of DataArea::buildPCFRequestQueues (DataArea.cpp:22646-22809) and of the reply walk in
// DataArea::processQueuesReply (DataArea.cpp:21831-22022) with the header and failure handling
// of DataArea::getListOfNames (DataArea.cpp:22968-23389).
//
// Request (what Windows sends, in native byte order): an MQCFH with Command MQCMD_INQUIRE_Q,
// MsgSeqNumber 1, Control MQCFC_LAST and 3 parameters (4 with the cluster filter):
//   MQCFST MQCA_Q_NAME "*" (CCSID MQCCSI_DEFAULT, string length 1, padded to 4 bytes)
//   MQCFIN MQIA_Q_TYPE = the wanted queue type
//   [MQCFIN MQIACF_CLUSTER_INFO = 0]      only when clusters are included
//   MQCFIL MQIACF_Q_ATTRS = { MQCA_Q_NAME, MQIA_Q_TYPE }
// A queue manager at command level 600 or above (or one whose level is unknown here) is sent
// MQCFT_COMMAND_XR and MQCFH_VERSION_3, which z/OS requires; an older one MQCFT_COMMAND and version 1.
//
// Reply, per message: it is read only when its type is MQCFT_RESPONSE or MQCFT_XR_ITEM and its
// CompCode is not MQCC_FAILED. The parameters are walked in order, each stepped over by its own
// StrucLength; the walk ends as soon as the queue name (an MQCFST with MQCA_Q_NAME) and the queue
// type (an MQCFIN with MQIA_Q_TYPE) are both found, so bytes after them are never looked at. As in
// Windows, any other parameter type is skipped, strings are translated from EBCDIC when the message
// CCSID says so, integers are byte-swapped when the message encoding is not the host's, and the name
// ends at the first blank.
//
// Differences from Windows, on purpose (Windows reads the reply with no bounds checks):
// - Every length is checked against the bytes received. A reply shorter than the 36 byte header, a
//   header whose own StrucLength is below 36 or past the bytes received (the parameters start at that
//   StrucLength, so a later, longer header version is still read), a
//   parameter that is shorter than its own fixed part or runs past the end, and a parameter length
//   of 0 (Windows would step over it forever) stop the walk with a warning; a name whose length
//   points past its parameter, is empty or is longer than 48 characters (Windows overruns its
//   48 byte field, or inserts a bare type letter for an empty name) is dropped with a warning.
// - A reply whose CompCode is MQCC_FAILED is reported with its Reason (Windows reads the reply
//   and drops the diagnosis: errtxt is a local, so the list is empty and the call "succeeds").
// - A reply whose CompCode is MQCC_WARNING is read like an OK one (Windows ignores it) and its
//   Reason is kept for the caller to report.
// - An undefined integer encoding is taken to be the host's; Windows swaps unless the encoding
//   is explicitly the PC's.
#pragma once

#include <cmqc.h>

#include <string>
#include <vector>

namespace rfh
{
	namespace pcf
	{
		const size_t kHeaderLength = 36;			// MQCFH_STRUC_LENGTH
		const size_t kMaxQueueNameLength = 48;		// MQ_Q_NAME_LENGTH

		// MQMD Expiry of the Inquire Queue request, in tenths of a second, for a reply wait of
		// replyWaitMs: ten times the wait, at least 100 (10 seconds) and at most 36000 (1 hour). A request
		// that is never answered (command server stopped, reply queue deleted) is removed from
		// SYSTEM.ADMIN.COMMAND.QUEUE by the queue manager after that time instead of staying to be
		// run by a server that starts later. A wait of zero gets the minimum, a wait that never ends
		// (negative) the maximum. Never MQEI_UNLIMITED. (Windows: 10 * 1000 tenths, 1000 seconds.)
		MQLONG requestExpiryTenths(MQLONG replyWaitMs);

		// The Inquire Queue request for queues of queueType (MQQT_LOCAL, MQQT_REMOTE, MQQT_ALIAS).
		// extended selects MQCFT_COMMAND_XR and header version 3.
		std::vector<unsigned char> buildInquireQueueRequest(MQLONG queueType, bool clusterInfo, bool extended);

		// What one reply message says.
		struct QueueReply
		{
			bool headerValid = false;	// false when the message is too short or its StrucLength is below 36 or past the message; nothing else is set
			MQLONG type = 0;			// MQCFH Type
			MQLONG compCode = 0;		// MQCFH CompCode
			MQLONG reason = 0;			// MQCFH Reason
			bool last = false;			// Control is anything but MQCFC_NOT_LAST, as Windows' loop tests it
			MQLONG parameterId = 0;		// MQIACF_PARAMETER_ID of a failed reply, 0 when absent
			bool hasQueue = false;		// a queue name and type were found
			MQLONG queueType = 0;		// MQQT_* as the reply reports it
			std::string name;			// the queue name, trimmed
			std::vector<std::string> warnings;
		};

		// Reads one reply. data/length are the bytes received (never more than the buffer holds);
		// encoding and ccsid are those of the message descriptor.
		QueueReply parseQueueReply(const unsigned char * data, size_t length, MQLONG encoding, MQLONG ccsid);
	}
}
