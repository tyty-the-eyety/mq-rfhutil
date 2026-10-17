// rfh_platform.cpp - see rfh_platform.h

#ifdef _WIN32
#ifndef SECURITY_WIN32
#define SECURITY_WIN32		// secext.h declares GetUserNameExA only with this defined
#endif
#endif

#include "rfh_platform.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <secext.h>			// GetUserNameExA; the projects link Secur32.lib
#else
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#endif

char * rfh_strupr(char * str)
{
	if (str != NULL)
	{
		for (char * p = str; *p != 0; p++)
		{
			*p = (char)toupper((unsigned char)*p);
		}
	}

	return str;
}

size_t rfh_ucs2len(const RFH_UCS2 * str)
{
	size_t len = 0;

	if (str != NULL)
	{
		while (str[len] != 0)
		{
			len++;
		}
	}

	return len;
}

#ifdef _WIN32

std::string rfh_current_user()
{
	// The Windows program (MQMDPAGE::setMessageMQMD) asks for DOMAIN\user first
	char name[256];
	DWORD size = (DWORD)sizeof(name);
	name[0] = 0;

	if (GetUserNameExA(NameSamCompatible, name, &size) && (name[0] != 0))
	{
		name[sizeof(name) - 1] = 0;
		return std::string(name);
	}

	// then the plain name (advapi32, a default library)
	size = (DWORD)sizeof(name);
	name[0] = 0;

	if (GetUserNameA(name, &size) && (name[0] != 0))
	{
		name[sizeof(name) - 1] = 0;
		return std::string(name);
	}

	return std::string();
}

uint64_t rfh_hrtime_now()
{
	LARGE_INTEGER count;

	if (!QueryPerformanceCounter(&count))
	{
		return 0;
	}

	return (uint64_t)count.QuadPart;
}

uint64_t rfh_hrtime_frequency()
{
	LARGE_INTEGER freq;

	if (!QueryPerformanceFrequency(&freq) || (freq.QuadPart <= 0))
	{
		return 1;
	}

	return (uint64_t)freq.QuadPart;
}

RfhUtcTime rfh_utc_now()
{
	SYSTEMTIME stime;
	GetSystemTime(&stime);

	RfhUtcTime t = { stime.wYear, stime.wMonth, stime.wDay, stime.wHour,
	                 stime.wMinute, stime.wSecond, stime.wMilliseconds };
	return t;
}

const char * rfh_dump_path(const char * baseName)
{
	thread_local char buffer[512];
	snprintf(buffer, sizeof(buffer), "c:\\%s", baseName);
	return buffer;
}

#else

std::string rfh_current_user()
{
	long suggested = sysconf(_SC_GETPW_R_SIZE_MAX);
	size_t size = (suggested > 0) ? (size_t)suggested : 1024;
	const size_t limit = 1024 * 1024;

	for (;;)
	{
		std::vector<char> buffer(size);
		struct passwd pwd;
		struct passwd * found = NULL;
		int rc = getpwuid_r(geteuid(), &pwd, buffer.data(), buffer.size(), &found);

		if ((rc == ERANGE) && (size < limit))
		{
			size *= 2;
			continue;
		}

		if ((rc == 0) && (found != NULL) && (found->pw_name != NULL) && (found->pw_name[0] != 0))
		{
			return std::string(found->pw_name);
		}

		break;
	}

	// the passwd lookup failed: use what the login environment says
	const char * fromEnv = getenv("USER");

	if ((fromEnv == NULL) || (fromEnv[0] == 0))
	{
		fromEnv = getenv("LOGNAME");
	}

	return ((fromEnv != NULL) && (fromEnv[0] != 0)) ? std::string(fromEnv) : std::string();
}

uint64_t rfh_hrtime_now()
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
	{
		return 0;
	}

	// 100 ns units, the same rate as a typical Windows QueryPerformanceCounter, so
	// DiffTime's (end - start) * 1000000 overflows only after about 10 days, not 2.5 hours
	return (uint64_t)ts.tv_sec * 10000000ULL + (uint64_t)ts.tv_nsec / 100ULL;
}

uint64_t rfh_hrtime_frequency()
{
	return 10000000ULL;
}

RfhUtcTime rfh_utc_now()
{
	struct timespec ts = {0, 0};
	struct tm utc = {};

	clock_gettime(CLOCK_REALTIME, &ts);
	gmtime_r(&ts.tv_sec, &utc);

	RfhUtcTime t = { utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
	                 utc.tm_min, utc.tm_sec, (int)(ts.tv_nsec / 1000000) };
	return t;
}

const char * rfh_dump_path(const char * baseName)
{
	thread_local char buffer[512];
	snprintf(buffer, sizeof(buffer), "/tmp/%s", baseName);
	return buffer;
}

#endif

#ifndef _WIN32
int _snprintf_s(char * buffer, size_t sizeOfBuffer, size_t count, const char * format, ...)
{
	if ((buffer == NULL) || (sizeOfBuffer == 0))
	{
		return -1;
	}
	if (count != _TRUNCATE)
	{
		buffer[0] = 0;
		return -1;
	}

	va_list args;
	va_start(args, format);
	int needed = vsnprintf(buffer, sizeOfBuffer, format, args);
	va_end(args);

	if (needed < 0)
	{
		buffer[0] = 0;
		return -1;
	}
	if ((size_t)needed >= sizeOfBuffer)
	{
		return -1;
	}
	return needed;
}
#endif
