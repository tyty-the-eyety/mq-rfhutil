# RFHUtil Linux Port, Plan 1: Portable Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a portable `rfhcore` C++17 library on Fedora that holds RFHUtil's
shared parsers plus a new MQ session layer, tested on Linux. The Windows MFC
application must keep building from the same shared sources.

**Architecture:**
- **Platform layer:** a small `core/platform/` layer replaces the Win32 calls
  in the six shared sources (`comsubs`, `xmlsubs`, `JsonParse`, `XMLParse`,
  `Names`, `mqerror`). Windows keeps its original calls; Linux matches them
  with iconv and POSIX.
- **Linux compat headers:** `core/compat/` provides the `stdafx.h` stand-in
  and a prelude that blocks `rfhutil.h`. This is the technique upstream
  already uses in `tests/UnitTests/prelude.h`.
- **MQ layer:** `core/mq/` loads `libmqm_r` at run time and provides
  `MqSession`, which connects in bindings or client mode and supports put,
  get, browse, depth, and purge.
- **Build:** CMake builds the core and its tests. `RFHUtil.sln` stays the
  Windows build.

**Tech Stack:** C++17, CMake ≥ 3.21, GoogleTest 1.17, IBM MQ 9.4 C MQI
(`/opt/mqm`), glibc iconv, Perl 5 (one-off edit scripts).

**Spec:** `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md`. This is
the first of four plans:
1. Portable core (this plan)
2. Message file format, MQMD fields, and display formatting
3. Qt6 application (Main, Data, and MQMD tabs)
4. RPM packaging and Linux CI

**Verified before writing:** every code block, script, and expected output in
this plan was compiled and run on Fedora 44 (GCC 16.2, IBM MQ 9.4.0.5,
queue manager `TEST_QM`). Results:
- Tasks 1–5 pass 302 tests: the 251 existing upstream unit tests, 38 new unit
  tests, 12 bindings-mode MQ tests, and 1 skipped client test.
- The two Perl scripts apply cleanly to upstream commit `09744ae`.
- **Not verified:** the Windows build (MSVC cannot run here) and client mode.
  Tasks 6 and 7 cover those and need the user's approval.

---

## Background the engineer needs

- **Shared sources:** `RFHUtil/comsubs.cpp` and its siblings are compiled
  into two Windows projects, `RFHUtil.vcxproj` (the MFC app) and
  `tests/UnitTests/UnitTests.vcxproj`. Every change to them must still
  compile with MSVC/MFC. Use `#ifdef _WIN32` only inside `core/platform/`,
  never in the shared sources.
- **Case-sensitive file names:** the shared sources say
  `#include "stdafx.h"`, but the file is `StdAfx.h`. On Linux the real file
  is therefore never found, and `core/compat/stdafx.h` is used instead.
- **`rfhutil.h`:** this header pulls in MFC and `DataArea.h`. Linux builds
  force-include `core/compat/core_prelude.h`, which defines that header's
  include guard so its body is skipped.
- **`wchar_t` is 32 bits on Linux.** RFHUtil treats `wchar_t` as a 16-bit
  UCS-2 code unit, so the shared sources now use `RFH_UCS2`, which is
  `wchar_t` on Windows and `char16_t` elsewhere.
- **Unchanged Windows behaviour:**
  - The CRT `wcstombs`/`mbstowcs` run in the MSVC "C" locale, because
    RFHUtil never calls `setlocale`. That is a Latin-1 identity mapping.
  - Code-page conversion substitutes `.` for characters the target code page
    cannot represent.
  - Linux reproduces both behaviours; the tests pin them.
- **Found during planning (report upstream, do not fix here):**
  `matchEbcdicToAscii` has `case 037:`, which C reads as octal 31. So CCSID 37
  has never matched an ANSI code page. A test pins the current behaviour so
  the port does not change it silently.
- **MQ library:** the server library (`libmqm_r.so`) makes both bindings and
  client connections, selected with `MQCNO_LOCAL_BINDING` /
  `MQCNO_CLIENT_BINDING`. Only one MQ library is ever loaded per process.
- **Commit rules (upstream PR template):**
  - Sign every commit for the DCO with `git commit -s`.
  - Add tests for code changes.
  - Add a `CHANGELOG.md` entry (Task 8).
  - End each message with:
    `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`

## File structure

| Path | Status | Responsibility |
|---|---|---|
| `CMakeLists.txt`, `CMakePresets.json` | new | Top-level CMake build; `linux-debug` / `linux-release` presets |
| `core/CMakeLists.txt` | new | `rfhcore` static library |
| `core/platform/rfh_platform.{h,cpp}` | new | Win32 type names on Linux, `RFH_UCS2`, `rfh_strupr`, `rfh_ucs2len`, timers, UTC time, dump-file path |
| `core/platform/rfh_codepage.{h,cpp}` | new | ACP, installed code pages, one-character conversion, C-locale UCS-2 conversion |
| `core/platform/rfh_trace.{h,cpp}` | new | `RfhTrace` sink interface, `rfhGetTrace` / `rfhSetTrace` |
| `core/compat/stdafx.h`, `core/compat/core_prelude.h` | new | Linux stand-ins for MFC's `stdafx.h` and `rfhutil.h` |
| `core/mq/rfh_result.{h,cpp}` | new | `rfh::Result` with MQ reason-code text |
| `core/mq/rfh_mq_loader.{h,cpp}` | new | Run-time loading of the MQ library |
| `core/mq/rfh_mq_session.{h,cpp}` | new | Connect, put, get, browse, depth, purge, close |
| `core/message/rfh_message.h` | new | `rfh::Message` (MQMD2 + body) |
| `RFHUtil/comsubs.{h,cpp}`, `xmlsubs.{h,cpp}`, `JsonParse.{h,cpp}`, `XMLParse.{h,cpp}`, `Names.cpp` | modified | Use the platform layer instead of Win32/MFC |
| `RFHUtil/uisubs.{h,cpp}` | new | `processBackspace` (MFC-only), moved out of `comsubs.cpp` |
| 10 MFC page files | modified | `#include "uisubs.h"` |
| `RFHUtil/rfhutil.{h,cpp}` | modified | `CRfhutilApp` implements `RfhTrace` and installs itself |
| `RFHUtil/RFHUtil.vcxproj`, `tests/UnitTests/UnitTests.vcxproj` | modified | Compile the platform layer and the new tests |
| `tests/UnitTests/stdafx.h`, `prelude.h`, `ReconnectTests.cpp` | modified | Build on Linux as well as Windows |
| `tests/CMakeLists.txt` | new | Test executables and ctest registration |
| `tests/core/*.cpp` | new | Platform, shared-source, Result, loader, and MQ session tests |
| `tools/port/*.pl` | new | One-off scripted edits, kept so the port can be re-applied after upstream changes |
| `.gitignore`, `CHANGELOG.md`, `README.md`, spec | modified | Build output, release notes, Linux build docs |

---

### Task 0: Prepare the branch and toolchain

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Rename the branch so upstream CI runs on it**

`.github/workflows/build-and-test.yml` runs only on `master` and `features/**`.

```bash
cd ~/projects/mq-rfhutil
git branch -m feat/linux-qt-port features/linux-qt-port
git branch --show-current
```

Expected: `features/linux-qt-port`

- [ ] **Step 2: Install the build dependencies (ask the user first; this uses sudo)**

```bash
rpm -q cmake gcc-c++ gtest-devel || sudo dnf install -y cmake gcc-c++ gtest-devel
```

Expected: `gtest-devel-1.17.0-2.fc44` among the installed packages. If the user
declines, configure every build in this plan with `-DRFHUTIL_FETCH_GTEST=ON`,
which downloads GoogleTest 1.17.0 by pinned SHA-256.

- [ ] **Step 3: Confirm the MQ SDK and the test queue manager**

```bash
ls /opt/mqm/inc/cmqc.h /opt/mqm/lib64/libmqm_r.so
/opt/mqm/bin/dspmq -m TEST_QM
id -nG | tr ' ' '\n' | grep -x mqm
```

Expected: both files are listed, `STATUS(Running)`, and `mqm`.

- [ ] **Step 4: Ignore CMake build output**

Append to `.gitignore`:

```gitignore

# CMake build output
/build/
```

- [ ] **Step 5: Commit**

```bash
git add .gitignore
git commit -s -m "build: ignore CMake build directory

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 1: Platform layer and CMake skeleton

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Create: `core/platform/rfh_platform.h`, `core/platform/rfh_platform.cpp`
- Create: `core/platform/rfh_codepage.h`, `core/platform/rfh_codepage.cpp`
- Create: `core/platform/rfh_trace.h`, `core/platform/rfh_trace.cpp`
- Test: `tests/core/PlatformTests.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/core/PlatformTests.cpp`:

```cpp
// PlatformTests.cpp — tests for the portable platform layer (core/platform).
// They run on every platform: Windows exercises the Win32 code paths and Linux
// the iconv/POSIX ones, against the same expectations.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "rfh_platform.h"
#include "rfh_codepage.h"
#include "rfh_trace.h"

// glibc always ships the IBM code pages; a Windows host may not have the EBCDIC
// ones installed, so conversion tests skip there instead of failing.
#define REQUIRE_CODEPAGE(ccsid) \
    do { if (!rfh_is_codepage_installed(ccsid)) GTEST_SKIP() << "code page " << (ccsid) << " is not installed"; } while (0)

// ---------------------------------------------------------------------------
// rfh_strupr / rfh_ucs2len
// ---------------------------------------------------------------------------

TEST(Platform_Strupr, UppercasesInPlace)
{
    char text[] = "Queue.Name_1";
    EXPECT_STREQ("QUEUE.NAME_1", rfh_strupr(text));
    EXPECT_STREQ("QUEUE.NAME_1", text);
}

TEST(Platform_Strupr, NullIsSafe)
{
    EXPECT_EQ(nullptr, rfh_strupr(nullptr));
}

TEST(Platform_Ucs2len, CountsSixteenBitUnits)
{
    const RFH_UCS2 text[] = { 'M', 'Q', 0x20AC, 0 };
    EXPECT_EQ(3u, rfh_ucs2len(text));
}

TEST(Platform_Ucs2len, NullIsZero)
{
    EXPECT_EQ(0u, rfh_ucs2len(nullptr));
}

// ---------------------------------------------------------------------------
// Time and paths
// ---------------------------------------------------------------------------

TEST(Platform_Time, CounterIsMonotonic)
{
    uint64_t start = rfh_hrtime_now();
    uint64_t end = rfh_hrtime_now();
    EXPECT_GE(end, start);
    EXPECT_GT(rfh_hrtime_frequency(), 0u);
}

TEST(Platform_Time, UtcFieldsAreInRange)
{
    RfhUtcTime t = rfh_utc_now();
    EXPECT_GE(t.year, 2024);
    EXPECT_GE(t.month, 1);
    EXPECT_LE(t.month, 12);
    EXPECT_GE(t.day, 1);
    EXPECT_LE(t.day, 31);
    EXPECT_LE(t.hour, 23);
    EXPECT_LE(t.minute, 59);
    EXPECT_LE(t.second, 60);
    EXPECT_LE(t.millisecond, 999);
}

TEST(Platform_DumpPath, UsesPlatformDirectory)
{
#ifdef _WIN32
    EXPECT_STREQ("c:\\rfhdump.txt", rfh_dump_path("rfhdump.txt"));
#else
    EXPECT_STREQ("/tmp/rfhdump.txt", rfh_dump_path("rfhdump.txt"));
#endif
}

// ---------------------------------------------------------------------------
// Code pages
// ---------------------------------------------------------------------------

TEST(Platform_Codepage, CommonCodePagesAreInstalled)
{
#ifdef _WIN32
    GTEST_SKIP() << "installed code pages depend on the Windows host";
#endif
    EXPECT_TRUE(rfh_is_codepage_installed(37));
    EXPECT_TRUE(rfh_is_codepage_installed(500));
    EXPECT_TRUE(rfh_is_codepage_installed(1252));
}

TEST(Platform_Codepage, UnknownCodePageIsNotInstalled)
{
    EXPECT_FALSE(rfh_is_codepage_installed(99999));
    EXPECT_FALSE(rfh_is_codepage_installed(0));
}

TEST(Platform_Codepage, ConvertsEbcdicLetterToAnsi)
{
    REQUIRE_CODEPAGE(500);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0xC1 };   // 'A' in EBCDIC 500
    ASSERT_EQ(1, rfh_convert_char(500, 1252, in, out, 2));
    EXPECT_EQ('A', out[0]);
}

TEST(Platform_Codepage, ConvertsEbcdicEuroIn1140)
{
    REQUIRE_CODEPAGE(1140);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0x9F };   // euro sign in EBCDIC 1140
    ASSERT_EQ(1, rfh_convert_char(1140, 1252, in, out, 2));
    EXPECT_EQ((char)0x80, out[0]);       // euro sign in Windows-1252
}

TEST(Platform_Codepage, UnrepresentableCharacterBecomesPeriod)
{
    REQUIRE_CODEPAGE(1140);
    REQUIRE_CODEPAGE(437);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0x9F };   // euro sign in EBCDIC 1140; code page 437 has none
    ASSERT_EQ(1, rfh_convert_char(1140, 437, in, out, 2));
    EXPECT_EQ('.', out[0]);
}

TEST(Platform_Codepage, MissingCodePageConvertsNothing)
{
    char out[2] = { 0, 0 };
    const char in[1] = { 'A' };
    EXPECT_EQ(0, rfh_convert_char(99999, 1252, in, out, 2));
    EXPECT_EQ(0, rfh_convert_char(0, 1252, in, out, 2));
}

TEST(Platform_Codepage, Ucs2ToCLocaleIsLatin1Identity)
{
    const RFH_UCS2 ucs[] = { 'M', 'Q', 0xE9, 0 };   // "MQé"
    char mbs[8] = { 'x', 'x', 'x', 'x', 'x', 'x', 'x', 'x' };
    EXPECT_EQ(3, rfh_ucs2_to_c_locale(ucs, mbs, sizeof(mbs)));
    EXPECT_EQ('M', mbs[0]);
    EXPECT_EQ((char)0xE9, mbs[2]);
    EXPECT_EQ(0, mbs[3]);
}

TEST(Platform_Codepage, Ucs2ToCLocaleRejectsAbove255)
{
    const RFH_UCS2 ucs[] = { 'M', 0x20AC, 0 };
    char mbs[8] = { 0 };
    EXPECT_EQ(-1, rfh_ucs2_to_c_locale(ucs, mbs, sizeof(mbs)));
}

TEST(Platform_Codepage, CLocaleToUcs2IsLatin1Identity)
{
    const char mbs[] = { 'M', 'Q', (char)0xE9, 0 };
    RFH_UCS2 ucs[8];
    EXPECT_EQ(3, rfh_c_locale_to_ucs2(mbs, ucs, 8));
    EXPECT_EQ((RFH_UCS2)'M', ucs[0]);
    EXPECT_EQ((RFH_UCS2)0xE9, ucs[2]);
    EXPECT_EQ((RFH_UCS2)0, ucs[3]);
}

// ---------------------------------------------------------------------------
// Trace sink
// ---------------------------------------------------------------------------

namespace
{
    class RecordingTrace : public RfhTrace
    {
    public:
        std::vector<std::string> lines;
        BOOL isTraceEnabled() override { return TRUE; }
        void logTraceEntry(const char * traceInfo) override { lines.push_back(traceInfo); }
        void dumpTraceData(const char *, const unsigned char *, unsigned int) override {}
    };
}

