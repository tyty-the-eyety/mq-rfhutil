// rfh_mq_session.cpp - see rfh_mq_session.h

#include "rfh_mq_session.h"

#include <string.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include <cmqcfc.h>

#include "rfh_connect_security.h"
#include "rfh_header_chain.h"
#include "rfh_trace.h"
#include "rfh_pcf_queues.h"
#include "mqsubs.h"

namespace rfh
{
	namespace
	{
		const MQLONG kWaitSliceMs = 1000;
		const MQLONG kPropertyOptions = MQGMO_NO_PROPERTIES | MQGMO_PROPERTIES_FORCE_MQRFH2 | MQGMO_PROPERTIES_COMPATIBILITY;
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

		// The alternate user id as Windows gives it to MQOPEN (openQ 11978-11990, OpenTempQ, openAdminQ): at most 12 characters.
		std::string alternateUserOf(const std::string & user)
		{
			return user.substr(0, MQ_USER_ID_LENGTH);
		}

		// MQOPEN as OpenTempQ and openAdminQ issue it: the alternate user id is cut to 12 characters.
		void setAlternateUser(MQOD & od, MQLONG & openOptions, const std::string & user)
		{
			if (user.empty())
			{
				return;
			}
			openOptions |= MQOO_ALTERNATE_USER_AUTHORITY;
			memset(od.AlternateUserId, 0, MQ_USER_ID_LENGTH);
			memcpy(od.AlternateUserId, user.data(), alternateUserOf(user).size());
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
		qmInfo = QueueManagerInfo();
	}

	// Forgets the open queue handle and browse state; the handle itself is closed by
	// MQCLOSE or, with the connection, by MQDISC.
	void MqSession::clearHandleState()
	{
		hObj = MQHO_UNUSABLE_HOBJ;
		openName.clear();
		openRemoteQm.clear();
		openOptions = 0;
		openSelector.clear();
		openAlternateUser.clear();
		browseCursor = 0;
	}

	void ConnectOptions::wipePassword()
	{
		detail::secureZero(&password[0], password.size());
		password.clear();
	}

