// rfh_mq_session.cpp - see rfh_mq_session.h

#include "rfh_mq_session.h"

#include <string.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "rfh_header_chain.h"
#include "mqsubs.h"

namespace rfh
{
	namespace
	{
		const MQLONG kWaitSliceMs = 1000;
		const size_t kMaxBufferBytes = 104857600;	// the largest message MQ allows (100 MB)

		// Copies text into a blank-padded MQ character field.
		bool copyField(MQCHAR * field, size_t fieldLength, const std::string & text)
		{
			if (text.size() > fieldLength)
			{
				return false;
			}

			memset(field, ' ', fieldLength);
			memcpy(field, text.data(), text.size());
			return true;
		}

		MQLONG bindOpenOption(BindOption bind)
		{
			switch (bind)
			{
			case BindOption::OnOpen: return MQOO_BIND_ON_OPEN;
			case BindOption::NotFixed: return MQOO_BIND_NOT_FIXED;
			case BindOption::OnGroup: return MQOO_BIND_ON_GROUP;
			case BindOption::AsQueueDefinition: break;
			}
			return MQOO_BIND_AS_Q_DEF;
		}

		MQLONG closeOptionValue(CloseOption option)
		{
			switch (option)
			{
			case CloseOption::Delete: return MQCO_DELETE;
			case CloseOption::Purge: return MQCO_DELETE_PURGE;
			case CloseOption::None: break;
			}
			return MQCO_NONE;
		}

		Result notConnected()
		{
			return Result::failure("Not connected to a queue manager");
		}

		// Reasons after which the connection or the queue manager can no longer be used.
		bool connectionLost(MQLONG rc)
		{
			return (rc == MQRC_CONNECTION_BROKEN) || (rc == MQRC_Q_MGR_QUIESCING) ||
			       (rc == MQRC_Q_MGR_STOPPING) || (rc == MQRC_CONNECTION_QUIESCING);
		}

		// Reasons after which a handle is gone even though its MQCLOSE failed.
		bool handleGone(MQLONG rc)
		{
			return (rc == MQRC_CONNECTION_BROKEN) || (rc == MQRC_HCONN_ERROR) || (rc == MQRC_HOBJ_ERROR);
		}

		// An open that fails with 2042 on a queue this session holds open conflicts with itself.
		void explainInUse(Result & r, bool heldBySession)
		{
			if (heldBySession && (r.rc == MQRC_OBJECT_IN_USE))
			{
				r.text += " - this session already has the queue open; close it first";
			}
		}
	}

	MqSession::~MqSession()
	{
		if (isConnected())
		{
			disconnect();
		}
	}

	// After a lost connection every handle is unusable: release what MQ still holds
	// and make the session ready for connect() again.
	void MqSession::dropIfBroken(MQLONG rc)
	{
		if (!connectionLost(rc))
		{
			return;
		}

		if (isConnected())
		{
			MQLONG cc = MQCC_OK;
			MQLONG reason = MQRC_NONE;
			api.disc(&hConn, &cc, &reason);	// the result is of no use: the connection is already gone
		}

		hConn = MQHC_UNUSABLE_HCONN;
		clearHandleState();
	}

	// Forgets the open queue handle and browse state; the handle itself is closed by
	// MQCLOSE or, with the connection, by MQDISC.
	void MqSession::clearHandleState()
	{
		hObj = MQHO_UNUSABLE_HOBJ;
		openName.clear();
		openRemoteQm.clear();
		openOptions = 0;
		browseCursor = 0;
	}

