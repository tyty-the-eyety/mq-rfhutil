#include "SessionController.h"

#include "rfh_trace.h"

#include <stdio.h>

#include <exception>
#include <utility>

namespace rfhqt
{
	namespace
	{
		// Wipes the password when it goes out of scope, so a connect that throws (bad_alloc in the core) leaves none behind.
		class PasswordWipe
		{
		public:
			explicit PasswordWipe(rfh::ConnectOptions & target) : options(target) {}
			~PasswordWipe() { options.wipePassword(); }
			PasswordWipe(const PasswordWipe &) = delete;
			PasswordWipe & operator=(const PasswordWipe &) = delete;
		private:
			rfh::ConnectOptions & options;
		};

		// What a job that threw leaves: a failed result, and one step that carries the text, so every consumer of
		// the steps shows it.
		JobResult thrownJob(const std::string & text)
		{
			JobResult outcome;
			outcome.result = rfh::Result::failure(text);
			JobStep step;
			step.operation = "Job";
			step.result = outcome.result;
			outcome.steps.push_back(step);
			return outcome;
		}
	}

	rfh::Result connectAndWipe(rfh::MqSession & session, rfh::ConnectOptions & options)
	{
		PasswordWipe wipe(options);
		return session.connect(options);
	}

	// Lives on the worker thread; its only job is to give queued calls a thread to run on.
	class SessionController::Worker : public QObject
	{
	public:
		Worker() = default;
	};

	SessionController::SessionController(QObject * parent)
		: QObject(parent)
	{
		start(std::make_unique<rfh::MqSession>());
	}

	SessionController::SessionController(const rfh::MqApi & api, QObject * parent)
		: QObject(parent)
	{
		start(std::make_unique<rfh::MqSession>(api));
	}

	void SessionController::start(std::unique_ptr<rfh::MqSession> newSession)
	{
		qRegisterMetaType<rfhqt::JobResult>("rfhqt::JobResult");
		session = std::move(newSession);
		worker = new Worker();
		worker->moveToThread(&thread);
		thread.setObjectName("rfh-mq-worker");
		thread.start();
	}

	SessionController::~SessionController()
	{
		cancelRequested.store(true);
		// Queued behind the running job, so it runs after that job has ended (the cancel flag stops a wait).
		rfh::MqSession * s = session.get();
		QMetaObject::invokeMethod(worker, [s]()
		{
			s->disconnect();	// MQDISC commits whatever unit is pending (Windows behaviour)
		}, Qt::BlockingQueuedConnection);
		thread.quit();
		thread.wait();
		delete worker;			// the thread has finished: no event can reach it any more
		session.reset();		// the worker has stopped: nothing else uses it
	}

	bool SessionController::submit(const QString & name, Job job)
	{
		if (isBusy || !job)
		{
			return false;
		}
		cancelRequested.store(false);
		isBusy = true;
		emit busyChanged(true);

		rfh::MqSession * s = session.get();
		QMetaObject::invokeMethod(worker, [this, s, name, job = std::move(job)]()
		{
			JobResult outcome;
			try
			{
				outcome = job(*s);
			}
			catch (const std::exception & e)
			{
				outcome = thrownJob(std::string("The operation failed with an exception: ") + e.what());
			}
			catch (...)
			{
				outcome = thrownJob("The operation failed with an unknown exception");
			}
			outcome.connected = s->isConnected();
			outcome.queueOpen = s->isQueueOpen();
			QMetaObject::invokeMethod(this, [this, name, outcome]()
			{
				jobDone(name, outcome);
			}, Qt::QueuedConnection);
		}, Qt::QueuedConnection);
		return true;
	}

	void SessionController::jobDone(const QString & name, const JobResult & result)
	{
		isBusy = false;
		emit busyChanged(false);
		emit finished(name, result);
	}

	void SessionController::cancel()
	{
		if (isBusy)
		{
			cancelRequested.store(true);
		}
	}

	bool SessionController::connectTo(rfh::ConnectOptions options)
	{
		std::shared_ptr<rfh::ConnectOptions> copy = std::make_shared<rfh::ConnectOptions>(options);
		options.wipePassword();
		return submit("connect", [copy](rfh::MqSession & s)
		{
			JobResult r;
			r.result = connectAndWipe(s, *copy);
			r.queueManager = s.queueManagerInfo();
			return r;
		});
	}

	bool SessionController::disconnectFromQm()
	{
		return submit("disconnect", [](rfh::MqSession & s)
		{
			JobResult r;
			r.result = s.disconnect();
			return r;
		});
	}

	bool SessionController::put(const std::string & queue, const rfh::Message & message, const rfh::PutOptions & options)
	{
		return submit("put", [queue, message, options](rfh::MqSession & s)
		{
			JobResult r;
			r.message = message;
			r.result = s.put(queue, r.message, options);
			return r;
		});
	}

	bool SessionController::get(const std::string & queue, const rfh::GetOptions & options)
	{
		rfh::GetOptions mine = options;
		mine.cancel = &cancelRequested;
		return submit("get", [queue, mine](rfh::MqSession & s)
		{
			JobResult r;
			r.result = s.get(queue, r.message, mine);
			r.browsePosition = s.browsePosition();
			return r;
		});
	}

	bool SessionController::purge(const std::string & queue, const rfh::PurgeOptions & options)
	{
		rfh::PurgeOptions mine = options;
		mine.cancel = &cancelRequested;
		return submit("purge", [queue, mine](rfh::MqSession & s)
		{
			JobResult r;
			MQLONG count = 0;
			MQLONG uncertain = 0;
			r.result = s.purge(queue, count, mine, uncertain);
			r.count = count;
			r.uncertain = uncertain;
			if (s.isConnected())
			{
				// Windows purgeQueue ends with getCurrentDepth (DataArea.cpp:10226)
				s.queueInfo(queue, r.queue);
				r.hasQueueInfo = true;
			}
			return r;
		});
	}

	bool SessionController::commit()
	{
		return submit("commit", [](rfh::MqSession & s)
		{
			JobResult r;
			r.result = s.commit();
			return r;
		});
	}

	bool SessionController::rollback()
	{
		return submit("rollback", [](rfh::MqSession & s)
		{
			JobResult r;
			r.result = s.rollback();
			return r;
		});
	}

	bool SessionController::listQueues(const rfh::QueueListOptions & options)
	{
		return submit("listQueues", [options](rfh::MqSession & s)
		{
			JobResult r;
			r.result = s.listQueues(options, r.names);
			return r;
		});
	}

	bool SessionController::inquireQueue(const std::string & queue)
	{
		return submit("queueInfo", [queue](rfh::MqSession & s)
		{
			JobResult r;
			r.result = s.queueInfo(queue, r.queue);
			r.hasQueueInfo = true;
			return r;
		});
	}
}
