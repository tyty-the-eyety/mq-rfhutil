// rfh_mq_loader.h - loads the IBM MQ library at run time.
//
// One binary serves both connection modes. The server library (libmqm_r /
// mqm.dll) can make local and client connections, chosen per MQCONNX with
// MQCNO_LOCAL_BINDING or MQCNO_CLIENT_BINDING. Having it does not mean a queue
// manager is installed: on Linux libmqm_r belongs to the runtime package, which
// client-only installations also have. The client library (libmqic_r / mqic.dll)
// makes client connections only. Only one library is loaded per process.
//
// Search order: the installation directory is the load argument, else the
// MQ_INSTALLATION_PATH environment variable. On Linux the default is /opt/mqm and
// the libraries are <home>/lib64/libmqm_r.so then libmqic_r.so. On Windows,
// when a directory is given, <home>\bin64\mqm.dll and mqic.dll (<home>\bin for a
// 32-bit process) are tried first, then the bare names through PATH.
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

		bool serverLibrary = false;		// the library can make local connections (mqm); false for the client library
		// A local queue manager can be reached: serverLibrary and, on Linux,
		// <home>/bin/strmqm exists. On Windows this equals serverLibrary; the presence
		// of a local queue manager is not detected there.
		bool bindingsAvailable = false;
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