TEST(Platform_Trace, DefaultSinkIsDisabledNoOp)
{
    RfhTrace * trace = rfhGetTrace();
    ASSERT_NE(nullptr, trace);
    EXPECT_FALSE(trace->isTraceEnabled());
    trace->logTraceEntry("ignored");
}

TEST(Platform_Trace, InstalledSinkReceivesEntries)
{
    RecordingTrace recorder;
    rfhSetTrace(&recorder);
    rfhGetTrace()->logTraceEntry("hello");
    rfhSetTrace(nullptr);

    ASSERT_EQ(1u, recorder.lines.size());
    EXPECT_EQ("hello", recorder.lines[0]);
}

TEST(Platform_Trace, NullRestoresNoOpSink)
{
    RecordingTrace recorder;
    rfhSetTrace(&recorder);
    rfhSetTrace(nullptr);
    EXPECT_FALSE(rfhGetTrace()->isTraceEnabled());
}
```

- [ ] **Step 2: Create the build files**

Create `CMakeLists.txt`:

```cmake
# Cross-platform build for the portable RFHUtil core and its tests.
# The Windows MFC application keeps building with RFHUtil.sln.
cmake_minimum_required(VERSION 3.21)

project(rfhutil LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RFHUTIL_BUILD_TESTS "Build the core tests" ON)
option(RFHUTIL_FETCH_GTEST "Download GoogleTest when no installed package is found" OFF)

if(WIN32)
    set(RFHUTIL_DEFAULT_MQ_HOME "$ENV{MQ_HOME}")
else()
    set(RFHUTIL_DEFAULT_MQ_HOME "/opt/mqm")
endif()
set(MQ_HOME "${RFHUTIL_DEFAULT_MQ_HOME}" CACHE PATH "IBM MQ installation directory (headers only; the library is loaded at run time)")

add_subdirectory(core)

if(RFHUTIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

Create `CMakePresets.json`:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "linux-debug",
      "displayName": "Linux debug",
      "generator": "Unix Makefiles",
      "binaryDir": "${sourceDir}/build/linux-debug",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
    },
    {
      "name": "linux-release",
      "displayName": "Linux release",
      "generator": "Unix Makefiles",
      "binaryDir": "${sourceDir}/build/linux-release",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    }
  ],
  "buildPresets": [
    { "name": "linux-debug", "configurePreset": "linux-debug" },
    { "name": "linux-release", "configurePreset": "linux-release" }
  ],
  "testPresets": [
    { "name": "linux-debug", "configurePreset": "linux-debug", "output": { "outputOnFailure": true } },
    { "name": "linux-release", "configurePreset": "linux-release", "output": { "outputOnFailure": true } }
  ]
}
```

Create `core/CMakeLists.txt` (Tasks 2 and 4 replace it as the core grows):

```cmake
# librfhcore: the portable RFHUtil core (no MFC, no Qt).

add_library(rfhcore STATIC
    platform/rfh_platform.cpp
    platform/rfh_codepage.cpp
    platform/rfh_trace.cpp
)

target_include_directories(rfhcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
)

if(NOT WIN32)
    set_source_files_properties(
        platform/rfh_platform.cpp platform/rfh_codepage.cpp platform/rfh_trace.cpp
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif()
```

Create `tests/CMakeLists.txt` (Tasks 2 and 4 replace it):

```cmake
# Core tests: the existing unit tests (shared with the Windows UnitTests project),
# the platform tests, and the MQ integration tests.

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    find_package(GTest QUIET)
endif()

if(NOT GTest_FOUND)
    if(RFHUTIL_FETCH_GTEST)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
            URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    else()
        message(FATAL_ERROR "GoogleTest not found. Install it (Fedora: sudo dnf install gtest-devel) "
                            "or configure with -DRFHUTIL_FETCH_GTEST=ON to download v1.17.0.")
    endif()
endif()

include(GoogleTest)

add_executable(rfhutil_unit_tests
    core/PlatformTests.cpp
)
target_link_libraries(rfhutil_unit_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_unit_tests)
```

- [ ] **Step 3: Run the build to verify it fails**

```bash
cmake --preset linux-debug && cmake --build --preset linux-debug
```

Expected: FAIL. CMake reports `Cannot find source file: platform/rfh_platform.cpp`.

- [ ] **Step 4: Write the platform layer**

Create `core/platform/rfh_platform.h`:

```cpp
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
```

Create `core/platform/rfh_platform.cpp`:

```cpp
// rfh_platform.cpp - see rfh_platform.h

#include "rfh_platform.h"

#include <ctype.h>
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

	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t rfh_hrtime_frequency()
{
	return 1000000000ULL;
}

RfhUtcTime rfh_utc_now()
{
	struct timespec ts;
	struct tm utc;

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
```

Create `core/platform/rfh_codepage.h`:

```cpp
// rfh_codepage.h - code page services used by the shared RFHUtil sources.
//
// Windows keeps the original Win32/CRT calls, so behaviour there is unchanged.
// Linux reproduces the Windows results with iconv.
#pragma once

#include "rfh_platform.h"

// The ANSI code page used for display translation.
// Windows: GetACP(). Linux: the RFHUTIL_ACP environment variable if it holds a
// positive number, otherwise 1252 (Western European, the common Windows default).
int rfh_get_acp();

// TRUE if conversions to and from the code page are available on this system.
BOOL rfh_is_codepage_installed(int ccsid);

// Translates the single byte input[0] from fromCcsid to toCcsid.
// Returns the number of bytes written to output (at most outputMax), or 0 when a
// code page is not positive or not available. A character that the target code
// page cannot represent becomes '.', which is the Windows default-character result.
int rfh_convert_char(int fromCcsid, int toCcsid, const char * input, char * output, int outputMax);

// NUL-terminated UCS-2 (host byte order) to single-byte text, with the semantics of
// the MSVC C-locale wcstombs that RFHUtil has always used: code units 0-255 map to
// the byte of the same value. Returns the bytes written excluding the terminator,
// or -1 if a code unit is above 255. Writes a terminator when there is room.
int rfh_ucs2_to_c_locale(const RFH_UCS2 * ucs, char * mbs, int mbsBytes);

// NUL-terminated single-byte text to UCS-2 with the semantics of the MSVC C-locale
// mbstowcs: each byte maps to the code unit of the same value. Converts at most
// ucsChars code units and returns the count excluding the terminator. Writes a
// terminator when there is room.
int rfh_c_locale_to_ucs2(const char * mbs, RFH_UCS2 * ucs, int ucsChars);
```

Create `core/platform/rfh_codepage.cpp`:

```cpp
// rfh_codepage.cpp - see rfh_codepage.h

#include "rfh_codepage.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

int rfh_get_acp()
{
	return (int)GetACP();
}

namespace
{
	BOOL ccsidInstalled;		// result of checking if code page is installed
	int ccsidInsCheck;			// code page to search for

	BOOL CALLBACK EnumCodePagesProc(LPSTR lpCodePageString)
	{
		// check if this is the code page we are looking for
		if (atoi(lpCodePageString) == ccsidInsCheck)
		{
			// remember we found the one we wanted
			ccsidInstalled = TRUE;
		}

		// continue if not found yet
		return !ccsidInstalled;
	}
}

BOOL rfh_is_codepage_installed(int ccsid)
{
	ccsidInstalled = FALSE;
	ccsidInsCheck = ccsid;

	// calls back to EnumCodePagesProc for each installed code page
	EnumSystemCodePagesA(&EnumCodePagesProc, CP_INSTALLED);

	return ccsidInstalled;
}

int rfh_convert_char(int fromCcsid, int toCcsid, const char * input, char * output, int outputMax)
{
	int tcount = 0;
	char defChar = '.';
	wchar_t ucsChar = 0;

	if ((fromCcsid > 0) && (toCcsid > 0) && (outputMax > 0))
	{
		// translate to UCS-2 first
		tcount = MultiByteToWideChar(fromCcsid, 0, input, 1, &ucsChar, 1);

		if (tcount > 0)
		{
			// then to the target code page
			tcount = WideCharToMultiByte(toCcsid, 0, &ucsChar, 1, output, outputMax, &defChar, NULL);
		}
	}

	return tcount;
}

int rfh_ucs2_to_c_locale(const RFH_UCS2 * ucs, char * mbs, int mbsBytes)
{
	return (int)wcstombs(mbs, ucs, mbsBytes);
}

int rfh_c_locale_to_ucs2(const char * mbs, RFH_UCS2 * ucs, int ucsChars)
{
	return (int)mbstowcs(ucs, mbs, ucsChars);
}

#else

#include <errno.h>
#include <iconv.h>
#include <stdio.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace
{
	// iconv names to try for an IBM CCSID, most specific first.
	std::vector<std::string> iconvNames(int ccsid)
	{
		static const std::map<int, const char *> specific = {
			{ 367, "ASCII" },
			{ 813, "ISO-8859-7" },
			{ 819, "ISO-8859-1" },
			{ 912, "ISO-8859-2" },
			{ 915, "ISO-8859-5" },
			{ 916, "ISO-8859-8" },
			{ 920, "ISO-8859-9" },
			{ 923, "ISO-8859-15" },
			{ 1089, "ISO-8859-6" },
			{ 1208, "UTF-8" },
		};

		std::vector<std::string> names;
		auto it = specific.find(ccsid);
		if (it != specific.end())
		{
			names.push_back(it->second);
		}

		char buf[32];
		snprintf(buf, sizeof(buf), "IBM%03d", ccsid);	// IBM037, IBM500, IBM1047, IBM1140
		names.push_back(buf);
		snprintf(buf, sizeof(buf), "CP%d", ccsid);		// CP437, CP850, CP1252
		names.push_back(buf);
		return names;
	}

	// Cached converters, one per thread because an iconv_t is not thread-safe.
	// Failures are cached as (iconv_t)-1 so a missing code page is probed once.
	iconv_t converterFor(int fromCcsid, int toCcsid)
	{
		thread_local std::map<std::pair<int, int>, iconv_t> cache;

		auto key = std::make_pair(fromCcsid, toCcsid);
		auto it = cache.find(key);
		if (it != cache.end())
		{
			return it->second;
		}

		iconv_t cd = (iconv_t)-1;
		for (const std::string & to : iconvNames(toCcsid))
		{
			for (const std::string & from : iconvNames(fromCcsid))
			{
				cd = iconv_open(to.c_str(), from.c_str());
				if (cd != (iconv_t)-1)
				{
					break;
				}
			}

			if (cd != (iconv_t)-1)
			{
				break;
			}
		}

		cache[key] = cd;
		return cd;
	}
}

int rfh_get_acp()
{
	const char * env = getenv("RFHUTIL_ACP");
	if (env != NULL)
	{
		int acp = atoi(env);
		if (acp > 0)
		{
			return acp;
		}
	}

	return 1252;
}

BOOL rfh_is_codepage_installed(int ccsid)
{
	if (ccsid <= 0)
	{
		return FALSE;
	}

	// a code page is usable when it converts to and from UTF-8
	return ((converterFor(ccsid, 1208) != (iconv_t)-1) && (converterFor(1208, ccsid) != (iconv_t)-1)) ? TRUE : FALSE;
}

int rfh_convert_char(int fromCcsid, int toCcsid, const char * input, char * output, int outputMax)
{
	if ((fromCcsid <= 0) || (toCcsid <= 0) || (outputMax <= 0))
	{
		return 0;
	}

	iconv_t cd = converterFor(fromCcsid, toCcsid);
	if (cd == (iconv_t)-1)
	{
		return 0;
	}

	// reset any shift state left by a previous call
	iconv(cd, NULL, NULL, NULL, NULL);

	char in[1] = { input[0] };
	char * inPtr = in;
	size_t inLeft = 1;
	char * outPtr = output;
	size_t outLeft = (size_t)outputMax;

	if (iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft) == (size_t)-1)
	{
		if (errno == EILSEQ)
		{
			// not representable: Windows substitutes the default character
			output[0] = '.';
			return 1;
		}

		return 0;
	}

	return (int)((size_t)outputMax - outLeft);
}

int rfh_ucs2_to_c_locale(const RFH_UCS2 * ucs, char * mbs, int mbsBytes)
{
	int count = 0;

	while (ucs[count] != 0)
	{
		if (ucs[count] > 0xFF)
		{
			return -1;
		}

		if (count >= mbsBytes)
		{
			return count;
		}

		mbs[count] = (char)ucs[count];
		count++;
	}

	if (count < mbsBytes)
	{
		mbs[count] = 0;
	}

	return count;
}

int rfh_c_locale_to_ucs2(const char * mbs, RFH_UCS2 * ucs, int ucsChars)
{
	int count = 0;

	while ((mbs[count] != 0) && (count < ucsChars))
	{
		ucs[count] = (RFH_UCS2)(unsigned char)mbs[count];
		count++;
	}

	if (count < ucsChars)
	{
		ucs[count] = 0;
	}

	return count;
}

#endif
```

Create `core/platform/rfh_trace.h`:

```cpp
// rfh_trace.h - trace sink used by the shared RFHUtil sources.
//
// The shared sources used to reach the trace through (CRfhutilApp *)AfxGetApp().
// They now call rfhGetTrace(). The MFC application object implements RfhTrace and
// installs itself with rfhSetTrace(this); other hosts install their own sink.
// rfhGetTrace() never returns NULL: without a host sink it returns a no-op sink.
#pragma once

#include "rfh_platform.h"

class RfhTrace
{
public:
	virtual ~RfhTrace() {}

	virtual BOOL isTraceEnabled() = 0;
	virtual void logTraceEntry(const char * traceInfo) = 0;
	virtual void dumpTraceData(const char * label, const unsigned char * data, unsigned int length) = 0;

	// Verbose trace also dumps data areas. Read directly by the shared sources.
	BOOL verboseTrace = FALSE;
};

RfhTrace * rfhGetTrace();

// Installs the process-wide trace sink. NULL restores the built-in no-op sink.
// Not synchronised: install the sink before starting threads that trace.
void rfhSetTrace(RfhTrace * trace);
```

Create `core/platform/rfh_trace.cpp`:

```cpp
// rfh_trace.cpp - see rfh_trace.h

#include "rfh_trace.h"

namespace
{
	class NullTrace : public RfhTrace
	{
	public:
		BOOL isTraceEnabled() override { return FALSE; }
		void logTraceEntry(const char *) override {}
		void dumpTraceData(const char *, const unsigned char *, unsigned int) override {}
	};

	NullTrace nullTrace;
	RfhTrace * currentTrace = &nullTrace;
}

RfhTrace * rfhGetTrace()
{
	return currentTrace;
}

void rfhSetTrace(RfhTrace * trace)
{
	currentTrace = (trace != NULL) ? trace : &nullTrace;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
```

Expected: `100% tests passed, 0 tests failed out of 19`

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt CMakePresets.json core tests/CMakeLists.txt tests/core/PlatformTests.cpp
git commit -s -m "feat(core): add portable platform layer and CMake build

Adds core/platform (Win32 type names, UCS-2 helpers, timers, code page
services via iconv on Linux and Win32 on Windows, and a trace sink) with
tests, built by a new CMake project.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Port the shared sources and run the existing tests on Linux

**Files:**
- Create: `tools/port/port-shared-sources.pl`
- Create: `core/compat/stdafx.h`, `core/compat/core_prelude.h`
- Create (by the script): `RFHUtil/uisubs.h`, `RFHUtil/uisubs.cpp`
- Modify (by the script): `RFHUtil/comsubs.h`, `comsubs.cpp`, `xmlsubs.h`, `xmlsubs.cpp`, `JsonParse.h`, `JsonParse.cpp`, `XMLParse.h`, `XMLParse.cpp`, `Names.cpp`, and the 10 pages that call `processBackspace` (`CICS.cpp`, `General.cpp`, `MQMDPAGE.cpp`, `pscr.cpp`, `Ims.cpp`, `Dlq.cpp`, `PubSub.cpp`, `jms.cpp`, `RFH.cpp`, `PS.cpp`)
- Modify: `tests/UnitTests/stdafx.h`, `tests/UnitTests/prelude.h`, `tests/UnitTests/ReconnectTests.cpp`
- Replace: `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/SharedSourcesTests.cpp`, plus the existing `tests/UnitTests/*Tests.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/core/SharedSourcesTests.cpp`:

```cpp
// SharedSourcesTests.cpp — the shared RFHUtil sources (comsubs, ...) on top of the
// platform layer: the Win32 calls they used to make now go through core/platform,
// and these tests pin that the results are the ones Windows produced.

#include <gtest/gtest.h>

#include <string.h>

#include <string>
#include <vector>

#include "rfh_platform.h"
#include "rfh_codepage.h"
#include "rfh_trace.h"
#include "comsubs.h"

#define REQUIRE_CODEPAGE(ccsid) \
    do { if (!rfh_is_codepage_installed(ccsid)) GTEST_SKIP() << "code page " << (ccsid) << " is not installed"; } while (0)

TEST(SharedSources_Time, GetTimeIsMonotonic)
{
    MY_TIME_T start = GetTime();
    MY_TIME_T end = GetTime();
    EXPECT_GE(end, start);
    EXPECT_GE(DiffTime(start, end), 0.0);
}

TEST(SharedSources_Time, DiffTimeScalesToMicroseconds)
{
    MY_TIME_T oneSecond = (MY_TIME_T)rfh_hrtime_frequency();
    EXPECT_DOUBLE_EQ(1000000.0, DiffTime(0, oneSecond));
}

TEST(SharedSources_Ucs2, RoundLength2PadsOddLengthLittleEndian)
{
    // roundLength2 used wcslen on 16-bit data; on Linux wchar_t is 32 bits.
    RFH_UCS2 buffer[8] = { 'a', 'b', 'c', 0, 0, 0, 0, 0 };
    int len = roundLength2(reinterpret_cast<unsigned char *>(buffer), 0 /* NUMERIC_PC */);
    EXPECT_EQ(4, len);
    EXPECT_EQ((RFH_UCS2)' ', buffer[3]);
}

