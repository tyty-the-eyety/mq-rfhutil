// rfh_mq_session.h - one connection to a queue manager and one open queue.
//
// MqSession owns its connection handle and at most one open queue handle for
// put/get/browse. depth() and purge() use their own short-lived handles so they
// never disturb a browse cursor. The connection is made with shared handles, so
// it may be used from another thread than the one that connected (for example to
// destroy it), but a session is not safe for concurrent use: call it from one
// thread at a time (the Qt application gives it a dedicated worker thread).
//
// UNITS OF WORK. The connection has one unit of work and the session keeps no state about it. A put with
// PutOptions::syncpoint and a purge with PurgeOptions::syncpoint join it; commit() publishes it and rollback()
// backs it out, both for everything the connection has pending, not only the call that came last. MQDISC
// (disconnect(), ~MqSession and the release of a lost connection) COMMITS what is pending, so a session that is
// simply dropped publishes its syncpoint puts. A connection that reports quiescing (2161, 2162, 2202) is still
// healthy, so the session backs its unit of work out with MQBACK before it releases it, rather than let MQDISC
// publish it; a connection that broke (2009, 2018, 2203) cannot be backed out and the queue manager settles its
// unit by itself. The calls that take part in syncpoint (put with syncpoint, commit, rollback, purge with
// syncpoint) say in their failure text whether the unit was backed out or its outcome is unknown, and never
// claim a message "remains on the queue" unless a back-out is certain.
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
		// session does not request); that copy is not reachable from here. ConnectOptions is freely copyable and
		// every copy duplicates the password; the session cannot reach those copies, so wipe each one.
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
		// from those flags. The put itself does not commit, but nothing keeps a later call or the end of
		// the session from committing it: MqSession::commit() and a syncpoint purge commit the whole
		// connection, and disconnect() and ~MqSession commit through MQDISC. Only rollback() (MQBACK), a
		// 2003, or the back-out of a quiescing connection discards it. See MqSession::commit.
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
		// When a converted message is too big for the buffer during a browse (2120, 2190), MQ has moved
		// the browse cursor onto it, so it is read again with MQGMO_BROWSE_MSG_UNDER_CURSOR and a larger
		// buffer. Every other option is sent again unchanged, and the message is not looked up by MsgId, so
		// messages that share a MsgId and the MQGMO_LOGICAL_ORDER and MQGMO_COMPLETE_MSG options (which
		// MQ answers 2033 to when a BROWSE_FIRST asks for a message that is not first in its group) are no
		// problem. (The buffer growth after a truncation, 2080, does match the MsgId MQ reported, as the
		// message and the cursor have not moved: messages with a shared MsgId can be confused there.)
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

	// Windows' DataArea::purgeQueue (10065-10244). Every get accepts truncation with a zero-length buffer, so
	// a message is removed without its data being read; a truncated message is counted, not an error.
	//
	// THE UNIT OF WORK IS THE CONNECTION'S. A syncpoint purge commits every batch with MQCMIT, which publishes
	// every syncpoint put the session made earlier on any queue, and a purge that ends in 2003 or in a
	// back-out of a quiescing connection backs those puts out with it. The session does not refuse a syncpoint
	// purge while a put is pending (the operations that drive it should; Task 23). Without syncpoint nothing
	// is committed or backed out.
	//
	// selector: given to MQOPEN in a version 4 MQOD (openQ 12056-12105): only matching messages are removed.
	//   Needs command level 700; the same checks as get (too long, level below 700) fail before any open.
	// syncpoint: gets use MQGMO_SYNCPOINT_IF_PERSISTENT as Windows always does (persistent messages in a unit
	//   of work, others outside it) and the unit of work is committed every batchSize messages and at the
	//   end. Off, the gets use MQGMO_NO_SYNCPOINT and nothing is committed.
	// batchSize: messages per commit, counting every message got as Windows does (uowCount, purgeQueue 10160). Windows
	//   commits at the queue manager's maximum uncommitted messages less one (128 - 1 by default) or after
	//   2000, whichever comes first; here the caller chooses and the cap of 2001 messages is kept. Zero
	//   or less means the cap.
	// cancel: set from another thread, it stops the purge before the next get (Windows has no cancel). What
	//   was got is committed, count is kept and the Result is a failure that says so.
	//
	// count is the number of messages known to be removed, kept as a fact on every failure. Under syncpoint,
	// persistent messages got since the last commit are not removed until the commit succeeds, so they leave
	// count when the commit fails or the unit is lost, and the text says how many and what is known:
	//   - backed out: MQ answered 2003, or the session backed out a quiescing connection with MQBACK. The text
	//     says "N persistent messages ... were backed out and remain on the queue". Only this case says so.
	//   - unknown: the connection broke, the back-out failed, MQCMIT lost the connection or ended with a
	//     warning (2123, 2124), or MQCMIT failed in any other way. The text says "N persistent messages ...
	//     have an unknown outcome": the queue manager may or may not have removed them. These messages are
	//     counted in `uncertain` of the overload below, not in count. A commit whose outcome is unknown ends the
	//     purge (a warning from it makes the Result a failure), because the next batch could not be trusted.
	// (Windows counts such messages as purged.) A get that fails with 2024 (too many uncommitted messages) is
	// retried once after a commit, as Windows does. Any other failed or warning get ends the purge; a warning
	// get other than the accepted truncation has removed its message, which is counted (Windows ends the loop
	// without counting it). Failure text: "... after N messages". count and uncertain are bounded by the
	// queue manager's maximum queue depth (999 999 999), which an MQLONG holds. Whatever the Result, an open
	// browse handle of the purged queue is closed after a purge that removed anything, because its cursor
	// would otherwise stay where it was while the messages under it are gone: browsePosition() is then 0 and the
	// next browse of the queue opens a new handle.
	struct PurgeOptions
	{
		std::string selector;
		bool syncpoint = false;
		MQLONG batchSize = 100;
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
		// get, inq, cmit, back) must be set; connect() fails otherwise.
		explicit MqSession(const MqApi & injectedApi) : api(injectedApi), apiInjected(true) {}
		~MqSession();

		MqSession(const MqSession &) = delete;
		MqSession & operator=(const MqSession &) = delete;

		Result connect(const ConnectOptions & options);
		// Closes the open queue and disconnects. MQDISC COMMITS the connection's pending syncpoint work (see the
		// note on units of work at the top); call rollback() first to discard it.
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
		// Same as the overload below with default options (no selector, no syncpoint).
		Result purge(const std::string & queue, MQLONG & count);

		// Options and rules: see PurgeOptions.
		Result purge(const std::string & queue, MQLONG & count, const PurgeOptions & options);

		// As above; uncertain also receives the number of persistent messages whose removal is unknown (see
		// PurgeOptions), which are in neither count nor on a queue that is known to still hold them.
		Result purge(const std::string & queue, MQLONG & count, const PurgeOptions & options, MQLONG & uncertain);

		// MQCMIT on the connection: commits the puts and gets this connection made under syncpoint. THE UNIT OF
		// WORK BELONGS TO THE CONNECTION, so this publishes every syncpoint put the session has pending (on any
		// queue, from put() or purge()), not only the last one's, and a 2003 backs all of them out. With
		// nothing pending MQ succeeds and so does this; the session keeps no unit-of-work state of its own
		// (Windows' unitOfWorkActive is set only by MQBEGIN, never by a put). Fails without a connection, where
		// Windows (commitUOW 12566) does nothing and says nothing. A failure is the MQCMIT reason in the Result
		// and the connection stays, except a lost one, which releases the session like any other call. Windows
		// disconnects after any cc other than OK (12584-12588) and treats a warning as that failure; here a
		// warning is ok() with its rc, and what to do after a failure is the caller's flow. The queue handle
		// stays open.
		//
		// Outcomes the text does not leave open: a connection lost during MQCMIT may or may not have committed
		// ("the outcome of the unit of work is unknown" in the text); a warning (2123 mixed, 2124 pending) is
		// ok() with the same statement in warnings; a quiescing connection is backed out and released ("was
		// backed out" in the text); 2003 is MQ's own word that the unit was backed out. Any other failure leaves
		// the unit as it was, which the session cannot tell from the call alone.
		Result commit();

		// MQBACK on the connection: discards the syncpoint puts and returns the syncpoint gets of the whole
		// connection (see commit). Windows: DataArea::rollbackUOW, 12600. With nothing pending MQ succeeds and so
		// does this. Fails without a connection. A connection lost during MQBACK is released and the text says
		// the outcome is unknown; a quiescing reason releases the connection without a second MQBACK. Any other
		// failure keeps the connection and its unit of work. Windows disconnects after any failure; here that is
		// the caller's flow.
		Result rollback();
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
		// What became of the unit of work of a connection, as far as the session can say.
		enum class UnitOutcome
		{
			Unaffected,		// the connection was not lost, nothing was settled
			Committed,
			BackedOut,		// MQ said so (2003), or MQBACK worked
			Unknown			// the connection broke, MQBACK failed or MQCMIT ended with a warning or a failure that says nothing
		};

		UnitOutcome dropIfBroken(MQLONG rc, bool mayBackOut = true);
		static std::string describeOutcome(UnitOutcome outcome);
		Result commitUnit(UnitOutcome & outcome);
		Result browseTo(const std::string & queue, Message & message, const GetOptions & options, long position);

		Result checkSelector(const std::string & selector) const;

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
		UnitOutcome lastDrop = UnitOutcome::Unaffected;	// what dropIfBroken last settled, for a call that learns of the loss from a nested one
		long browseCursor = 0;	// 1-based position of the message under the browse cursor; 0 none, -1 unknown
	};
}
