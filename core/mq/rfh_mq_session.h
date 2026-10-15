// rfh_mq_session.h - one connection to a queue manager and one open queue.
//
// MqSession owns its connection handle and at most one open queue handle for
// put/get/browse. depth() and purge() use their own short-lived handles so they
// never disturb a browse cursor. A session is not thread-safe: use it from one
// thread only (the Qt application gives it a dedicated worker thread).
#pragma once

#include <cmqc.h>
#include <cmqxc.h>

#include <atomic>
#include <string>

#include "rfh_message.h"
#include "rfh_mq_loader.h"
#include "rfh_result.h"

namespace rfh
{
	enum class Mode { Bindings, Client };

	struct ConnectOptions
	{
		Mode mode = Mode::Bindings;
		std::string qmName;			// empty connects to the default queue manager

		// Client mode. With channel set, connect through channel/connName; otherwise
		// with ccdtUrl set, use that CCDT; otherwise MQ uses MQSERVER or MQCHLLIB/MQCHLTAB.
		std::string channel;
		std::string connName;		// "host(port)"
		std::string ccdtUrl;		// file path or URL of a CCDT
		MQLONG heartbeat = 60;		// seconds; client channel definitions only
		MQLONG keepAlive = MQKAI_AUTO;

		// Credentials are sent only when a password is given (the IDPWOS rule).
		// The password is never traced, logged, or kept after connect().
		std::string userId;
		std::string password;
	};

	enum class BindOption { AsQueueDefinition, OnOpen, NotFixed, OnGroup };
	enum class CloseOption { None, Delete, Purge };

	struct PutOptions
	{
		std::string remoteQm;		// remote queue manager name for remote queues
		BindOption bind = BindOption::AsQueueDefinition;
		bool newMsgId = true;
		bool newCorrelId = false;
		bool setIdentityContext = false;	// put the context fields from the MQMD as given
		bool setAllContext = false;
	};

	enum class GetMode { Destructive, BrowseFirst, BrowseNext, BrowsePrevious };

	struct GetOptions
	{
		GetMode mode = GetMode::Destructive;
		MQLONG waitMs = 0;			// 0 returns at once when no message is available
		bool convert = false;		// MQGMO_CONVERT using the MQMD CodedCharSetId/Encoding
		bool matchMsgId = false;	// match msgId below
		bool matchCorrelId = false;	// match correlId below
		MQBYTE24 msgId = { 0 };
		MQBYTE24 correlId = { 0 };
		size_t initialBufferSize = 64 * 1024;	// grown to the message length on truncation

		// Optional: set to true from another thread to stop a waiting get. The wait is
		// split into slices of at most one second, so it stops within about a second.
		const std::atomic<bool> * cancel = nullptr;
	};

	class MqSession
	{
	public:
		MqSession() = default;
		~MqSession();

		MqSession(const MqSession &) = delete;
		MqSession & operator=(const MqSession &) = delete;

		Result connect(const ConnectOptions & options);
		Result disconnect();
		bool isConnected() const { return hConn != MQHC_UNUSABLE_HCONN; }

		// Puts message to queue. On success message.mqmd holds the descriptor MQ
		// returned (new MsgId, put date and time, ...).
		Result put(const std::string & queue, Message & message, const PutOptions & options);

		// Gets or browses one message from queue into message. A browse continues on
		// the same queue handle; switching queue or putting closes it and resets the
		// browse position. BrowsePrevious re-browses from the first message.
		Result get(const std::string & queue, Message & message, const GetOptions & options);

		Result depth(const std::string & queue, MQLONG & depth);

		// Removes every message from queue with destructive gets; count receives how many.
		Result purge(const std::string & queue, MQLONG & count);

		// Closes the open queue handle, if any.
		Result closeQueue(CloseOption option = CloseOption::None);

	private:
		Result openQueue(const std::string & queue, const std::string & remoteQm, MQLONG options, MQHOBJ & hObj);
		Result ensureOpen(const std::string & queue, const std::string & remoteQm, MQLONG options);
		Result getOnce(MQHOBJ obj, Message & message, const GetOptions & options, MQLONG getOptions, MQLONG waitMs);
		Result browseTo(const std::string & queue, Message & message, const GetOptions & options, long position);

		MqApi api;
		MQHCONN hConn = MQHC_UNUSABLE_HCONN;

		MQHOBJ hObj = MQHO_UNUSABLE_HOBJ;
		std::string openName;
		std::string openRemoteQm;
		MQLONG openOptions = 0;
		long browsePosition = 0;	// 1-based position of the message under the browse cursor
	};
}
