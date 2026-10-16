// rfh_platform.cpp - see rfh_platform.h

#include "rfh_platform.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

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
