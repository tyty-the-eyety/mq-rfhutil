#include "Operations.h"

#include <QDir>
#include <QFileInfo>
#include <time.h>

#include <utility>

#include "AppSettings.h"
#include "LoadQDialog.h"
#include "SaveQDialog.h"
#include "rfh_format.h"
#include "rfh_header_chain.h"
#include "rfh_id_display.h"
#include "rfh_mqmd_fields.h"
#include "rfh_platform.h"
#include "rfh_queue_file.h"
#include "rfh_status_text.h"
#include "rfh_trace.h"

namespace rfhqt
{
	namespace
	{
		// ---- the worker side: building blocks of the jobs. They run on the worker thread, with the session. ----

		void addStep(JobResult & r, const char * operation, const rfh::Result & result, bool detail = false, bool syncpoint = false)
		{
			JobStep step;
			step.operation = operation;
			step.result = result;
			step.detail = detail;
			step.syncpoint = syncpoint;
			r.steps.push_back(step);
			r.result = result;
		}

		// Adds the step; setErrorMsg case 6104 (DataArea.cpp:9859) closes the queue as part of reporting the error.
		void record(rfh::MqSession & s, JobResult & r, const char * operation, const rfh::Result & result, bool detail = false,
		            bool syncpoint = false)
		{
			addStep(r, operation, result, detail, syncpoint);
			if ((result.rc == MQRC_ATTRIBUTE_LOCKED) && s.isConnected())
			{
				addStep(r, "Close", s.closeQueue(rfh::CloseOption::None));
			}
		}

		// DataArea::closeQ (12397): Windows' unitOfWorkActive is never set, so MQDISC commits whatever unit is pending.
		void closeQueue(rfh::MqSession & s, rfh::CloseOption option, JobResult & r)
		{
			if (s.isConnected())
			{
				record(s, r, "Close", s.closeQueue(option));
			}
		}

		// The end of a browse (discQM after a browse that ended or failed: DA 8281, 8648, 9208, 8438).
		// MQDISC commits whatever unit is pending (Windows behaviour).
		void leaveBrowse(rfh::MqSession & s, JobResult & r)
		{
			closeQueue(s, rfh::CloseOption::None, r);
			addStep(r, "Disc", s.disconnect());
		}

