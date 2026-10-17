// rfh_status_text.cpp - see rfh_status_text.h

#include "rfh_status_text.h"

#include <stdio.h>

#include "mqerror.h"

namespace rfh
{
	namespace
	{
		// The reason codes of DataArea::setErrorMsg (RFHUtil/DataArea.cpp:9330-9878) whose text is fixed,
		// in the order of the Windows switch. The texts are copied byte for byte, including the
		// misspelling in 6104 and the repeated number in 2101/2102.
		const char * fixedText(MQLONG rc)
		{
			switch (rc)
			{
			case MQRC_ALIAS_BASE_Q_TYPE_ERROR:
				return "2001 Invalid base queue type for alias queue";
			case MQRC_BUFFER_ERROR:
				return "2004 Buffer error";
			case MQRC_BUFFER_LENGTH_ERROR:
				return "2005 Buffer length error";
			case MQRC_CONNECTION_BROKEN:
				return "2009 Connection to QM has been lost - Q closed";
			case MQRC_EXPIRY_ERROR:
				return "2013 Invalid expiry value";
			case MQRC_FEEDBACK_ERROR:
				return "2014 Invalid feedback value";
			case MQRC_GET_INHIBITED:
				return "2016 Queue get inhibited";
			case MQRC_HCONN_ERROR:
				return "2018 Connection handle error";
			case MQRC_HOBJ_ERROR:
				return "2019 Queue handle error";
			case MQRC_SYNCPOINT_LIMIT_REACHED:
				return "2024 Maximum uncommitted messages in UOW exceeded for queue manager";
			case MQRC_MAX_CONNS_LIMIT_REACHED:
				return "2025 Maximum connections exceeded for queue manager";
			case MQRC_MD_ERROR:
				return "2026 Error in MQMD";
			case MQRC_MISSING_REPLY_TO_Q:
				return "2027 Reply to queue name missing - request message or report options specified";
			case MQRC_MSG_TYPE_ERROR:
				return "2029 Invalid message type";
			case MQRC_NO_MSG_AVAILABLE:
				return "2033 No messages in queue";
			case MQRC_NOT_OPEN_FOR_INQUIRE:
				return "2038 Not open for inquire";
			case MQRC_NOT_OPEN_FOR_OUTPUT:
				return "2039 Not open for output";
			case MQRC_OBJECT_CHANGED:
				return "2041 Object definition changed";
			case MQRC_OBJECT_IN_USE:
				return "2042 Object in use";
			case MQRC_OPTION_NOT_VALID_FOR_TYPE:
				return "2045 Cannot read remote queue";
			case MQRC_PERSISTENCE_ERROR:
				return "2047 Persistence error";
			case MQRC_PERSISTENT_NOT_ALLOWED:
				return "2048 Persistence not supported on this queue";
			case MQRC_PRIORITY_EXCEEDS_MAXIMUM:
				return "2049 Priority exceeds maximum";
			case MQRC_PRIORITY_ERROR:
				return "2050 Invalid priority value";
			case MQRC_PUT_INHIBITED:
				return "2051 Put inhibited";
			case MQRC_Q_DELETED:
				return "2052 Queue has been deleted";
			case MQRC_Q_FULL:
				return "2053 Queue full";
			case MQRC_Q_SPACE_NOT_AVAILABLE:
				return "2056 Out of space on queue disk";
			case MQRC_REPORT_OPTIONS_ERROR:
				return "2061 Invalid report options";
			case MQRC_SECURITY_ERROR:
				return "2063 Security exit rejected connection";
			case MQRC_STORAGE_NOT_AVAILABLE:
				return "2071 Main storage not available";
			case MQRC_TRUNCATED_MSG_ACCEPTED:
				return "2079 Truncated message accepted";
			case MQRC_TRUNCATED_MSG_FAILED:
				return "2080 Message too long for buffer";
			case MQRC_UNKNOWN_OBJECT_NAME:
				return "2085 No such queue";
			case MQRC_UNKNOWN_REMOTE_Q_MGR:
				return "2087 Invalid remote queue manager name";
			case MQRC_NOT_OPEN_FOR_PASS_ALL:
				return "2093 Queue not open for pass all";
			case MQRC_NOT_OPEN_FOR_PASS_IDENT:
				return "2094 Queue not open for pass identity";
			case MQRC_NOT_OPEN_FOR_SET_ALL:
				return "2095 Queue not open for set all";
			case MQRC_NOT_OPEN_FOR_SET_IDENT:
				return "2096 Queue not open for set identity";
			case MQRC_OBJECT_DAMAGED:
				return "2101 Object has been corrupted";
			case MQRC_RESOURCE_PROBLEM:
				return "2101 Resource shortage - see FFST record";
			case MQRC_FORMAT_ERROR:
				return "2110 Convert requested but format does not allow conversion";
			case MQRC_SOURCE_CCSID_ERROR:
				return "2111 Invalid or unsupported code page";
			case MQRC_HEADER_ERROR:
				return "2142 Invalid MQ header";
			case MQRC_Q_MGR_QUIESCING:
				return "2161 Queue manager is quiescing";
			case MQRC_Q_MGR_STOPPING:
				return "2162 Queue manager is stopping";
			case MQRC_STORAGE_MEDIUM_FULL:
				return "2192 No more space on disk";
			case MQRC_PAGESET_ERROR:
				return "2193 Page set error";
			case MQRC_UNEXPECTED_ERROR:
				return "2195 Internal error processing request";
			case MQRC_CONNECTION_QUIESCING:
				return "2202 Connection is quiescing";
			case MQRC_CONNECTION_STOPPING:
				return "2203 Connection is stopping";
			case MQRC_CD_ERROR:
				return "2277 Invalid channel name";
			case MQRC_SELECTOR_NOT_SUPPORTED:
				return "2318 QMgr does not support selectors";
			case MQRC_RFH_ERROR:
				return "2334 Error in RFH";
			case MQRC_RFH_STRING_ERROR:
				return "2335 RFH string error";
			case MQRC_RFH_COMMAND_ERROR:
				return "2336 RFH command error";
			case MQRC_RFH_PARM_ERROR:
				return "2337 RFH parameter error";
			case MQRC_RFH_DUPLICATE_PARM:
				return "2338 Duplicate parameter in RFH";
			case MQRC_RFH_PARM_MISSING:
				return "2339 Missing parameter in RFH";
			case MQRC_KEY_REPOSITORY_ERROR:
				return "2381 Key repository error";
			case MQRC_SSL_CONFIG_ERROR:
				return "2392 SSL configuration error";
			case MQRC_SSL_INITIALIZATION_ERROR:
				return "2393 SSL unable to initialize - check SSL parms";
			case MQRC_SSL_NOT_ALLOWED:
				return "2396 SSL connection not allowed";
			case MQRC_SSL_PEER_NAME_MISMATCH:
				return "2398 Peer name mismatch";
			case MQRC_SSL_PEER_NAME_ERROR:
				return "2399 Peer name error";
			case MQRC_SSL_CERTIFICATE_REVOKED:
				return "2401 Certificate revoked";
			case MQRC_SSL_CERT_STORE_ERROR:
				return "2402 Certificate store error";
			case MQRC_SSL_KEY_RESET_ERROR:
				return "2409 SSL key reset error";
			case MQRC_MSG_NOT_ALLOWED_IN_GROUP:
				return "2417 Published message cannot be part of group";
			case MQRC_SD_ERROR:
				return "2424 Error in subscription descriptor";
			case MQRC_TOPIC_STRING_ERROR:
				return "2425 Topic string error";
			case MQRC_NO_SUBSCRIPTION:
				return "2428 No subscription";
			case MQRC_SUBSCRIPTION_IN_USE:
				return "2429 Subscription in use";
			case MQRC_SUB_USER_DATA_ERROR:
				return "2431 Subscription user data error";
			case MQRC_SUB_ALREADY_EXISTS:
				return "2432 Subscription already exists";
			case MQRC_SUB_NAME_ERROR:
				return "2440 Subscription name error";
			case MQRC_PROPERTY_NAME_ERROR:
				return "2442 Property name error";
			case MQRC_SELECTOR_SYNTAX_ERROR:
				return "2459 Selector syntax error";
			case MQRC_PROP_TYPE_NOT_SUPPORTED:
				return "2467 Property type not supported";
			case MQRC_PROPERTY_TYPE_ERROR:
				return "2473 Invalid property type";
			case MQRC_PUBLICATION_FAILURE:
				return "2502 Publication failed";
			case MQRC_SUB_INHIBITED:
				return "2503 Topic is subscribe inhibited";
			case MQRC_SELECTOR_ALWAYS_FALSE:
				return "2504 Selector is always false";
			case MQRC_PROPERTY_NAME_LENGTH_ERR:
				return "2513 Property name length error";
			case MQRC_SELECTION_STRING_ERROR:
				return "2519 Selection string error";
			case MQRC_SELECTOR_NOT_ALTERABLE:
				return "2524 Selector cannot be altered";
			case MQRC_CONNECTION_STOPPED:
				return "2528 Connection stopped";
			case MQRC_PUBSUB_INHIBITED:
				return "2531 Pub/sub inhibited";
			case MQRC_CHANNEL_NOT_AVAILABLE:
				return "2537 Channel not available or all connections in use";
			case MQRC_HOST_NOT_AVAILABLE:
				return "2538 Host not available";
			case MQRC_CHANNEL_CONFIG_ERROR:
				return "2539 channel configuration error";
			case MQRC_UNKNOWN_CHANNEL_NAME:
				return "2540 Invalid MQ server connection channel name";
			case MQRC_STANDBY_Q_MGR:
				return "2543 Queue manager is in standby mode";
			case MQRC_ATTRIBUTE_LOCKED:
				return "6104 Resouce locked - queue has been closed";
			default:
				break;
			}

			return NULL;
		}
	}