	Result MqSession::connect(const ConnectOptions & options)
	{
		if (isConnected())
		{
			return Result::failure("Already connected; disconnect first");
		}

		// a stale queue handle from an earlier connection must never be reused
		clearHandleState();

		if (apiInjected && ((api.connx == nullptr) || (api.disc == nullptr) || (api.open == nullptr) ||
		                    (api.close == nullptr) || (api.put == nullptr) || (api.get == nullptr) || (api.inq == nullptr)))
		{
			return Result::failure("Injected MQ API is incomplete");
		}

		Result loaded = Result::success();
		if (!apiInjected)
		{
			loaded = MqLoader::load(api);
			if (!loaded.ok())
			{
				return loaded;
			}
		}

		if ((options.mode == Mode::Bindings) && !api.bindingsAvailable)
		{
			return Result::failure("Local bindings need a local IBM MQ queue manager installation, and none was found for " + api.libraryPath + "; use client mode");
		}

		MQCHAR48 qmName;
		memset(qmName, 0, sizeof(qmName));
		if (options.qmName.size() > sizeof(qmName))
		{
			return Result::failure("Queue manager name is longer than 48 characters");
		}
		memcpy(qmName, options.qmName.data(), options.qmName.size());

		MQCNO cno = { MQCNO_DEFAULT };
		MQCD cd = { MQCD_CLIENT_CONN_DEFAULT };
		MQCSP csp = { MQCSP_DEFAULT };
		cno.Version = MQCNO_VERSION_6;	// SecurityParmsPtr (v5) and CCDTUrlPtr (v6)
		// Handles are not tied to the creating thread, and a second session on the same
		// thread gets its own connection instead of MQRC_ALREADY_CONNECTED.
		cno.Options |= MQCNO_HANDLE_SHARE_NO_BLOCK;

		if (options.mode == Mode::Bindings)
		{
			cno.Options |= MQCNO_LOCAL_BINDING;
		}
		else
		{
			cno.Options |= MQCNO_CLIENT_BINDING;

			if (!options.channel.empty())
			{
				if (!copyField(cd.ChannelName, sizeof(cd.ChannelName), options.channel))
				{
					return Result::failure("Channel name is longer than 20 characters");
				}
				if (!copyField(cd.ConnectionName, sizeof(cd.ConnectionName), options.connName))
				{
					return Result::failure("Connection name is longer than 264 characters");
				}

				cd.Version = MQCD_CURRENT_VERSION;
				cd.MaxMsgLength = 104857600;	// as the MFC application does; the default would limit messages to 4 MB
				cd.TransportType = MQXPT_TCP;
				cd.HeartbeatInterval = options.heartbeat;
				cd.KeepAliveInterval = options.keepAlive;
				cno.ClientConnPtr = &cd;
			}
			else if (!options.ccdtUrl.empty())
			{
				cno.CCDTUrlPtr = const_cast<PMQCHAR>(options.ccdtUrl.c_str());
				cno.CCDTUrlLength = (MQLONG)options.ccdtUrl.size();
			}
		}

		// IDPWOS rule: attach credentials only when a password is supplied
		if (!options.password.empty())
		{
			csp.AuthenticationType = MQCSP_AUTH_USER_ID_AND_PWD;
			csp.CSPUserIdPtr = const_cast<char *>(options.userId.c_str());
			csp.CSPUserIdLength = (MQLONG)options.userId.size();
			csp.CSPPasswordPtr = const_cast<char *>(options.password.c_str());
			csp.CSPPasswordLength = (MQLONG)options.password.size();
			cno.SecurityParmsPtr = &csp;
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.connx(qmName, &cno, &hConn, &cc, &rc);

		if (cc == MQCC_FAILED)
		{
			hConn = MQHC_UNUSABLE_HCONN;
		}

		Result result = Result::fromMq("MQCONNX", cc, rc);
		result.warnings.insert(result.warnings.end(), loaded.warnings.begin(), loaded.warnings.end());
		return result;
	}

	Result MqSession::disconnect()
	{
		if (!isConnected())
		{
			return Result::success();
		}

		Result closed = closeQueue();
		if (!isConnected())
		{
			// the close found the connection lost and released it
			Result lost = Result::success();
			if (!closed.ok())
			{
				lost.warnings.push_back(closed.text);
			}
			return lost;
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		MQHCONN handle = hConn;
		api.disc(&handle, &cc, &rc);

		Result r = Result::fromMq("MQDISC", cc, rc);
		if (!closed.ok())
		{
			r.warnings.push_back(closed.text);
		}

		if (rc == MQRC_CALL_IN_PROGRESS)
		{
			return r;	// another call is still using the connection, which stays alive
		}

		// MQDISC closes every handle of the connection, so none of them is valid now
		hConn = MQHC_UNUSABLE_HCONN;
		clearHandleState();
		return r;
	}

	Result MqSession::openQueue(const std::string & queue, const std::string & remoteQm, MQLONG options, MQHOBJ & obj, std::string * resolvedName)
	{
		MQOD od = { MQOD_DEFAULT };
		if (resolvedName != nullptr)
		{
			od.Version = MQOD_VERSION_3;
			resolvedName->clear();
		}

		if (!copyField(od.ObjectName, sizeof(od.ObjectName), queue))
		{
			return Result::failure("Queue name is longer than 48 characters");
		}
		if (!copyField(od.ObjectQMgrName, sizeof(od.ObjectQMgrName), remoteQm))
		{
			return Result::failure("Remote queue manager name is longer than 48 characters");
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.open(hConn, &od, options | MQOO_FAIL_IF_QUIESCING, &obj, &cc, &rc);
		dropIfBroken(rc);

		if (cc == MQCC_FAILED)
		{
			obj = MQHO_UNUSABLE_HOBJ;
		}
		else if (resolvedName != nullptr)
		{
			size_t length = sizeof(od.ResolvedQName);
			while ((length > 0) && ((od.ResolvedQName[length - 1] == ' ') || (od.ResolvedQName[length - 1] == '\0')))
			{
				length--;
			}
			resolvedName->assign(od.ResolvedQName, length);
		}

		return Result::fromMq("MQOPEN", cc, rc);
	}

	Result MqSession::inquireInteger(MQHOBJ obj, MQLONG selector, MQLONG & value)
	{
		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		MQLONG answer = 0;
		api.inq(hConn, obj, 1, &selector, 1, &answer, 0, nullptr, &cc, &rc);
		dropIfBroken(rc);
		if (cc != MQCC_FAILED)
		{
			value = answer;
		}
		return Result::fromMq("MQINQ", cc, rc);
	}

	// Closes an inquiry handle unless the connection (and with it the handle) is gone.
	void MqSession::closeInquiryHandle(MQHOBJ & obj, Result & result)
	{
		if (!isConnected())
		{
			return;
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.close(hConn, &obj, MQCO_NONE, &cc, &rc);
		dropIfBroken(rc);
		if (cc == MQCC_FAILED)
		{
			result.warnings.push_back(Result::fromMq("MQCLOSE", cc, rc).text);
		}
	}

	Result MqSession::ensureOpen(const std::string & queue, const std::string & remoteQm, MQLONG options)
	{
		if ((hObj != MQHO_UNUSABLE_HOBJ) && (openName == queue) && (openRemoteQm == remoteQm) && (openOptions == options))
		{
			return Result::success();
		}

		Result closed = closeQueue();
		if (!closed.ok())
		{
			return closed;
		}

		Result opened = openQueue(queue, remoteQm, options, hObj);
		if (opened.ok())
		{
			openName = queue;
			openRemoteQm = remoteQm;
			openOptions = options;
		}

		return opened;
	}

	Result MqSession::closeQueue(CloseOption option)
	{
		if (hObj == MQHO_UNUSABLE_HOBJ)
		{
			browseCursor = 0;
			return Result::success();
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.close(hConn, &hObj, closeOptionValue(option), &cc, &rc);

		if ((cc == MQCC_FAILED) && !handleGone(rc))
		{
			// the handle is still live (for example MQCLOSE with an option the queue
			// does not allow): keep it so it is neither leaked nor reopened blindly
			return Result::fromMq("MQCLOSE", cc, rc);
		}

		browseCursor = 0;
		dropIfBroken(rc);

		hObj = MQHO_UNUSABLE_HOBJ;
		openName.clear();
		openRemoteQm.clear();
		openOptions = 0;

		return Result::fromMq("MQCLOSE", cc, rc);
	}

	Result MqSession::put(const std::string & queue, Message & message, const PutOptions & options)
	{
		if (!isConnected())
		{
			return notConnected();
		}

		// guards the MQLONG cast; MQ reports MAXMSGL limits itself
		if (message.headers.size() + message.body.size() > (size_t)std::numeric_limits<MQLONG>::max())
		{
			return Result::failure("Message is larger than MQ allows");
		}

		MQLONG openOpts = MQOO_OUTPUT | bindOpenOption(options.bind);
		if (options.setAllContext)
		{
			openOpts |= MQOO_SET_ALL_CONTEXT;
		}
		else if (options.setIdentityContext)
		{
			openOpts |= MQOO_SET_IDENTITY_CONTEXT;
		}

		Result opened = ensureOpen(queue, options.remoteQm, openOpts);
		if (!opened.ok())
		{
			return opened;
		}

		MQPMO pmo = { MQPMO_DEFAULT };
		pmo.Options = MQPMO_NO_SYNCPOINT | MQPMO_FAIL_IF_QUIESCING;
		if (options.newMsgId)
		{
			pmo.Options |= MQPMO_NEW_MSG_ID;
		}
		if (options.newCorrelId)
		{
			pmo.Options |= MQPMO_NEW_CORREL_ID;
		}
		if (options.setAllContext)
		{
			pmo.Options |= MQPMO_SET_ALL_CONTEXT;
		}
		else if (options.setIdentityContext)
		{
			pmo.Options |= MQPMO_SET_IDENTITY_CONTEXT;
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		// on the wire the MQ headers simply precede the body
		std::vector<unsigned char> * payload = &message.body;
		std::vector<unsigned char> joined;
		size_t splitAt = 0;
		if (!message.headers.empty())
		{
			joined.reserve(message.headers.size() + message.body.size());
			joined.insert(joined.end(), message.headers.begin(), message.headers.end());
			joined.insert(joined.end(), message.body.begin(), message.body.end());
			payload = &joined;

			// walk what is sent exactly as a get will, to see where it splits the data
			splitAt = walkHeaders(payload->data(), payload->size(), message.mqmd).bodyOffset;
		}

		api.put(hConn, hObj, &message.mqmd, &pmo, (MQLONG)payload->size(),
		        payload->empty() ? nullptr : payload->data(), &cc, &rc);
		dropIfBroken(rc);

		Result r = Result::fromMq("MQPUT", cc, rc);
		if (r.ok() && !message.headers.empty() && (splitAt != message.headers.size()))
		{
			r.warnings.push_back("headers do not match the MQMD format chain; a get splits them differently");
		}
		return r;
	}

	Result MqSession::getOnce(MQHOBJ obj, Message & message, const GetOptions & options, MQLONG getOptions, MQLONG waitMs)
	{
		const bool unlimited = (waitMs == MQWI_UNLIMITED);	// get() has rejected other negative waits

		const bool browsing = (getOptions & (MQGMO_BROWSE_FIRST | MQGMO_BROWSE_NEXT)) != 0;
		std::vector<unsigned char> buffer(std::min<size_t>(std::max<size_t>(options.initialBufferSize, 1), kMaxBufferBytes));
		MQLONG remaining = unlimited ? 0 : waitMs;
		bool retryByMsgId = false;
		MQBYTE24 retryMsgId;
		int convertRetries = 0;

		for (;;)
		{
			MQMD2 md = { MQMD2_DEFAULT };
			md.Version = MQMD_VERSION_2;
			if (options.convert)
			{
				// MQGMO_CONVERT converts to the CCSID and encoding given in the MQMD
				md.CodedCharSetId = options.convertCcsid;
				md.Encoding = options.convertEncoding;
			}

			MQGMO gmo = { MQGMO_DEFAULT };
			gmo.Version = MQGMO_VERSION_2;
			gmo.MatchOptions = MQMO_NONE;
			gmo.Options = getOptions | MQGMO_FAIL_IF_QUIESCING;
			if (options.convert)
			{
				gmo.Options |= MQGMO_CONVERT;
			}

			if (options.matchMsgId)
			{
				gmo.MatchOptions |= MQMO_MATCH_MSG_ID;
				memcpy(md.MsgId, options.msgId, sizeof(md.MsgId));
			}
			if (options.matchCorrelId)
			{
				gmo.MatchOptions |= MQMO_MATCH_CORREL_ID;
				memcpy(md.CorrelId, options.correlId, sizeof(md.CorrelId));
			}
			if (retryByMsgId)
			{
				// The MsgId MQ returned identifies the message. (A MsgId of MQMI_NONE would
				// match any message, but MQ assigns every message a non-zero MsgId.)
				gmo.MatchOptions |= MQMO_MATCH_MSG_ID;
				memcpy(md.MsgId, retryMsgId, sizeof(md.MsgId));
			}

			MQLONG slice = unlimited ? kWaitSliceMs : std::min(remaining, kWaitSliceMs);
			if ((slice > 0) && !retryByMsgId)
			{
				gmo.Options |= MQGMO_WAIT;
				gmo.WaitInterval = slice;
			}
			else
			{
				gmo.Options |= MQGMO_NO_WAIT;
				slice = 0;
			}

			MQLONG dataLength = 0;
			MQLONG cc = MQCC_OK;
			MQLONG rc = MQRC_NONE;
			api.get(hConn, obj, &md, &gmo, (MQLONG)buffer.size(), buffer.data(), &dataLength, &cc, &rc);
			dropIfBroken(rc);

			if ((rc == MQRC_TRUNCATED_MSG_FAILED) && (dataLength > 0) && ((size_t)dataLength > buffer.size()))
			{
				// The message stays on the queue and a browse cursor does not move, so
				// repeat the same get matched on the MsgId MQ just reported. dataLength is
				// the unconverted length; converted text can be longer, so leave room.
				size_t wanted = (size_t)dataLength;
				if (options.convert)
				{
					wanted *= 4;
				}
				wanted = std::min(wanted, kMaxBufferBytes);

				if (wanted > buffer.size())
				{
					buffer.resize(wanted);
					retryByMsgId = true;
					memcpy(retryMsgId, md.MsgId, sizeof(retryMsgId));
					continue;
				}
			}

			if (((rc == MQRC_CONVERTED_MSG_TOO_BIG) || (rc == MQRC_CONVERTED_STRING_TOO_BIG)) &&
			    browsing && (convertRetries < 2) && (buffer.size() < kMaxBufferBytes))
			{
				// The converted data did not fit and the unconverted message was returned.
				// A browse consumed nothing, so read it again with room for the expansion.
				// The browse cursor has moved past the message, so find it from the start.
				buffer.resize(std::min(buffer.size() * 4, kMaxBufferBytes));
				convertRetries++;
				retryByMsgId = true;
				memcpy(retryMsgId, md.MsgId, sizeof(retryMsgId));
				getOptions = (getOptions & ~MQGMO_BROWSE_NEXT) | MQGMO_BROWSE_FIRST;
				continue;
			}

			if ((rc == MQRC_NO_MSG_AVAILABLE) && (slice > 0) && (unlimited || (remaining > slice)))
			{
				if (!unlimited)
				{
					remaining -= slice;
				}
				if ((options.cancel != nullptr) && options.cancel->load())
				{
					Result r = Result::fromMq("MQGET", cc, rc);
					r.text += " - wait cancelled";
					return r;
				}
				continue;
			}

			std::vector<std::string> headerWarnings;
			if (cc != MQCC_FAILED)
			{
				message.mqmd = md;
				size_t received = std::min<size_t>((size_t)dataLength, buffer.size());

				// split the MQ headers named by the MQMD format chain from the body
				// the chain's warnings are returned with the result
				HeaderChain chain = walkHeaders(buffer.data(), received, md);
				message.headers.assign(buffer.begin(), buffer.begin() + chain.bodyOffset);
				message.body.assign(buffer.begin() + chain.bodyOffset, buffer.begin() + received);
				headerWarnings = chain.warnings;
			}

			Result r = Result::fromMq("MQGET", cc, rc);
			r.warnings.insert(r.warnings.end(), headerWarnings.begin(), headerWarnings.end());
			if (retryByMsgId && (rc == MQRC_NO_MSG_AVAILABLE))
			{
				r.text += " - message was removed before it could be re-read";
			}
			return r;
		}
	}

	Result MqSession::browseTo(const std::string & queue, Message & message, const GetOptions & options, long position)
	{
		GetOptions step = options;
		step.waitMs = 0;
		step.mode = GetMode::BrowseFirst;

		// walk with a local message: the caller's one changes only on reaching the target
		Message walked;
		for (;;)
		{
			if ((options.cancel != nullptr) && options.cancel->load())
			{
				return Result::failure("Browse previous cancelled before reaching position " + std::to_string(position));
			}

			Result r = get(queue, walked, step);
			if (!r.ok())
			{
				return r;
			}
			if (browseCursor >= position)
			{
				message = std::move(walked);
				return r;
			}
			step.mode = GetMode::BrowseNext;
		}
	}

	Result MqSession::get(const std::string & queue, Message & message, const GetOptions & options)
	{
		if (!isConnected())
		{
			return notConnected();
		}

		// checked before any queue is opened or closed, so a bad wait cannot disturb a browse
		if ((options.waitMs < 0) && (options.waitMs != MQWI_UNLIMITED))
		{
			return Result::failure("Invalid wait interval");
		}

		if (options.mode == GetMode::BrowsePrevious)
		{
			if ((browseCursor < 0) && (openName == queue))
			{
				Result r = Result::fromMq("MQGET", MQCC_FAILED, MQRC_NO_MSG_AVAILABLE);
				r.text += " - browse position unknown after a destructive get; browse first";
				return r;
			}

			if ((browseCursor <= 1) || (openName != queue))
			{
				Result r = Result::fromMq("MQGET", MQCC_FAILED, MQRC_NO_MSG_AVAILABLE);
				r.text += " - already at the first message";
				return r;
			}

			return browseTo(queue, message, options, browseCursor - 1);
		}

		Result opened = ensureOpen(queue, std::string(), MQOO_INPUT_AS_Q_DEF | MQOO_BROWSE);
		if (!opened.ok())
		{
			return opened;
		}

		MQLONG getOptions = MQGMO_NO_SYNCPOINT;
		switch (options.mode)
		{
		case GetMode::BrowseFirst: getOptions = MQGMO_BROWSE_FIRST; break;
		case GetMode::BrowseNext: getOptions = MQGMO_BROWSE_NEXT; break;
		case GetMode::Destructive: case GetMode::BrowsePrevious: break;
		}

		Result r = getOnce(hObj, message, options, getOptions, options.waitMs);

		if (r.ok() && isConnected())
		{
			switch (options.mode)
			{
			case GetMode::BrowseFirst: browseCursor = 1; break;
			case GetMode::BrowseNext: if (browseCursor >= 0) browseCursor++; break;
			case GetMode::Destructive: if (browseCursor != 0) browseCursor = -1; break;	// the browse cursor no longer maps to a position
			case GetMode::BrowsePrevious: break;
			}
		}
		else if ((options.mode == GetMode::BrowseFirst) && (r.rc == MQRC_NO_MSG_AVAILABLE) && isConnected())
		{
			browseCursor = 0;	// the restarted browse found nothing: the cursor is before the first message
		}

		return r;
	}

	Result MqSession::depth(const std::string & queue, MQLONG & count)
	{
		count = 0;
		if (!isConnected())
		{
			return notConnected();
		}

		MQHOBJ obj = MQHO_UNUSABLE_HOBJ;
		Result opened = openQueue(queue, std::string(), MQOO_INQUIRE, obj);
		if (!opened.ok())
		{
			return opened;
		}

		Result r = inquireInteger(obj, MQIA_CURRENT_Q_DEPTH, count);
		closeInquiryHandle(obj, r);
		return r;
	}

	Result MqSession::queueInfo(const std::string & queue, QueueInfo & info)
	{
		info = QueueInfo();
		if (!isConnected())
		{
			return notConnected();
		}

		MQHOBJ obj = MQHO_UNUSABLE_HOBJ;
		std::string resolved;
		Result opened = openQueue(queue, std::string(), MQOO_INQUIRE, obj, &resolved);
		if (!opened.ok())
		{
			return opened;
		}

		if (resolved != queue)
		{
			info.resolvedName = resolved;
		}

		// One MQINQ per attribute, in Windows' order (type 18244, depth 18200, maximum length 12273)
		struct Step
		{
			MQLONG selector;
			MQLONG * value;
		};
		const Step steps[] = { { MQIA_Q_TYPE, &info.type }, { MQIA_CURRENT_Q_DEPTH, &info.depth }, { MQIA_MAX_MSG_LENGTH, &info.maxMsgLength } };

		Result r;
		for (size_t i = 0; (i < sizeof(steps) / sizeof(steps[0])) && isConnected(); i++)
		{
			Result inquired = inquireInteger(obj, steps[i].selector, *steps[i].value);
			if (inquired.ok())
			{
				continue;
			}
			if (r.ok())
			{
				r = inquired;
			}
			else
			{
				r.warnings.push_back(inquired.text);
			}
		}

		switch (info.type)
		{
		case 0:				// not learned
		case MQQT_LOCAL:
		case MQQT_MODEL:
		case MQQT_ALIAS:
		case MQQT_REMOTE:
		case MQQT_CLUSTER:
			break;
		default:
			r.warnings.push_back("Unrecognised queue type " + std::to_string(info.type));
			break;
		}

		closeInquiryHandle(obj, r);
		return r;
	}

	Result MqSession::purge(const std::string & queue, MQLONG & count)
	{
		count = 0;
		if (!isConnected())
		{
			return notConnected();
		}

		const bool heldBySession = (hObj != MQHO_UNUSABLE_HOBJ) && (openName == queue);
		// only an input or browse handle of this session can make the purge's open fail with 2042
		const MQLONG inputOptions = MQOO_INPUT_AS_Q_DEF | MQOO_INPUT_SHARED | MQOO_INPUT_EXCLUSIVE | MQOO_BROWSE;
		const bool conflictsWithSession = heldBySession && ((openOptions & inputOptions) != 0);

		MQHOBJ obj = MQHO_UNUSABLE_HOBJ;
		Result opened = openQueue(queue, std::string(), MQOO_INPUT_AS_Q_DEF, obj);
		if (!opened.ok())
		{
			explainInUse(opened, conflictsWithSession);
			return opened;
		}

		Result r;
		for (;;)
		{
			MQMD2 md = { MQMD2_DEFAULT };
			MQGMO gmo = { MQGMO_DEFAULT };
			gmo.Options = MQGMO_NO_WAIT | MQGMO_NO_SYNCPOINT | MQGMO_ACCEPT_TRUNCATED_MSG | MQGMO_FAIL_IF_QUIESCING;

			unsigned char unused = 0;
			MQLONG dataLength = 0;
			MQLONG cc = MQCC_OK;
			MQLONG rc = MQRC_NONE;
			api.get(hConn, obj, &md, &gmo, 0, &unused, &dataLength, &cc, &rc);
			dropIfBroken(rc);

			if ((cc == MQCC_OK) || (rc == MQRC_TRUNCATED_MSG_ACCEPTED))
			{
				count++;
				continue;
			}

			// an empty queue is the expected end of a purge
			r = (rc == MQRC_NO_MSG_AVAILABLE) ? Result::success() : Result::fromMq("MQGET", cc, rc);
			if (!r.ok())
			{
				r.text += " after " + std::to_string(count) + " messages";
			}
			break;
		}

		if (isConnected())
		{
			MQLONG ccc = MQCC_OK;
			MQLONG crc = MQRC_NONE;
			api.close(hConn, &obj, MQCO_NONE, &ccc, &crc);
			dropIfBroken(crc);
			if (ccc == MQCC_FAILED)
			{
				r.warnings.push_back(Result::fromMq("MQCLOSE", ccc, crc).text);
			}
		}

		if (r.ok() && heldBySession)
		{
			browseCursor = 0;	// the session's own cursor now sits on an emptied queue
		}

		return r;
	}
}
