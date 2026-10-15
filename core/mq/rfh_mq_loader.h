// rfh_mq_loader.h - loads the IBM MQ library at run time.
//
// One binary serves both connection modes. The server library (libmqm_r /
// mqm.dll) makes local and client connections, chosen per MQCONNX with
// MQCNO_LOCAL_BINDING or MQCNO_CLIENT_BINDING. A client-only installation has
// just the client library (libmqic_r / mqic.dll), which makes client connections.
// Only one library is loaded per process, so the two never share a process.
#pragma once

#include <cmqc.h>

#include <string>

#include "rfh_result.h"

namespace rfh
{
	struct MqApi
	{
		decltype(&::MQCONNX) connx = nullptr;
		decltype(&::MQDISC) disc = nullptr;
		decltype(&::MQOPEN) open = nullptr;
		decltype(&::MQCLOSE) close = nullptr;
		decltype(&::MQPUT) put = nullptr;
		decltype(&::MQGET) get = nullptr;
		decltype(&::MQINQ) inq = nullptr;

		bool supportsBindings = false;	// false when only the client library is available
		std::string libraryPath;		// the library the entry points came from
	};

	class MqLoader
	{
	public:
		// Loads the MQ library once per process and returns its entry points.
		// installationPath overrides the MQ installation directory; when empty, the
		// MQ_INSTALLATION_PATH environment variable is used, then /opt/mqm (Linux).
		// A later call returns the library loaded first and ignores installationPath.
		static Result load(MqApi & api, const std::string & installationPath = std::string());
	};
}
