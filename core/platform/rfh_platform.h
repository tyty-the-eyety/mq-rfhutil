// rfh_platform.h - portability layer shared by the Windows (MFC) and Linux builds.
//
// The shared RFHUtil sources were written against Win32 types. On Windows this
// header only adds the few portable helpers below; on other platforms it also
// supplies the Win32 type names that the shared sources use.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef _WIN32
#include <windows.h>
typedef wchar_t RFH_UCS2;		// wchar_t is a 16-bit UTF-16 code unit on Windows
#else
typedef int BOOL;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
typedef const char * LPCTSTR;
typedef char16_t RFH_UCS2;		// wchar_t is 32 bits on Linux; MQ UCS-2 data is 16 bits
#endif

static_assert(sizeof(RFH_UCS2) == 2, "RFH_UCS2 must be a 16-bit code unit");

// Upper-cases a NUL-terminated string in place and returns it (replaces MSVC _strupr).
char * rfh_strupr(char * str);

// Number of 16-bit code units before the NUL terminator (replaces wcslen on UCS-2 data).
size_t rfh_ucs2len(const RFH_UCS2 * str);

// High-resolution monotonic counter and its frequency in ticks per second.
// Windows: QueryPerformanceCounter/QueryPerformanceFrequency. Linux: CLOCK_MONOTONIC in ns.
uint64_t rfh_hrtime_now();
uint64_t rfh_hrtime_frequency();

// Current UTC time broken into fields (replaces GetSystemTime).
struct RfhUtcTime
{
	int year;
	int month;
	int day;
	int hour;
	int minute;
	int second;
	int millisecond;
};
RfhUtcTime rfh_utc_now();

// Path of a debugging dump file with the given base name.
// Windows: "c:\<name>" (historic location). Linux: "/tmp/<name>".
// The result stays valid until the next call on the same thread.
const char * rfh_dump_path(const char * baseName);
