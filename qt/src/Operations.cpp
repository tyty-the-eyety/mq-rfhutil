#include "Operations.h"

#include <QFileInfo>

#include <utility>

#include "AppSettings.h"
#include "rfh_status_text.h"
#include "rfh_trace.h"

namespace rfhqt
{
	namespace
	{
		// ---- the worker side: building blocks of the jobs. They run on the worker thread, with the session. ----

		void addStep(JobResult & r, const char * operation, const rfh::Result & result)
		{
			JobStep step;
			step.operation = operation;
			step.result = result;
			r.steps.push_back(step);
			r.result = result;
		}

		// Adds the step; setErrorMsg case 6104 (DataArea.cpp:9859) closes the queue as part of reporting the error.
		void record(rfh::MqSession & s, JobResult & r, const char * operation, const rfh::Result & result)
		{
			addStep(r, operation, result);
			if ((result.rc == MQRC_ATTRIBUTE_LOCKED) && s.isConnected())
			{
				addStep(r, "Close", s.closeQueue(rfh::CloseOption::None));
			}
		}

		// DataArea::closeQ (12397): a unit of work that is still open is rolled back first (the inventory's reading of
		// 12413; Windows' unitOfWorkActive is never set, so there the disconnect that follows commits it).
		void closeQueue(rfh::MqSession & s, bool pendingUnit, rfh::CloseOption option, JobResult & r)
		{
			if (pendingUnit && s.isConnected())
			{
				addStep(r, "rollback", s.rollback());
			}
			if (s.isConnected())
			{
				record(s, r, "Close", s.closeQueue(option));
			}
		}

		// DataArea::checkConnection (12489). False when the flow must stop because the connect failed.
		bool checkConnection(rfh::MqSession & s, const std::string & qm, const std::string & connectedQm, bool pendingUnit,
		                     const std::shared_ptr<rfh::ConnectOptions> & connect, JobResult & r)
		{
			if (s.isConnected() && (connectedQm != qm))
			{
				closeQueue(s, pendingUnit, rfh::CloseOption::None, r);
				addStep(r, "Disc", s.disconnect());
			}
			if (!s.isConnected())
			{
				rfh::ConnectOptions fallback;
				rfh::ConnectOptions & options = connect ? *connect : fallback;
				options.qmName = qm;
				rfh::Result c = connectAndWipe(s, options);
				r.queueManager = s.queueManagerInfo();
				addStep(r, "Connect", c);
				r.reconnected = c.ok();
				return c.ok();
			}
			return true;
		}
	}

	// ---- construction and plumbing ------------------------------------------------------------------------------

	Operations::Operations(AppDocument & document, SessionController & sessionController, StatusLog & statusLog, QObject * parent)
		: QObject(parent), doc(document), controller(sessionController), log(statusLog)
	{
		connect(&controller, &SessionController::finished, this, &Operations::onFinished);
		connect(&controller, &SessionController::busyChanged, this, &Operations::mirrorBusy);
	}

	Operations::~Operations()
	{
		options.connect.wipePassword();
	}

	void Operations::setOptions(const MainOptions & newOptions)
	{
		options.connect.wipePassword();
		options = newOptions;
	}

	void Operations::setPrompter(Prompter * newPrompter)
	{
		prompter = newPrompter;
	}

	void Operations::setSettings(AppSettings * newSettings)
	{
		settings = newSettings;
	}

	void Operations::trace(const QString & text) const
	{
		RfhTrace * sink = rfhGetTrace();
		if (sink->isTraceEnabled())
		{
			sink->logTraceEntry(text.toLocal8Bit().constData());
		}
	}

	void Operations::mirrorBusy(bool busy)
	{
		ShellState shell = doc.shell();
		shell.busy = busy;
		shell.cancellable = busy && running.cancellable;
		doc.setShell(shell);
	}

	void Operations::addStatus(const QStringList & lines)
	{
		log.add(lines);
	}

	void Operations::finishFlow(const QString & flow)
	{
		emit statusChanged();
		emit flowFinished(flow);
	}

	bool Operations::admit(const QString & flow, bool enabled)
	{
		if (controller.busy())
		{
			addStatus(QStringList() << QStringLiteral("Another operation is still running"));
			finishFlow(flow);
			return false;
		}
		if (!enabled)
		{
			finishFlow(flow);
			return false;
		}
		return true;
	}

	Operations::Env Operations::capture() const
	{
		Env env;
		env.qm = doc.queueManager().toStdString();
		env.connectedQm = connectedQm.toStdString();
		env.queue = doc.queueName().toStdString();
		env.remoteQm = doc.remoteQueueManager().toStdString();
		env.pendingUnit = pendingUnit;
		env.closeOption = options.closeOption;
		return env;
	}