TEST(SharedSources_Ucs2, MultiByteToUcs2RoundTrips)
{
    unsigned char text[] = "RFH2 folder";
    RFH_UCS2 ucs[32];
    unsigned char back[32] = { 0 };

    int chars = MultiByteToUCS2(reinterpret_cast<unsigned char *>(ucs), sizeof(ucs), text, (int)strlen((char *)text));
    ASSERT_EQ((int)strlen((char *)text), chars);
    EXPECT_EQ(chars, UCS2ToMultiByte(reinterpret_cast<unsigned char *>(ucs), chars * 2, back, sizeof(back)));
    EXPECT_STREQ("RFH2 folder", reinterpret_cast<char *>(back));
}

TEST(SharedSources_Codepage, IsCodePageInstalledUsesCodePageServices)
{
    EXPECT_EQ(rfh_is_codepage_installed(500), isCodePageInstalled(500));
    EXPECT_FALSE(isCodePageInstalled(99999));
}

TEST(SharedSources_Codepage, EbcdicCharToAsciiCharTranslatesOneCharacter)
{
    REQUIRE_CODEPAGE(500);
    char out[2] = { 0, 0 };
    const char in[1] = { (char)0x81 };   // 'a' in EBCDIC 500
    EXPECT_EQ(1, EbcdicCharToAsciiChar(500, 1252, in, out));
    EXPECT_EQ('a', out[0]);
}

TEST(SharedSources_Codepage, TranslateEbcdicDataUsesCodePage500)
{
    REQUIRE_CODEPAGE(500);
    const unsigned char in[] = { 0xC8, 0x85, 0x93, 0x93, 0x96 };   // "Hello" in EBCDIC 500
    unsigned char out[sizeof(in) + 1] = { 0 };
    translateEbcdicData(out, in, sizeof(in), 500);
    EXPECT_STREQ("Hello", reinterpret_cast<char *>(out));
}

TEST(SharedSources_Codepage, MatchEbcdicToAsciiKeepsHistoricCcsid37Behaviour)
{
    // Upstream writes "case 037:", which C reads as octal 31, so CCSID 37 has
    // never matched an ANSI code page. Kept for compatibility; reported upstream.
    EXPECT_EQ(0, matchEbcdicToAscii(37));
    EXPECT_EQ(1252, matchEbcdicToAscii(500));
}

namespace
{
    class RecordingTrace : public RfhTrace
    {
    public:
        std::vector<std::string> lines;
        BOOL isTraceEnabled() override { return TRUE; }
        void logTraceEntry(const char * traceInfo) override { lines.push_back(traceInfo); }
        void dumpTraceData(const char *, const unsigned char *, unsigned int) override {}
    };

    // Restores the no-op sink even when an assertion fails.
    struct TraceGuard
    {
        explicit TraceGuard(RfhTrace * trace) { rfhSetTrace(trace); }
        ~TraceGuard() { rfhSetTrace(nullptr); }
    };
}

TEST(SharedSources_Trace, TraceGoesToInstalledSink)
{
    RecordingTrace recorder;
    TraceGuard guard(&recorder);

    void * ptr = rfhMalloc(16, "TESTTAG ");
    ASSERT_NE(nullptr, ptr);
    rfhFree(ptr);

    ASSERT_FALSE(recorder.lines.empty());
    EXPECT_NE(std::string::npos, recorder.lines.front().find("Entering rfhMalloc()"));
}

TEST(SharedSources_Trace, TraceFormatsSixtyFourBitCounters)
{
    // %I64d is MSVC-only; the shared sources now use PRId64.
    RecordingTrace recorder;
    TraceGuard guard(&recorder);

    void * ptr = rfhMalloc(16, "TESTTAG ");
    rfhFree(ptr);

    ASSERT_FALSE(recorder.lines.empty());
    EXPECT_EQ(std::string::npos, recorder.lines.front().find("I64d"));
    EXPECT_NE(std::string::npos, recorder.lines.front().find("mallocReqs="));
}
```

Replace `tests/CMakeLists.txt` so it builds the existing upstream tests too:

```cmake
# Core tests: the existing unit tests (shared with the Windows UnitTests project),
# the platform tests, and the MQ integration tests.

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    find_package(GTest QUIET)
endif()

if(NOT GTest_FOUND)
    if(RFHUTIL_FETCH_GTEST)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
            URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    else()
        message(FATAL_ERROR "GoogleTest not found. Install it (Fedora: sudo dnf install gtest-devel) "
                            "or configure with -DRFHUTIL_FETCH_GTEST=ON to download v1.17.0.")
    endif()
endif()

include(GoogleTest)

add_executable(rfhutil_unit_tests
    UnitTests/ComsubsTests.cpp
    UnitTests/EncodingTests.cpp
    UnitTests/FileFormatTests.cpp
    UnitTests/HeaderUtilTests.cpp
    UnitTests/JsonParseTests.cpp
    UnitTests/MQErrorTests.cpp
    UnitTests/NamesTests.cpp
    UnitTests/ReconnectTests.cpp
    UnitTests/XMLParseTests.cpp
    UnitTests/XmlsubsTests.cpp
    core/PlatformTests.cpp
    core/SharedSourcesTests.cpp
)
target_include_directories(rfhutil_unit_tests PRIVATE UnitTests)
target_link_libraries(rfhutil_unit_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_unit_tests)
```

- [ ] **Step 2: Run the build to verify it fails**

```bash
cmake --preset linux-debug && cmake --build --preset linux-debug
```

Expected: FAIL to compile. The upstream test files reach the MFC-only
`tests/UnitTests/stdafx.h` (`targetver.h` / `afxwin.h: No such file or directory`),
and `SharedSourcesTests.cpp` cannot find `comsubs.h`.

- [ ] **Step 3: Make the test stub headers cross-platform**

Replace `tests/UnitTests/stdafx.h`:

```cpp
// stdafx.h — minimal stub for the unit test project
// Overrides RFHUtil/stdafx.h so source files compile without the full
// application headers (no MQ, no DataArea, no resource.h).
// Windows uses MFC; other platforms use the portable core compat header.
#pragma once

#ifdef _WIN32
#define VC_EXTRALEAN
#include "targetver.h"
#include <afxwin.h>
#else
#include "../../core/compat/stdafx.h"
#endif
```

Replace `tests/UnitTests/prelude.h`. The Windows stub `CRfhutilApp` now derives
from `RfhTrace` and no longer declares `verboseTrace`, which it inherits:

```cpp
// prelude.h — force-included before every translation unit in the UnitTests project.
// Purpose: prevent RFHUtil/rfhutil.h from pulling in DataArea.h → cmqc.h (MQ headers).
//
// Problem: source files (comsubs.cpp, JsonParse.cpp, etc.) live in RFHUtil/ and do
//   #include "rfhutil.h"
// MSVC resolves quoted includes relative to the source file's directory first, so it
// always finds RFHUtil/rfhutil.h — not our stub in tests/UnitTests/.
//
// Solution: pre-define rfhutil.h's own include guard so that when the real file is
// found, its entire body (including #include "DataArea.h") is skipped.
// On Windows, also provide the minimal CRfhutilApp stub that TestMain.cpp needs.
#pragma once

// Pull in MFC on Windows, the portable compat layer elsewhere.
#include "stdafx.h"

// Names.h is normally pulled in via rfhutil.h → DataArea.h. Since we block that
// chain, include it directly. JsonParse.h and XMLParse.h both embed a Names member.
#include "Names.h"

#include "rfh_trace.h"

// Block RFHUtil/rfhutil.h from being processed — the guard is the one defined in
// RFHUtil/rfhutil.h line 22.
#define AFX_RFHUTIL_H__96F2439F_AE02_4650_8A3E_EE7F0EC7E72C__INCLUDED_

#ifdef _WIN32
// Minimal CRfhutilApp stub so TestMain.cpp can derive its MFC application object.
// The shared sources reach the trace through rfhGetTrace(), which falls back to a
// no-op sink, so trace code stays disabled.
class CRfhutilApp : public CWinApp, public RfhTrace
{
public:
    BOOL isTraceEnabled() override                                                 { return FALSE; }
    void logTraceEntry(const char *) override                                      {}
    void logTraceEntry(const CString&)                                             {}
    void dumpTraceData(const char *, const unsigned char *, unsigned int) override {}
};
#endif

// Tab page constants (defined in real rfhutil.h; kept here as a safety net).
#define PAGE_MAIN     0
#define PAGE_DATA     1
#define PAGE_MQMD     2
#define PAGE_CONN    14
```

`ReconnectTests.cpp` uses `pow`, which MFC happened to declare. Include it explicitly:

```bash
perl -0pi -e 's/(#include <gtest\/gtest\.h>\n)/$1#include <math.h>\n/' tests/UnitTests/ReconnectTests.cpp
grep -n "math.h" tests/UnitTests/ReconnectTests.cpp
```

Expected: one line, `#include <math.h>`.

- [ ] **Step 4: Add the Linux compat headers**

Create `core/compat/stdafx.h`:

```cpp
// stdafx.h - non-Windows stand-in for RFHUtil/StdAfx.h.
//
// The shared RFHUtil sources start with #include "stdafx.h", which on Windows
// pulls in MFC. Linux file names are case-sensitive, so the real StdAfx.h is not
// found and this header supplies the portable equivalents instead.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "rfh_platform.h"
```

Create `core/compat/core_prelude.h`:

```cpp
// core_prelude.h - force-included before each shared RFHUtil source on non-Windows
// builds (the same technique tests/UnitTests/prelude.h uses on Windows).
//
// Several shared sources include RFHUtil/rfhutil.h, which pulls in MFC and
// DataArea.h. Defining that header's include guard skips its body; the shared
// sources only needed Names.h from that include chain.
#pragma once

#include "stdafx.h"
#include "Names.h"

#define AFX_RFHUTIL_H__96F2439F_AE02_4650_8A3E_EE7F0EC7E72C__INCLUDED_
```

- [ ] **Step 5: Add and run the shared-source port script**

The script makes each edit only if its anchor text matches, and stops with the
file and edit name otherwise. It changes:

| File | Change |
|---|---|
| `comsubs.h` | include `rfh_platform.h`; `MY_TIME_T` becomes `unsigned long long`; `wchar_t` → `RFH_UCS2`; drop the `processBackspace` declaration |
| `comsubs.cpp` | `GetTime`/`DiffTime` use `rfh_hrtime_*`; `EbcdicCharToAsciiChar`, `translateEbcdicData`, `isCodePageInstalled` use `rfh_codepage`; `wcslen`/`wcstombs`/`mbstowcs`/`GetACP` replaced; `%I64d` → `PRId64`; trace through `rfhGetTrace()`; 32-bit pointer trace casts via `uintptr_t`; dump file via `rfh_dump_path` |
| `processBackspace` | moved verbatim to new `uisubs.cpp`/`uisubs.h`; the 10 MFC pages that call it include `uisubs.h` |
| `xmlsubs.{h,cpp}` | include `rfh_platform.h`; `replaceChars` takes `const char *` (callers pass `CString`, which converts implicitly); `_strupr` → `rfh_strupr`; `wchar_t` → `RFH_UCS2` |
| `JsonParse`, `XMLParse` | `CString lastError` → `std::string`; trace through `rfhGetTrace()`; pointer trace casts |
| `Names.cpp` | `rfh_platform.h` instead of `windows.h`; `GetSystemTime` → `rfh_utc_now`; dump file via `rfh_dump_path`; pointer trace cast |

Create `tools/port/port-shared-sources.pl`:

