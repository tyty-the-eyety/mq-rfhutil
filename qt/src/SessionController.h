// SessionController.h - one worker thread that owns the MqSession.
//
// The GUI thread never touches the session. It submits a job (a function that runs on the worker with the
// session) and receives the outcome as the finished signal, on the GUI thread, carrying value types only.
// One job runs at a time: submit() refuses a job while busy() and the UI disables conflicting actions. A Get
// that waits can be stopped with cancel(); the flag is the one GetOptions::cancel and PurgeOptions::cancel
// point at (cancelFlag()), which the core polls in slices of at most one second.
//
// Passwords are kept in memory only: connectTo() wipes the controller's copy as soon as the connect returns
// and nothing here logs or emits a password.
#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rfh_message.h"
#include "rfh_message_file.h"
#include "rfh_mq_session.h"
#include "rfh_mqmd_fields.h"
#include "rfh_result.h"

namespace rfhqt
{
	// One call a multi-step job made, for the flows of Operations: the Windows operation word the status text
	// uses ("Connect", "Get", "Put", "Close", "Disc", "commit", "rollback"...) and what the core answered. Every
	// step the job ran is listed in order, the ones that simply worked included, so the UI thread can tell
	// which state changes happened (a rollback that worked, a commit that worked).
	struct JobStep
	{
		std::string operation;
		rfh::Result result;
	};

	// What a job hands back. Plain values: no reference to the session crosses the thread boundary.
	struct JobResult
	{
		rfh::Result result;
		rfh::Message message;						// for gets
		rfh::QueueInfo queue;						// filled when the job asked for it
		bool hasQueueInfo = false;
		long count = 0;								// purge: messages removed
		long browsePosition = 0;					// get: MqSession::browsePosition() after the call
		std::vector<std::string> names;				// listQueues
		rfh::QueueManagerInfo queueManager;			// connect: what connect() learned
		bool connected = false;						// MqSession::isConnected() when the job ended
		bool queueOpen = false;						// MqSession::isQueueOpen() when the job ended
		long uncertain = 0;							// purge: persistent messages whose removal is unknown (never in count)

		// Set by the multi-step jobs of Operations (the controller's ready-made jobs leave them empty).
		std::vector<JobStep> steps;
		bool reconnected = false;					// the job connected (queueManager is then valid)
		std::shared_ptr<rfh::fileio::ReadResult> fileRead;		// openFile: what fileio::read produced
		std::shared_ptr<rfh::MqmdFields> fileFields;			// openFile: the MQMD tab after the read
	};

	// Runs connect on the session and wipes the password of options afterwards, whatever the outcome.
	rfh::Result connectAndWipe(rfh::MqSession & session, rfh::ConnectOptions & options);

	class SessionController : public QObject
	{
		Q_OBJECT

	public:
		using Job = std::function<JobResult(rfh::MqSession &)>;

		explicit SessionController(QObject * parent = nullptr);							// real MQ
		explicit SessionController(const rfh::MqApi & api, QObject * parent = nullptr);	// injected, for tests

		// Cancels, waits for the running job, disconnects on the worker and joins the thread.
		~SessionController() override;

		bool busy() const { return isBusy; }

		// Runs job on the worker. Returns false, and does not run it, while busy(). Must be called on the
		// thread that owns the controller. The cancel flag is cleared here, so a cancel() that follows this
		// call stops this job.
		bool submit(const QString & name, Job job);

		// Stops the running job (a waiting get, a browse previous walk, a purge). A no-op while idle.
		void cancel();

		const std::atomic<bool> * cancelFlag() const { return &cancelRequested; }

		// Ready-made jobs. Each returns what submit returns and finishes with the name in parentheses.
		// The password in options is copied once into the job, wiped there after the connect, and the
		// parameter itself is wiped before this returns; wipe the caller's own copy too.
		bool connectTo(rfh::ConnectOptions options);									// ("connect")
		bool disconnectFromQm();														// ("disconnect")
		bool put(const std::string & queue, const rfh::Message & message, const rfh::PutOptions & options);	// ("put"; message holds the MQMD MQ returned)
		bool get(const std::string & queue, const rfh::GetOptions & options);			// ("get"; options.cancel is set to cancelFlag())
		// ("purge"; options.cancel is set). The result carries count and uncertain (the persistent messages whose
		// removal is unknown) and, while the connection survives, the queue facts after the purge.
		bool purge(const std::string & queue, const rfh::PurgeOptions & options);
		bool commit();																	// ("commit")
		bool rollback();																// ("rollback"; MQBACK for the whole connection)
		bool listQueues(const rfh::QueueListOptions & options);							// ("listQueues")
		bool inquireQueue(const std::string & queue);									// ("queueInfo")

	signals:
		void busyChanged(bool busy);
		void finished(const QString & name, const rfhqt::JobResult & result);	// on the owner's thread

	private:
		class Worker;
		void start(std::unique_ptr<rfh::MqSession> newSession);
		void jobDone(const QString & name, const JobResult & result);

		std::unique_ptr<rfh::MqSession> session;	// created here, used and destroyed only on the worker
		QThread thread;
		Worker * worker = nullptr;
		std::atomic<bool> cancelRequested{false};
		bool isBusy = false;
	};
}

Q_DECLARE_METATYPE(rfhqt::JobResult)