		// DataArea::checkConnection (12489). False when the flow must stop because the connect failed.
		bool checkConnection(rfh::MqSession & s, const std::string & qm, const std::string & connectedQm, bool pendingUnit,
		                     const std::shared_ptr<rfh::ConnectOptions> & connect, JobResult & r)
		{
			if (s.isConnected() && (connectedQm != qm))
			{
				closeQueue(s, rfh::CloseOption::None, r);
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
		if (doc.shell().busy)
		{
			mirrorBusy(false);			// a job still running at this point reports to nobody: the document must not stay busy
		}
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
		if (controller.busy() || prompting)		// a confirmation is open: its flow has not finished
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

	Operations::Env Operations::capture(bool mayConnect) const
	{
		Env env;
		env.checks = mayConnect;
		env.qm = doc.queueManager().toStdString();
		env.connectedQm = connectedQm.toStdString();
		env.queue = doc.queueName().toStdString();
		env.remoteQm = doc.remoteQueueManager().toStdString();
		env.pendingUnit = pendingUnit;
		env.closeOption = options.closeOption;
		if (mayConnect && (!doc.connected() || (connectedQm != doc.queueManager())))
		{
			env.connect = std::make_shared<rfh::ConnectOptions>(options.connect);		// the job wipes this copy
		}
		return env;
	}

	bool Operations::submitFlow(const QString & flow, const Env & env, bool cancellable, SessionController::Job job,
	                            std::function<void(const JobResult &)> done)
	{
		running = Running();
		running.name = flow;
		running.qm = QString::fromStdString(env.qm);
		running.checked = env.checks;
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
		applyEffects(r);		// before the job's own queue facts: a depth read after the 2033 wins (browseNext, DA 8640)
		doc.applyJobResult(r.reconnected ? QStringLiteral("connect") : name, r);
		// The unit of work follows the steps in order: a commit or rollback that worked ends the unit that was open at
		// that point (the old connection's, when checkConnection backed it out before a put), and a put under
		// syncpoint that worked opens the next one.
		for (const JobStep & step : r.steps)
		{
			if (step.result.ok() && ((step.operation == "rollback") || (step.operation == "commit")))
			{
				pendingUnit = false;
			}
			else if ((step.operation == "Put") && step.syncpoint && (step.result.cc != MQCC_FAILED))
			{
				pendingUnit = true;
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
		if (r.reconnected && (settings != nullptr))
		{
			// connect2QM remembers the latest queue manager (saveClientQM, DataArea.cpp:11222) and the connection values
			rfh::ConnectOptions remembered = options.connect;
			remembered.wipePassword();
			remembered.qmName = flow.qm.toStdString();
			settings->addQueueManager(flow.qm);
			settings->setConnection(remembered);
		}
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
		if (step.detail && !r.ok() && !r.text.empty())
		{
			lines << QString::fromStdString(r.text);					// what became of the unit of work, as the core says it
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
		return lines + noteLines(result);
	}

	QStringList Operations::noteLines(const JobResult & result)
	{
		QStringList lines;
		for (const std::string & note : result.notes)
		{
			lines << QString::fromStdString(note);
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
		const Env env = capture(true);
		const QString qm = doc.queueManager();
		trace(QStringLiteral("Entering Operations::connectQm QM=%1").arg(qm));
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			checkConnection(s, env.qm, env.connectedQm, env.pendingUnit, env.connect, r);
			return r;
		}, [this, flow, qm](const JobResult & r)
		{
			QStringList lines;
			if (r.connected)
			{
				lines << QStringLiteral("Connected to ") + qm;		// explicitConnect: sprintf "Connected to %s" (11796)
			}
			lines += describe(r);
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
		const Env env = capture(false);
		trace(QStringLiteral("Entering Operations::disconnectQm QM=%1").arg(doc.queueManager()));
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			if (s.isQueueOpen())
			{
				closeQueue(s, env.closeOption, r);		// explicitCloseQ -> closeQ(m_close_option)
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
		const Env env = capture(false);
		trace(QStringLiteral("Entering Operations::closeQueueMenu Q=%1").arg(doc.queueName()));
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			closeQueue(s, env.closeOption, r);
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
		const Env env = capture(false);
		trace(QStringLiteral("Entering Operations::closeQ QM=%1 Q=%2").arg(doc.queueManager(), doc.queueName()));
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			closeQueue(s, env.closeOption, r);		// closeQ(m_close_options)
			addStep(r, "Disc", s.disconnect());		// discQM()
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
		if (controller.busy() || prompting)
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
		const Env env = capture(true);
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

	QStringList Operations::queueNames(const QString & queueManager) const
	{
		return loadedNames.value(queueManager);
	}

	// OnLoadQueueNames (General.cpp:1642): an open queue is closed (close option None) and the connection dropped, because
	// getQueueNames uses a connection of its own; it connects to the queue manager of the tab, asks the command server
	// for the local, remote and alias queues (cluster queues are not offered: View > Cluster Queues is not ported) and
	// disconnects. A failure shows the MQ text and then "Error getting queue names" (General.cpp:1710).
	void Operations::loadNames()
	{
		const QString flow = QStringLiteral("loadNames");
		if (!admit(flow, doc.enables().loadNames))
		{
			return;
		}
		trace(QStringLiteral("Entering Operations::loadNames QM=%1").arg(doc.queueManager()));
		Env env = capture(false);
		env.checks = true;
		env.connect = std::make_shared<rfh::ConnectOptions>(options.connect);		// the job wipes this copy
		const QString qm = doc.queueManager();
		rfh::QueueListOptions list;
		list.alternateUser = doc.shell().alternateUser ? doc.mqmd().userId : std::string();
		submitFlow(flow, env, false, [env, list](rfh::MqSession & s)
		{
			JobResult r;
			if (s.isConnected())
			{
				closeQueue(s, rfh::CloseOption::None, r);
				addStep(r, "Disc", s.disconnect());
			}
			env.connect->qmName = env.qm;
			const rfh::Result c = connectAndWipe(s, *env.connect);
			r.queueManager = s.queueManagerInfo();
			addStep(r, "Connect", c);
			if (c.ok())
			{
				addStep(r, "listQueues", s.listQueues(list, r.names));
				addStep(r, "Disc", s.disconnect());
			}
			return r;
		}, [this, flow, qm](const JobResult & r)
		{
			QStringList lines;
			bool failed = false;
			bool closeFailed = false;
			for (const JobStep & step : r.steps)
			{
				if (step.operation == "listQueues")
				{
					// the core's own text carries the Windows wording of the admin queue failures
					if (!step.result.ok() && !step.result.text.empty())
					{
						lines << QString::fromStdString(step.result.text);
					}
					for (const std::string & warning : step.result.warnings)
					{
						lines << QString::fromStdString(warning);
					}
				}
				else
				{
					lines += describeStep(step, closeFailed);
				}
				failed = failed || (!step.result.ok() && ((step.operation == "Connect") || (step.operation == "listQueues")));
				closeFailed = closeFailed || ((step.operation == "Close") && !step.result.ok());
			}
			if (failed)
			{
				lines << QStringLiteral("Error getting queue names");
			}
			QStringList names;
			for (const std::string & name : r.names)
			{
				names << QString::fromStdString(name);
			}
			names.removeDuplicates();
			names.sort();
			if (!names.isEmpty() || !failed)
			{
				loadedNames.insert(qm, names);
				emit queueNamesChanged(qm);
			}
			addStatus(lines);
			finishFlow(flow);
		});
	}

	// ---- messages from queues -------------------------------------------------------------------------------------

	namespace
	{
		// " (data %d dlq %d rfh %d cics %d ims %d)" when any header was found (getMessage, DataArea.cpp:8135-8145).
		// The lengths are the header chain's (the core's own walk of the message); an MQMDE is not counted.
		QString headerSuffix(const rfh::Message & m)
		{
			const rfh::HeaderChain chain = rfh::walkHeaders(m.headers.data(), m.headers.size(), m.mqmd);
			size_t rfh = 0, dlq = 0, cics = 0, ims = 0;
			for (const rfh::HeaderInfo & h : chain.headers)
			{
				switch (h.type)
				{
				case rfh::HeaderType::Rfh1:
				case rfh::HeaderType::Rfh2: rfh += h.length; break;
				case rfh::HeaderType::DeadLetter: dlq += h.length; break;
				case rfh::HeaderType::Cics: cics += h.length; break;
				case rfh::HeaderType::Ims: ims += h.length; break;
				case rfh::HeaderType::MdExtension: break;
				}
			}
			if (rfh + dlq + cics + ims == 0)
			{
				return QString();
			}
			return QStringLiteral(" (data %1 dlq %2 rfh %3 cics %4 ims %5)").arg(m.body.size()).arg(dlq).arg(rfh).arg(cics).arg(ims);
		}

		// The reason code of the step that did the get (the last step of a failed browse is the disconnect)
		MQLONG reasonOfStep(const JobResult & r, const char * operation)
		{
			for (const JobStep & step : r.steps)
			{
				if (step.operation == operation)
				{
					return step.result.rc;
				}
			}
			return MQRC_NONE;
		}

		QString trimmedLine(const std::string & text)
		{
			QString line = QString::fromStdString(text);
			while (line.endsWith(QLatin1Char('\r')) || line.endsWith(QLatin1Char('\n')))
			{
				line.chop(1);
			}
			return line;
		}
	}

	rfh::GetOptions Operations::buildGet(rfh::GetMode mode, bool useMatch, MQLONG hostEncoding, bool wait) const
	{
		rfh::GetOptions g = options.get;
		const rfh::MqmdFields & f = doc.mqmd();
		g.mode = mode;
		g.waitMs = wait ? options.get.waitMs : 0;
		// Get by Msg Id / Correl Id / Group Id apply only when the MQMD tab holds a non-zero id (getMessage 8043-8059)
		g.matchMsgId = useMatch && options.get.matchMsgId && !rfh::id_display::isZero(f.msgId);
		g.matchCorrelId = useMatch && options.get.matchCorrelId && !rfh::id_display::isZero(f.correlId);
		g.matchGroupId = useMatch && options.get.matchGroupId && !rfh::id_display::isZero(f.groupId);
		memcpy(g.msgId, f.msgId, sizeof(g.msgId));
		memcpy(g.correlId, f.correlId, sizeof(g.correlId));
		memcpy(g.groupId, f.groupId, sizeof(g.groupId));
		if (g.convert)
		{
			// the code page of the MQMD tab (the queue manager's when it is empty) and the tab's integer format (8000-8030)
			g.convertCcsid = f.ccsid.empty() ? MQCCSI_Q_MGR : (MQLONG)atoi(f.ccsid.c_str());
			g.convertEncoding = (f.encoding == NUMERIC_PC) ? 546 : hostEncoding;
		}
		g.alternateUser = doc.shell().alternateUser ? f.userId : std::string();
		g.cancel = controller.cancelFlag();
		return g;
	}

	QString Operations::depthText() const
	{
		return (doc.queueInfo().depth >= 0) ? QString::number(doc.queueInfo().depth) : QStringLiteral("?");
	}

	// processMessage: the MQMD tab, the display formats and the data come from the message; the document keeps the
	// only copy. The message, the MQMD fields and the formats reach the document as one change.
	void Operations::acceptMessage(const rfh::Message & message, const std::string & source, QStringList & warnings)
	{
		rfh::MqmdFields fields = doc.mqmd();				// keeps the float format, as the Windows page does
		rfh::mqmd_fields::fromGetMqmd(message.mqmd, fields);
		std::vector<std::string> formatWarnings;
		rfh::format::RenderOptions render = doc.render();
		render.apply(rfh::format::defaultsFor(message, formatWarnings));
		for (const std::string & w : formatWarnings)
		{
			warnings << trimmedLine(w);
		}
		rfh::Message copy = message;
		doc.acceptMessage(std::move(copy), source, true, fields, render);
	}

	void Operations::clearAfterGet(bool clearFileName)
	{
		ShellState shell = doc.shell();
		if (shell.setIdentity)
		{
			shell.setIdentity = false;			// m_set_UserID = FALSE: the user id may have changed
			doc.setShell(shell);
		}
		if (clearFileName && doc.hasFile())
		{
			doc.setFilePath(QString());			// fileName[0] = 0: the data came from a queue
		}
	}

	// A failure that processMQGet reported with one operation word and its caller again with another (startBrowse,
	// browseNext): both texts, unless they are the same text.
	QStringList Operations::describeBrowseFailure(const JobResult & result, const char * secondWord) const
	{
		QStringList lines;
		bool closeFailed = false;
		for (const JobStep & step : result.steps)
		{
			const QStringList first = describeStep(step, closeFailed);
			lines += first;
			if (!step.result.ok() && ((step.operation == "StartBr") || (step.operation == "BrowseNext")))
			{
				JobStep again = step;
				again.operation = secondWord;
				again.result.warnings.clear();
				for (const QString & line : describeStep(again, closeFailed))
				{
					if (!first.contains(line))
					{
						lines << line;
					}
				}
			}
			closeFailed = closeFailed || ((step.operation == "Close") && !step.result.ok());
		}
		return lines + noteLines(result);
	}

	void Operations::readQ()
	{
		queueGet(GetKind::Read);
	}

	void Operations::browseQ()
	{
		queueGet(GetKind::BrowseQ);
	}

	// Read Q and Browse Q: getMessage(READQ / BROWSEQ), then the handler's own clean-up (OnReadq GEN 276, OnBrowse GEN 372)
	void Operations::queueGet(GetKind kind)
	{
		const bool browse = (kind == GetKind::BrowseQ);
		const QString flow = browse ? QStringLiteral("browseQ") : QStringLiteral("readQ");
		if (!admit(flow, browse ? doc.enables().browseQ : doc.enables().readQ))
		{
			return;
		}
		const QString queue = doc.queueName();
		trace(QStringLiteral("Entering Operations::%1 QM=%2 Q=%3").arg(flow, doc.queueManager(), queue));
		if (queue.isEmpty())
		{
			addStatus(QStringList() << QStringLiteral("*Queue Name required*"));		// DA 7991 (no blank)
			clearAfterGet(true);
			emit titleChanged(queue);
			finishFlow(flow);
			return;
		}
		const Env env = capture(true);
		const rfh::GetOptions g = buildGet(browse ? rfh::GetMode::BrowseFirst : rfh::GetMode::Destructive, true, 273, !browse);
		const bool waits = !browse && (g.waitMs != 0);
		submitFlow(flow, env, waits, [env, g, browse](rfh::MqSession & s)
		{
			JobResult r;
			if (checkConnection(s, env.qm, env.connectedQm, env.pendingUnit, env.connect, r))
			{
				rfh::Message m;
				const rfh::Result got = s.get(env.queue, m, g);
				r.browsePosition = s.browsePosition();
				record(s, r, "Get", got);
				if (got.cc != MQCC_FAILED)
				{
					r.message = std::move(m);
					r.hasMessage = true;
					r.hasQueueInfo = true;
					s.queueInfo(env.queue, r.queue);		// getCurrentDepth, getQueueType, the maximum length
					if (browse)
					{
						record(s, r, "Close", s.closeQueue());		// closeQ(Q_CLOSE_NONE), DA 8151
					}
				}
			}
			return r;
		}, [this, flow, queue, browse](const JobResult & r)
		{
			logResolvedName(r);
			QStringList lines;
			if (r.hasMessage)
			{
				const QString source = QStringLiteral("Message Data (%1) from %2").arg(r.message.body.size()).arg(queue);
				QStringList warnings;
				acceptMessage(r.message, source.toStdString(), warnings);
				lines += describe(r);
				lines += warnings;
				lines << QStringLiteral("Msg %1 from %2 length=%3%4").arg(browse ? "browse" : "read", queue)
					.arg(r.message.body.size()).arg(headerSuffix(r.message));
			}
			else if (userCancelled && !browse && (r.result.rc == MQRC_NO_MSG_AVAILABLE))
			{
				lines << QStringLiteral("Get cancelled");			// NEW: the wait was stopped, which is not a 2033 report
			}
			else
			{
				lines += describe(r);
			}
			clearAfterGet(true);
			if (r.hasMessage || r.queueOpen)
			{
				rememberQueue(queue);
			}
			emit titleChanged(queue);
			addStatus(lines);
			finishFlow(flow);
		});
	}

	void Operations::startBrowse()
	{
		const QString flow = QStringLiteral("startBrowse");
		if (!admit(flow, doc.enables().startBrowse))
		{
			return;
		}
		const QString queue = doc.queueName();
		trace(QStringLiteral("Entering Operations::startBrowse QM=%1 Q=%2").arg(doc.queueManager(), queue));
		if (queue.isEmpty())
		{
			addStatus(QStringList() << QStringLiteral("*Queue Name required* "));		// DA 8368
			finishFlow(flow);
			return;
		}
		const Env env = capture(true);
		// readFirstMessage sets only the PC encoding for a conversion: host integers keep the native one (8226-8240)
		const rfh::GetOptions g = buildGet(rfh::GetMode::BrowseFirst, true, MQENC_NATIVE, false);
		submitFlow(flow, env, false, [env, g](rfh::MqSession & s)
		{
			JobResult r;
			if (checkConnection(s, env.qm, env.connectedQm, env.pendingUnit, env.connect, r))
			{
				rfh::Message m;
				const rfh::Result got = s.get(env.queue, m, g);
				r.browsePosition = s.browsePosition();
				record(s, r, "StartBr", got);
				if (got.cc != MQCC_FAILED)
				{
					r.message = std::move(m);
					r.hasMessage = true;
					r.hasQueueInfo = true;
					s.queueInfo(env.queue, r.queue);
				}
				else if (s.isConnected())
				{
					leaveBrowse(s, r);		// discQM closes the queue and disconnects

				}
			}
			return r;
		}, [this, flow, queue](const JobResult & r)
		{
			logResolvedName(r);
			QStringList lines;
			if (r.hasMessage)
			{
				const QString source = QStringLiteral("Message Data (%1) msg 1 of %2 from %3").arg(r.message.body.size()).arg(depthText(), queue);
				QStringList warnings;
				acceptMessage(r.message, source.toStdString(), warnings);
				lines += describe(r);
				lines += warnings;
				lines << QStringLiteral("First message read from %1 length=%2%3").arg(queue).arg(r.message.body.size()).arg(headerSuffix(r.message));
				BrowseState state;
				state.active = true;
				state.position = (r.browsePosition > 0) ? r.browsePosition : 1;
				doc.setBrowse(state);
				if (doc.hasFile())
				{
					doc.setFilePath(QString());
				}
				emit titleChanged(queue);
				rememberQueue(queue);
			}
			else if (reasonOfStep(r, "StartBr") == MQRC_NO_MSG_AVAILABLE)
			{
				lines << QStringLiteral("No messages in %1 queue").arg(queue);		// replaces the pending text, DA 8263
				lines += noteLines(r);
			}
			else
			{
				lines += describeBrowseFailure(r, "Get (Browse)");
			}
			addStatus(lines);
			finishFlow(flow);
		});
	}

	void Operations::browseNext()
	{
		const QString flow = QStringLiteral("browseNext");
		if (!admit(flow, doc.enables().browseNext))
		{
			return;
		}
		const QString queue = doc.queueName();
		trace(QStringLiteral("Entering Operations::browseNext Q=%1").arg(queue));
		const Env env = capture(false);
		const rfh::GetOptions g = buildGet(rfh::GetMode::BrowseNext, false, MQENC_NATIVE, false);
		submitFlow(flow, env, false, [env, g](rfh::MqSession & s)
		{
			JobResult r;
			rfh::Message m;
			const rfh::Result got = s.get(env.queue, m, g);
			r.browsePosition = s.browsePosition();
			record(s, r, "BrowseNext", got);
			if (got.cc != MQCC_FAILED)
			{
				r.message = std::move(m);
				r.hasMessage = true;
				r.hasQueueInfo = true;
				s.queueInfo(env.queue, r.queue);
			}
			else if (s.isConnected())
			{
				// getCurrentDepth, then discQM: the browse ends whatever went wrong (DA 8638-8648)
				r.hasQueueInfo = true;
				s.queueInfo(env.queue, r.queue);
				leaveBrowse(s, r);
			}
			return r;
		}, [this, flow, queue](const JobResult & r)
		{
			QStringList lines;
			if (r.hasMessage)
			{
				const QString source = QStringLiteral("Message Data (%1) msg %2 of %3 from %4")
					.arg(r.message.body.size()).arg(r.browsePosition).arg(depthText(), queue);
				QStringList warnings;
				acceptMessage(r.message, source.toStdString(), warnings);
				lines += describe(r);
				lines += warnings;
				lines << QStringLiteral("Next message (%1) read from %2 length=%3%4").arg(r.browsePosition).arg(queue)
					.arg(r.message.body.size()).arg(headerSuffix(r.message));
				BrowseState state;
				state.active = true;
				state.position = r.browsePosition;
				doc.setBrowse(state);
			}
			else
			{
				if (reasonOfStep(r, "BrowseNext") == MQRC_NO_MSG_AVAILABLE)
				{
					lines << QStringLiteral("No more messages in queue");		// replaces the pending text, DA 8638
					lines += noteLines(r);
				}
				else
				{
					lines += describeBrowseFailure(r, "GetNext");
				}
				doc.setBrowse(BrowseState());
			}
			clearAfterGet(false);
			addStatus(lines);
			finishFlow(flow);
		});
	}

	void Operations::browsePrev()
	{
		const QString flow = QStringLiteral("browsePrev");
		if (!admit(flow, doc.enables().browsePrev))
		{
			return;
		}
		const QString queue = doc.queueName();
		trace(QStringLiteral("Entering Operations::browsePrev Q=%1").arg(queue));
		const Env env = capture(false);
		const rfh::GetOptions g = buildGet(rfh::GetMode::BrowsePrevious, false, 273, false);
		const std::atomic<bool> * cancelFlag = controller.cancelFlag();
		submitFlow(flow, env, true, [env, g, cancelFlag](rfh::MqSession & s)
		{
			JobResult r;
			rfh::Message m;
			const rfh::Result got = s.get(env.queue, m, g);
			r.browsePosition = s.browsePosition();
			record(s, r, "BrowsePrevGet", got);
			if (got.cc != MQCC_FAILED)
			{
				r.message = std::move(m);
				r.hasMessage = true;
				r.hasQueueInfo = true;
				s.queueInfo(env.queue, r.queue);
			}
			else if (s.isConnected() && !cancelFlag->load())
			{
				leaveBrowse(s, r);		// the previous message cannot be found: discQM (DA 9208)
			}
			return r;
		}, [this, flow, queue](const JobResult & r)
		{
			QStringList lines;
			if (r.hasMessage)
			{
				const QString source = QStringLiteral("Message Data (%1) msg %2 of %3 from %4")
					.arg(r.message.body.size()).arg(r.browsePosition).arg(depthText(), queue);
				QStringList warnings;
				acceptMessage(r.message, source.toStdString(), warnings);
				lines += describe(r);
				lines += warnings;
				lines << QStringLiteral("Previous message (%1) read from %2 length=%3%4").arg(r.browsePosition).arg(queue)
					.arg(r.message.body.size()).arg(headerSuffix(r.message));
			}
			else
			{
				lines += describe(r);
			}
			if (r.connected && r.queueOpen)
			{
				BrowseState state;				// a cancelled walk keeps the browse where it stopped
				state.active = true;
				state.position = r.browsePosition;
				doc.setBrowse(state);
			}
			else
			{
				doc.setBrowse(BrowseState());
			}
			clearAfterGet(false);
			addStatus(lines);
			finishFlow(flow);
		});
	}

	void Operations::endBrowse()
	{
		const QString flow = QStringLiteral("endBrowse");
		if (!admit(flow, doc.enables().endBrowse))
		{
			return;
		}
		trace(QStringLiteral("Entering Operations::endBrowse Q=%1").arg(doc.queueName()));
		const Env env = capture(false);
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			if (s.isQueueOpen())
			{
				r.hasQueueInfo = true;
				s.queueInfo(env.queue, r.queue);										// getCurrentDepth
				leaveBrowse(s, r);
			}
			return r;
		}, [this, flow](const JobResult & r)
		{
			QStringList lines = describe(r);
			if (!r.steps.empty())
			{
				lines << QStringLiteral("Browse operation ended");
			}
			doc.setBrowse(BrowseState());
			addStatus(lines);
			finishFlow(flow);
		});
	}

	// ---- Write Q ---------------------------------------------------------------------------------------------------

	void Operations::writeQ()
	{
		const QString flow = QStringLiteral("writeQ");
		if (!admit(flow, doc.enables().writeQ))
		{
			return;
		}
		const QString queue = doc.queueName();
		trace(QStringLiteral("Entering Operations::writeQ QM=%1 Q=%2").arg(doc.queueManager(), queue));
		if (queue.isEmpty())
		{
			addStatus(QStringList() << QStringLiteral("*Queue Name required* "));		// DA 6218
			finishFlow(flow);
			return;
		}

		// the MQMD of the tab (setMessageMQMD), the headers and data of the document (DA 6296)
		const rfh::MqmdFields fields = doc.mqmd();
		const ShellState shell = doc.shell();
		rfh::PutContext context;
		context.setIdentity = shell.setIdentity;
		context.setAll = shell.setAll;
		context.osUser = rfh_current_user();
		time_t now_t = time(nullptr);
		struct tm now_buf;
		localtime_r(&now_t, &now_buf);
		context.now = &now_buf;
		auto message = std::make_shared<rfh::Message>();
		message->mqmd = MQMD2 { MQMD2_DEFAULT };
		rfh::mqmd_fields::toPutMqmd(fields, context, message->mqmd);
		message->headers = doc.message().headers;
		message->body = doc.message().body;
		const MQLONG length = (MQLONG)(message->headers.size() + message->body.size());

		rfh::PutOptions put = options.put;
		put.remoteQm = doc.remoteQueueManager().toStdString();
		put.setIdentityContext = shell.setIdentity;
		put.setAllContext = shell.setAll;
		put.alternateUser = shell.alternateUser ? fields.userId : std::string();
		// Windows has no syncpoint choice: a group or segment flag of the tab puts under syncpoint (DA 6342-6351)
		put.syncpoint = fields.segmentYes || fields.segmentLast || fields.groupYes || fields.groupLast;
		// commit after the last segment (of the last message of a group) or after the last message of a group (DA 6484-6503)
		const bool commit = fields.segmentYes ? (fields.segmentLast && (!fields.groupYes || fields.groupLast)) : fields.groupLast;

		const Env env = capture(true);
		// The job runs once, on the worker, and nothing else reads the snapshot: it is put in place, without a copy
		// of the body, and std::function copies of the job share it.
		submitFlow(flow, env, false, [env, message = std::move(message), put, commit](rfh::MqSession & s)
		{
			JobResult r;
			if (checkConnection(s, env.qm, env.connectedQm, env.pendingUnit, env.connect, r))
			{
				rfh::Message & sent = *message;
				const rfh::Result res = s.put(env.queue, sent, put);
				record(s, r, "Put", res, put.syncpoint, put.syncpoint);
				if (res.cc != MQCC_FAILED)
				{
					r.message.mqmd = sent.mqmd;				// the descriptor MQ returned
					r.hasMessage = true;
					if (commit)
					{
						const rfh::Result c = s.commit();
						addStep(r, "commit", c, true);
						if (!c.ok() && s.isConnected())
						{
							// commitUOW disconnects after a failure (DA 12586); MQDISC commits what MQCMIT did not
							addStep(r, "Disc", s.disconnect());
						}
					}
					if (s.isConnected())
					{
						r.hasQueueInfo = true;
						s.queueInfo(env.queue, r.queue);		// getCurrentDepth
					}
				}
			}
			return r;
		}, [this, flow, queue, fields, length](const JobResult & r)
		{
			logResolvedName(r);
			QStringList lines;
			for (const JobStep & step : r.steps)
			{
				lines += describeStep(step, false);
				if ((step.operation == "Put") && r.hasMessage)
				{
					rfh::MqmdFields after = fields;
					rfh::mqmd_fields::applyAfterPut(r.message.mqmd, length, after);
					doc.setMqmd(after);
					lines << QStringLiteral("Message sent to %1 length=%2").arg(queue).arg(length);
				}
			}
			if (r.hasMessage || r.queueOpen)
			{
				rememberQueue(queue);
			}
			if (r.hasMessage)
			{
				emit titleChanged(queue);
			}
			addStatus(lines);
			finishFlow(flow);
		});
	}

	// ---- files ----------------------------------------------------------------------------------------------------

	void Operations::openFile(const QString & path)
	{
		const QString flow = QStringLiteral("openFile");
		if (!admit(flow, doc.enables().openFile))
		{
			return;
		}
		trace(QStringLiteral("Entering Operations::openFile %1").arg(path));
		rfh::fileio::ReadOptions readOptions = options.read;
		readOptions.fileCcsid = options.fileCodePage;
		const rfh::MqmdFields fields = doc.mqmd();				// the MQMD tab supplies the defaults, as in Windows
		const std::string name = path.toStdString();
		const Env env = capture(false);
		submitFlow(flow, env, false, [path, name, readOptions, fields](rfh::MqSession &)
		{
			JobResult r;
			// A file larger than the largest message cannot be shown (render fails above it) or put: refuse it before
			// it is read, so the document is not replaced by something unusable. The size is read here, on the worker:
			// a hung mount must not block the GUI thread.
			const QFileInfo info(path);
			if (info.isFile() && (info.size() > (qint64)rfh::format::kMaxRenderBody))
			{
				addStep(r, "Read", rfh::Result::failure(QStringLiteral("File %1 is too large: %2 bytes, the limit is %3 bytes (the largest MQ message)")
					.arg(path).arg(info.size()).arg((qint64)rfh::format::kMaxRenderBody).toStdString()));
				return r;
			}
			auto read = std::make_shared<rfh::fileio::ReadResult>();
			auto tab = std::make_shared<rfh::MqmdFields>(fields);
			const rfh::Result res = rfh::fileio::read(name, readOptions, *tab, *read);
			addStep(r, "Read", res);
			if (res.ok())
			{
				r.fileRead = read;
				r.fileFields = tab;
			}
			return r;
		}, [this, flow, path](const JobResult & r)
		{
			QStringList lines;
			if (r.fileRead)
			{
				// The ReadResult belongs to this flow alone: its message moves into the document without a copy.
				rfh::fileio::ReadResult & read = *r.fileRead;
				rfh::format::RenderOptions render = doc.render();
				render.apply(rfh::format::defaultsFor(read));
				doc.acceptMessage(std::move(read.message), read.dataSource, false, *r.fileFields, render);
				doc.markSaved(path);
				if (settings != nullptr)
				{
					settings->setLastDirectory(QStringLiteral("open"), QFileInfo(path).absolutePath());
				}
			}
			// Windows prefixes the header warnings to the status text (rfh_message_file.h)
			const rfh::Result & res = r.result;
			for (const std::string & warning : res.warnings)
			{
				lines << trimmedLine(warning);
			}
			lines << QString::fromStdString(res.text);
			addStatus(lines);
			finishFlow(flow);
		});
	}

	void Operations::saveFile(const QString & path)
	{
		const QString flow = QStringLiteral("saveFile");
		if (!admit(flow, doc.enables().saveFile))
		{
			return;
		}
		trace(QStringLiteral("Entering Operations::saveFile %1").arg(path));
		// The MQMD written is the tab's (buildMQMD, DataArea.cpp:1807), not the one MQ returned
		// One snapshot of the message (the document keeps its own, and may change while the worker writes). The job and
		// every std::function copy of it share it: a large body is copied once, not once per hand-over.
		auto message = std::make_shared<rfh::Message>();
		message->mqmd = MQMD2 { MQMD2_DEFAULT };
		rfh::mqmd_fields::toMqmd(doc.mqmd(), message->mqmd);
		message->headers = doc.message().headers;
		message->body = doc.message().body;
		const rfh::fileio::WriteOptions writeOptions = options.write;
		const std::string name = path.toStdString();
		const Env env = capture(false);
		submitFlow(flow, env, false, [name, message = std::shared_ptr<const rfh::Message>(std::move(message)), writeOptions](rfh::MqSession &)
		{
			JobResult r;
			addStep(r, "Write", rfh::fileio::write(name, *message, writeOptions));
			return r;
		}, [this, flow, path](const JobResult & r)
		{
			if (r.result.ok())
			{
				doc.markSaved(path);			// WriteFile sets fileName; the data is saved
				if (settings != nullptr)
				{
					settings->setLastDirectory(QStringLiteral("save"), QFileInfo(path).absolutePath());
				}
			}
			QStringList lines;
			for (const std::string & warning : r.result.warnings)
			{
				lines << trimmedLine(warning);
			}
			lines << QString::fromStdString(r.result.text);
			addStatus(lines);
			finishFlow(flow);
		});
	}

	void Operations::clearData()
	{
		const QString flow = QStringLiteral("clearData");
		if (!admit(flow, doc.enables().clearData))
		{
			return;
		}
		doc.clearData();
		finishFlow(flow);
	}

	void Operations::clearAll()
	{
		const QString flow = QStringLiteral("clearAll");
		if (!admit(flow, doc.enables().clearAll))
		{
			return;
		}
		doc.clearAll();
		finishFlow(flow);
	}

	// ---- Purge Q -----------------------------------------------------------------------------------------------------

	void Operations::purgeQ()
	{
		const QString flow = QStringLiteral("purgeQ");
		if (!admit(flow, doc.enables().purgeQ))
		{
			return;
		}
		const QString queue = doc.queueName();
		trace(QStringLiteral("Entering Operations::purgeQ QM=%1 Q=%2").arg(doc.queueManager(), queue));
		if (queue.isEmpty())
		{
			addStatus(QStringList() << QStringLiteral("*Queue Name required"));		// OnPurgeQ 880: no stars after, no blank
			finishFlow(flow);
			return;
		}
		if (pendingUnit)
		{
			// the syncpoint purge commits the connection's whole unit of work, which would publish the unfinished group
			addStatus(QStringList() << QStringLiteral("Purge was not started: a group or segment put is waiting for its commit. Write its last message, or use Close Q to back it out"));
			finishFlow(flow);
			return;
		}
		if (prompter == nullptr)
		{
			addStatus(QStringList() << QStringLiteral("Purge was not started: there is no way to ask for the confirmation"));
			finishFlow(flow);
			return;
		}
		// getQueueDepth: connect on demand and read the depth, then ask
		const Env env = capture(true);
		submitFlow(flow, env, false, [env](rfh::MqSession & s)
		{
			JobResult r;
			if (checkConnection(s, env.qm, env.connectedQm, env.pendingUnit, env.connect, r))
			{
				r.hasQueueInfo = true;
				record(s, r, "Open", s.queueInfo(env.queue, r.queue));
			}
			return r;
		}, [this, flow, queue](const JobResult & r)
		{
			logResolvedName(r);
			if (!r.result.ok())
			{
				addStatus(describe(r));
				clearAfterGet(false);
				finishFlow(flow);
				return;
			}
			prompting = true;
			const bool confirmed = prompter->confirm(AppDocument::purgeConfirmation(queue, r.queue.depth, options.get.selector));
			prompting = false;
			if (!confirmed)
			{
				clearAfterGet(false);
				finishFlow(flow);
				return;
			}
			startPurge(queue);
		});
	}

	void Operations::startPurge(const QString & queue)
	{
		const QString flow = QStringLiteral("purgeQ");
		rfh::PurgeOptions purge;
		purge.selector = options.get.selector;
		purge.syncpoint = true;						// MQGMO_SYNCPOINT_IF_PERSISTENT, always (DA 10117)
		running = Running();
		running.name = QStringLiteral("purge");
		running.qm = doc.queueManager();
		running.checked = false;
		running.cancellable = true;
		userCancelled = false;
		running.done = [this, flow, queue](const JobResult & r)
		{
			QStringList lines;
			if (r.result.ok())
			{
				if (r.count > 0)
				{
					lines << QStringLiteral("%1 messages purged from %2").arg(r.count).arg(queue);			// DA 10199
				}
				else
				{
					lines << QStringLiteral("Queue empty - no messages purged from %1").arg(queue);		// DA 10208
				}
			}
			else if (r.result.rc == MQRC_BACKED_OUT)
			{
				lines << QStringLiteral("Commit failed (rc=2003) - the MQ log may be too small");		// DA 10212
				// What the core knows follows (Windows has only the line above): the count, what remains on the queue
				// and what is unknown from earlier batches.
				const QString detail = QString::fromStdString(r.result.text);
				if (!detail.isEmpty())
				{
					lines << detail;
				}
				if ((r.uncertain > 0) && !detail.contains(QStringLiteral("unknown outcome")))
				{
					lines << QStringLiteral("%1 persistent messages got under syncpoint have an unknown outcome: check the queue depth").arg(r.uncertain);
				}
			}
			else if (r.result.rc != MQRC_NONE)
			{
				JobStep step;
				step.operation = "Get";
				step.result = r.result;
				lines += describeStep(step, false);
				// DA 10218 prints cc where it says rc and the reverse; the owner approved the right order
				lines << QStringLiteral("%1 messages purged from %2 before error rc=%3 cc=%4").arg(r.count).arg(queue).arg(r.result.rc).arg(r.result.cc);
				// the core's own text carries what became of the unit of work, verbatim
				lines << QString::fromStdString(r.result.text);
			}
			else
			{
				lines << QString::fromStdString(r.result.text);		// a failure of the core's own, such as a cancel, with the count
			}
			rememberQueue(queue);
			emit titleChanged(queue);
			clearAfterGet(false);
			addStatus(lines);
			finishFlow(flow);
		};
		if (!controller.purge(queue.toStdString(), purge))
		{
			running = Running();
			addStatus(QStringList() << QStringLiteral("Another operation is still running"));
			finishFlow(flow);
		}
	}

	// ---- Save Q -------------------------------------------------------------------------------------------------

	void Operations::saveQ(QWidget * parent)
	{
		const QString flow = QStringLiteral("saveQ");
		if (!admit(flow, doc.enables().saveQ))
			return;

		const QString queue = doc.queueName();
		trace(QStringLiteral("Entering Operations::saveQ QM=%1 Q=%2").arg(doc.queueManager(), queue));
		if (queue.isEmpty())
		{
			addStatus(QStringList() << QStringLiteral("*Queue Name required*"));
			finishFlow(flow);
			return;
		}

		SaveQDialog dlg(queue, parent);
		if (dlg.exec() != QDialog::Accepted)
		{
			finishFlow(flow);
			return;
		}

		rfh::SaveQueueOptions opts = dlg.values();
		opts.qmName = doc.queueManager().toStdString();

		const Env env = capture(true);
		submitFlow(flow, env, false, [env, opts](rfh::MqSession & s) mutable
		{
			JobResult r;
			if (checkConnection(s, env.qm, env.connectedQm, env.pendingUnit, env.connect, r))
			{
				rfh::Result res = rfh::saveQueue(s, opts, r.count);
				addStep(r, "SaveQ", res);
			}
			return r;
		}, [this, flow, queue](const JobResult & r)
		{
			QStringList lines;
			for (const JobStep & step : r.steps)
				lines += describeStep(step, false);
			if (r.steps.empty() || r.steps.back().result.ok())
				lines << QStringLiteral("%1 messages saved from %2").arg(r.count).arg(queue);
			rememberQueue(queue);
			addStatus(lines);
			finishFlow(flow);
		});
	}

	// ---- Load Q -------------------------------------------------------------------------------------------------

	void Operations::loadQ(QWidget * parent)
	{
		const QString flow = QStringLiteral("loadQ");
		if (!admit(flow, doc.enables().loadQ))
			return;

		const QString queue = doc.queueName();
		trace(QStringLiteral("Entering Operations::loadQ QM=%1 Q=%2").arg(doc.queueManager(), queue));
		if (queue.isEmpty())
		{
			addStatus(QStringList() << QStringLiteral("*Queue Name required*"));
			finishFlow(flow);
			return;
		}

		LoadQDialog dlg(queue, parent);
		if (dlg.exec() != QDialog::Accepted)
		{
			finishFlow(flow);
			return;
		}

		rfh::LoadQueueOptions opts = dlg.values();
		opts.qmName = doc.queueManager().toStdString();

		const Env env = capture(true);
		submitFlow(flow, env, false, [env, opts](rfh::MqSession & s) mutable
		{
			JobResult r;
			if (checkConnection(s, env.qm, env.connectedQm, env.pendingUnit, env.connect, r))
			{
				rfh::Result res = rfh::loadQueue(s, opts, r.count);
				addStep(r, "LoadQ", res);
			}
			return r;
		}, [this, flow, queue](const JobResult & r)
		{
			QStringList lines;
			for (const JobStep & step : r.steps)
				lines += describeStep(step, false);
			if (r.steps.empty() || r.steps.back().result.ok())
				lines << QStringLiteral("%1 messages loaded to %2").arg(r.count).arg(queue);
			rememberQueue(queue);
			emit titleChanged(queue);
			addStatus(lines);
			finishFlow(flow);
		});
	}
}