	Result MqSession::connect(const ConnectOptions & options)
	{
		if (isConnected())
		{
			return Result::failure("Already connected; disconnect first");
		}

		// a stale queue handle from an earlier connection must never be reused, nor its queue manager's facts
		clearHandleState();
		qmInfo = QueueManagerInfo();

		if (apiInjected && ((api.connx == nullptr) || (api.disc == nullptr) || (api.open == nullptr) ||
		                    (api.close == nullptr) || (api.put == nullptr) || (api.get == nullptr) || (api.inq == nullptr) || (api.cmit == nullptr)))
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
		MQSCO sco = { MQSCO_DEFAULT };
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

				// Version 6, as Windows (DataArea.cpp 10715-10735): an older client library refuses a version it does
				// not know. TLS, a local address or a non-default keep-alive raise it to 7 (applyConnectSecurity).
				cd.Version = MQCD_VERSION_6;
				cd.StrucLength = MQCD_LENGTH_6;
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

		// TLS, security exit, local address and credentials (rfh_connect_security.h); nothing reaches MQ if a value is refused
		std::vector<std::string> securityWarnings;
		Result secured = detail::applyConnectSecurity(options, cno, (cno.ClientConnPtr != nullptr) ? &cd : nullptr, sco, csp,
		                                              passwordScratch, securityWarnings);
		if (!secured.ok())
		{
			return secured;
		}

		RfhTrace * trace = rfhGetTrace();
		if (trace->isTraceEnabled())
		{
			trace->logTraceEntry(detail::describeConnect(options).c_str());
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.connx(qmName, &cno, &hConn, &cc, &rc);

		// MQ has what it needs: no readable copy of the password stays with the session
		detail::wipePasswordScratch(passwordScratch);

		if (trace->isTraceEnabled())
		{
			trace->logTraceEntry(("MQCONNX cc=" + std::to_string(cc) + " rc=" + std::to_string(rc)).c_str());
		}

		if (cc == MQCC_FAILED)
		{
			hConn = MQHC_UNUSABLE_HCONN;
		}

		Result result = Result::fromMq("MQCONNX", cc, rc);
		result.warnings.insert(result.warnings.end(), loaded.warnings.begin(), loaded.warnings.end());
		result.warnings.insert(result.warnings.end(), securityWarnings.begin(), securityWarnings.end());

		if (cc != MQCC_FAILED)
		{
			Result learned = inquireQueueManager();
			if (!learned.ok())
			{
				return learned;		// the connection was lost during the inquiry; the session is not connected
			}
			result.warnings.insert(result.warnings.end(), learned.warnings.begin(), learned.warnings.end());
		}

		return result;
	}

	// connect2QM 11242-11395: open the queue manager object and inquire its maximum message length,
	// platform and command level, in that order. Windows ignores a failure of any of these calls and
	// keeps 0; a failure here is a warning, except a lost connection, which fails the connect.
	Result MqSession::inquireQueueManager()
	{
		qmInfo = QueueManagerInfo();
		Result result;

		MQOD od = { MQOD_DEFAULT };
		od.ObjectType = MQOT_Q_MGR;
		MQHOBJ qmgr = MQHO_UNUSABLE_HOBJ;
		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.open(hConn, &od, MQOO_INQUIRE | MQOO_FAIL_IF_QUIESCING, &qmgr, &cc, &rc);
		dropIfBroken(rc);
		if (cc == MQCC_FAILED)
		{
			Result failed = Result::fromMq("MQOPEN", cc, rc);
			if (!isConnected())
			{
				return failed;
			}
			result.warnings.push_back("The platform, command level and maximum message length of the queue manager are unknown: " + failed.text);
			return result;
		}

		struct Step
		{
			MQLONG selector;
			MQLONG * value;
			const char * what;
		};
		const Step steps[] = { { MQIA_MAX_MSG_LENGTH, &qmInfo.maxMsgLength, "maximum message length" },
		                       { MQIA_PLATFORM, &qmInfo.platform, "platform" },
		                       { MQIA_COMMAND_LEVEL, &qmInfo.commandLevel, "command level" } };
		for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
		{
			Result inquired = inquireInteger(qmgr, steps[i].selector, *steps[i].value);
			if (!inquired.ok())
			{
				*steps[i].value = 0;
				if (!isConnected())
				{
					qmInfo = QueueManagerInfo();
					return inquired;
				}
				result.warnings.push_back(std::string("The ") + steps[i].what + " of the queue manager is unknown: " + inquired.text);
			}
		}

		closeInquiryHandle(qmgr, result);
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
		qmInfo = QueueManagerInfo();
		return r;
	}

	Result MqSession::openQueue(const std::string & queue, const std::string & remoteQm, MQLONG options, MQHOBJ & obj, std::string * resolvedName, const OpenExtras * extras)
	{
		MQOD od = { MQOD_DEFAULT };
		if (resolvedName != nullptr)
		{
			od.Version = MQOD_VERSION_3;
			resolvedName->clear();
		}

		// openQ 12079-12105: a selector needs a version 4 descriptor. Version 7 answers 2520 when the
		// resolved object string has no buffer, although the call does not need it.
		char resolvedObject[MQ_TOPIC_STR_LENGTH];
		if ((extras != nullptr) && !extras->selector.empty())
		{
			od.Version = MQOD_VERSION_4;
			od.SelectionString.VSPtr = const_cast<char *>(extras->selector.c_str());
			od.SelectionString.VSLength = (MQLONG)extras->selector.size();
			od.SelectionString.VSBufSize = 0;
			od.ResObjectString.VSPtr = resolvedObject;
			od.ResObjectString.VSBufSize = (MQLONG)sizeof(resolvedObject);
		}
		if (extras != nullptr)
		{
			setAlternateUser(od, options, extras->alternateUser);
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

	// True when the open queue handle is the one an open of these arguments would give. Windows compares the
	// queue, the remote queue manager, the selector and (only with set-user or set-all context) the user id
	// (openQ 11853-11875); here every argument of the open counts, so a changed alternate user reopens too.
	bool MqSession::isOpenAs(const std::string & queue, const std::string & remoteQm, MQLONG options, const OpenExtras & extras) const
	{
		return (hObj != MQHO_UNUSABLE_HOBJ) && (openName == queue) && (openRemoteQm == remoteQm) && (openOptions == options) &&
		       (openSelector == extras.selector) && (openAlternateUser == extras.alternateUser);
	}

	Result MqSession::ensureOpen(const std::string & queue, const std::string & remoteQm, MQLONG options, const OpenExtras & extras)
	{
		if (isOpenAs(queue, remoteQm, options, extras))
		{
			return Result::success();
		}

		Result closed = closeQueue();
		if (!closed.ok())
		{
			return closed;
		}

		Result opened = openQueue(queue, remoteQm, options, hObj, nullptr, &extras);
		if (opened.ok())
		{
			openName = queue;
			openRemoteQm = remoteQm;
			openOptions = options;
			openSelector = extras.selector;
			openAlternateUser = extras.alternateUser;
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
		openSelector.clear();
		openAlternateUser.clear();

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

		std::vector<std::string> notes;
		OpenExtras extras;
		extras.alternateUser = alternateUserOf(options.alternateUser);
		if (extras.alternateUser.size() < options.alternateUser.size())
		{
			notes.push_back("The alternate user id was cut to " + std::to_string(MQ_USER_ID_LENGTH) + " characters");
		}

		Result opened = ensureOpen(queue, options.remoteQm, openOpts, extras);
		if (!opened.ok())
		{
			return opened;
		}

		MQPMO pmo = { MQPMO_DEFAULT };
		pmo.Options = (options.syncpoint ? MQPMO_SYNCPOINT : MQPMO_NO_SYNCPOINT) | MQPMO_FAIL_IF_QUIESCING;
		if (options.logicalOrder)
		{
			pmo.Version = MQPMO_VERSION_2;
			pmo.Options |= MQPMO_LOGICAL_ORDER;
		}
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
		r.warnings.insert(r.warnings.end(), notes.begin(), notes.end());
		if (r.ok() && !message.headers.empty() && (splitAt != message.headers.size()))
		{
			r.warnings.push_back("headers do not match the MQMD format chain; a get splits them differently");
		}
		return r;
	}

	Result MqSession::commit()
	{
		if (!isConnected())
		{
			return notConnected();
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.cmit(hConn, &cc, &rc);
		dropIfBroken(rc);
		return Result::fromMq("MQCMIT", cc, rc);
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
			if ((getOptions & kPropertyOptions) != 0)
			{
				// the property options come with the version 4 GMO in Windows (processMQGet 7693, browsePrev 9057)
				gmo.Version = MQGMO_VERSION_4;
			}
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
			if (options.matchGroupId)
			{
				gmo.MatchOptions |= MQMO_MATCH_GROUP_ID;
				memcpy(md.GroupId, options.groupId, sizeof(md.GroupId));
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

		// The selector, alternate user and shared input belong to the queue handle. All checks come
		// before any queue is opened or closed, so a refused option cannot disturb a browse.
		if (!options.selector.empty())
		{
			if (options.selector.size() >= MQ_SELECTOR_LENGTH)
			{
				// Windows cuts the selector at MQ_SELECTOR_LENGTH - 1 characters (openQ 12087-12095), which selects something else
				return Result::failure("The selector is longer than " + std::to_string(MQ_SELECTOR_LENGTH - 1) + " characters");
			}
			if ((qmInfo.commandLevel > 0) && (qmInfo.commandLevel < MQCMDL_LEVEL_700))
			{
				return Result::failure("Message selectors need a queue manager at command level 700 (MQ V7) or above; this queue manager is at command level " +
				                       std::to_string(qmInfo.commandLevel));
			}
		}

		std::vector<std::string> notes;
		OpenExtras extras;
		extras.selector = options.selector;
		extras.alternateUser = alternateUserOf(options.alternateUser);
		if (extras.alternateUser.size() < options.alternateUser.size())
		{
			notes.push_back("The alternate user id was cut to " + std::to_string(MQ_USER_ID_LENGTH) + " characters");
		}
		const MQLONG openFlags = (options.inputShared ? MQOO_INPUT_SHARED : MQOO_INPUT_AS_Q_DEF) | MQOO_BROWSE;

		// setMQoptions 17819-17855: complete message and logical order need a queue manager that is not
		// z/OS or is at command level 600; all available sets both options except on z/OS, where
		// level 600 allows only the first. Windows drops a refused option silently; a warning says so.
		MQLONG groupFlags = 0;
		const bool zos = (qmInfo.platform == MQPL_MVS);
		const bool groupsOk = !zos || (qmInfo.commandLevel >= MQCMDL_LEVEL_600);
		const std::string zosText = " was not used: this z/OS queue manager is below command level 600 or its level is unknown";
		if (options.completeMsg)
		{
			if (groupsOk)
			{
				groupFlags |= MQGMO_COMPLETE_MSG;
			}
			else
			{
				notes.push_back("The complete message option" + zosText);
			}
		}
		if (options.logicalOrder)
		{
			if (groupsOk)
			{
				groupFlags |= MQGMO_LOGICAL_ORDER;
			}
			else
			{
				notes.push_back("The logical order option" + zosText);
			}
		}
		if (options.allAvailable)
		{
			if (!zos)
			{
				groupFlags |= MQGMO_ALL_MSGS_AVAILABLE | MQGMO_ALL_SEGMENTS_AVAILABLE;
			}
			else if (qmInfo.commandLevel >= MQCMDL_LEVEL_600)
			{
				groupFlags |= MQGMO_ALL_MSGS_AVAILABLE;
			}
			else
			{
				notes.push_back("The all available option" + zosText);
			}
		}

		if (options.mode == GetMode::BrowsePrevious)
		{
			if ((browseCursor < 0) && (openName == queue))
			{
				Result r = Result::fromMq("MQGET", MQCC_FAILED, MQRC_NO_MSG_AVAILABLE);
				r.text += " - browse position unknown after a destructive get; browse first";
				return r;
			}

			// the browse position belongs to the handle that is open, so another queue or another way of
			// opening it has no position to step back from
			if ((browseCursor <= 1) || !isOpenAs(queue, std::string(), openFlags, extras))
			{
				Result r = Result::fromMq("MQGET", MQCC_FAILED, MQRC_NO_MSG_AVAILABLE);
				r.text += " - already at the first message";
				return r;
			}

			return browseTo(queue, message, options, browseCursor - 1);
		}

		Result opened = ensureOpen(queue, std::string(), openFlags, extras);
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
		getOptions |= groupFlags;

		// processMQGet 7690: only on a queue manager of command level 700 or above, or one whose level is unknown (0)
		if (options.properties != PropertiesMode::AsQueue)
		{
			if ((qmInfo.commandLevel != 0) && (qmInfo.commandLevel < MQCMDL_LEVEL_700))
			{
				notes.push_back("The message properties option was not used: it needs a queue manager at command level 700 (MQ V7) or above; this queue manager is at command level " +
				                std::to_string(qmInfo.commandLevel));
			}
			else
			{
				getOptions |= (options.properties == PropertiesMode::None) ? MQGMO_NO_PROPERTIES :
				              (options.properties == PropertiesMode::Rfh2) ? MQGMO_PROPERTIES_FORCE_MQRFH2 : MQGMO_PROPERTIES_COMPATIBILITY;
			}
		}

		Result r = getOnce(hObj, message, options, getOptions, options.waitMs);
		r.warnings.insert(r.warnings.end(), notes.begin(), notes.end());

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

	namespace
	{
		const size_t kMaxListWarnings = 50;
		const size_t kReplyBufferBytes = 64 * 1024;
		const char * const kModelQueue = "SYSTEM.DEFAULT.MODEL.QUEUE";
		const char * const kReplyQueueTemplate = "RFHUTIL.REPLY.QUEUE*";
		const char * const kAdminQueue = "SYSTEM.ADMIN.COMMAND.QUEUE";

		void addListWarning(std::vector<std::string> & warnings, const std::string & text)
		{
			if (warnings.size() < kMaxListWarnings)
			{
				warnings.push_back(text);
			}
			else if (warnings.size() == kMaxListWarnings)
			{
				warnings.push_back("further warnings were not kept");
			}
		}

		std::string trimmedField(const char * field, size_t size)
		{
			size_t length = size;
			while ((length > 0) && ((field[length - 1] == ' ') || (field[length - 1] == '\0')))
			{
				length--;
			}
			return std::string(field, length);
		}

		const char * typeLabel(MQLONG queueType)
		{
			switch (queueType)
			{
			case MQQT_LOCAL: return "local queues";
			case MQQT_REMOTE: return "remote queues";
			case MQQT_ALIAS: return "alias queues";
			default: break;
			}
			return "queues";
		}
	}

	// One request and its replies (DataArea::PutAdminMsg, getReplyMsg and getListOfNames).
	Result MqSession::collectQueueNames(MQHOBJ adminQ, MQHOBJ replyQ, const std::string & replyName, MQLONG queueType, bool extended,
	                                    const QueueListOptions & options, std::vector<std::string> & names, std::set<std::string> & seen,
	                                    std::vector<std::string> & warnings, bool & limitReached, bool & replyCapReached)
	{
		const std::vector<unsigned char> request = pcf::buildInquireQueueRequest(queueType, options.cluster, extended);

		MQMD2 putMd = { MQMD2_DEFAULT };
		memcpy(putMd.Format, MQFMT_ADMIN, MQ_FORMAT_LENGTH);
		putMd.MsgType = MQMT_REQUEST;
		memset(putMd.ReplyToQ, ' ', sizeof(putMd.ReplyToQ));
		memcpy(putMd.ReplyToQ, replyName.data(), std::min(replyName.size(), sizeof(putMd.ReplyToQ)));
		putMd.Persistence = MQPER_NOT_PERSISTENT;
		// Tenths of a second, ten times the reply wait: a request nobody waits for any more must not stay on
		// the command queue for a server that starts later (Windows: 10 * 1000, which is 1000 seconds)
		putMd.Expiry = pcf::requestExpiryTenths(options.replyWaitMs);

		MQPMO pmo = { MQPMO_DEFAULT };
		pmo.Options = MQPMO_NO_SYNCPOINT;

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.put(hConn, adminQ, &putMd, &pmo, (MQLONG)request.size(), const_cast<unsigned char *>(request.data()), &cc, &rc);
		dropIfBroken(rc);
		if (cc == MQCC_FAILED)
		{
			Result failed;
			failed.cc = cc;
			failed.rc = rc;
			failed.text = "***** Error writing to Admin queue cc=" + std::to_string(cc) + ", reason=" + std::to_string(rc);
			return failed;
		}

		std::vector<unsigned char> buffer(kReplyBufferBytes);
		Result outcome;
		size_t received = 0;
		bool damaged = false;		// a reply could not be read as PCF: its sender's later replies are still to come

		for (;;)
		{
			if (received >= options.maxReplies)
			{
				// replies left on the queue would be read as the answers to the next request
				addListWarning(warnings, std::string("the command server sent more than ") + std::to_string(options.maxReplies) +
				                         " reply messages for the " + typeLabel(queueType) + "; the list is incomplete and the other types were not asked");
				replyCapReached = true;
				return outcome;
			}

			MQMD2 md = { MQMD2_DEFAULT };
			MQGMO gmo = { MQGMO_DEFAULT };
			gmo.Options = MQGMO_FAIL_IF_QUIESCING | MQGMO_WAIT | MQGMO_NO_SYNCPOINT | MQGMO_CONVERT | MQGMO_ACCEPT_TRUNCATED_MSG;
			gmo.WaitInterval = options.replyWaitMs;

			MQLONG dataLength = 0;
			api.get(hConn, replyQ, &md, &gmo, (MQLONG)buffer.size(), buffer.data(), &dataLength, &cc, &rc);
			dropIfBroken(rc);

			if (cc == MQCC_FAILED)
			{
				if (damaged && (rc == MQRC_NO_MSG_AVAILABLE))
				{
					return outcome;		// the damaged reply was probably the last one; it was reported when it came
				}
				outcome = Result::fromMq("MQGET", cc, rc);
				if (rc == MQRC_NO_MSG_AVAILABLE)
				{
					outcome.text += " - no reply from the command server within " + std::to_string(options.replyWaitMs) + " ms after " +
					                std::to_string(received) + " reply message(s); is the command server running?";
				}
				return outcome;
			}

			received++;
			if ((cc == MQCC_WARNING) && (rc != MQRC_TRUNCATED_MSG_ACCEPTED))
			{
				addListWarning(warnings, Result::fromMq("MQGET", cc, rc).text);
			}

			size_t length = (dataLength < 0) ? 0 : (size_t)dataLength;
			if (length > buffer.size())
			{
				addListWarning(warnings, "PCF reply of " + std::to_string(length) + " bytes is longer than the " + std::to_string(buffer.size()) +
				                         " byte buffer; it was read as far as the buffer goes");
				length = buffer.size();
			}

			const pcf::QueueReply reply = pcf::parseQueueReply(buffer.data(), length, md.Encoding, md.CodedCharSetId);
			for (const std::string & w : reply.warnings)
			{
				addListWarning(warnings, w);
			}

			if (!reply.headerValid)
			{
				// keep reading, bounded by maxReplies: whether this was the last reply is not known
				addListWarning(warnings, std::string("the list of ") + typeLabel(queueType) + " may be incomplete");
				damaged = true;
				continue;
			}

			if (reply.compCode == MQCC_FAILED)
			{
				// "none found" is how a command server says there is no queue of the type: an empty list, not an error
				if (reply.reason != MQRCCF_NONE_FOUND)
				{
					outcome = Result();
					outcome.cc = MQCC_FAILED;
					outcome.rc = reply.reason;
					outcome.text = std::string("The command server rejected the request for ") + typeLabel(queueType) + ": reason " + std::to_string(reply.reason);
					if (reply.parameterId != 0)
					{
						outcome.text += " (parameter " + std::to_string(reply.parameterId) + ")";
					}
				}
			}
			else if (reply.hasQueue && (reply.queueType > 0) && (reply.queueType != MQQT_MODEL) && (reply.name != replyName) && (seen.count(reply.name) == 0))
			{
				seen.insert(reply.name);
				names.push_back(reply.name);
				if ((names.size() >= options.maxNames) && !reply.last)
				{
					limitReached = true;
					return outcome;
				}
			}

			if (reply.last)
			{
				return outcome;
			}
		}
	}

	Result MqSession::listQueues(const QueueListOptions & options, std::vector<std::string> & names)
	{
		names.clear();
		if (!isConnected())
		{
			return notConnected();
		}

		Result result;
		std::vector<std::string> warnings;

		// platform and command level, learned by connect() (connect2QM); 0 is an inquiry that failed there
		const bool levelKnown = (qmInfo.platform != 0) && (qmInfo.commandLevel != 0);
		if (!levelKnown)
		{
			addListWarning(warnings, "The platform and command level of the queue manager are unknown (see the warnings of the connect); "
			                         "the request is built for a queue manager at command level 600 or above");
		}

		// z/OS before V6 has no PCF (DataArea::loadNames)
		if (levelKnown && (qmInfo.platform == MQPL_MVS) && (qmInfo.commandLevel < MQCMDL_LEVEL_600))
		{
			return Result::failure("MQ on 390 does not support PCF prior to V6");
		}
		const bool extended = !levelKnown || (qmInfo.commandLevel >= MQCMDL_LEVEL_600);

		// a temporary dynamic queue receives the replies (OpenTempQ)
		MQHOBJ replyQ = MQHO_UNUSABLE_HOBJ;
		std::string replyName;
		{
			MQOD od = { MQOD_DEFAULT };
			od.ObjectType = MQOT_Q;
			copyField(od.ObjectName, sizeof(od.ObjectName), kModelQueue);
			memset(od.DynamicQName, ' ', sizeof(od.DynamicQName));
			memcpy(od.DynamicQName, kReplyQueueTemplate, strlen(kReplyQueueTemplate));
			MQLONG openOptions = MQOO_INPUT_SHARED | MQOO_FAIL_IF_QUIESCING;
			setAlternateUser(od, openOptions, options.alternateUser);

			MQLONG cc = MQCC_OK;
			MQLONG rc = MQRC_NONE;
			api.open(hConn, &od, openOptions, &replyQ, &cc, &rc);
			dropIfBroken(rc);
			if (cc == MQCC_FAILED)
			{
				Result failed;
				failed.cc = cc;
				failed.rc = rc;
				failed.text = "Open for temporary reply queue failed rc=" + std::to_string(rc);
				return failed;
			}
			replyName = trimmedField(od.ObjectName, sizeof(od.ObjectName));
		}

		// the command server's queue (openAdminQ)
		MQHOBJ adminQ = MQHO_UNUSABLE_HOBJ;
		{
			MQOD od = { MQOD_DEFAULT };
			od.ObjectType = MQOT_Q;
			copyField(od.ObjectName, sizeof(od.ObjectName), kAdminQueue);
			MQLONG openOptions = MQOO_OUTPUT | MQOO_FAIL_IF_QUIESCING;
			setAlternateUser(od, openOptions, options.alternateUser);

			MQLONG cc = MQCC_OK;
			MQLONG rc = MQRC_NONE;
			api.open(hConn, &od, openOptions, &adminQ, &cc, &rc);
			dropIfBroken(rc);
			if (cc == MQCC_FAILED)
			{
				result.cc = cc;
				result.rc = rc;
				result.text = "Open for admin queue failed rc=" + std::to_string(rc);
				adminQ = MQHO_UNUSABLE_HOBJ;
			}
		}

		if (adminQ != MQHO_UNUSABLE_HOBJ)
		{
			// Windows lists local, then remote, then alias queues, and decides success by the local ones
			struct Step
			{
				bool wanted;
				MQLONG type;
			};
			const Step steps[] = { { options.local, MQQT_LOCAL }, { options.remote, MQQT_REMOTE }, { options.alias, MQQT_ALIAS } };

			std::set<std::string> seen;
			bool limitReached = false;
			bool replyCapReached = false;
			bool firstStep = true;
			for (size_t i = 0; (i < sizeof(steps) / sizeof(steps[0])) && isConnected() && !limitReached && !replyCapReached; i++)
			{
				if (!steps[i].wanted)
				{
					continue;
				}
				if (names.size() >= options.maxNames)
				{
					limitReached = true;
					break;
				}

				Result step = collectQueueNames(adminQ, replyQ, replyName, steps[i].type, extended, options, names, seen, warnings, limitReached, replyCapReached);
				if (!step.ok())
				{
					if (firstStep)
					{
						result = step;
					}
					else
					{
						addListWarning(warnings, step.text);
					}
					if (step.rc == MQRC_NO_MSG_AVAILABLE)
					{
						break;	// the command server is not answering: the other types would only wait as long again
					}
				}
				firstStep = false;
			}

			if (limitReached)
			{
				addListWarning(warnings, "The list is limited to " + std::to_string(options.maxNames) + " names and is incomplete");
			}
		}

		// close and delete the reply queue, close the admin queue (closeReplyQ, closeAdminQ); a lost connection took them already
		if (isConnected())
		{
			MQLONG cc = MQCC_OK;
			MQLONG rc = MQRC_NONE;
			api.close(hConn, &replyQ, MQCO_DELETE_PURGE, &cc, &rc);
			dropIfBroken(rc);
			if (cc == MQCC_FAILED)
			{
				addListWarning(warnings, Result::fromMq("MQCLOSE", cc, rc).text + " (temporary reply queue " + replyName + ")");
			}
			if (isConnected() && (adminQ != MQHO_UNUSABLE_HOBJ))
			{
				api.close(hConn, &adminQ, MQCO_NONE, &cc, &rc);
				dropIfBroken(rc);
				if (cc == MQCC_FAILED)
				{
					addListWarning(warnings, Result::fromMq("MQCLOSE", cc, rc).text + " (" + kAdminQueue + ")");
				}
			}
		}

		result.warnings.insert(result.warnings.end(), warnings.begin(), warnings.end());
		return result;
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