```perl
#!/usr/bin/perl
# port-shared-sources.pl - one-off edits that make the shared RFHUtil sources portable.
# Usage: perl port-shared-sources.pl <repo-root>
# Every edit asserts that it matched, so a drifted source tree fails loudly.
use strict;
use warnings;

my $root = shift or die "usage: $0 <repo-root>\n";
chdir $root or die "cannot chdir $root: $!\n";

sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "$f: $!\n"; local $/; my $t = <$h>; close $h; return $t; }
sub spew  { my ($f, $t) = @_; open(my $h, '>:raw', $f) or die "$f: $!\n"; print $h $t; close $h; }

# Applies a substitution and dies unless it changed the text at least $min times.
sub edit {
    my ($file, $textref, $label, $min, $code) = @_;
    my $count = $code->($textref);
    die "$file: edit '$label' matched $count time(s), expected at least $min\n" if $count < $min;
}

# The sources use CRLF line endings; work on LF and restore CRLF on write.
sub load { my $t = slurp($_[0]); my $crlf = ($t =~ /\r\n/); $t =~ s/\r\n/\n/g; return ($t, $crlf); }
sub save { my ($f, $t, $crlf) = @_; $t =~ s/\n/\r\n/g if $crlf; spew($f, $t); }

# Trace access and 32-bit pointer formatting are shared by several files.
sub portTrace {
    my ($f, $t) = @_;
    edit($f, $t, 'trace pointer type', 1, sub { ${$_[0]} =~ s/CRfhutilApp\s*\*(\s*)app;/RfhTrace *$1app;/g });
    edit($f, $t, 'trace lookup', 1, sub { ${$_[0]} =~ s/\(CRfhutilApp \*\)AfxGetApp\(\)/rfhGetTrace()/g });
}
sub portPointerTrace {
    my ($f, $t, $min) = @_;
    edit($f, $t, 'pointer casts in %8.8X trace', $min, sub {
        my $n = 0;
        ${$_[0]} =~ s{^(.*%8\.8[Xx].*)$}{ my $l = $1; $n += ($l =~ s/\(unsigned int\)(?!\(uintptr_t\))/(unsigned int)(uintptr_t)/g); $l }gme;
        $n;
    });
}
sub includeAfter {
    my ($f, $t, $anchor, $add) = @_;
    edit($f, $t, "include after $anchor", 1, sub { ${$_[0]} =~ s/^(#include "\Q$anchor\E"\n)/$1$add/m });
}
sub includeAtTop {
    my ($f, $t, $add) = @_;
    edit($f, $t, 'include after license', 1, sub { ${$_[0]} =~ s{^(\*/\n)}{$1\n$add}m });
}

# ---------------------------------------------------------------- comsubs.h
{
    my $f = 'RFHUtil/comsubs.h'; my ($t, $crlf) = load($f);
    includeAtTop($f, \$t, "#include \"rfh_platform.h\"\n\n");
    edit($f, \$t, 'MY_TIME_T', 1, sub { $t =~ s/typedef unsigned __int64 MY_TIME_T;/typedef unsigned long long MY_TIME_T;/ });
    edit($f, \$t, 'processBackspace decl', 1, sub { $t =~ s/^void processBackspace\(CWnd \*wnd\);\n//m });
    edit($f, \$t, 'wchar_t', 10, sub { $t =~ s/\bwchar_t\b/RFH_UCS2/g });
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- comsubs.cpp
my $backspace;
{
    my $f = 'RFHUtil/comsubs.cpp'; my ($t, $crlf) = load($f);
    edit($f, \$t, 'dump file', 1, sub { $t =~ s/#define DUMP_FILE_NAME "c:\\\\rfhdump\.txt"/#define DUMP_FILE_NAME rfh_dump_path("rfhdump.txt")/ });
    includeAfter($f, \$t, 'comsubs.h', "#include \"rfh_trace.h\"\n#include \"rfh_codepage.h\"\n");
    edit($f, \$t, 'code page globals', 1, sub { $t =~ s/\n\tBOOL\tccsidInstalled;[^\n]*\n\tint\t\tccsidInsCheck;[^\n]*\n/\n/ });

    edit($f, \$t, 'GetTime', 1, sub { $t =~ s/MY_TIME_T GetTime\(\)\n\n\{\n.*?\n\}\n/MY_TIME_T GetTime()\n\n{\n\treturn (MY_TIME_T)rfh_hrtime_now();\n}\n/s });
    edit($f, \$t, 'DiffTime', 1, sub { $t =~ s/double DiffTime\(MY_TIME_T start, MY_TIME_T end\)\n\n\{\n.*?\n\}\n/double DiffTime(MY_TIME_T start, MY_TIME_T end)\n\n{\n\t\/\/ difference in the high frequency counter, scaled to microseconds\n\tint64_t diff = (int64_t)((end - start) * 1000000);\n\n\treturn (double)diff \/ (double)rfh_hrtime_frequency();\n}\n/s });

    edit($f, \$t, 'EbcdicCharToAsciiChar', 1, sub { $t =~ s/int EbcdicCharToAsciiChar\(int fromCcsid, int toCcsid, const char \* input, char \* output\)\n\n\{\n.*?\n\}\n/int EbcdicCharToAsciiChar(int fromCcsid, int toCcsid, const char * input, char * output)\n\n{\n\t\/\/ translate one character, returning the number of bytes produced\n\treturn rfh_convert_char(fromCcsid, toCcsid, input, output, 2);\n}\n/s });

    edit($f, \$t, 'translateEbcdicData locals', 1, sub { $t =~ s/\twchar_t\t\t\tucsChar\[4\];[^\n]*\n(\tunsigned char\ttempTxt\[8\];[^\n]*\n)\tchar\t\t\tdefChar='\.';[^\n]*\n/$1/ });
    edit($f, \$t, 'translateEbcdicData body', 1, sub { $t =~ s/\t\t\t\/\/ start by trying to do a proper translation, using Windows\n\t\t\ttcount = MultiByteToWideChar\(ebcdicCP, 0, \(char \*\)in \+ i, 1, ucsChar, 1\);\n\n\t\t\t\/\/ did the function succeed\n\t\t\tif \(tcount > 0\)\n\t\t\t\{\n\t\t\t\ttcount = WideCharToMultiByte\(asciiCcsid, 0, ucsChar, 1, \(char \*\)tempTxt, 2, &defChar, NULL\);\n\t\t\t\}\n/\t\t\t\/\/ start by trying to do a proper translation using the code page services\n\t\t\ttcount = rfh_convert_char(ebcdicCP, asciiCcsid, (const char *)in + i, (char *)tempTxt, 2);\n/ });

    edit($f, \$t, 'isCodePageInstalled', 1, sub { $t =~ s/BOOL CALLBACK EnumCodePagesProc\(LPTSTR lpCodePageString\)\n\n\{\n.*?\n\}\n\nBOOL isCodePageInstalled\(const int ccsid\)\n\n\{\n.*?\n\}\n/BOOL isCodePageInstalled(const int ccsid)\n\n{\n\treturn rfh_is_codepage_installed(ccsid);\n}\n/s });

    edit($f, \$t, 'processBackspace move', 1, sub { $t =~ s/\n(void processBackspace\(CWnd \*wnd\)\n\n\{\n.*?\n\}\n)\n/\n/s and do { $backspace = $1; 1 } });

    edit($f, \$t, 'roundLength2', 1, sub { $t =~ s/length = wcslen\(\(wchar_t \*\)ptr\);/length = (int)rfh_ucs2len((const RFH_UCS2 *)ptr);/ });
    edit($f, \$t, 'wcstombs', 1, sub { $t =~ s/charCount = wcstombs\(\(char \*\)mbsptr, \(const wchar_t \*\)ucsptr, mbslen\);/charCount = rfh_ucs2_to_c_locale((const RFH_UCS2 *)ucsptr, (char *)mbsptr, mbslen);/ });
    edit($f, \$t, 'mbstowcs', 1, sub { $t =~ s/charCount = mbstowcs\(\(wchar_t \*\)ucsptr, \(char \*\)mbsptr, ucslen\/2\);/charCount = rfh_c_locale_to_ucs2((char *)mbsptr, (RFH_UCS2 *)ucsptr, ucslen\/2);/ });
    edit($f, \$t, 'GetACP', 1, sub { $t =~ s/\bGetACP\(\)/rfh_get_acp()/g });
    edit($f, \$t, 'wchar_t', 5, sub { $t =~ s/\bwchar_t\b/RFH_UCS2/g });
    edit($f, \$t, '%I64d', 4, sub { $t =~ s/%I64d/%" PRId64 "/g });
    portTrace($f, \$t);
    portPointerTrace($f, \$t, 3);
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- uisubs (MFC only)
{
    my $crlf = 1;
    save('RFHUtil/uisubs.h', "// uisubs.h - MFC-only helpers moved out of comsubs so comsubs stays portable.\n\n#pragma once\n\nvoid processBackspace(CWnd *wnd);\n", $crlf);
    save('RFHUtil/uisubs.cpp', "// uisubs.cpp - MFC-only helpers moved out of comsubs.cpp so comsubs stays portable.\n\n#include \"stdafx.h\"\n#include \"uisubs.h\"\n\n$backspace", $crlf);
    for my $f (qw(CICS.cpp General.cpp MQMDPAGE.cpp pscr.cpp Ims.cpp Dlq.cpp PubSub.cpp jms.cpp RFH.cpp PS.cpp)) {
        my $p = "RFHUtil/$f"; my ($t, $c) = load($p);
        includeAfter($p, \$t, 'comsubs.h', "#include \"uisubs.h\"\n");
        save($p, $t, $c);
    }
}

# ---------------------------------------------------------------- xmlsubs
{
    my $f = 'RFHUtil/xmlsubs.h'; my ($t, $crlf) = load($f);
    includeAtTop($f, \$t, "#include \"rfh_platform.h\"\n\n");
    edit($f, \$t, 'replaceChars decl', 1, sub { $t =~ s/void replaceChars\(const CString &value, char \*valStr\);/void replaceChars(const char * value, char *valStr);/ });
    edit($f, \$t, 'wchar_t', 2, sub { $t =~ s/\bwchar_t\b/RFH_UCS2/g });
    save($f, $t, $crlf);

    $f = 'RFHUtil/xmlsubs.cpp'; ($t, $crlf) = load($f);
    edit($f, \$t, 'replaceChars def', 1, sub { $t =~ s/void replaceChars\(const CString &value, char \*valStr\)\n\n\{\n\tint j = insertEscChars\(valStr, \(LPCTSTR\)value, value\.GetLength\(\)\);\n\}/void replaceChars(const char * value, char *valStr)\n\n{\n\tinsertEscChars(valStr, value, (int)strlen(value));\n}/ });
    edit($f, \$t, '_strupr', 3, sub { $t =~ s/\b_strupr\(/rfh_strupr(/g });
    edit($f, \$t, 'wchar_t', 2, sub { $t =~ s/\bwchar_t\b/RFH_UCS2/g });
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- JsonParse / XMLParse
for my $base (qw(JsonParse XMLParse)) {
    my $f = "RFHUtil/$base.h"; my ($t, $crlf) = load($f);
    includeAtTop($f, \$t, "#include <string>\n#include \"rfh_platform.h\"\n\n");
    edit($f, \$t, 'lastError member', 1, sub { $t =~ s/\tCString lastError;/\tstd::string lastError;/ });
    save($f, $t, $crlf);

    $f = "RFHUtil/$base.cpp"; ($t, $crlf) = load($f);
    includeAfter($f, \$t, 'rfhutil.h', "#include \"rfh_trace.h\"\n");
    edit($f, \$t, 'lastError length', 1, sub { $t =~ s/len = lastError\.GetLength\(\);/len = (int)lastError.size();/ });
    edit($f, \$t, 'lastError copy', 1, sub { $t =~ s/memcpy\(errtxt, \(LPCTSTR\)lastError, len\);/memcpy(errtxt, lastError.c_str(), len);/ });
    portTrace($f, \$t);
    portPointerTrace($f, \$t, 2);
    if ($base eq 'JsonParse') {
        # trace argument on the continuation line of a two-line sprintf
        edit($f, \$t, 'processArray trace cast', 1, sub { $t =~ s/^(\t+)\(unsigned int\)ptr, name, names\.getNameAddr/$1(unsigned int)(uintptr_t)ptr, name, names.getNameAddr/m });
    }
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- Names.cpp
{
    my $f = 'RFHUtil/Names.cpp'; my ($t, $crlf) = load($f);
    edit($f, \$t, 'windows.h', 1, sub { $t =~ s/#include <windows\.h>/#include "rfh_platform.h"/ });
    edit($f, \$t, 'dump file', 1, sub { $t =~ s/#define DUMP_FILE_NAME "c:\\\\namedump\.txt"/#define DUMP_FILE_NAME rfh_dump_path("namedump.txt")/ });
    edit($f, \$t, 'SYSTEMTIME', 1, sub { $t =~ s/\tSYSTEMTIME\tstime;/\tRfhUtcTime\tstime;/ });
    edit($f, \$t, 'GetSystemTime', 1, sub { $t =~ s/GetSystemTime\(&stime\);/stime = rfh_utc_now();/ });
    edit($f, \$t, 'time fields', 7, sub {
        my $n = 0;
        my %map = (wYear => 'year', wMonth => 'month', wDay => 'day', wHour => 'hour', wMinute => 'minute', wSecond => 'second', wMilliseconds => 'millisecond');
        $n += ($t =~ s/stime\.(wYear|wMonth|wDay|wHour|wMinute|wSecond|wMilliseconds)\b/'stime.' . $map{$1}/ge);
        $n;
    });
    portPointerTrace($f, \$t, 1);
    save($f, $t, $crlf);
}

print "shared sources ported\n";
```

Run it on the repository root:

```bash
perl tools/port/port-shared-sources.pl .
```

Expected: `shared sources ported`

Check that no Win32/MFC call is left in the shared sources:

```bash
grep -n -E "AfxGetApp|MultiByteToWideChar|WideCharToMultiByte|QueryPerformance|EnumSystemCodePages|GetSystemTime|GetACP\(|_strupr|wcslen|wcstombs|mbstowcs|CString|CWnd|I64d|\bwchar_t\b" \
  RFHUtil/comsubs.cpp RFHUtil/comsubs.h RFHUtil/xmlsubs.cpp RFHUtil/xmlsubs.h RFHUtil/JsonParse.cpp RFHUtil/JsonParse.h RFHUtil/XMLParse.cpp RFHUtil/XMLParse.h RFHUtil/Names.cpp
```

Expected: no output.

- [ ] **Step 6: Add the shared sources to the core library**

Replace `core/CMakeLists.txt`:

```cmake
# librfhcore: the portable RFHUtil core (no MFC, no Qt).

# Shared sources that also build into the Windows MFC application. They stay in
# RFHUtil/ so the Visual Studio projects keep their paths.
set(RFHUTIL_SHARED_SOURCES
    ${PROJECT_SOURCE_DIR}/RFHUtil/comsubs.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/xmlsubs.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/JsonParse.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/XMLParse.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/Names.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/mqerror.cpp
)

add_library(rfhcore STATIC
    platform/rfh_platform.cpp
    platform/rfh_codepage.cpp
    platform/rfh_trace.cpp
    ${RFHUTIL_SHARED_SOURCES}
)

target_include_directories(rfhcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
        ${PROJECT_SOURCE_DIR}/RFHUtil
)

if(NOT WIN32)
    # The shared sources include "stdafx.h" and "rfhutil.h", which pull in MFC on
    # Windows. Supply the portable stand-ins: compat/ provides stdafx.h and the
    # prelude blocks rfhutil.h (the technique tests/UnitTests/prelude.h uses).
    target_include_directories(rfhcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compat)
    set_source_files_properties(${RFHUTIL_SHARED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/compat/core_prelude.h")

    # New core code is held to a clean warning level; the shared sources keep
    # their historic warnings, which are only in trace formatting.
    set_source_files_properties(
        platform/rfh_platform.cpp platform/rfh_codepage.cpp platform/rfh_trace.cpp
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif()
```

- [ ] **Step 7: Run the tests to verify they pass**

```bash
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
```

Expected: `100% tests passed, 0 tests failed out of 280`. That is the 251
upstream unit tests, now running on Linux, plus 19 platform and 10
shared-source tests. Compiler warnings are only `-Wformat` and
`-Wwrite-strings` in the shared sources' trace code, which already exist on
64-bit Windows.

- [ ] **Step 8: Commit**