	std::string statusText(const Result & result, const StatusContext & context)
	{
		const char * operation = (context.operation != NULL) ? context.operation : "";
		char text[512];		// Windows uses a 512 byte work area; the longest text is well below it

		if (result.rc == MQRC_NONE)
		{
			// no MQ reason code: a plain success is silent, a failure that did not come from an MQ call keeps its text
			return (result.cc == MQCC_FAILED) ? result.text : std::string();
		}

		const char * fixed = fixedText(result.rc);

		if (fixed != NULL)
		{
			return fixed;
		}

		switch (result.rc)
		{
		case MQRC_DATA_LENGTH_ERROR:
			snprintf(text, sizeof(text), "2010 Max msg length exceeded - check Q/QM/Channel defs - QMgr max=%d Queue max=%d", (int)context.maxMsgLenQm, (int)context.maxMsgLenQueue);
			break;
		case MQRC_MSG_TOO_BIG_FOR_Q:
			snprintf(text, sizeof(text), "2030 Message too large for queue (check q and qm settings) QMgr max=%d Queue max=%d", (int)context.maxMsgLenQm, (int)context.maxMsgLenQueue);
			break;
		case MQRC_MSG_TOO_BIG_FOR_Q_MGR:
			snprintf(text, sizeof(text), "2031 Message is too large for queue manager QMgr max=%d Queue max=%d", (int)context.maxMsgLenQm, (int)context.maxMsgLenQueue);
			break;
		case MQRC_NOT_AUTHORIZED:
			snprintf(text, sizeof(text), "2035 Not authorized (%s)", operation);
			break;
		case MQRC_OPTIONS_ERROR:
			snprintf(text, sizeof(text), "2046 Invalid option for operation %s", operation);
			break;
		case MQRC_Q_TYPE_ERROR:
			snprintf(text, sizeof(text), "2057 Invalid queue type for operation (%s)", operation);
			break;
		case MQRC_Q_MGR_NAME_ERROR:
			snprintf(text, sizeof(text), "2058 Queue manager name not found (%s)", operation);
			break;
		case MQRC_Q_MGR_NOT_AVAILABLE:
			snprintf(text, sizeof(text), "2059 Queue manager not available (%s) - may not be started", operation);
			break;
		default:
			{
				const char * reasonText = mqReasonString((long)result.rc);

				if (reasonText != NULL)
				{
					snprintf(text, sizeof(text), "*Error cc=%d rc=%d (%s) Cannot %s", (int)result.cc, (int)result.rc, reasonText, operation);
				}
				else
				{
					snprintf(text, sizeof(text), "*Error cc=%d rc=%d Cannot %s", (int)result.cc, (int)result.rc, operation);
				}
			}
			break;
		}

		return text;
	}
}