	bool Operations::submitFlow(const QString & flow, const Env & env, bool cancellable, SessionController::Job job,
	                            std::function<void(const JobResult &)> done)
	{
		running = Running();
		running.name = flow;
		running.qm = QString::fromStdString(env.qm);
		running.checked = true;
		running.cancellable = cancellable;
		running.done = std::move(done);
		userCancelled = false;
		const bool accepted = controller.submit(flow, std::move(job));
		if (env.connect)
		{
			options.connect.wipePassword();		// the copy in env is the job's now; the stored one is spent
		}
		if (!accepted)
		{
			if (env.connect)
			{
				env.connect->wipePassword();
			}
			running = Running();
			addStatus(QStringList() << QStringLiteral("Another operation is still running"));
			finishFlow(flow);
		}
		return accepted;
	}

	void Operations::onFinished(const QString & name, const JobResult & result)
	{
		if (!running.done || (running.name != name))
		{
			return;
		}
		Running mine = std::move(running);
		running = Running();
		absorb(name, mine, result);
		mine.done(result);
		userCancelled = false;
	}

	// The state every flow shares: the connection, the queue handle and the queue facts come from the session
	// (through AppDocument::applyJobResult); the unit of work and the connection name are Operations' own.
	void Operations::absorb(const QString & name, const Running & flow, const JobResult & r)
	{
		doc.applyJobResult(r.reconnected ? QStringLiteral("connect") : name, r);
		for (const JobStep & step : r.steps)
		{
			if (step.result.ok() && ((step.operation == "rollback") || (step.operation == "commit")))
			{
				pendingUnit = false;
			}
		}
		if (!r.connected)
		{
			connectedQm.clear();
			pendingUnit = false;			// released without a back-out: the outcome is the queue manager's
			resolvedLogged.clear();
		}
		else if (flow.checked)
		{
			connectedQm = flow.qm;
		}
		applyEffects(r);
	}

	// The state changes of DataArea::setErrorMsg that a text function does not make: 2009 is the connection flag
	// (applyJobResult), 6104 is done by the job (record), 2033 sets the queue depth to 0 (DataArea.cpp:9427).
	void Operations::applyEffects(const JobResult & r)
	{
		for (const JobStep & step : r.steps)
		{
			if ((step.result.rc == MQRC_NO_MSG_AVAILABLE) && !userCancelled)
			{
				rfh::QueueInfo info = doc.queueInfo();
				info.depth = 0;
				doc.setQueueInfo(info);
			}
		}
	}

	QStringList Operations::describeStep(const JobStep & step, bool closeFailed) const
	{
		QStringList lines;
		const rfh::Result & r = step.result;
		if (!r.ok() || (r.cc == MQCC_WARNING) || (r.rc != MQRC_NONE))
		{
			rfh::StatusContext context;
			context.operation = step.operation.c_str();
			context.maxMsgLenQm = doc.queueManagerInfo().maxMsgLength;
			context.maxMsgLenQueue = (doc.queueInfo().maxMsgLength > 0) ? doc.queueInfo().maxMsgLength : 0;
			const std::string text = rfh::statusText(r, context);
			if (!text.empty())
			{
				lines << QString::fromStdString(text);
			}
		}
		if ((step.operation == "rollback") && r.ok())
		{
			lines << QStringLiteral("Unit of work rolled back");		// DataArea::rollbackUOW (12618)
		}
		// A failed close leaves the handle to the disconnect, which closes it again and warns of the same failure
		// that the Close step already reported: Windows reports a failed close once (closeQ clears the handle).
		if (!((step.operation == "Disc") && closeFailed))
		{
			for (const std::string & warning : r.warnings)
			{
				lines << QString::fromStdString(warning);
			}
		}
		return lines;
	}

	QStringList Operations::describe(const JobResult & result) const
	{
		QStringList lines;
		bool closeFailed = false;
		for (const JobStep & step : result.steps)
		{
			lines += describeStep(step, closeFailed);
			closeFailed = closeFailed || ((step.operation == "Close") && !step.result.ok());
		}
		return lines;
	}

	// openQ logs "Queue name resolved to %.48s" as an entry of its own (DataArea.cpp:12303), once per open.
	void Operations::logResolvedName(const JobResult & r)
	{
		if (!r.hasQueueInfo || !r.result.ok() || r.queue.resolvedName.empty())
		{
			return;
		}
		const QString key = doc.queueManager() + QLatin1Char('/') + doc.queueName() + QLatin1Char('>') + QString::fromStdString(r.queue.resolvedName);
		if (key == resolvedLogged)
		{
			return;
		}
		resolvedLogged = key;
		log.add(QStringLiteral("Queue name resolved to ") + QString::fromStdString(r.queue.resolvedName).left(48));
	}

	void Operations::rememberQueue(const QString & queue)
	{
		if (settings != nullptr)
		{
			settings->addQueue(doc.queueManager(), queue);
		}
	}

	void Operations::cancelGet()
	{
		if (controller.busy())
		{
			userCancelled = true;
			controller.cancel();
		}
	}

	// ---- connection and queue flows ---------------------------------------------------------------------------------

