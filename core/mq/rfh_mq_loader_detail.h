// rfh_mq_loader_detail.h - the uncached parts of the loader, exposed for tests.
#pragma once

#include <string>
#include <vector>

#include "rfh_mq_loader.h"

namespace rfh
{
	namespace detail
	{
		struct LibraryCandidate
		{
			std::string path;
			bool serverLibrary;
			bool bindingsAvailable = false;	// see MqApi::bindingsAvailable
		};

		// The libraries MqLoader::load tries, in order.
		std::vector<LibraryCandidate> libraryCandidates(const std::string & installationPath);

		// Loads the first usable candidate; uncached. A library that opens but lacks an
		// entry point ends the search, so two MQ libraries never share a process.
		Result loadFrom(const std::vector<LibraryCandidate> & candidates, MqApi & api);
	}
}
