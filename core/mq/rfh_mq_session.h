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
#include <set>
#include <string>
#include <vector>

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

		// Credentials are sent only when a password is given and a user id with it (the IDPWOS rule; Windows
		// connect2QM 10753-10789). A user id without a password, or a password without a user id, is not sent and
		// a warning says so; Windows drops both silently. Limits are the Windows dialog's (ConnUser.cpp 59-62):
		// user id 255, password 1024 bytes; longer values fail the connect without a call to MQ.
		//
		// PASSWORDS LIVE ONLY IN MEMORY. connect() copies the password into a buffer of its own for MQCSP, wipes
		// that buffer as soon as MQCONNX returns, and never writes the password to a trace, a warning, an error or
		// Result::text. The caller's string is the caller's: call wipePassword() when the connect is done.
		// MQ may keep its own copy (a client library keeps credentials for an automatic reconnect, which this
		// session does not request); that copy is not reachable from here.
		std::string userId;
		std::string password;

		// Overwrites the bytes of `password` with zeros and empties it.
		void wipePassword();

		// TLS (Windows connect2QM 10867-10951; General::OnSetConnUser 2347-2412; ConnUser.cpp 59-79). Client mode
		// only: a bindings connection ignores every field of this group and says so in a warning when one is set.
		// Nothing below is used unless sslEnabled is true. With a channel (channel/connName), the channel
		// definition carries the cipher spec, peer name and client authentication; with only a CCDT or MQSERVER
		// those come from there, a cipher spec given here is rejected (it would be ignored and the connection
		// could silently be unencrypted), and a peer name, client validation, security exit or local address
		// given here is ignored with a warning. Where Windows silently cuts a value, this rejects it.
		bool sslEnabled = false;				// Windows m_use_ssl; Windows also requires a cipher spec, see cipherSpec
		std::string cipherSpec;					// MQCD.SSLCipherSpec, at most 32 characters (Windows cuts at 31); required with a channel
		std::string keyRepository;				// MQSCO.KeyRepository, at most 255 characters (Windows ignores longer ones); empty keeps MQ's default
		std::string sslPeerName;				// MQCD.SSLPeerNamePtr/Length, at most 1024 bytes
		bool sslClientValidation = false;		// MQCD.SSLClientAuth REQUIRED when true, OPTIONAL when false (Windows m_ssl_validate)
		bool fipsRequired = false;				// MQSCO.FipsRequired = MQSSL_FIPS_YES (MQSCO version 2)
		int sslResetCount = 0;					// MQSCO.KeyResetCount, 0 to 999999999
		std::string certificateLabel;			// MQSCO.CertificateLabel (version 5), at most 64 characters. Not in Windows.

		// Client mode with a channel only.
		std::string securityExit;				// MQCD.SecurityExit, at most 128 characters
		std::string securityData;				// MQCD.SecurityUserData, at most 32; ignored, with a warning, without securityExit (as Windows, silently)
		std::string localAddress;				// MQCD.LocalAddress, at most 48 characters (Windows cuts)
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
		// Added in Task 14 (Windows: DataArea::putMessage 6299-6351, openQ 11973-11991).
		bool logicalOrder = false;			// MQPMO_LOGICAL_ORDER (and a version 2 MQPMO, as Windows sets)
		// MQPMO_SYNCPOINT instead of MQPMO_NO_SYNCPOINT. Windows has no such choice: it puts under syncpoint
		// exactly when the MQMD tab marks a group or segment message (6342-6351), so the caller sets this
		// from those flags. The put never commits; see MqSession::commit.
		bool syncpoint = false;
		// MQOO_ALTERNATE_USER_AUTHORITY with this user id at open; empty means none. At most 12 characters are
		// used, as in Windows, and a warning says so. It belongs to the queue handle: a put with another id
		// closes the open handle and opens a new one.
		std::string alternateUser;
	};

	enum class GetMode { Destructive, BrowseFirst, BrowseNext, BrowsePrevious };

	// What a get does with message properties (the Main tab's User Props radios, Windows m_mq_props).
	enum class PropertiesMode
	{
		AsQueue,	// no option: the queue's PROPDEF decides (MQGMO_PROPERTIES_AS_Q_DEF, which is 0)
		None,		// MQGMO_NO_PROPERTIES
		Rfh2,		// MQGMO_PROPERTIES_FORCE_MQRFH2
		Compat		// MQGMO_PROPERTIES_COMPATIBILITY
	};

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

		// Group, segment and open options (Windows: DataArea::setMQoptions, getMessage, openQ). The gates
		// below use the queue manager's platform and command level, which connect() learns (queueManagerInfo).
		// Where a gate refuses an option, get still runs without it and says so in Result::warnings.
		bool matchGroupId = false;	// MQMO_MATCH_GROUP_ID with groupId below; the caller decides whether an all-zero id means "any"
		MQBYTE24 groupId = { 0 };
		bool logicalOrder = false;	// MQGMO_LOGICAL_ORDER; not on z/OS below command level 600
		bool completeMsg = false;	// MQGMO_COMPLETE_MSG; not on z/OS below command level 600
		bool allAvailable = false;	// MQGMO_ALL_MSGS_AVAILABLE | MQGMO_ALL_SEGMENTS_AVAILABLE; on z/OS only ALL_MSGS_AVAILABLE
									// from command level 600, and neither below it

		// These three belong to the queue handle, not to the single get: a get that changes one of
		// them closes the open handle (and so resets a browse position) and opens a new one.
		std::string selector;		// message selector, given to MQOPEN in a version 4 MQOD; needs a queue manager at command level 700
		std::string alternateUser;	// MQOO_ALTERNATE_USER_AUTHORITY; at most 12 characters are used, as in Windows, and a warning says so
		bool inputShared = false;	// MQOO_INPUT_SHARED instead of MQOO_INPUT_AS_Q_DEF

		// Message properties. A mode other than AsQueue raises the GMO to version 4, as Windows does, and needs
		// a queue manager at command level 700; below it (or on a level that is known and lower) the option is
		// not sent and a warning says so. An unknown level (0) is allowed, as in Windows. Windows' Get
		// (processMQGet) only honours its Yes radio, so None, RFH2 and Compat are no-ops there; this core
		// honours all three. Not ported: Yes (a message handle and a property list).
		PropertiesMode properties = PropertiesMode::AsQueue;

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

	// Which queue names listQueues asks the command server for (Windows: DataArea::getQueueNames).
	struct QueueListOptions
	{
		bool local = true;
		bool remote = true;
		bool alias = true;
		bool cluster = false;			// add the cluster filter, so cluster queues are listed too

		// Added by the port; the defaults are Windows' constants.
		MQLONG replyWaitMs = 3000;		// how long each reply is waited for (getReplyMsg: 3 * 1000); the request expires after ten times this
		size_t maxNames = 100000;		// stop collecting at this many names and say so in a warning
		size_t maxReplies = 10000;		// reply messages read for one request; more stop the whole listing with a warning
		std::string alternateUser;		// MQOD alternate user id for both opens (the MQMD tab's); at most 12 characters are used, as in Windows
	};

	// What connect() learns by inquiring on the queue manager object (Windows: DataArea::connect2QM). A value
	// the inquiry could not give stays 0, which is never a real platform, level or maximum length, and
	// connect() reports the failed inquiry in its warnings.
	struct QueueManagerInfo
	{
		MQLONG platform = 0;			// MQPL_*
		MQLONG commandLevel = 0;		// MQCMDL_LEVEL_*
		MQLONG maxMsgLength = 0;		// the queue manager's maximum message length
	};

	class MqSession
	{
	public:
		MqSession() = default;

		// For tests and embedding: use the given entry points instead of loading the
		// IBM MQ library with MqLoader. Every entry point (connx, disc, open, close, put,
		// get, inq, cmit) must be set; connect() fails otherwise.
		explicit MqSession(const MqApi & injectedApi) : api(injectedApi), apiInjected(true) {}
		~MqSession();

		MqSession(const MqSession &) = delete;
		MqSession & operator=(const MqSession &) = delete;

		Result connect(const ConnectOptions & options);
		Result disconnect();
		bool isConnected() const { return hConn != MQHC_UNUSABLE_HCONN; }

		// Platform, command level and maximum message length of the queue manager of the current
		// connection; 0 for a fact that is not known: before connect(), after a disconnect or a lost
		// connection, and when MQ refused the inquiry (connect() then warns).
		const QueueManagerInfo & queueManagerInfo() const { return qmInfo; }

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

		// The 1-based position of the message the browse cursor of the open queue handle
		// last returned (Windows: DataArea::browseCount, shown as "msg k of depth"); 0 when
		// there is none. 0 also stands for an unknown position: after a destructive get on
		// the browse handle the cursor no longer maps to a position (BrowseNext goes on
		// from it, BrowsePrevious is refused until BrowseFirst). Differences from Windows:
		// a failed BrowseNext (end of queue) leaves the position on the last message, where
		// Windows disconnects; a failed BrowseFirst on an emptied queue gives 0; a failed or
		// cancelled BrowsePrevious walk leaves the cursor, and so the position, where the
		// walk stopped.
		long browsePosition() const { return (browseCursor > 0) ? browseCursor : 0; }

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

		// Lists queue names over PCF, as the Main tab's Load Names does (see rfh_pcf_queues.h for the
		// messages and DataArea::loadNames, openAdminQ, OpenTempQ and getListOfNames for the flow).
		// names is cleared, then receives the names without duplicates (by name alone: Windows keeps an
		// entry per type letter and name, so a name that is both a local and an alias queue is two entries
		// there and one here), local queues first, then remote, then alias, each in the order the command
		// server answered. Left out: model queues, the temporary reply queue itself and queues whose
		// reported type is not positive (Windows records only QType > 0). Queue types that were not
		// asked for are not filtered; the command server is asked for one type at a time. Names that
		// start with SYSTEM. are included: hiding them is the caller's choice.
		//
		// Flow: inquire the platform and command level of the queue manager (unknown levels are
		// reported in a warning and treated as modern), open a dynamic reply queue from
		// SYSTEM.DEFAULT.MODEL.QUEUE (RFHUTIL.REPLY.QUEUE*), open SYSTEM.ADMIN.COMMAND.QUEUE for output,
		// then for each requested type put one non-persistent request (no syncpoint; Expiry is ten times
		// replyWaitMs in tenths of a second, at least 10 s and at most 1 h, see pcf::requestExpiryTenths, so
		// an unanswered request does not linger on the command queue) and read replies, each waited for
		// replyWaitMs, until one is marked last. A reply that is not valid PCF is reported and reading goes on
		// (bounded by maxReplies), so the rest of that request's replies are not taken for the next one's. The
		// reply queue is closed with MQCO_DELETE_PURGE and the admin queue closed in every case unless
		// the connection was lost, which takes the handles with it. Windows opens a connection per type; this
		// uses the session's connection and one pair of queues for all types.
		//
		// The result is a failure when the first requested type fails (no reply within replyWaitMs:
		// rc 2033, rejected by the command server: its reason, connection lost: its reason, an open
		// or put failing: that reason), as Windows decides by its local queues; the names already
		// read stay in names. A failure of a later type is a warning. Windows reports none of the
		// command server's rejections. A z/OS queue manager below command level 600 has no PCF
		// and is refused with the Windows text.
		//
		// Bounds: one reply is read into 64 KiB (Windows 32 KiB plus a header); a longer reply is
		// read as far as it goes and a warning says so. At most options.maxNames names are kept. At most
		// options.maxReplies reply messages are read for a request; more stop the whole listing with a
		// warning (the unread replies would be taken for the next request's). At most 50 warnings are kept.
		Result listQueues(const QueueListOptions & options, std::vector<std::string> & names);

		// Removes every message from queue with destructive gets; count receives how many.
		Result purge(const std::string & queue, MQLONG & count);

		// MQCMIT on the connection: commits the puts and gets this connection made under syncpoint. With
		// nothing pending MQ succeeds and so does this; the session keeps no unit-of-work state of its own
		// (Windows' unitOfWorkActive is set only by MQBEGIN, never by a put). Fails without a connection, where
		// Windows (commitUOW 12566) does nothing and says nothing. A failure is the MQCMIT reason in the Result
		// and the connection stays, except a lost one, which releases the session like any other call. Windows
		// disconnects after any cc other than OK (12584-12588) and treats a warning as that failure; here a
		// warning is ok() with its rc, and what to do after a failure is the caller's flow. The queue handle
		// stays open.
		Result commit();
		// Closes the open queue handle, if any.
		Result closeQueue(CloseOption option = CloseOption::None);

	private:
		// What an open takes besides the queue and the open options (the alternate user id already cut to 12 characters).
		struct OpenExtras
		{
			std::string selector;
			std::string alternateUser;
		};

		// resolvedName, when given, makes the open use an MQOD version 3 and receives the trimmed ResolvedQName.
		// extras, when given, adds a selector (MQOD version 4) and the alternate user authority.
		Result openQueue(const std::string & queue, const std::string & remoteQm, MQLONG options, MQHOBJ & hObj, std::string * resolvedName = nullptr, const OpenExtras * extras = nullptr);
		Result inquireQueueManager();
		Result inquireInteger(MQHOBJ obj, MQLONG selector, MQLONG & value);
		void closeInquiryHandle(MQHOBJ & obj, Result & result);
		bool isOpenAs(const std::string & queue, const std::string & remoteQm, MQLONG options, const OpenExtras & extras) const;
		Result ensureOpen(const std::string & queue, const std::string & remoteQm, MQLONG options, const OpenExtras & extras = OpenExtras());
		Result getOnce(MQHOBJ obj, Message & message, const GetOptions & options, MQLONG getOptions, MQLONG waitMs);
		void dropIfBroken(MQLONG rc);
		Result browseTo(const std::string & queue, Message & message, const GetOptions & options, long position);

		void clearHandleState();

		// listQueues: one request and its replies; false when the connection was lost.
		Result collectQueueNames(MQHOBJ adminQ, MQHOBJ replyQ, const std::string & replyName, MQLONG queueType, bool extended,
		                         const QueueListOptions & options, std::vector<std::string> & names, std::set<std::string> & seen,
		                         std::vector<std::string> & warnings, bool & limitReached, bool & replyCapReached);

		MqApi api;
		bool apiInjected = false;
		MQHCONN hConn = MQHC_UNUSABLE_HCONN;

		MQHOBJ hObj = MQHO_UNUSABLE_HOBJ;
		std::string openName;
		std::string openRemoteQm;
		MQLONG openOptions = 0;
		std::string openSelector;		// the selector the open queue handle was opened with
		std::string openAlternateUser;	// the alternate user id it was opened with, cut to 12 characters
		QueueManagerInfo qmInfo;
		std::vector<char> passwordScratch;	// the password as MQCSP sees it during MQCONNX; zeroed (not freed) afterwards
		long browseCursor = 0;	// 1-based position of the message under the browse cursor; 0 none, -1 unknown
	};
}