	void Operations::connectQm()
	{
		const QString flow = QStringLiteral("connect");
		if (!admit(flow, doc.enables().menuMqConn))
		{
			return;
		}
		const Env base = capture();
		trace(QStringLiteral("Entering Operations::connectQm QM=%1").arg(doc.queueManager()));
		Env env = base;
		env.connect = std::make_shared<rfh::ConnectOptions>(options.connect);
		const QString qm = doc.queueManager();
		rfh::ConnectOptions remembered = options.connect;
		remembered.wipePassword();
		remembered.qmName = qm.toStdString();
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			checkConnection(s, env.qm, env.connectedQm, env.pendingUnit, env.connect, r);
			return r;
		}, [this, flow, qm, remembered](const JobResult & r)
		{
			QStringList lines;
			if (r.connected)
			{
				lines << QStringLiteral("Connected to ") + qm;		// explicitConnect: sprintf "Connected to %s" (11796)
			}
			lines += describe(r);
			if (r.connected && r.result.ok() && (settings != nullptr))
			{
				settings->addQueueManager(qm);
				settings->setConnection(remembered);
			}
			addStatus(lines);
			finishFlow(flow);
		});
	}

	void Operations::disconnectQm()
	{
		const QString flow = QStringLiteral("disconnect");
		if (!admit(flow, doc.enables().menuMqDisc))
		{
			return;
		}
		const Env env = capture();
		trace(QStringLiteral("Entering Operations::disconnectQm QM=%1").arg(doc.queueManager()));
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			if (s.isQueueOpen())
			{
				closeQueue(s, env.pendingUnit, env.closeOption, r);		// explicitCloseQ -> closeQ(m_close_option)
			}
			else if (env.pendingUnit && s.isConnected())
			{
				addStep(r, "rollback", s.rollback());
			}
			addStep(r, "Disc", s.disconnect());
			return r;
		}, [this, flow](const JobResult & r)
		{
			QStringList lines;
			bool closeFailed = false;
			for (const JobStep & step : r.steps)
			{
				lines += describeStep(step, closeFailed);
				if (step.operation == "Close")
				{
					lines << QStringLiteral("Queue closed");			// explicitCloseQ: appendError("Queue closed")
					closeFailed = !step.result.ok();
				}
			}
			lines << QStringLiteral("Disconnected from queue manager");	// explicitDiscQM
			addStatus(lines);
			finishFlow(flow);
		});
	}

	void Operations::closeQueueMenu()
	{
		const QString flow = QStringLiteral("closeQueue");
		if (!admit(flow, doc.enables().menuMqClose))
		{
			return;
		}
		const Env env = capture();
		trace(QStringLiteral("Entering Operations::closeQueueMenu Q=%1").arg(doc.queueName()));
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			closeQueue(s, env.pendingUnit, env.closeOption, r);
			return r;
		}, [this, flow](const JobResult & r)
		{
			QStringList lines = describe(r);
			lines << QStringLiteral("Queue closed");
			addStatus(lines);
			finishFlow(flow);
		});
	}

	void Operations::closeQ()
	{
		const QString flow = QStringLiteral("closeQ");
		if (!admit(flow, doc.enables().closeQ))
		{
			return;
		}
		const Env env = capture();
		trace(QStringLiteral("Entering Operations::closeQ QM=%1 Q=%2").arg(doc.queueManager(), doc.queueName()));
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			closeQueue(s, env.pendingUnit, env.closeOption, r);			// closeQ(m_close_options)
			addStep(r, "Disc", s.disconnect());							// discQM()
			return r;
		}, [this, flow](const JobResult & r)
		{
			addStatus(describe(r));
			finishFlow(flow);
		});
	}

	void Operations::refreshQueueInfo()
	{
		const QString flow = QStringLiteral("queueInfo");
		if (controller.busy())
		{
			admit(flow, false);
			return;
		}
		trace(QStringLiteral("Entering Operations::refreshQueueInfo QM=%1 Q=%2").arg(doc.queueManager(), doc.queueName()));
		if (doc.queueName().isEmpty())
		{
			addStatus(QStringList() << QStringLiteral("*Queue Name required* "));		// DataArea.cpp:10270
			finishFlow(flow);
			return;
		}
		Env env = capture();
		if (!doc.connected() || (connectedQm != doc.queueManager()))
		{
			env.connect = std::make_shared<rfh::ConnectOptions>(options.connect);
		}
		const QString queue = doc.queueName();
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			if (checkConnection(s, env.qm, env.connectedQm, env.pendingUnit, env.connect, r))
			{
				rfh::QueueInfo info;
				rfh::Result q = s.queueInfo(env.queue, info);
				r.queue = info;
				r.hasQueueInfo = true;
				record(s, r, "Open", q);
			}
			return r;
		}, [this, flow, queue](const JobResult & r)
		{
			logResolvedName(r);
			addStatus(describe(r));
			if (r.result.ok() && r.hasQueueInfo)
			{
				rememberQueue(queue);
			}
			finishFlow(flow);
		});
	}
}
