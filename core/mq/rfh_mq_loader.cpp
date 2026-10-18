// rfh_mq_loader.cpp - see rfh_mq_loader.h

#include "rfh_mq_loader.h"
#include "rfh_mq_loader_detail.h"

#include <stdlib.h>

#include <mutex>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace rfh
{
	namespace detail
	{
		std::vector<LibraryCandidate> libraryCandidates(const std::string & installationPath)
		{
			std::string home = installationPath;
			if (home.empty())
			{
				const char * env = getenv("MQ_INSTALLATION_PATH");
				if (env != NULL && env[0] != 0)
				{
					home = env;
				}
			}

#ifdef _WIN32
			// With a known installation, use its DLLs; otherwise (or after those) let
			// Windows resolve the bare names through PATH, as DataArea::loadMQdll does.
			// Whether a local queue manager exists is not detected: bindingsAvailable
			// follows the library.
			std::vector<LibraryCandidate> list;
			if (!home.empty())
			{
				const std::string bin = home + ((sizeof(void *) == 4) ? "\\bin\\" : "\\bin64\\");
				list.push_back({ bin + "mqm.dll", true, true });
				list.push_back({ bin + "mqic.dll", false, false });
			}
			list.push_back({ "mqm.dll", true, true });
			list.push_back({ "mqic.dll", false, false });
			return list;
#else
			if (home.empty())
			{
				home = "/opt/mqm";
			}

			// libmqm_r is in the runtime package that client-only installations share;
			// a queue manager is present only when the server package put strmqm there.
			// existence, not X_OK: strmqm is executable only by the mqm group, yet any
			// authorised user can connect in bindings mode
			const bool server = (access((home + "/bin/strmqm").c_str(), F_OK) == 0);
			return { { home + "/lib64/libmqm_r.so", true, server }, { home + "/lib64/libmqic_r.so", false, false } };
#endif
		}

		namespace
		{
#ifdef _WIN32
			typedef HMODULE LibHandle;
			LibHandle openLibrary(const std::string & path, std::string & error)
			{
				// A path with a directory is loaded with its own directory searched for
				// its dependencies (mqe.dll, ...); a bare name uses the normal search.
				const bool absolute = (path.find_first_of("\\/") != std::string::npos);
				HMODULE h = absolute ? LoadLibraryExA(path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH)
				                     : LoadLibraryA(path.c_str());
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
		}

		Result loadFrom(const std::vector<LibraryCandidate> & candidates, MqApi & api)
		{
			std::string errors;
			for (const LibraryCandidate & c : candidates)
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
				      resolve(h, "MQINQ", found.inq, missing) &&
				      resolve(h, "MQCMIT", found.cmit, missing)))
				{
					// keep the handle open: the library may already be initialised.
					// Do not try another library: two MQ libraries must not share a process.
					errors += (errors.empty() ? "" : "; ") + c.path + ": missing entry point " + missing;
					return Result::failure("IBM MQ library unusable: " + errors);
				}

				found.serverLibrary = c.serverLibrary;
				found.bindingsAvailable = c.serverLibrary && c.bindingsAvailable;
				found.libraryPath = c.path;
				api = found;

				// Keep what failed before this library loaded, so a fallback to
				// another installation is visible.
				Result loaded = Result::success();
				if (!errors.empty())
				{
					loaded.warnings.push_back("IBM MQ library fallback: " + errors);
				}
				return loaded;
			}

			return Result::failure("IBM MQ library not found: " + errors);
		}
	}

	namespace
	{
		struct LoaderState
		{
			std::mutex mutex;
			bool loaded = false;
			MqApi api;
		};

		LoaderState & loaderState()
		{
			static LoaderState s;
			return s;
		}
	}

	Result MqLoader::load(MqApi & api, const std::string & installationPath)
	{
		LoaderState & state = loaderState();
		std::lock_guard<std::mutex> lock(state.mutex);

		Result r = Result::success();
		if (!state.loaded)
		{
			r = detail::loadFrom(detail::libraryCandidates(installationPath), state.api);
			if (!r.ok())
			{
				return r;
			}
			state.loaded = true;
		}

		api = state.api;
		return r;
	}
}
