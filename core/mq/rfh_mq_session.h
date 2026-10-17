// rfh_mq_session.h - one connection to a queue manager and one open queue.
//
// MqSession owns its connection handle and at most one open queue handle for
// put/get/browse. depth() and purge() use their own short-lived handles so they
// never disturb a browse cursor. The connection is made with shared handles, so
// it may be used from another thread than the one that connected (for example to
// destroy it), but a session is not safe for concurrent use: call it from one
// thread at a time (the Qt application gives it a dedicated worker thread).
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
		// MqSession does not copy or trace the password; it is referenced only during
		// MQCONNX. The caller owns the string and should clear it when done (a future
		// reconnect option will need the caller to supply credentials again).
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
		MQLONG waitMs = 0;			// 0 returns at once when no message is available.
									// MQWI_UNLIMITED waits until a message arrives or cancel is set.
									// Any other negative value is invalid and fails without calling MQGET.
		// MQGMO_CONVERT to convertCcsid and convertEncoding. The message's MQMD then
		// reports those values. If MQ cannot hold the converted data in the buffer, a
		// browse grows the buffer and repeats; a destructive get has already removed the
		// message, so a 2120/2190 warning delivers the unconverted data. Browse first
		// when that matters.
		// When a converted message is too big for the buffer during a browse, the
		// message is re-read by matching its MsgId. If several messages share that MsgId
		// (e.g. replayed with newMsgId=false), the re-read can return an earlier message
		// with the same MsgId, and the browse does not advance past it.
		// In that case the re-read is reported as success, and browse positions after it are unreliable.
		bool convert = false;
		MQLONG convertCcsid = MQCCSI_Q_MGR;		// target CCSID; MQCCSI_Q_MGR is the queue manager's
		MQLONG convertEncoding = MQENC_NATIVE;	// target encoding
		bool matchMsgId = false;	// match msgId below
		bool matchCorrelId = false;	// match correlId below
		MQBYTE24 msgId = { 0 };
		MQBYTE24 correlId = { 0 };
		size_t initialBufferSize = 64 * 1024;	// grown to the message length on truncation

		// Optional: set to true from another thread to stop a waiting get. The wait is
		// split into slices of at most one second, so it stops within about a second.
		const std::atomic<bool> * cancel = nullptr;
	};

	// What MQINQ and MQOPEN report about a queue. Every field has an explicit unknown
	// value for an inquiry that failed (Windows stores 0 or -1 silently; see queueInfo).
	struct QueueInfo
	{
		MQLONG type = 0;				// MQQT_LOCAL, MQQT_MODEL, MQQT_ALIAS, MQQT_REMOTE, MQQT_CLUSTER; 0 unknown; any other value is kept as MQ returned it
		MQLONG depth = -1;				// -1 when unknown
		MQLONG maxMsgLength = -1;		// -1 when unknown
		std::string resolvedName;		// the queue the name resolved to (MQOD.ResolvedQName); empty when it is the same or unknown
	};

	class MqSession
	{
	public:
		MqSession() = default;

		// For tests and embedding: use the given entry points instead of loading the
		// IBM MQ library with MqLoader. Every entry point (connx, disc, open, close, put,
		// get, inq) must be set; connect() fails otherwise.
		explicit MqSession(const MqApi & injectedApi) : api(injectedApi), apiInjected(true) {}
		~MqSession();

		MqSession(const MqSession &) = delete;
		MqSession & operator=(const MqSession &) = delete;

		Result connect(const ConnectOptions & options);
		Result disconnect();
		bool isConnected() const { return hConn != MQHC_UNUSABLE_HCONN; }

		// Puts message to queue: message.headers followed by message.body.
		// message.mqmd.Format must name the first header; put warns when
		// message.headers is non-empty and a get would split the data anywhere but
		// between the headers and the body. On success message.mqmd holds the
		// descriptor MQ returned (new MsgId, put date and time, ...).
		Result put(const std::string & queue, Message & message, const PutOptions & options);

		// Gets or browses one message from queue into message. The MQ headers named by
		// the MQMD format chain (see walkHeaders) go to message.headers and the rest to
		// message.body. A browse continues on the same queue handle; switching queue or
		// putting closes it and resets the browse position. BrowsePrevious re-browses
		// from the first message, so each step costs one MQGET per position (counted
		// under the current match criteria), and lands on a different message if other
		// applications change the queue meanwhile. A destructive get leaves the browse
		// position unknown: BrowseNext continues, but BrowsePrevious fails until
		// BrowseFirst.
		Result get(const std::string & queue, Message & message, const GetOptions & options);

		Result depth(const std::string & queue, MQLONG & count);

		// Opens queue for inquiry on a handle of its own (a browse cursor is not
		// disturbed), asks MQ for its type, current depth and maximum message length
		// with one MQINQ each, as Windows does, and closes the handle. info is reset
		// first and then holds every fact that was learned. The result is the first
		// failure (MQOPEN, or the first failing MQINQ); later failures go to
		// warnings, and so does an unrecognised queue type or a failing MQCLOSE.
		// Inquiries stop when the connection is lost. A remote queue definition
		// refuses the inquiry at open (2045); that is the failure reported.
		Result queueInfo(const std::string & queue, QueueInfo & info);

		// Removes every message from queue with destructive gets; count receives how many.
		Result purge(const std::string & queue, MQLONG & count);

		// Closes the open queue handle, if any.
		Result closeQueue(CloseOption option = CloseOption::None);

	private:
		// resolvedName, when given, makes the open use an MQOD version 3 and receives the trimmed ResolvedQName.
		Result openQueue(const std::string & queue, const std::string & remoteQm, MQLONG options, MQHOBJ & hObj, std::string * resolvedName = nullptr);
		Result inquireInteger(MQHOBJ obj, MQLONG selector, MQLONG & value);
		void closeInquiryHandle(MQHOBJ & obj, Result & result);
		Result ensureOpen(const std::string & queue, const std::string & remoteQm, MQLONG options);
		Result getOnce(MQHOBJ obj, Message & message, const GetOptions & options, MQLONG getOptions, MQLONG waitMs);
		void dropIfBroken(MQLONG rc);
		Result browseTo(const std::string & queue, Message & message, const GetOptions & options, long position);

		void clearHandleState();

		MqApi api;
		bool apiInjected = false;
		MQHCONN hConn = MQHC_UNUSABLE_HCONN;

		MQHOBJ hObj = MQHO_UNUSABLE_HOBJ;
		std::string openName;
		std::string openRemoteQm;
		MQLONG openOptions = 0;
		long browsePosition = 0;	// 1-based position of the message under the browse cursor; 0 none, -1 unknown
	};
}
