// rfh_mq_loader.cpp - see rfh_mq_loader.h

#include "rfh_mq_loader.h"

#include <stdlib.h>

#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rfh
{
	namespace
	{
		struct Candidate
		{
			std::string path;
			bool supportsBindings;
		};

		std::vector<Candidate> candidates(const std::string & installationPath)
		{
#ifdef _WIN32
			// Windows resolves the DLLs through PATH, as DataArea::loadMQdll does.
			(void)installationPath;
			return { { "mqm.dll", true }, { "mqic.dll", false } };
#else
			std::string home = installationPath;
			if (home.empty())
			{
				const char * env = getenv("MQ_INSTALLATION_PATH");
				home = (env != NULL && env[0] != 0) ? env : "/opt/mqm";
			}

			return { { home + "/lib64/libmqm_r.so", true }, { home + "/lib64/libmqic_r.so", false } };
#endif
		}

#ifdef _WIN32
		typedef HMODULE LibHandle;
		LibHandle openLibrary(const std::string & path, std::string & error)
		{
			HMODULE h = LoadLibraryA(path.c_str());
			if (h == NULL)
			{
				error = path + ": LoadLibrary error " + std::to_string((unsigned long)GetLastError());
			}
			return h;
		}
		void * symbol(LibHandle h, const char * name) { return (void *)GetProcAddress(h, name); }
#else
		typedef void * LibHandle;
		LibHandle openLibrary(const std::string & path, std::string & error)
		{
			void * h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
			if (h == NULL)
			{
				const char * e = dlerror();
				error = (e != NULL) ? e : (path + ": dlopen failed");
			}
			return h;
		}
		void * symbol(LibHandle h, const char * name) { return dlsym(h, name); }
#endif

		template <typename T>
		bool resolve(LibHandle h, const char * name, T & out, std::string & missing)
		{
			out = reinterpret_cast<T>(symbol(h, name));
			if (out == nullptr)
			{
				missing = name;
				return false;
			}
			return true;
		}

		std::mutex loadMutex;
		bool loaded = false;
		MqApi loadedApi;
	}

	Result MqLoader::load(MqApi & api, const std::string & installationPath)
	{
		std::lock_guard<std::mutex> lock(loadMutex);

		if (loaded)
		{
			api = loadedApi;
			return Result::success();
		}

		std::string errors;
		for (const Candidate & c : candidates(installationPath))
		{
			std::string error;
			LibHandle h = openLibrary(c.path, error);
			if (h == NULL)
			{
				errors += (errors.empty() ? "" : "; ") + error;
				continue;
			}

			MqApi found;
			std::string missing;
			if (!(resolve(h, "MQCONNX", found.connx, missing) &&
			      resolve(h, "MQDISC", found.disc, missing) &&
			      resolve(h, "MQOPEN", found.open, missing) &&
			      resolve(h, "MQCLOSE", found.close, missing) &&
			      resolve(h, "MQPUT", found.put, missing) &&
			      resolve(h, "MQGET", found.get, missing) &&
			      resolve(h, "MQINQ", found.inq, missing)))
			{
				// keep the handle open: the library may already be initialised
				errors += (errors.empty() ? "" : "; ") + c.path + ": missing entry point " + missing;
				continue;
			}

			found.supportsBindings = c.supportsBindings;
			found.libraryPath = c.path;

			loadedApi = found;
			loaded = true;
			api = found;
			return Result::success();
		}

		return Result::failure("IBM MQ library not found: " + errors);
	}
}