```bash
git add tools/port/port-shared-sources.pl core tests RFHUtil
git commit -s -m "refactor(core): make shared RFHUtil sources portable

comsubs, xmlsubs, JsonParse, XMLParse, Names and mqerror now build on
Linux through core/platform. Windows keeps its original Win32 calls behind
the same functions. processBackspace moves to the MFC-only uisubs.cpp.
The existing upstream unit tests now also run on Linux.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Wire the platform layer into the Windows projects

**Files:**
- Create: `tools/port/port-windows-projects.pl`
- Modify (by the script): `RFHUtil/RFHUtil.vcxproj`, `tests/UnitTests/UnitTests.vcxproj`, `RFHUtil/rfhutil.h`, `RFHUtil/rfhutil.cpp`

These changes cannot be compiled on Linux. Task 6 verifies them in Windows CI.

- [ ] **Step 1: Add and run the Windows project script**

It changes:
- **Both `.vcxproj` files:** add `core\platform` to the include path, and
  compile the three `core/platform/*.cpp` files.
- **`RFHUtil.vcxproj`:** also compile `uisubs.cpp`.
- **`UnitTests.vcxproj`:** also compile `tests/core/PlatformTests.cpp` and
  `SharedSourcesTests.cpp`.
- **`CRfhutilApp`:** derives from `RfhTrace` and drops its own
  `verboseTrace` member, which it now inherits.
- **`CRfhutilApp` constructor:** calls `rfhSetTrace(this)`.

Create `tools/port/port-windows-projects.pl`:

```perl
#!/usr/bin/perl
# port-windows-projects.pl - wires the portable core into the Windows build.
# Usage: perl port-windows-projects.pl <repo-root>
# Every edit asserts that it matched, so a drifted source tree fails loudly.
use strict;
use warnings;

my $root = shift or die "usage: $0 <repo-root>\n";
chdir $root or die "cannot chdir $root: $!\n";

sub slurp { my ($f) = @_; open(my $h, '<:raw', $f) or die "$f: $!\n"; local $/; my $t = <$h>; close $h; return $t; }
sub spew  { my ($f, $t) = @_; open(my $h, '>:raw', $f) or die "$f: $!\n"; print $h $t; close $h; }
sub load { my $t = slurp($_[0]); my $crlf = ($t =~ /\r\n/); $t =~ s/\r\n/\n/g; return ($t, $crlf); }
sub save { my ($f, $t, $crlf) = @_; $t =~ s/\n/\r\n/g if $crlf; spew($f, $t); }
sub edit {
    my ($file, $label, $expected, $count) = @_;
    die "$file: edit '$label' matched $count time(s), expected $expected\n" unless $count == $expected;
}

# ---------------------------------------------------------------- RFHUtil.vcxproj
{
    my $f = 'RFHUtil/RFHUtil.vcxproj'; my ($t, $crlf) = load($f);
    edit($f, 'include dirs', 4, scalar($t =~ s/<AdditionalIncludeDirectories>\$\(MQ_HOME\)\\tools\\c\\include;/<AdditionalIncludeDirectories>..\\core\\platform;\$(MQ_HOME)\\tools\\c\\include;/g));
    edit($f, 'sources', 1, scalar($t =~ s/^(\s*)<ClCompile Include="comsubs\.cpp" \/>\n/$1<ClCompile Include="comsubs.cpp" \/>\n$1<ClCompile Include="uisubs.cpp" \/>\n$1<ClCompile Include="..\\core\\platform\\rfh_platform.cpp" \/>\n$1<ClCompile Include="..\\core\\platform\\rfh_codepage.cpp" \/>\n$1<ClCompile Include="..\\core\\platform\\rfh_trace.cpp" \/>\n/m));
    edit($f, 'headers', 1, scalar($t =~ s/^(\s*)<ClInclude Include="comsubs\.h" \/>\n/$1<ClInclude Include="comsubs.h" \/>\n$1<ClInclude Include="uisubs.h" \/>\n$1<ClInclude Include="..\\core\\platform\\rfh_platform.h" \/>\n$1<ClInclude Include="..\\core\\platform\\rfh_codepage.h" \/>\n$1<ClInclude Include="..\\core\\platform\\rfh_trace.h" \/>\n/m));
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- UnitTests.vcxproj
{
    my $f = 'tests/UnitTests/UnitTests.vcxproj'; my ($t, $crlf) = load($f);
    edit($f, 'include dirs', 2, scalar($t =~ s/<AdditionalIncludeDirectories>\$\(ProjectDir\);\.\.\\\.\.\\RFHUtil;/<AdditionalIncludeDirectories>\$(ProjectDir);..\\..\\RFHUtil;..\\..\\core\\platform;/g));
    edit($f, 'core sources', 1, scalar($t =~ s/^(\s*)<ClCompile Include="\.\.\\\.\.\\RFHUtil\\mqerror\.cpp" \/>\n/$1<ClCompile Include="..\\..\\RFHUtil\\mqerror.cpp" \/>\n$1<ClCompile Include="..\\..\\core\\platform\\rfh_platform.cpp" \/>\n$1<ClCompile Include="..\\..\\core\\platform\\rfh_codepage.cpp" \/>\n$1<ClCompile Include="..\\..\\core\\platform\\rfh_trace.cpp" \/>\n/m));
    edit($f, 'platform tests', 1, scalar($t =~ s/^(\s*)<ClCompile Include="MQErrorTests\.cpp" \/>\n/$1<ClCompile Include="MQErrorTests.cpp" \/>\n$1<ClCompile Include="..\\core\\PlatformTests.cpp" \/>\n$1<ClCompile Include="..\\core\\SharedSourcesTests.cpp" \/>\n/m));
    save($f, $t, $crlf);
}

# ---------------------------------------------------------------- rfhutil.h / rfhutil.cpp
{
    my $f = 'RFHUtil/rfhutil.h'; my ($t, $crlf) = load($f);
    edit($f, 'include trace', 1, scalar($t =~ s/^(#include "DataArea\.h"[^\n]*\n)/$1#include "rfh_trace.h"\n/m));
    edit($f, 'base class', 1, scalar($t =~ s/^class CRfhutilApp : public CWinApp$/class CRfhutilApp : public CWinApp, public RfhTrace/m));
    edit($f, 'verboseTrace member', 1, scalar($t =~ s/^\tBOOL verboseTrace;\n//m));
    save($f, $t, $crlf);

    $f = 'RFHUtil/rfhutil.cpp'; ($t, $crlf) = load($f);
    edit($f, 'install trace', 1, scalar($t =~ s/^(\tverboseTrace = FALSE;\n)/$1\n\t\/\/ the shared core sources trace through this application object\n\trfhSetTrace(this);\n/m));
    save($f, $t, $crlf);
}

print "windows projects ported\n";
```

```bash
perl tools/port/port-windows-projects.pl .
```

Expected: `windows projects ported`

- [ ] **Step 2: Review the MFC header change**

```bash
git diff RFHUtil/rfhutil.h RFHUtil/rfhutil.cpp
```

Expected: the diff shows exactly these changes:
- `#include "rfh_trace.h"` added after `#include "DataArea.h"`.
- `class CRfhutilApp : public CWinApp, public RfhTrace`.
- The `BOOL verboseTrace;` line removed.
- `rfhSetTrace(this);` added after `verboseTrace = FALSE;` in the constructor.

- [ ] **Step 3: Confirm the Linux build is unaffected**

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug
```

Expected: `100% tests passed, 0 tests failed out of 280`

- [ ] **Step 4: Commit**

```bash
git add tools/port/port-windows-projects.pl RFHUtil tests/UnitTests/UnitTests.vcxproj
git commit -s -m "build(windows): compile the platform layer in the MFC and test projects

CRfhutilApp implements RfhTrace and installs itself, so the shared sources
keep tracing through the application on Windows.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Result type and MQ library loader

**Files:**
- Create: `core/mq/rfh_result.h`, `core/mq/rfh_result.cpp`
- Create: `core/mq/rfh_mq_loader.h`, `core/mq/rfh_mq_loader.cpp`
- Create: `core/message/rfh_message.h`
- Create: `core/mq/rfh_mq_session.h`, `core/mq/rfh_mq_session.cpp` (Task 5 tests them; the final CMake file needs them)
- Replace: `core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/core/ResultTests.cpp`, `tests/core/MqLoaderTests.cpp`

- [ ] **Step 1: Write the failing tests**

Create `tests/core/ResultTests.cpp`:

```cpp
// ResultTests.cpp — tests for rfh::Result, the outcome type of every core operation.

#include <gtest/gtest.h>

#include "rfh_result.h"

using rfh::Result;

TEST(Result, SuccessIsOkWithoutText)
{
    Result r = Result::success();
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(MQCC_OK, r.cc);
    EXPECT_EQ(MQRC_NONE, r.rc);
    EXPECT_TRUE(r.text.empty());
}

TEST(Result, FailureCarriesTextWithoutReasonCode)
{
    Result r = Result::failure("IBM MQ library not found");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(MQRC_NONE, r.rc);
    EXPECT_EQ("IBM MQ library not found", r.text);
}

TEST(Result, MqSuccessHasNoText)
{
    Result r = Result::fromMq("MQPUT", MQCC_OK, MQRC_NONE);
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.text.empty());
}

TEST(Result, MqFailureNamesOperationCodesAndReason)
{
    Result r = Result::fromMq("MQGET", MQCC_FAILED, MQRC_NO_MSG_AVAILABLE);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("MQGET cc=2 rc=2033 MQRC_NO_MSG_AVAILABLE (No message available)", r.text);
}

TEST(Result, MqWarningIsOkAndDescribed)
{
    Result r = Result::fromMq("MQGET", MQCC_WARNING, MQRC_TRUNCATED_MSG_ACCEPTED);
    EXPECT_TRUE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("rc=2079 MQRC_TRUNCATED_MSG_ACCEPTED"));
}

TEST(Result, UnknownReasonCodeIsNamedUnknown)
{
    EXPECT_STREQ("MQRC_UNKNOWN", rfh::reasonName(987654));
    Result r = Result::fromMq("MQOPEN", MQCC_FAILED, 987654);
    EXPECT_EQ("MQOPEN cc=2 rc=987654 MQRC_UNKNOWN", r.text);
}
```

Create `tests/core/MqLoaderTests.cpp`:

```cpp
// MqLoaderTests.cpp — tests for rfh::MqLoader.
// Its own executable because the loader keeps the first library it loads for
// the life of the process; the failure case must run before any success.

#include <gtest/gtest.h>

#include "rfh_mq_loader.h"

using namespace rfh;

TEST(MqLoader, MissingInstallationReportsEveryPathTried)
{
    MqApi api;
    Result r = MqLoader::load(api, "/nonexistent/mqm");
    EXPECT_FALSE(r.ok());
    EXPECT_NE(std::string::npos, r.text.find("IBM MQ library not found"));
    EXPECT_NE(std::string::npos, r.text.find("/nonexistent/mqm/lib64/libmqm_r.so"));
    EXPECT_NE(std::string::npos, r.text.find("/nonexistent/mqm/lib64/libmqic_r.so"));
    EXPECT_EQ(nullptr, api.connx);
}

TEST(MqLoader, LoadsServerLibraryWithAllEntryPoints)
{
    MqApi api;
    Result r = MqLoader::load(api);
    if (!r.ok())
    {
        GTEST_SKIP() << r.text;   // no IBM MQ on this machine
    }

    EXPECT_NE(nullptr, api.connx);
    EXPECT_NE(nullptr, api.disc);
    EXPECT_NE(nullptr, api.open);
    EXPECT_NE(nullptr, api.close);
    EXPECT_NE(nullptr, api.put);
    EXPECT_NE(nullptr, api.get);
    EXPECT_NE(nullptr, api.inq);
    EXPECT_FALSE(api.libraryPath.empty());
    EXPECT_EQ(api.libraryPath.find("libmqm_r") != std::string::npos, api.supportsBindings);
}

TEST(MqLoader, LaterCallsReturnTheFirstLibrary)
{
    MqApi first;
    MqApi second;
    if (!MqLoader::load(first).ok())
    {
        GTEST_SKIP() << "no IBM MQ on this machine";
    }

    ASSERT_TRUE(MqLoader::load(second, "/nonexistent/mqm").ok());
    EXPECT_EQ(first.libraryPath, second.libraryPath);
    EXPECT_EQ(first.connx, second.connx);
}
```

Replace `tests/CMakeLists.txt` with its final form. It also registers the
session tests, so create the Task 5 test file now:

```cmake
# Core tests: the existing unit tests (shared with the Windows UnitTests project),
# the platform tests, and the MQ integration tests.

find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    find_package(GTest QUIET)
endif()

if(NOT GTest_FOUND)
    if(RFHUTIL_FETCH_GTEST)
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
            URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    else()
        message(FATAL_ERROR "GoogleTest not found. Install it (Fedora: sudo dnf install gtest-devel) "
                            "or configure with -DRFHUTIL_FETCH_GTEST=ON to download v1.17.0.")
    endif()
endif()

include(GoogleTest)

add_executable(rfhutil_unit_tests
    UnitTests/ComsubsTests.cpp
    UnitTests/EncodingTests.cpp
    UnitTests/FileFormatTests.cpp
    UnitTests/HeaderUtilTests.cpp
    UnitTests/JsonParseTests.cpp
    UnitTests/MQErrorTests.cpp
    UnitTests/NamesTests.cpp
    UnitTests/ReconnectTests.cpp
    UnitTests/XMLParseTests.cpp
    UnitTests/XmlsubsTests.cpp
    core/PlatformTests.cpp
    core/SharedSourcesTests.cpp
)
target_include_directories(rfhutil_unit_tests PRIVATE UnitTests)
target_link_libraries(rfhutil_unit_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_unit_tests)

add_executable(rfhutil_core_tests core/ResultTests.cpp)
target_link_libraries(rfhutil_core_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_core_tests)

add_executable(rfhutil_loader_tests core/MqLoaderTests.cpp)
target_link_libraries(rfhutil_loader_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_loader_tests PROPERTIES LABELS mq)

add_executable(rfhutil_mq_tests core/MqSessionTests.cpp)
target_link_libraries(rfhutil_mq_tests PRIVATE rfhcore GTest::gtest_main)
gtest_discover_tests(rfhutil_mq_tests PROPERTIES LABELS mq RUN_SERIAL TRUE)
```

Create `tests/core/MqSessionTests.cpp` (Task 5 covers it):

```cpp
// MqSessionTests.cpp — integration tests for rfh::MqSession against a real queue manager.
//
// Bindings mode (always attempted):
//   MQ_BINDINGS_QMGR  local queue manager (default TEST_QM). The fixture creates the
//                     queues RFHUTIL.TEST.Q and RFHUTIL.TEST.EMPTY with runmqsc and
//                     deletes them afterwards, so the user running the tests must be
//                     in the mqm group. Tests skip when the queue manager is unavailable.
// Client mode (only when MQ_CHANNEL is set; matches tests/IntegrationTests):
//   MQ_HOST, MQ_PORT, MQ_CHANNEL, MQ_QMGR, MQ_USER, MQ_PASSWORD, MQ_CLIENT_QUEUE
//   (defaults: localhost, 1414, -, QM1, app, passw0rd, DEV.QUEUE.1), e.g. the
//   queue manager from tests/docker/compose.yaml.

#include <gtest/gtest.h>

#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <chrono>
#include <regex>
#include <string>
#include <thread>

#include "rfh_mq_session.h"

using namespace rfh;

namespace
{
    std::string env(const char * name, const char * def)
    {
        const char * v = getenv(name);
        return (v != nullptr && v[0] != 0) ? v : def;
    }

    std::string bindingsQm() { return env("MQ_BINDINGS_QMGR", "TEST_QM"); }

    const char * kQueue = "RFHUTIL.TEST.Q";
    const char * kEmptyQueue = "RFHUTIL.TEST.EMPTY";

    // Runs MQSC commands against the bindings queue manager; returns true on success.
    bool runMqsc(const std::string & commands)
    {
        const std::string qm = bindingsQm();
        if (!std::regex_match(qm, std::regex("[A-Za-z0-9._%/]{1,48}")))
        {
            return false;   // never pass an unexpected name to the shell
        }

        const std::string bin = env("MQ_INSTALLATION_PATH", "/opt/mqm") + "/bin/runmqsc";
        const std::string cmd = "printf '%s\\n' \"" + commands + "\" | " + bin + " " + qm + " >/dev/null 2>&1";
        return system(cmd.c_str()) == 0;
    }

    Message textMessage(const std::string & text)
    {
        Message m;
        memcpy(m.mqmd.Format, MQFMT_STRING, MQ_FORMAT_LENGTH);
        m.body.assign(text.begin(), text.end());
        return m;
    }

    std::string bodyText(const Message & m)
    {
        return std::string(m.body.begin(), m.body.end());
    }
}

class MqBindingsTest : public ::testing::Test
{
protected:
    static bool queuesReady;

    static void SetUpTestSuite()
    {
        queuesReady = runMqsc(std::string("DEFINE QLOCAL(") + kQueue + ") DEFSOPT(SHARED) SHARE REPLACE\n"
                              "DEFINE QLOCAL(" + kEmptyQueue + ") REPLACE");
    }

    static void TearDownTestSuite()
    {
        runMqsc(std::string("DELETE QLOCAL(") + kQueue + ") PURGE\nDELETE QLOCAL(" + kEmptyQueue + ") PURGE");
    }

    void SetUp() override
    {
        if (!queuesReady)
        {
            GTEST_SKIP() << "queue manager " << bindingsQm() << " is not available for bindings tests";
        }

        ConnectOptions options;
        options.mode = Mode::Bindings;
        options.qmName = bindingsQm();
        Result r = session.connect(options);
        ASSERT_TRUE(r.ok()) << r.text;

        MQLONG purged = 0;
        ASSERT_TRUE(session.purge(kQueue, purged).ok());
    }

    MqSession session;
};

bool MqBindingsTest::queuesReady = false;

TEST(MqSession, OperationsFailWhenNotConnected)
{
    MqSession session;
    Message m = textMessage("x");
    Result r = session.put(kQueue, m, PutOptions());
    EXPECT_FALSE(r.ok());
    EXPECT_EQ("Not connected to a queue manager", r.text);
}

TEST(MqSession, UnknownQueueManagerFails)
{
    MqSession session;
    ConnectOptions options;
    options.mode = Mode::Bindings;
    options.qmName = "RFHUTIL_NO_SUCH_QM";

    Result r = session.connect(options);
    if (!r.ok() && r.rc == MQRC_NONE)
    {
        GTEST_SKIP() << r.text;   // no MQ library on this machine
    }

    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_TRUE(r.rc == MQRC_Q_MGR_NAME_ERROR || r.rc == MQRC_Q_MGR_NOT_AVAILABLE) << r.text;
    EXPECT_FALSE(session.isConnected());
}

TEST_F(MqBindingsTest, PutThenGetReturnsSameMessage)
{
    Message out = textMessage("hello rfhutil");
    Result put = session.put(kQueue, out, PutOptions());
    ASSERT_TRUE(put.ok()) << put.text;

    MQBYTE24 none = { 0 };
    EXPECT_NE(0, memcmp(out.mqmd.MsgId, none, sizeof(none))) << "MQ should assign a MsgId";

    GetOptions get;
    get.matchMsgId = true;
    memcpy(get.msgId, out.mqmd.MsgId, sizeof(get.msgId));

    Message in;
    Result r = session.get(kQueue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ("hello rfhutil", bodyText(in));
    EXPECT_EQ(0, memcmp(in.mqmd.Format, MQFMT_STRING, MQ_FORMAT_LENGTH));
    EXPECT_EQ(0, memcmp(in.mqmd.MsgId, out.mqmd.MsgId, sizeof(MQBYTE24)));
}

TEST_F(MqBindingsTest, GetFromEmptyQueueReportsNoMessage)
{
    Message in;
    Result r = session.get(kEmptyQueue, in, GetOptions());
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_EQ("MQGET cc=2 rc=2033 MQRC_NO_MSG_AVAILABLE (No message available)", r.text);
}

TEST_F(MqBindingsTest, OpenUnknownQueueReportsUnknownObject)
{
    Message in;
    Result r = session.get("RFHUTIL.TEST.NO.SUCH.QUEUE", in, GetOptions());
    EXPECT_EQ(MQCC_FAILED, r.cc);
    EXPECT_EQ(MQRC_UNKNOWN_OBJECT_NAME, r.rc);
}

TEST_F(MqBindingsTest, BrowsesForwardAndBackward)
{
    for (const char * text : { "one", "two", "three" })
    {
        Message m = textMessage(text);
        ASSERT_TRUE(session.put(kQueue, m, PutOptions()).ok());
    }

    GetOptions browse;
    Message m;

    browse.mode = GetMode::BrowseFirst;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("one", bodyText(m));

    browse.mode = GetMode::BrowseNext;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("two", bodyText(m));
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("three", bodyText(m));

    browse.mode = GetMode::BrowsePrevious;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("two", bodyText(m));
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("one", bodyText(m));

    Result atStart = session.get(kQueue, m, browse);
    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, atStart.rc);
    EXPECT_NE(std::string::npos, atStart.text.find("already at the first message"));

    browse.mode = GetMode::BrowseNext;
    ASSERT_TRUE(session.get(kQueue, m, browse).ok());
    EXPECT_EQ("two", bodyText(m));

    MQLONG depth = 0;
    ASSERT_TRUE(session.depth(kQueue, depth).ok());
    EXPECT_EQ(3, depth) << "browsing must not remove messages";
}

TEST_F(MqBindingsTest, GetsByCorrelId)
{
    PutOptions keepCorrelId;
    for (const char * text : { "first", "second" })
    {
        Message m = textMessage(text);
        memset(m.mqmd.CorrelId, 0, sizeof(m.mqmd.CorrelId));
        memcpy(m.mqmd.CorrelId, text, strlen(text));
        ASSERT_TRUE(session.put(kQueue, m, keepCorrelId).ok());
    }

    GetOptions get;
    get.matchCorrelId = true;
    memcpy(get.correlId, "second", strlen("second"));

    Message in;
    Result r = session.get(kQueue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ("second", bodyText(in));
}

TEST_F(MqBindingsTest, ReportsDepthAndPurges)
{
    for (int i = 0; i < 3; i++)
    {
        Message m = textMessage("purge me");
        ASSERT_TRUE(session.put(kQueue, m, PutOptions()).ok());
    }

    MQLONG depth = 0;
    ASSERT_TRUE(session.depth(kQueue, depth).ok());
    EXPECT_EQ(3, depth);

    MQLONG purged = 0;
    Result r = session.purge(kQueue, purged);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(3, purged);

    ASSERT_TRUE(session.depth(kQueue, depth).ok());
    EXPECT_EQ(0, depth);
}

TEST_F(MqBindingsTest, GrowsBufferForLargeMessage)
{
    std::string big(100 * 1024, 'x');
    big[0] = 'A';
    big[big.size() - 1] = 'Z';

    Message out = textMessage(big);
    ASSERT_TRUE(session.put(kQueue, out, PutOptions()).ok());

    GetOptions browse;
    browse.mode = GetMode::BrowseFirst;
    browse.initialBufferSize = 16;
    Message browsed;
    Result b = session.get(kQueue, browsed, browse);
    ASSERT_TRUE(b.ok()) << b.text;
    EXPECT_EQ(big, bodyText(browsed));

    GetOptions get;
    get.initialBufferSize = 16;
    Message in;
    Result r = session.get(kQueue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ(big, bodyText(in));

    MQLONG depth = 0;
    ASSERT_TRUE(session.depth(kQueue, depth).ok());
    EXPECT_EQ(0, depth);
}

TEST_F(MqBindingsTest, WaitTimesOutAfterInterval)
{
    GetOptions get;
    get.waitMs = 1500;

    auto start = std::chrono::steady_clock::now();
    Message in;
    Result r = session.get(kEmptyQueue, in, get);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_GE(elapsed.count(), 1400);
    EXPECT_LT(elapsed.count(), 5000);
}

TEST_F(MqBindingsTest, CancelStopsWaitingGet)
{
    std::atomic<bool> cancel(false);
    GetOptions get;
    get.waitMs = 20000;
    get.cancel = &cancel;

    std::thread canceller([&cancel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        cancel = true;
    });

    auto start = std::chrono::steady_clock::now();
    Message in;
    Result r = session.get(kEmptyQueue, in, get);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    canceller.join();

    EXPECT_EQ(MQRC_NO_MSG_AVAILABLE, r.rc);
    EXPECT_NE(std::string::npos, r.text.find("wait cancelled"));
    EXPECT_LT(elapsed.count(), 2500);
}

TEST_F(MqBindingsTest, DisconnectEndsSession)
{
    ASSERT_TRUE(session.disconnect().ok());
    EXPECT_FALSE(session.isConnected());

    Message in;
    EXPECT_EQ("Not connected to a queue manager", session.get(kQueue, in, GetOptions()).text);
}

TEST(MqClient, PutThenGetOverClientChannel)
{
    if (env("MQ_CHANNEL", "").empty())
    {
        GTEST_SKIP() << "set MQ_CHANNEL (and MQ_HOST, MQ_PORT, MQ_QMGR, MQ_USER, MQ_PASSWORD) to test client mode";
    }

    ConnectOptions options;
    options.mode = Mode::Client;
    options.qmName = env("MQ_QMGR", "QM1");
    options.channel = env("MQ_CHANNEL", "");
    options.connName = env("MQ_HOST", "localhost") + "(" + env("MQ_PORT", "1414") + ")";
    options.userId = env("MQ_USER", "app");
    options.password = env("MQ_PASSWORD", "passw0rd");

    MqSession session;
    Result c = session.connect(options);
    ASSERT_TRUE(c.ok()) << c.text;

    const std::string queue = env("MQ_CLIENT_QUEUE", "DEV.QUEUE.1");
    Message out = textMessage("over the channel");
    ASSERT_TRUE(session.put(queue, out, PutOptions()).ok());

    GetOptions get;
    get.matchMsgId = true;
    memcpy(get.msgId, out.mqmd.MsgId, sizeof(get.msgId));
    Message in;
    Result r = session.get(queue, in, get);
    ASSERT_TRUE(r.ok()) << r.text;
    EXPECT_EQ("over the channel", bodyText(in));
}
```

Replace `core/CMakeLists.txt` with its final form:

```cmake
# librfhcore: the portable RFHUtil core (no MFC, no Qt).

find_path(MQ_INCLUDE_DIR cmqc.h
    HINTS "${MQ_HOME}/inc" "${MQ_HOME}/tools/c/include"
    NO_DEFAULT_PATH
    REQUIRED)

find_package(Threads REQUIRED)

# Shared sources that also build into the Windows MFC application. They stay in
# RFHUtil/ so the Visual Studio projects keep their paths.
set(RFHUTIL_SHARED_SOURCES
    ${PROJECT_SOURCE_DIR}/RFHUtil/comsubs.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/xmlsubs.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/JsonParse.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/XMLParse.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/Names.cpp
    ${PROJECT_SOURCE_DIR}/RFHUtil/mqerror.cpp
)

add_library(rfhcore STATIC
    platform/rfh_platform.cpp
    platform/rfh_codepage.cpp
    platform/rfh_trace.cpp
    mq/rfh_result.cpp
    mq/rfh_mq_loader.cpp
    mq/rfh_mq_session.cpp
    ${RFHUTIL_SHARED_SOURCES}
)

target_include_directories(rfhcore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/platform
        ${CMAKE_CURRENT_SOURCE_DIR}/mq
        ${CMAKE_CURRENT_SOURCE_DIR}/message
        ${PROJECT_SOURCE_DIR}/RFHUtil
        ${MQ_INCLUDE_DIR}
)

target_link_libraries(rfhcore PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

if(NOT WIN32)
    # The shared sources include "stdafx.h" and "rfhutil.h", which pull in MFC on
    # Windows. Supply the portable stand-ins: compat/ provides stdafx.h and the
    # prelude blocks rfhutil.h (the technique tests/UnitTests/prelude.h uses).
    target_include_directories(rfhcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compat)
    set_source_files_properties(${RFHUTIL_SHARED_SOURCES} PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/compat/core_prelude.h")

    # New core code is held to a clean warning level; the shared sources keep
    # their historic warnings, which are only in trace formatting.
    set_source_files_properties(
        platform/rfh_platform.cpp platform/rfh_codepage.cpp platform/rfh_trace.cpp
        mq/rfh_mq_loader.cpp mq/rfh_mq_session.cpp
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
    # cmqstrc.h returns string literals as char *.
    set_source_files_properties(mq/rfh_result.cpp PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wno-write-strings")
endif()
```

- [ ] **Step 2: Run the build to verify it fails**

```bash
cmake --preset linux-debug && cmake --build --preset linux-debug
```

Expected: FAIL. CMake reports `Cannot find source file: mq/rfh_result.cpp`.

- [ ] **Step 3: Write Result and the loader**

Create `core/mq/rfh_result.h`:

```cpp
// rfh_result.h - outcome of a core operation.
//
// Core operations never show UI or exit; they report what happened here and the
// host decides how to present it.
#pragma once

#include <cmqc.h>

#include <string>
#include <vector>

namespace rfh
{
	struct Result
	{
		MQLONG cc = MQCC_OK;		// MQ completion code; MQCC_FAILED for non-MQ failures too
		MQLONG rc = MQRC_NONE;		// MQ reason code; MQRC_NONE for non-MQ failures
		std::string text;			// human-readable outcome, empty on plain success
		std::vector<std::string> warnings;

		bool ok() const { return cc != MQCC_FAILED; }

		static Result success() { return Result(); }

		// A failure that did not come from an MQ call, e.g. a missing library.
		static Result failure(const std::string & text);

		// The outcome of an MQ call such as "MQGET" with its completion and reason codes.
		// text has the form "MQGET cc=2 rc=2033 MQRC_NO_MSG_AVAILABLE (No message available)".
		static Result fromMq(const char * operation, MQLONG cc, MQLONG rc);
	};

	// "MQRC_NO_MSG_AVAILABLE" for 2033, or "MQRC_UNKNOWN" when not a known reason code.
	const char * reasonName(MQLONG rc);
}
```

Create `core/mq/rfh_result.cpp`:

```cpp
// rfh_result.cpp - see rfh_result.h

#include "rfh_result.h"

#include <stdio.h>

#include "mqerror.h"

// cmqstrc.h defines its lookup functions in the header, so include it in exactly
// one translation unit.
#include <cmqstrc.h>

namespace rfh
{
	Result Result::failure(const std::string & text)
	{
		Result r;
		r.cc = MQCC_FAILED;
		r.rc = MQRC_NONE;
		r.text = text;
		return r;
	}

	Result Result::fromMq(const char * operation, MQLONG cc, MQLONG rc)
	{
		Result r;
		r.cc = cc;
		r.rc = rc;

		if ((cc != MQCC_OK) || (rc != MQRC_NONE))
		{
			char text[512];
			const char * description = mqReasonString((long)rc);

			if (description != NULL)
			{
				snprintf(text, sizeof(text), "%s cc=%d rc=%d %s (%s)", operation, (int)cc, (int)rc, reasonName(rc), description);
			}
			else
			{
				snprintf(text, sizeof(text), "%s cc=%d rc=%d %s", operation, (int)cc, (int)rc, reasonName(rc));
			}

			r.text = text;
		}

		return r;
	}

	const char * reasonName(MQLONG rc)
	{
		const char * name = MQRC_STR(rc);
		return (name != NULL && name[0] != 0) ? name : "MQRC_UNKNOWN";
	}
}
```

Create `core/mq/rfh_mq_loader.h`:

```cpp
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
```

Create `core/mq/rfh_mq_loader.cpp`:

```cpp
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
```

- [ ] **Step 4: Add the message model and a session stub so the build links**

Create `core/message/rfh_message.h`:

```cpp
// rfh_message.h - one MQ message: its descriptor and its bytes.
//
// Phase 1 keeps the MQ headers that follow the MQMD (RFH2, DLQ, CICS, ...) inside
// body, exactly as MQGET returns them. Header parsing comes with the file format
// extraction, which adds a header chain without changing body's meaning.
#pragma once

#include <cmqc.h>

#include <vector>

namespace rfh
{
	struct Message
	{
		MQMD2 mqmd = { MQMD2_DEFAULT };
		std::vector<unsigned char> body;

		Message() { mqmd.Version = MQMD_VERSION_2; }
	};
}
```

Create `core/mq/rfh_mq_session.h` (the complete header; Task 5 writes the implementation):

```cpp
// rfh_mq_session.h - one connection to a queue manager and one open queue.
//
// MqSession owns its connection handle and at most one open queue handle for
// put/get/browse. depth() and purge() use their own short-lived handles so they
// never disturb a browse cursor. A session is not thread-safe: use it from one
// thread only (the Qt application gives it a dedicated worker thread).
#pragma once

#include <cmqc.h>
#include <cmqxc.h>

#include <atomic>
#include <string>

#include "rfh_message.h"
#include "rfh_mq_loader.h"
#include "rfh_result.h"

namespace rfh
{
	enum class Mode { Bindings, Client };

	struct ConnectOptions
	{
		Mode mode = Mode::Bindings;
		std::string qmName;			// empty connects to the default queue manager

		// Client mode. With channel set, connect through channel/connName; otherwise
		// with ccdtUrl set, use that CCDT; otherwise MQ uses MQSERVER or MQCHLLIB/MQCHLTAB.
		std::string channel;
		std::string connName;		// "host(port)"
		std::string ccdtUrl;		// file path or URL of a CCDT
		MQLONG heartbeat = 60;		// seconds; client channel definitions only
		MQLONG keepAlive = MQKAI_AUTO;

		// Credentials are sent only when a password is given (the IDPWOS rule).
		// The password is never traced, logged, or kept after connect().
		std::string userId;
		std::string password;
	};

	enum class BindOption { AsQueueDefinition, OnOpen, NotFixed, OnGroup };
	enum class CloseOption { None, Delete, Purge };

	struct PutOptions
	{
		std::string remoteQm;		// remote queue manager name for remote queues
		BindOption bind = BindOption::AsQueueDefinition;
		bool newMsgId = true;
		bool newCorrelId = false;
		bool setIdentityContext = false;	// put the context fields from the MQMD as given
		bool setAllContext = false;
	};

	enum class GetMode { Destructive, BrowseFirst, BrowseNext, BrowsePrevious };

	struct GetOptions
	{
		GetMode mode = GetMode::Destructive;
		MQLONG waitMs = 0;			// 0 returns at once when no message is available
		bool convert = false;		// MQGMO_CONVERT using the MQMD CodedCharSetId/Encoding
		bool matchMsgId = false;	// match msgId below
		bool matchCorrelId = false;	// match correlId below
		MQBYTE24 msgId = { 0 };
		MQBYTE24 correlId = { 0 };
		size_t initialBufferSize = 64 * 1024;	// grown to the message length on truncation

		// Optional: set to true from another thread to stop a waiting get. The wait is
		// split into slices of at most one second, so it stops within about a second.
		const std::atomic<bool> * cancel = nullptr;
	};

	class MqSession
	{
	public:
		MqSession() = default;
		~MqSession();

		MqSession(const MqSession &) = delete;
		MqSession & operator=(const MqSession &) = delete;

		Result connect(const ConnectOptions & options);
		Result disconnect();
		bool isConnected() const { return hConn != MQHC_UNUSABLE_HCONN; }

		// Puts message to queue. On success message.mqmd holds the descriptor MQ
		// returned (new MsgId, put date and time, ...).
		Result put(const std::string & queue, Message & message, const PutOptions & options);

		// Gets or browses one message from queue into message. A browse continues on
		// the same queue handle; switching queue or putting closes it and resets the
		// browse position. BrowsePrevious re-browses from the first message.
		Result get(const std::string & queue, Message & message, const GetOptions & options);

		Result depth(const std::string & queue, MQLONG & depth);

		// Removes every message from queue with destructive gets; count receives how many.
		Result purge(const std::string & queue, MQLONG & count);

		// Closes the open queue handle, if any.
		Result closeQueue(CloseOption option = CloseOption::None);

	private:
		Result openQueue(const std::string & queue, const std::string & remoteQm, MQLONG options, MQHOBJ & hObj);
		Result ensureOpen(const std::string & queue, const std::string & remoteQm, MQLONG options);
		Result getOnce(MQHOBJ obj, Message & message, const GetOptions & options, MQLONG getOptions, MQLONG waitMs);
		Result browseTo(const std::string & queue, Message & message, const GetOptions & options, long position);

		MqApi api;
		MQHCONN hConn = MQHC_UNUSABLE_HCONN;

		MQHOBJ hObj = MQHO_UNUSABLE_HOBJ;
		std::string openName;
		std::string openRemoteQm;
		MQLONG openOptions = 0;
		long browsePosition = 0;	// 1-based position of the message under the browse cursor
	};
}
```

Create `core/mq/rfh_mq_session.cpp` as a stub that fails every call, so Task 5 starts from failing tests:

```cpp
// rfh_mq_session.cpp - see rfh_mq_session.h (stub until Task 5)

#include "rfh_mq_session.h"

namespace rfh
{
	namespace { Result notImplemented() { return Result::failure("MqSession is not implemented yet"); } }

	MqSession::~MqSession() {}
	Result MqSession::connect(const ConnectOptions &) { return notImplemented(); }
	Result MqSession::disconnect() { return notImplemented(); }
	Result MqSession::put(const std::string &, Message &, const PutOptions &) { return notImplemented(); }
	Result MqSession::get(const std::string &, Message &, const GetOptions &) { return notImplemented(); }
	Result MqSession::depth(const std::string &, MQLONG &) { return notImplemented(); }
	Result MqSession::purge(const std::string &, MQLONG &) { return notImplemented(); }
	Result MqSession::closeQueue(CloseOption) { return notImplemented(); }
}
```

- [ ] **Step 5: Run the Result and loader tests to verify they pass**

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug -R "Result|MqLoader"
```

Expected: `100% tests passed, 0 tests failed out of 9`

- [ ] **Step 6: Commit**

```bash
git add core tests
git commit -s -m "feat(core): add Result type and run-time MQ library loader

MqLoader loads libmqm_r (bindings and client) or, on a client-only
installation, libmqic_r, once per process. Result carries the MQ
completion and reason codes with their names and descriptions.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: MQ session (connect, put, get, browse, depth, purge)

**Files:**
- Replace: `core/mq/rfh_mq_session.cpp`
- Test: `tests/core/MqSessionTests.cpp` (created in Task 4)

The bindings tests create `RFHUTIL.TEST.Q` and `RFHUTIL.TEST.EMPTY` on
`TEST_QM` (or `$MQ_BINDINGS_QMGR`) with `runmqsc`, and delete them at the end.
They touch nothing else on the queue manager.

- [ ] **Step 1: Run the session tests to verify they fail**

```bash
ctest --preset linux-debug -L mq -E MqLoader
```

Expected: FAIL. The bindings tests stop in `SetUp` with
`MqSession is not implemented yet`.

- [ ] **Step 2: Write the session**

Replace `core/mq/rfh_mq_session.cpp`:

```cpp
// rfh_mq_session.cpp - see rfh_mq_session.h

#include "rfh_mq_session.h"

#include <string.h>

#include <algorithm>
#include <limits>

namespace rfh
{
	namespace
	{
		const MQLONG kWaitSliceMs = 1000;

		// Copies text into a blank-padded MQ character field.
		bool copyField(MQCHAR * field, size_t fieldLength, const std::string & text)
		{
			if (text.size() > fieldLength)
			{
				return false;
			}

			memset(field, ' ', fieldLength);
			memcpy(field, text.data(), text.size());
			return true;
		}

		MQLONG bindOpenOption(BindOption bind)
		{
			switch (bind)
			{
			case BindOption::OnOpen: return MQOO_BIND_ON_OPEN;
			case BindOption::NotFixed: return MQOO_BIND_NOT_FIXED;
			case BindOption::OnGroup: return MQOO_BIND_ON_GROUP;
			case BindOption::AsQueueDefinition: break;
			}
			return MQOO_BIND_AS_Q_DEF;
		}

		MQLONG closeOptionValue(CloseOption option)
		{
			switch (option)
			{
			case CloseOption::Delete: return MQCO_DELETE;
			case CloseOption::Purge: return MQCO_DELETE_PURGE;
			case CloseOption::None: break;
			}
			return MQCO_NONE;
		}

		Result notConnected()
		{
			return Result::failure("Not connected to a queue manager");
		}
	}

	MqSession::~MqSession()
	{
		if (isConnected())
		{
			disconnect();
		}
	}

	Result MqSession::connect(const ConnectOptions & options)
	{
		if (isConnected())
		{
			return Result::failure("Already connected; disconnect first");
		}

		Result loaded = MqLoader::load(api);
		if (!loaded.ok())
		{
			return loaded;
		}

		if ((options.mode == Mode::Bindings) && !api.supportsBindings)
		{
			return Result::failure("Local bindings need the IBM MQ server library; only " + api.libraryPath + " is available");
		}

		MQCHAR48 qmName;
		memset(qmName, 0, sizeof(qmName));
		if (options.qmName.size() > sizeof(qmName))
		{
			return Result::failure("Queue manager name is longer than 48 characters");
		}
		memcpy(qmName, options.qmName.data(), options.qmName.size());

		MQCNO cno = { MQCNO_DEFAULT };
		MQCD cd = { MQCD_CLIENT_CONN_DEFAULT };
		MQCSP csp = { MQCSP_DEFAULT };
		cno.Version = MQCNO_VERSION_6;	// SecurityParmsPtr (v5) and CCDTUrlPtr (v6)

		if (options.mode == Mode::Bindings)
		{
			cno.Options |= MQCNO_LOCAL_BINDING;
		}
		else
		{
			cno.Options |= MQCNO_CLIENT_BINDING;

			if (!options.channel.empty())
			{
				if (!copyField(cd.ChannelName, sizeof(cd.ChannelName), options.channel))
				{
					return Result::failure("Channel name is longer than 20 characters");
				}
				if (!copyField(cd.ConnectionName, sizeof(cd.ConnectionName), options.connName))
				{
					return Result::failure("Connection name is longer than 264 characters");
				}

				cd.Version = MQCD_CURRENT_VERSION;
				cd.TransportType = MQXPT_TCP;
				cd.HeartbeatInterval = options.heartbeat;
				cd.KeepAliveInterval = options.keepAlive;
				cno.ClientConnPtr = &cd;
			}
			else if (!options.ccdtUrl.empty())
			{
				cno.CCDTUrlPtr = const_cast<PMQCHAR>(options.ccdtUrl.c_str());
				cno.CCDTUrlLength = (MQLONG)options.ccdtUrl.size();
			}
		}

		// IDPWOS rule: attach credentials only when a password is supplied
		if (!options.password.empty())
		{
			csp.AuthenticationType = MQCSP_AUTH_USER_ID_AND_PWD;
			csp.CSPUserIdPtr = const_cast<char *>(options.userId.c_str());
			csp.CSPUserIdLength = (MQLONG)options.userId.size();
			csp.CSPPasswordPtr = const_cast<char *>(options.password.c_str());
			csp.CSPPasswordLength = (MQLONG)options.password.size();
			cno.SecurityParmsPtr = &csp;
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.connx(qmName, &cno, &hConn, &cc, &rc);

		if (cc == MQCC_FAILED)
		{
			hConn = MQHC_UNUSABLE_HCONN;
		}

		return Result::fromMq("MQCONNX", cc, rc);
	}

	Result MqSession::disconnect()
	{
		if (!isConnected())
		{
			return Result::success();
		}

		Result closed = closeQueue();

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.disc(&hConn, &cc, &rc);
		hConn = MQHC_UNUSABLE_HCONN;

		Result r = Result::fromMq("MQDISC", cc, rc);
		if (r.ok() && !closed.ok())
		{
			r.warnings.push_back(closed.text);
		}
		return r;
	}

	Result MqSession::openQueue(const std::string & queue, const std::string & remoteQm, MQLONG options, MQHOBJ & obj)
	{
		MQOD od = { MQOD_DEFAULT };

		if (!copyField(od.ObjectName, sizeof(od.ObjectName), queue))
		{
			return Result::failure("Queue name is longer than 48 characters");
		}
		if (!copyField(od.ObjectQMgrName, sizeof(od.ObjectQMgrName), remoteQm))
		{
			return Result::failure("Remote queue manager name is longer than 48 characters");
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.open(hConn, &od, options | MQOO_FAIL_IF_QUIESCING, &obj, &cc, &rc);

		if (cc == MQCC_FAILED)
		{
			obj = MQHO_UNUSABLE_HOBJ;
		}

		return Result::fromMq("MQOPEN", cc, rc);
	}

	Result MqSession::ensureOpen(const std::string & queue, const std::string & remoteQm, MQLONG options)
	{
		if ((hObj != MQHO_UNUSABLE_HOBJ) && (openName == queue) && (openRemoteQm == remoteQm) && (openOptions == options))
		{
			return Result::success();
		}

		Result closed = closeQueue();
		if (!closed.ok())
		{
			return closed;
		}

		Result opened = openQueue(queue, remoteQm, options, hObj);
		if (opened.ok())
		{
			openName = queue;
			openRemoteQm = remoteQm;
			openOptions = options;
		}

		return opened;
	}

	Result MqSession::closeQueue(CloseOption option)
	{
		browsePosition = 0;

		if (hObj == MQHO_UNUSABLE_HOBJ)
		{
			return Result::success();
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.close(hConn, &hObj, closeOptionValue(option), &cc, &rc);

		hObj = MQHO_UNUSABLE_HOBJ;
		openName.clear();
		openRemoteQm.clear();
		openOptions = 0;

		return Result::fromMq("MQCLOSE", cc, rc);
	}

	Result MqSession::put(const std::string & queue, Message & message, const PutOptions & options)
	{
		if (!isConnected())
		{
			return notConnected();
		}

		if (message.body.size() > (size_t)std::numeric_limits<MQLONG>::max())
		{
			return Result::failure("Message is larger than MQ allows");
		}

		MQLONG openOpts = MQOO_OUTPUT | bindOpenOption(options.bind);
		if (options.setAllContext)
		{
			openOpts |= MQOO_SET_ALL_CONTEXT;
		}
		else if (options.setIdentityContext)
		{
			openOpts |= MQOO_SET_IDENTITY_CONTEXT;
		}

		Result opened = ensureOpen(queue, options.remoteQm, openOpts);
		if (!opened.ok())
		{
			return opened;
		}

		MQPMO pmo = { MQPMO_DEFAULT };
		pmo.Options = MQPMO_NO_SYNCPOINT | MQPMO_FAIL_IF_QUIESCING;
		if (options.newMsgId)
		{
			pmo.Options |= MQPMO_NEW_MSG_ID;
		}
		if (options.newCorrelId)
		{
			pmo.Options |= MQPMO_NEW_CORREL_ID;
		}
		if (options.setAllContext)
		{
			pmo.Options |= MQPMO_SET_ALL_CONTEXT;
		}
		else if (options.setIdentityContext)
		{
			pmo.Options |= MQPMO_SET_IDENTITY_CONTEXT;
		}

		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.put(hConn, hObj, &message.mqmd, &pmo, (MQLONG)message.body.size(),
		        message.body.empty() ? nullptr : message.body.data(), &cc, &rc);

		return Result::fromMq("MQPUT", cc, rc);
	}

	Result MqSession::getOnce(MQHOBJ obj, Message & message, const GetOptions & options, MQLONG getOptions, MQLONG waitMs)
	{
		std::vector<unsigned char> buffer(std::max<size_t>(options.initialBufferSize, 1));
		MQLONG remaining = std::max<MQLONG>(waitMs, 0);
		bool retryByMsgId = false;
		MQBYTE24 retryMsgId;

		for (;;)
		{
			MQMD2 md = { MQMD2_DEFAULT };
			md.Version = MQMD_VERSION_2;
			if (options.convert)
			{
				// MQGMO_CONVERT converts to the CCSID and encoding given in the MQMD
				md.CodedCharSetId = message.mqmd.CodedCharSetId;
				md.Encoding = message.mqmd.Encoding;
			}

			MQGMO gmo = { MQGMO_DEFAULT };
			gmo.Version = MQGMO_VERSION_2;
			gmo.MatchOptions = MQMO_NONE;
			gmo.Options = getOptions | MQGMO_FAIL_IF_QUIESCING;
			if (options.convert)
			{
				gmo.Options |= MQGMO_CONVERT;
			}

			if (options.matchMsgId)
			{
				gmo.MatchOptions |= MQMO_MATCH_MSG_ID;
				memcpy(md.MsgId, options.msgId, sizeof(md.MsgId));
			}
			if (options.matchCorrelId)
			{
				gmo.MatchOptions |= MQMO_MATCH_CORREL_ID;
				memcpy(md.CorrelId, options.correlId, sizeof(md.CorrelId));
			}
			if (retryByMsgId)
			{
				gmo.MatchOptions |= MQMO_MATCH_MSG_ID;
				memcpy(md.MsgId, retryMsgId, sizeof(md.MsgId));
			}

			MQLONG slice = std::min(remaining, kWaitSliceMs);
			if ((slice > 0) && !retryByMsgId)
			{
				gmo.Options |= MQGMO_WAIT;
				gmo.WaitInterval = slice;
			}
			else
			{
				gmo.Options |= MQGMO_NO_WAIT;
				slice = 0;
			}

			MQLONG dataLength = 0;
			MQLONG cc = MQCC_OK;
			MQLONG rc = MQRC_NONE;
			api.get(hConn, obj, &md, &gmo, (MQLONG)buffer.size(), buffer.data(), &dataLength, &cc, &rc);

			if ((rc == MQRC_TRUNCATED_MSG_FAILED) && (dataLength > (MQLONG)buffer.size()))
			{
				// The message stays on the queue and a browse cursor does not move, so
				// repeat the same get matched on the MsgId MQ just reported.
				buffer.resize((size_t)dataLength);
				retryByMsgId = true;
				memcpy(retryMsgId, md.MsgId, sizeof(retryMsgId));
				continue;
			}

			if ((rc == MQRC_NO_MSG_AVAILABLE) && (remaining > slice) && (slice > 0))
			{
				remaining -= slice;
				if ((options.cancel != nullptr) && options.cancel->load())
				{
					Result r = Result::fromMq("MQGET", cc, rc);
					r.text += " - wait cancelled";
					return r;
				}
				continue;
			}

			if (cc != MQCC_FAILED)
			{
				message.mqmd = md;
				message.body.assign(buffer.begin(), buffer.begin() + std::min<size_t>((size_t)dataLength, buffer.size()));
			}

			return Result::fromMq("MQGET", cc, rc);
		}
	}

	Result MqSession::browseTo(const std::string & queue, Message & message, const GetOptions & options, long position)
	{
		GetOptions step = options;
		step.waitMs = 0;

		step.mode = GetMode::BrowseFirst;
		Result r = get(queue, message, step);

		step.mode = GetMode::BrowseNext;
		while (r.ok() && (browsePosition < position))
		{
			r = get(queue, message, step);
		}

		return r;
	}

	Result MqSession::get(const std::string & queue, Message & message, const GetOptions & options)
	{
		if (!isConnected())
		{
			return notConnected();
		}

		if (options.mode == GetMode::BrowsePrevious)
		{
			if ((browsePosition <= 1) || (openName != queue))
			{
				Result r = Result::fromMq("MQGET", MQCC_FAILED, MQRC_NO_MSG_AVAILABLE);
				r.text += " - already at the first message";
				return r;
			}

			return browseTo(queue, message, options, browsePosition - 1);
		}

		Result opened = ensureOpen(queue, std::string(), MQOO_INPUT_AS_Q_DEF | MQOO_BROWSE);
		if (!opened.ok())
		{
			return opened;
		}

		MQLONG getOptions = MQGMO_NO_SYNCPOINT;
		switch (options.mode)
		{
		case GetMode::BrowseFirst: getOptions = MQGMO_BROWSE_FIRST; break;
		case GetMode::BrowseNext: getOptions = MQGMO_BROWSE_NEXT; break;
		case GetMode::Destructive: case GetMode::BrowsePrevious: break;
		}

		Result r = getOnce(hObj, message, options, getOptions, options.waitMs);

		if (r.ok())
		{
			switch (options.mode)
			{
			case GetMode::BrowseFirst: browsePosition = 1; break;
			case GetMode::BrowseNext: browsePosition++; break;
			case GetMode::Destructive: browsePosition = 0; break;	// a destructive get ends the browse
			case GetMode::BrowsePrevious: break;
			}
		}

		return r;
	}

	Result MqSession::depth(const std::string & queue, MQLONG & depth)
	{
		depth = 0;
		if (!isConnected())
		{
			return notConnected();
		}

		MQHOBJ obj = MQHO_UNUSABLE_HOBJ;
		Result opened = openQueue(queue, std::string(), MQOO_INQUIRE, obj);
		if (!opened.ok())
		{
			return opened;
		}

		MQLONG selector = MQIA_CURRENT_Q_DEPTH;
		MQLONG cc = MQCC_OK;
		MQLONG rc = MQRC_NONE;
		api.inq(hConn, obj, 1, &selector, 1, &depth, 0, nullptr, &cc, &rc);
		Result r = Result::fromMq("MQINQ", cc, rc);

		MQLONG ccc = MQCC_OK;
		MQLONG crc = MQRC_NONE;
		api.close(hConn, &obj, MQCO_NONE, &ccc, &crc);

		return r;
	}

	Result MqSession::purge(const std::string & queue, MQLONG & count)
	{
		count = 0;
		if (!isConnected())
		{
			return notConnected();
		}

		MQHOBJ obj = MQHO_UNUSABLE_HOBJ;
		Result opened = openQueue(queue, std::string(), MQOO_INPUT_AS_Q_DEF, obj);
		if (!opened.ok())
		{
			return opened;
		}

		Result r;
		for (;;)
		{
			MQMD2 md = { MQMD2_DEFAULT };
			MQGMO gmo = { MQGMO_DEFAULT };
			gmo.Options = MQGMO_NO_WAIT | MQGMO_NO_SYNCPOINT | MQGMO_ACCEPT_TRUNCATED_MSG | MQGMO_FAIL_IF_QUIESCING;

			unsigned char unused = 0;
			MQLONG dataLength = 0;
			MQLONG cc = MQCC_OK;
			MQLONG rc = MQRC_NONE;
			api.get(hConn, obj, &md, &gmo, 0, &unused, &dataLength, &cc, &rc);

			if ((cc == MQCC_OK) || (rc == MQRC_TRUNCATED_MSG_ACCEPTED))
			{
				count++;
				continue;
			}

			// an empty queue is the expected end of a purge
			r = (rc == MQRC_NO_MSG_AVAILABLE) ? Result::success() : Result::fromMq("MQGET", cc, rc);
			break;
		}

		MQLONG ccc = MQCC_OK;
		MQLONG crc = MQRC_NONE;
		api.close(hConn, &obj, MQCO_NONE, &ccc, &crc);

		return r;
	}
}
```

Design points the tests pin:
- **Wait slicing:** a waiting get is split into ≤ 1 s slices, so a `cancel`
  flag set from another thread stops it within about a second.
- **Truncation:** on `MQRC_TRUNCATED_MSG_FAILED` (2080) the buffer grows and
  the same get repeats, matched on the MsgId MQ just returned. A browse
  cursor does not move on 2080, so "browse message under cursor" would fail
  with 2034. This was observed on MQ 9.4.0.5 during planning.
- **Browse previous:** MQ has no browse-previous option, so it re-browses from
  the first message to the previous position.
- **Separate handles:** `depth()` and `purge()` open their own handles, so a
  browse in progress keeps its cursor.

- [ ] **Step 3: Run all tests to verify they pass**

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug
```

Expected: `100% tests passed, 0 tests failed out of 302`, with
`MqClient.PutThenGetOverClientChannel` reported as skipped. Run the session
tests twice more to check for flakiness:

```bash
for i in 1 2; do ./build/linux-debug/tests/rfhutil_mq_tests | tail -3 | head -1; done
```

Expected: `[  PASSED  ] 12 tests.` twice.

- [ ] **Step 4: Confirm the fixture cleaned up**

```bash
echo "DISPLAY QLOCAL(RFHUTIL.TEST.*)" | /opt/mqm/bin/runmqsc TEST_QM | grep AMQ8147E
```

Expected: `AMQ8147E: IBM MQ object RFHUTIL.TEST.* not found.`

- [ ] **Step 5: Commit**

```bash
git add core/mq/rfh_mq_session.cpp
git commit -s -m "feat(core): add MqSession for bindings and client connections

Connects in bindings or client mode (channel, CCDT, or MQSERVER) and puts,
gets, browses forward and backward, reports depth and purges. Waits can be
cancelled and large messages grow the buffer automatically. Integration
tests run against a local queue manager.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Verify the Windows build in CI (needs user approval: creates a fork and pushes)

**Files:** none

- [ ] **Step 1: Ask the user to create the fork**

`gh` is not installed. Ask the user to open
https://github.com/ibm-messaging/mq-rfhutil/fork and create
`tyty-the-eyety/mq-rfhutil`, or to approve installing `gh`. Then:

```bash
git remote rename origin upstream
git remote add origin https://github.com/tyty-the-eyety/mq-rfhutil.git
git remote -v
```

Expected: `origin` points at the fork and `upstream` at `ibm-messaging/mq-rfhutil`.

- [ ] **Step 2: Push the branch (only after the user approves the push)**

```bash
git push -u origin features/linux-qt-port
```

- [ ] **Step 3: Check the "CI — Build & Test" run**

The user may need to enable Actions on the fork first (Actions tab → enable).
The existing workflow builds `UnitTests` for Win32 and x64. That build now
compiles the ported shared sources, the platform layer, and both new test
files with MSVC/MFC.

Expected: both matrix jobs are green. If a job fails, fix the reported error
in `core/platform/` or the test stubs (never with `#ifdef` in the shared
sources), push again, and repeat.

- [ ] **Step 4: Record what remains unverified**

The MFC application itself (`RFHUtil.vcxproj` plus the `rfhutil.h`/`.cpp`,
`uisubs`, and page include changes) is not built in CI. That build needs the
MQ C headers, and IBM's public redistributable-client URL returned
HTTP 403 when checked. Ask the user to build `RFHUtil.sln` (Release|x64) on a
Windows machine with IBM MQ installed, or to choose a CI source for the MQ
SDK. Plan 4 covers CI.

---

### Task 7: Verify client mode (needs user approval: pulls and runs a container)

**Files:** none

- [ ] **Step 1: Start the upstream test queue manager (after the user approves pulling `icr.io/ibm-messaging/mq:latest`)**

```bash
docker compose -f tests/docker/compose.yaml up -d
docker compose -f tests/docker/compose.yaml ps
```

Expected: `test-qm1` is `healthy` within about two minutes.

- [ ] **Step 2: Run the client test**

```bash
MQ_CHANNEL=DEV.APP.SVRCONN ./build/linux-debug/tests/rfhutil_mq_tests --gtest_filter='MqClient.*'
```

Expected: `[  PASSED  ] 1 test.`

- [ ] **Step 3: Stop the container**

```bash
docker compose -f tests/docker/compose.yaml down
```

---

### Task 8: Documentation and release notes

**Files:**
- Modify: `CHANGELOG.md`, `README.md`
- Modify: `docs/superpowers/specs/2026-10-15-linux-qt-port-design.md`

- [ ] **Step 1: Add the CHANGELOG entry**

Read the top of `CHANGELOG.md` and add, under a new `## [Unreleased]` heading in
the file's existing style:

```markdown
### Added
- Portable core library (`core/`) and CMake build: the shared parsers and a new
  MQ session layer build and pass their tests on Linux (Fedora 44, IBM MQ 9.4).
- The existing unit tests also run on Linux (`ctest --preset linux-debug`).

### Changed
- comsubs, xmlsubs, JsonParse, XMLParse and Names use `core/platform` instead of
  direct Win32 calls; behaviour on Windows is unchanged.
- `processBackspace` moved from comsubs.cpp to the MFC-only uisubs.cpp.
```

- [ ] **Step 2: Document the Linux build in README.md**

Add this section after the existing build instructions:

````markdown
## Building the portable core on Linux

Requires CMake 3.21+, a C++17 compiler, GoogleTest and the IBM MQ SDK
(`MQSeriesSDK`, default `/opt/mqm`).

```bash
sudo dnf install cmake gcc-c++ gtest-devel     # Fedora
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug                      # MQ tests need a local queue manager
```

MQ integration tests use `MQ_BINDINGS_QMGR` (default `TEST_QM`) and create and
delete `RFHUTIL.TEST.*` queues; client tests run when `MQ_CHANNEL` is set (see
`tests/docker/compose.yaml`). Without an installed GoogleTest, add
`-DRFHUTIL_FETCH_GTEST=ON`.
````

- [ ] **Step 3: Update the spec with decisions made while planning**

In the spec's section 4, make these changes:
- Replace the `TraceSink` / `setTraceSink` lines with the `RfhTrace` interface
  and `rfhGetTrace()` / `rfhSetTrace()`. Reason: 28 shared-source call sites
  already use this shape, so the MFC application object implements it
  directly.
- Note that `MqLoader` loads one library per process: `libmqm_r` for both
  modes, or `libmqic_r` for client mode only.
- Note that `TlsOptions` and `autoReconnect` move to the plan that ports the
  Connection Settings tab.

- [ ] **Step 4: Run the full suite one last time**

```bash
cmake --build --preset linux-debug && ctest --preset linux-debug
```

Expected: `100% tests passed, 0 tests failed out of 302`

- [ ] **Step 5: Commit**

```bash
git add CHANGELOG.md README.md docs/superpowers/specs/2026-10-15-linux-qt-port-design.md
git commit -s -m "docs: document the Linux core build and planning decisions

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Report the upstream issue (draft only; the user decides whether to file it)**

Draft an issue for `ibm-messaging/mq-rfhutil`: "`matchEbcdicToAscii` uses
`case 037:` (octal 31), so CCSID 37 never maps to an ANSI code page". Include
the test `SharedSources_Codepage.MatchEbcdicToAsciiKeepsHistoricCcsid37Behaviour`
as the reproduction. Do not change the behaviour in this plan.

---

## Not in this plan (later plans)

- **PCF queue-name lookup** (`MqSession::queueNames`), the message file format
  (`fileio`), MQMD field conversion (`MqmdFields`), the header chain, and
  display formatting: Plan 2. That plan begins by generating the golden files
  from the pre-refactor Windows code.
- **`DataArea` delegating to `MqSession`:** Plan 2. Until then, `DataArea`
  keeps its own `LoadLibrary` loader.
- **TLS, reconnect, and heartbeat settings in the UI:** with the Connection
  Settings tab.
- **Qt6 application:** Plan 3.
- **RPM, `.desktop` file, and Linux CI** (including a source for the MQ
  headers in CI): Plan 4.
